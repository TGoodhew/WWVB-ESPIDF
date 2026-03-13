/*
  WWVB Emulator for Adafruit Huzzah32 Featherboard (ESP32)

  There has been construction up the hill from me and this has caused the WWVB signal to be degraded all across my house except for one rear corner.
  Every daylight savings change I need to cycle my atomic clocks through this corner to get them updated. The goal of this emulator is to grab the current
  time via NTP and then create a local signal that my clocks can sync to.

  Change log:

    0.1   Deploy default ESP32 app using ESP-IDF only
    0.2   Create a first version using the code generation from https://www.instructables.com/WWVB-radio-time-signal-generator-for-ATTINY45-or-A/
    0.3   Use ESP32 timers to enable tweaking of the modulation to match the proper signal timing
    0.4   Added encoding to create the bit patterns for Years, Days, Hours & Minutes from the system time converted to UTC
    0.5   Added BLE WiFi provisioning using the ESP-IDF example code
    0.6   Added SNTP call & synd to get UTC time
    0.7   Added 60KHz output using the ESP32 LEDC PWM
    0.8   Implemented ESP Logging & Error Checking
*/

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp_sntp.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include "wwvb_codec.h"

#define WWVBDEBUG

// Compile-time validation: warn if using default PoP in non-debug builds
#if !defined(WWVBDEBUG) && defined(CONFIG_WWVB_WIFI_POP)
#if __cplusplus
#warning "Using default PoP ('abcd1234') for production build. Change CONFIG_WWVB_WIFI_POP for security."
#else
#pragma message("WARNING: Using default PoP for production build. Set CONFIG_WWVB_WIFI_POP.")
#endif
#endif

// Function Prototypes - I wanted to keep this as a single file if people wanted to grab it and drop it into their projects
void TimerSignalReenable_ISR();
void ZeroCarrier();
void TimerSecond_ISR();
void TimerMinuteAlign_ISR(void *param);
void TimerSyncWait_ISR(void *param);
#if CONFIG_WWVB_STATUS_HEARTBEAT_ENABLE
void TimerHeartbeat_ISR(void *param);
#endif
void FrameBuilderTask(void *param);
void SetupWiFi();
void SetupSNTP();
void SetupTimers();
void SetupWWVBArray(uint8_t *signal);
void calculateDSTDays(int year, int *startDay, int *endDay);
bool isDaylightSavingTime(int year, int daysPassed);
void LogCurrentTime();
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void get_device_service_name(char *service_name, size_t max);
void Setup60KHzOutput();
void SNTP_callback (struct timeval *tv);
void HealthCheck_ISR();

// WWVB related
static const char *ntpServer = CONFIG_WWVB_NTP_SERVER;
static const gpio_num_t status_led_gpio = (gpio_num_t)CONFIG_WWVB_STATUS_LED_GPIO;
static const gpio_num_t wwvb_output_gpio = (gpio_num_t)CONFIG_WWVB_OUTPUT_GPIO;
static const uint32_t wwvb_carrier_freq_hz = (uint32_t)CONFIG_WWVB_CARRIER_FREQ_HZ;
static const int wifi_retry_limit = CONFIG_WWVB_WIFI_RETRY_LIMIT;
static const uint64_t status_led_blink_period_us = 1000000ULL / (2ULL * (uint64_t)CONFIG_WWVB_STATUS_LED_BLINK_HZ);
#if CONFIG_WWVB_STATUS_HEARTBEAT_ENABLE
static const uint64_t status_heartbeat_period_us = 1000000ULL / (2ULL * (uint64_t)CONFIG_WWVB_STATUS_HEARTBEAT_HZ);
#endif
uint8_t WWVBBufferA[60] = {0};
uint8_t WWVBBufferB[60] = {0};
uint8_t *activeWWVBBuffer = WWVBBufferA;
uint8_t *stagingWWVBBuffer = WWVBBufferB;
volatile uint8_t slot = 0;
volatile bool stagingFrameReady = false;
TaskHandle_t frameBuilderTaskHandle = NULL;

// WiFi Provisioning
bool is_provisioned = false;
bool provisioning_in_progress = false;
int s_retry_num = 0;
EventGroupHandle_t s_wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;

// Error recovery types
typedef enum
{
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED
} wifi_state_t;

typedef struct
{
    uint64_t uptime_sec;
    uint32_t frames_generated;
    uint32_t frames_swapped;
    uint32_t wifi_disconnects;
    uint32_t wifi_reconnects;
    uint32_t sntp_syncs;
    int64_t last_sync_time_ms;
    int64_t last_frame_generated_ms;
    int64_t last_frame_swapped_ms;
    int64_t last_second_tick_us;
    int64_t last_second_period_us;
    int64_t max_second_jitter_us;
    int64_t last_minute_period_us;
    int64_t max_minute_drift_us;
} runtime_metrics_t;

// WiFi state machine
static wifi_state_t wifi_state = WIFI_STATE_DISCONNECTED;
static runtime_metrics_t runtime_metrics = {0};

// // Bit & Marker timers
esp_timer_handle_t TimerBit0 = NULL;
esp_timer_handle_t TimerBit1 = NULL;
esp_timer_handle_t TimerBitMarker = NULL;

// // One Second timer
esp_timer_handle_t TimerSecond = NULL;
esp_timer_handle_t TimerMinuteAlign = NULL;
esp_timer_handle_t TimerSyncWait = NULL;
#if CONFIG_WWVB_STATUS_HEARTBEAT_ENABLE
esp_timer_handle_t TimerHeartbeat = NULL;
#endif

// 60KHz output
ledc_channel_config_t ledc_channel;

void app_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    ESP_LOGI("GPIO", "Configuring GPIO");

    gpio_reset_pin(status_led_gpio);
    gpio_set_direction(status_led_gpio, GPIO_MODE_OUTPUT);

    ESP_LOGI("NVS", "Initializing NVS partition");

    /* Initialize NVS partition */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated
        * and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_LOGI("WiFi", "Initializing WiFi");

    SetupWiFi();

    ESP_LOGI("WiFi", "Waiting for WiFi connection before SNTP initialization");
    xEventGroupWaitBits(s_wifi_event_group,
                        WIFI_CONNECTED_BIT,
                        pdFALSE,
                        pdFALSE,
                        portMAX_DELAY);

    ESP_LOGI("SNTP", "WWVB buffers will be initialized after SNTP sync");

    xTaskCreatePinnedToCore(FrameBuilderTask,
                            "WWVBFrameBuilder",
                            4096,
                            NULL,
                            5,
                            &frameBuilderTaskHandle,
                            tskNO_AFFINITY);

    ESP_LOGI("SNTP", "Initializing Timers");

    SetupTimers();

    ESP_LOGI("SNTP", "Initializing Signal Output");

    Setup60KHzOutput();

    ESP_LOGI("SNTP", "Initializing SNTP");

    SetupSNTP();

    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void SetupSNTP()
{
    // Toggle every half-cycle to achieve configured full-cycle blink rate.
    ESP_ERROR_CHECK(esp_timer_start_periodic(TimerSyncWait, status_led_blink_period_us));

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntpServer);
    sntp_config.sync_cb = SNTP_callback;

    // sntp_set_time_sync_notification_cb(SNTP_callback);
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_config));

    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK)
    {
        ESP_LOGI("SNTP", "Failed to update system time within 10s timeout");
    }
    else
    {
        ESP_LOGI("SNTP", "System time updated");
    }
}

void SNTP_callback (struct timeval *tv)
{
    ESP_LOGI("SNTP", "SNTP Syncronized");

    runtime_metrics.sntp_syncs++;
    runtime_metrics.last_sync_time_ms = esp_timer_get_time() / 1000;

    // Build initial synchronized frames before enabling transmission.
    SetupWWVBArray(activeWWVBBuffer);
    SetupWWVBArray(stagingWWVBBuffer);
    runtime_metrics.frames_generated += 2;
    runtime_metrics.last_frame_generated_ms = esp_timer_get_time() / 1000;
    stagingFrameReady = true;

    // Start first transmission on the next UTC minute boundary.
    slot = 0;

    if (esp_timer_is_active(TimerSyncWait))
        ESP_ERROR_CHECK(esp_timer_stop(TimerSyncWait));
    ESP_ERROR_CHECK(gpio_set_level(status_led_gpio, 0));

#if CONFIG_WWVB_STATUS_HEARTBEAT_ENABLE
    ESP_ERROR_CHECK(esp_timer_start_periodic(TimerHeartbeat, status_heartbeat_period_us));
#endif

    ESP_ERROR_CHECK(ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 127));
    ESP_ERROR_CHECK(ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel));

    // Use callback timestamp to align first bit to exact minute boundary.
    int64_t sec_in_minute = (int64_t)(tv->tv_sec % 60);
    if (sec_in_minute < 0)
        sec_in_minute += 60;

    int64_t usec_past_minute = sec_in_minute * 1000000LL + (int64_t)tv->tv_usec;
    int64_t usec_to_next_minute = (60000000LL - usec_past_minute) % 60000000LL;

    if (usec_to_next_minute == 0)
    {
        if (!esp_timer_is_active(TimerSecond))
            ESP_ERROR_CHECK(esp_timer_start_periodic(TimerSecond, 1000000)); // 1 second
    }
    else
    {
        int64_t wait_sec = usec_to_next_minute / 1000000LL;
        int64_t wait_msec = (usec_to_next_minute % 1000000LL) / 1000LL;
        ESP_LOGI("SNTP", "First WWVB frame starts at next minute boundary in %lld.%03lld s", wait_sec, wait_msec);
        ESP_ERROR_CHECK(esp_timer_start_once(TimerMinuteAlign, usec_to_next_minute));
    }
}

void SetupWiFi()
{
    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_wifi_event_group = xEventGroupCreate();

    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Configuration for the provisioning manager */
    wifi_prov_mgr_config_t wifi_prov_config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM};

    /* Initialize provisioning manager with the configuration parameters set above */
    ESP_ERROR_CHECK(wifi_prov_mgr_init(wifi_prov_config));

    /* Let's find out if the device is provisioned */
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&is_provisioned));

    ESP_LOGI("WiFI", "Is provisioned: %s", is_provisioned ? "true" : "false");

    /* If device is not yet provisioned start provisioning service */
    if (!is_provisioned)
    {
        ESP_LOGI("WiFi", "Starting provisioning");
        provisioning_in_progress = true;

        char service_name[12];
        get_device_service_name(service_name, sizeof(service_name));

        /* Do we want a proof-of-possession (ignored if Security 0 is selected):
         *      - this should be a string with length > 0
         *      - NULL if not used
         */
        const char *pop = CONFIG_WWVB_WIFI_POP;

        /* This is the structure for passing security parameters
         * for the protocomm security 1.
         */
        wifi_prov_security1_params_t *sec_params = pop;

        uint8_t custom_service_uuid[] = {
            /* LSB <---------------------------------------
             * ---------------------------------------> MSB */
            0xb4,
            0xdf,
            0x5a,
            0x1c,
            0x3f,
            0x6b,
            0xf4,
            0xbf,
            0xea,
            0x4a,
            0x82,
            0x03,
            0x04,
            0x90,
            0x1a,
            0x02,
        };

        ESP_ERROR_CHECK(wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid));

        /* Start provisioning service */
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, (const void *)sec_params, service_name, NULL));
    }
    else
    {
        ESP_LOGI("WiFi", "Already provisioned, starting Wi-Fi STA");

        /* We don't need the manager as device is already provisioned, so let's release it's resources */
        wifi_prov_mgr_deinit();

        /* Start Wi-Fi station */
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        /* Only attempt to connect if provisioning is complete */
        if (!provisioning_in_progress)
        {
            wifi_state = WIFI_STATE_CONNECTING;
            ESP_ERROR_CHECK(esp_wifi_connect());
        }
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        wifi_state = WIFI_STATE_DISCONNECTED;
        runtime_metrics.wifi_disconnects++;
        if (s_retry_num < wifi_retry_limit) 
        {
            ESP_ERROR_CHECK(esp_wifi_connect());
            s_retry_num++;
            ESP_LOGI("WiFi", "retry to connect to the AP (%d/%d)", s_retry_num, wifi_retry_limit);
        } 
        else 
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI("WiFi","connect to the AP fail");
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI("WiFi", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        if (wifi_state != WIFI_STATE_CONNECTED)
        {
            runtime_metrics.wifi_reconnects++;
        }
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        wifi_state = WIFI_STATE_CONNECTED;
    } 
    else if (event_base == WIFI_PROV_EVENT) 
    {
        switch (event_id) 
        {
            case WIFI_PROV_START:
                ESP_LOGI("WiFi", "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: 
            {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI("WiFi", "Received Wi-Fi credentials"
                         "\n\tSSID     : %s\n\tPassword : %s",
                         (const char *) wifi_sta_cfg->ssid,
                         (const char *) wifi_sta_cfg->password);
                break;
            }
            case WIFI_PROV_CRED_FAIL: 
            {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE("WiFi", "Provisioning failed!\n\tReason : %s" "\n\tPlease reset to factory and retry provisioning",  (*reason == WIFI_PROV_STA_AUTH_ERROR) ?  "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI("WiFi", "Provisioning successful");
                break;
            case WIFI_PROV_END:
                /* De-initialize manager once provisioning is finished */
                provisioning_in_progress = false;
                wifi_prov_mgr_deinit();
                /* Now attempt to connect with the provisioned credentials */
                ESP_LOGI("WiFi", "Provisioning complete, connecting to AP");
                ESP_ERROR_CHECK(esp_wifi_connect());
                break;
            default:
                break;
        }
    }
}

void LogCurrentTime()
{
    time_t rawtime;
    struct tm *utcTime;

    time(&rawtime);
    utcTime = gmtime(&rawtime);

    // Format the time as a string
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", utcTime);

    int year = utcTime->tm_year + 1900;
    int dayOfYear = utcTime->tm_yday + 1;
    int dstStartDay = 0;
    int dstEndDay = 0;
    bool isDst = isDaylightSavingTime(year, dayOfYear);
    calculateDSTDays(year, &dstStartDay, &dstEndDay);

    // Write the system time as a log entry
    ESP_LOGI("Time", "Current system time: %s (UTC day %d, DST: %s, start day: %d, end day: %d)",
             strftime_buf,
             dayOfYear,
             isDst ? "ON" : "OFF",
             dstStartDay,
             dstEndDay);
}

void SetupWWVBArray(uint8_t *signal)
{
    time_t rawtime;
    struct tm utcTime;

    time(&rawtime);
    // Frame builder runs one minute ahead so the swapped-in frame matches the next minute boundary.
    rawtime += 60;
    gmtime_r(&rawtime, &utcTime);

    // Build a complete one-minute frame for the given UTC minute.
    encodeYear(utcTime.tm_year + 1900, signal);
    encodeDayOfYear(utcTime.tm_yday + 1, signal);
    encodeHour(utcTime.tm_hour, signal);
    encodeMinute(utcTime.tm_min, signal);
    setMarkersAndIndicators(signal);
    setDUT1(signal); // We're ignoring DUT1 as it has been deprecated and not used in this scenario
    setLeapYear(utcTime.tm_year + 1900, signal);
    setLeapSecond(false, signal); // Ignore leap seconds in this scenario
    setDST(isDaylightSavingTime(utcTime.tm_year + 1900, utcTime.tm_yday + 1), signal);
}

void SetupTimers()
{
    // Setup 1 second timer
    const esp_timer_create_args_t timer_second_config = {
        .callback = &TimerSecond_ISR,
        .name = "One Second Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_second_config, &TimerSecond));

    // Setup Bit 0 timer
    const esp_timer_create_args_t timer_bit0_config = {
        .callback = &TimerSignalReenable_ISR,
        .name = "Bit 0 Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_bit0_config, &TimerBit0));
            
    // Setup Bit 1 timer
    const esp_timer_create_args_t timer_bit1_config = {
        .callback = &TimerSignalReenable_ISR,
        .name = "Bit 1 Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_bit1_config, &TimerBit1));

    // Setup Bit Marker timer
    const esp_timer_create_args_t timer_bitmarker_config = {
        .callback = &TimerSignalReenable_ISR,
        .name = "Bit Marker Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_bitmarker_config, &TimerBitMarker));

    // Setup timer to blink red LED at 8 Hz while waiting for SNTP sync
    const esp_timer_create_args_t timer_wait_sync_config = {
        .callback = &TimerSyncWait_ISR,
        .name = "Wait Sync Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_wait_sync_config, &TimerSyncWait));

    // Setup one-shot timer to align first transmission to UTC minute boundary.
    const esp_timer_create_args_t timer_minute_align_config = {
        .callback = &TimerMinuteAlign_ISR,
        .name = "Minute Align Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_minute_align_config, &TimerMinuteAlign));

#if CONFIG_WWVB_STATUS_HEARTBEAT_ENABLE
    // Setup periodic heartbeat LED timer
    const esp_timer_create_args_t timer_heartbeat_config = {
        .callback = &TimerHeartbeat_ISR,
        .name = "Heartbeat Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_heartbeat_config, &TimerHeartbeat));
#endif
}

void TimerMinuteAlign_ISR(void *param)
{
    (void)param;
    slot = 0;

    if (!esp_timer_is_active(TimerSecond))
        ESP_ERROR_CHECK(esp_timer_start_periodic(TimerSecond, 1000000)); // 1 second
}

void TimerSyncWait_ISR(void *param)
{
    (void)param;
    static bool waiting_led_on;
    waiting_led_on = !waiting_led_on;
    ESP_ERROR_CHECK(gpio_set_level(status_led_gpio, waiting_led_on));
}

#if CONFIG_WWVB_STATUS_HEARTBEAT_ENABLE
void TimerHeartbeat_ISR(void *param)
{
    (void)param;
    static bool heartbeat_on;
    heartbeat_on = !heartbeat_on;
    ESP_ERROR_CHECK(gpio_set_level(status_led_gpio, heartbeat_on));
}
#endif

// All the bit/marker timers just reenable the 50%^ duty cycle of the 60KHz signal
void IRAM_ATTR TimerSignalReenable_ISR()
{
    //analogWrite(A0, 127);
    ESP_ERROR_CHECK(ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 127));
    ESP_ERROR_CHECK(ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel));
}

void TimerSecond_ISR(void *param)
{
    (void)param;
  static int64_t last_minute_boundary_us = 0;
  int64_t now_us = esp_timer_get_time();

    if (runtime_metrics.last_second_tick_us != 0)
    {
        int64_t second_period_us = now_us - runtime_metrics.last_second_tick_us;
        int64_t second_jitter_us = second_period_us - 1000000;
        int64_t abs_second_jitter_us = (second_jitter_us < 0) ? -second_jitter_us : second_jitter_us;

        runtime_metrics.last_second_period_us = second_period_us;
        if (abs_second_jitter_us > runtime_metrics.max_second_jitter_us)
            runtime_metrics.max_second_jitter_us = abs_second_jitter_us;
    }
    runtime_metrics.last_second_tick_us = now_us;

    if (slot == 0)
    {
        if (last_minute_boundary_us != 0)
        {
            int64_t minute_period_us = now_us - last_minute_boundary_us;
            int64_t minute_drift_us = minute_period_us - 60000000;
            int64_t abs_minute_drift_us = (minute_drift_us < 0) ? -minute_drift_us : minute_drift_us;

            runtime_metrics.last_minute_period_us = minute_period_us;
            if (abs_minute_drift_us > runtime_metrics.max_minute_drift_us)
                runtime_metrics.max_minute_drift_us = abs_minute_drift_us;
        }
        last_minute_boundary_us = now_us;

            // Only touch transmit buffer pointers at the minute boundary.
            if (stagingFrameReady)
            {
                    uint8_t *previousActive = activeWWVBBuffer;
                    activeWWVBBuffer = stagingWWVBBuffer;
                    stagingWWVBBuffer = previousActive;
                    stagingFrameReady = false;
                    runtime_metrics.frames_swapped++;
            runtime_metrics.last_frame_swapped_ms = now_us / 1000;
            }

            if (frameBuilderTaskHandle != NULL)
                    xTaskNotifyGive(frameBuilderTaskHandle);
    }
  
    switch (activeWWVBBuffer[slot])
  {
  case 0:
  {
      #ifdef WWVBDEBUG
      printf("0");
      #endif

      // 0 (0.2s reduced power)
      ZeroCarrier();

      // TimerBit0
      ESP_ERROR_CHECK(esp_timer_start_once(TimerBit0, 200000)); // 0.2 second
    }
  break;
  case 1:
  {
      #ifdef WWVBDEBUG
      printf("1");
      #endif

      // 1 (0.5s reduced power)
      ZeroCarrier();

      // TimerBit1
      ESP_ERROR_CHECK(esp_timer_start_once(TimerBit1, 500000)); // 0.5 second

  }
  break;
  case 2:
  {
      #ifdef WWVBDEBUG
      printf("M");
      #endif

      // Marker (0.8s reduced power)
      ZeroCarrier();

      // TimerBitMarker
      ESP_ERROR_CHECK(esp_timer_start_once(TimerBitMarker, 800000)); // 0.8 second
  }
  break;
  }

  slot++; // Advance data slot in minute data packet
  if (slot == 60)
  {
      slot = 0; // Reset slot to 0 if at 60 seconds
      #ifdef WWVBDEBUG
      printf("\n");
      LogCurrentTime();
      #endif

      HealthCheck_ISR();
  }
}

void FrameBuilderTask(void *param)
{
    (void)param;

    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        SetupWWVBArray(stagingWWVBBuffer);
        runtime_metrics.frames_generated++;
        runtime_metrics.last_frame_generated_ms = esp_timer_get_time() / 1000;
        stagingFrameReady = true;
    }
}

void HealthCheck_ISR()
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    int64_t gen_age_ms = (runtime_metrics.last_frame_generated_ms > 0) ? (now_ms - runtime_metrics.last_frame_generated_ms) : -1;
    int64_t swap_age_ms = (runtime_metrics.last_frame_swapped_ms > 0) ? (now_ms - runtime_metrics.last_frame_swapped_ms) : -1;

    runtime_metrics.uptime_sec = esp_timer_get_time() / 1000000;
    ESP_LOGI("HEALTH", "Up:%llu sec|Frm:gen=%lu sw=%lu genAge=%lldms swAge=%lldms|Tick:last=%lldus maxJit=%lldus|Min:last=%lldus maxDrift=%lldus|WiFi:dis=%lu con=%lu st=%d|SNTP:syn=%lu",
             runtime_metrics.uptime_sec,
             runtime_metrics.frames_generated,
             runtime_metrics.frames_swapped,
             gen_age_ms,
             swap_age_ms,
             runtime_metrics.last_second_period_us,
             runtime_metrics.max_second_jitter_us,
             runtime_metrics.last_minute_period_us,
             runtime_metrics.max_minute_drift_us,
             runtime_metrics.wifi_disconnects,
             runtime_metrics.wifi_reconnects,
             wifi_state,
             runtime_metrics.sntp_syncs);
}

void ZeroCarrier()
{
    ESP_ERROR_CHECK(ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0));
    ESP_ERROR_CHECK(ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel));
}

void Setup60KHzOutput()
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
        .freq_hz = wwvb_carrier_freq_hz,      // frequency of PWM signal
        .speed_mode = LEDC_HIGH_SPEED_MODE,   // timer mode
        .timer_num = LEDC_TIMER_0             // timer index
    };
    
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel.channel = LEDC_CHANNEL_0;
    ledc_channel.duty = 0; // Hold output low until SNTP synchronization
    ledc_channel.gpio_num = wwvb_output_gpio;
    ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_channel.timer_sel = LEDC_TIMER_0;

    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char *ssid_prefix = "PROV_";
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, eth_mac));
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

// The following is AI generated code
// This should be checked to see if it is actually works as expected
// I just needed something quick to fill this DST calc

// Function to calculate the start and end days for DST in a given year
void calculateDSTDays(int year, int *startDay, int *endDay)
{
    struct tm date = {0};

    // Calculate the second Sunday in March
    date.tm_year = year - 1900;
    date.tm_mon = 2;   // March (0-based)
    date.tm_mday = 1;  // March 1st
    mktime(&date);
    int firstSundayMarch = (date.tm_wday == 0) ? 1 : (8 - date.tm_wday);
    date.tm_mday = firstSundayMarch + 7; // Second Sunday
    mktime(&date);
    *startDay = date.tm_yday + 1; // 1-based day-of-year

    // Calculate the first Sunday in November
    date.tm_mon = 10;  // November (0-based)
    date.tm_mday = 1;  // November 1st
    mktime(&date);
    int firstSundayNovember = (date.tm_wday == 0) ? 1 : (8 - date.tm_wday);
    date.tm_mday = firstSundayNovember;
    mktime(&date);
    *endDay = date.tm_yday + 1; // 1-based day-of-year
}

// Function to check if the current day is within DST period
bool isDaylightSavingTime(int year, int daysPassed)
{
    int startDay, endDay;
    calculateDSTDays(year, &startDay, &endDay);
    return (daysPassed >= startDay && daysPassed < endDay);
}
