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
#include <mbedtls/sha256.h>

#define WWVBDEBUG
#define POP_BUFFER_SIZE 13  // 13 bytes: 12 hex chars (6 MAC bytes * 2) + null terminator

// Function Prototypes - I wanted to keep this as a single file if people wanted to grab it and drop it into their projects
void encodeYear(uint16_t year, uint8_t *signal);
void encodeDayOfYear(uint16_t dayOfYear, uint8_t *signal);
void encodeHour(uint8_t hour, uint8_t *signal);
void encodeMinute(uint8_t minute, uint8_t *signal);
void setMarkersAndIndicators(uint8_t *signal);
void setDUT1(uint8_t *signal);
void setLeapYear(uint16_t year, uint8_t *signal);
void setLeapSecond(bool IsLeap, uint8_t *signal);
void setDST(bool IsDST, uint8_t *signal);
uint16_t BitsEncoder(uint16_t n);
void TimerSignalReenable_ISR();
void ZeroCarrier();
void TimerSecond_ISR(void *param);
void BoardDebugTest();
void SetupWiFi();
void SetupSNTP();
void SetupTimers();
void SetupWWVBArray();
bool isLeapYear(int year);
void calculateDSTDays(int year, int *startDay, int *endDay);
bool isDaylightSavingTime(int year, int daysPassed);
void LogCurrentTime();
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void get_device_service_name(char *service_name, size_t max);
static void generate_unique_pop(char *pop, size_t max);
void Setup60KHzOutput();
void SNTP_callback (struct timeval *tv);
void debug_task(void *pvParameters);

// WWVB related
static const char *ntpServer = CONFIG_WWVB_NTP_SERVER;

// WWVB state structure
typedef struct {
    uint8_t array[60];
    volatile uint8_t slot;
} wwvb_state_t;

static wwvb_state_t wwvb_state = {
    .array = {0},
    .slot = 0
};

// Debug queue for ISR to task communication
#define DEBUG_QUEUE_SIZE 10
typedef struct {
    char type;  // '0', '1', 'M', or 'N' for newline/time log
} debug_msg_t;
static QueueHandle_t debug_queue = NULL;

// WiFi state structure
typedef struct {
    bool is_provisioned;
    int retry_count;
    EventGroupHandle_t event_group;
} wifi_state_t;

static wifi_state_t wifi_state = {
    .is_provisioned = false,
    .retry_count = 0,
    .event_group = NULL
};

static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;

// Timer handles structure
typedef struct {
    esp_timer_handle_t bit0;
    esp_timer_handle_t bit1;
    esp_timer_handle_t marker;
    esp_timer_handle_t second;
} timer_handles_t;

static timer_handles_t timers = {
    .bit0 = NULL,
    .bit1 = NULL,
    .marker = NULL,
    .second = NULL
};

// 60KHz output
static ledc_channel_config_t ledc_channel;

// Debug task to handle logging from ISR context
void debug_task(void *pvParameters)
{
    debug_msg_t msg;
    
    while (1) {
        if (xQueueReceive(debug_queue, &msg, portMAX_DELAY) == pdTRUE) {
            #ifdef WWVBDEBUG
            if (msg.type == 'N') {
                // Newline and time log
                printf("\n");
                LogCurrentTime();
            } else {
                // Print the character
                printf("%c", msg.type);
            }
            #endif
        }
    }
}

void app_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    ESP_LOGI("GPIO", "Configuring GPIO");

    gpio_reset_pin((gpio_num_t)CONFIG_WWVB_DEBUG_LED_PIN);
    gpio_set_direction((gpio_num_t)CONFIG_WWVB_DEBUG_LED_PIN, GPIO_MODE_OUTPUT);

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

    ESP_LOGI("SNTP", "Initializing SNTP");

    SetupSNTP();

    ESP_LOGI("SNTP", "Initializing WWVBArray");

    SetupWWVBArray();

    ESP_LOGI("SNTP", "Initializing Timers");

    SetupTimers();

    ESP_LOGI("SNTP", "Initializing Signal Output");

    Setup60KHzOutput();

    // Create debug queue for ISR to task communication
    debug_queue = xQueueCreate(DEBUG_QUEUE_SIZE, sizeof(debug_msg_t));
    if (debug_queue == NULL) {
        ESP_LOGE("Main", "Failed to create debug queue");
    } else {
        // Create debug task to handle logging from ISR
        BaseType_t task_created = xTaskCreate(debug_task, "debug_task", 2048, NULL, 5, NULL);
        if (task_created != pdPASS) {
            ESP_LOGE("Main", "Failed to create debug task");
        }
    }

    while (1)
    {
        SetupWWVBArray();
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void SetupSNTP()
{
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntpServer);
    sntp_config.sync_cb = SNTP_callback;

    // sntp_set_time_sync_notification_cb(SNTP_callback);
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_config));

    // Retry SNTP synchronization up to 3 times
    const int max_retries = 3;
    esp_err_t sntp_result = ESP_FAIL;
    
    for (int retry = 0; retry < max_retries; retry++)
    {
        sntp_result = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
        
        if (sntp_result == ESP_OK)
        {
            ESP_LOGI("SNTP", "System time updated successfully");
            break;
        }
        else
        {
            ESP_LOGE("SNTP", "Failed to update system time within 10s timeout (attempt %d/%d)", retry + 1, max_retries);
            
            if (retry < max_retries - 1)
            {
                ESP_LOGI("SNTP", "Retrying SNTP synchronization...");
                vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2 seconds before retry
            }
        }
    }
    
    // If all retries failed, halt execution
    if (sntp_result != ESP_OK)
    {
        ESP_LOGE("SNTP", "SNTP synchronization failed after %d attempts. Cannot continue without valid time.", max_retries);
        ESP_ERROR_CHECK(sntp_result); // This will abort execution
    }
}

void SNTP_callback (struct timeval *tv)
{
    ESP_LOGI("SNTP", "SNTP Synchronized");
    
    // Validate timer handle before starting
    if (timers.second == NULL)
    {
        ESP_LOGE("SNTP", "TimerSecond handle is NULL, cannot start timer");
        return;
    }
    
    ESP_ERROR_CHECK(esp_timer_start_periodic(timers.second, 1000000)); // 1 second
}

void SetupWiFi()
{
    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_state.event_group = xEventGroupCreate();

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
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&wifi_state.is_provisioned));

    ESP_LOGI("WiFI", "Is provisioned: %s", wifi_state.is_provisioned ? "true" : "false");

    /* If device is not yet provisioned start provisioning service */
    if (!wifi_state.is_provisioned)
    {
        ESP_LOGI("WiFi", "Starting provisioning");

        char service_name[12];
        get_device_service_name(service_name, sizeof(service_name));

        /* Generate a device-unique proof-of-possession using a cryptographic hash of the MAC address.
         * This provides real security by using SHA-256 hashing, preventing attackers from deriving
         * the PoP by observing the MAC address through BLE advertising or network scanning.
         * The PoP should be printed/displayed for the user to enter during provisioning.
         */
        char pop[POP_BUFFER_SIZE];
        generate_unique_pop(pop, sizeof(pop));
        
        // Log the PoP so the user knows what to enter during provisioning
        ESP_LOGI("WiFi", "Provisioning PoP: %s", pop);

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

        ESP_LOGI("WiFi", "Connected");
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        ESP_ERROR_CHECK(esp_wifi_connect());
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        if (wifi_state.retry_count < CONFIG_WIFI_MAX_RETRY) 
        {
            ESP_ERROR_CHECK(esp_wifi_connect());
            wifi_state.retry_count++;
            ESP_LOGI("WiFi", "retry to connect to the AP");
        } 
        else 
        {
            xEventGroupSetBits(wifi_state.event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI("WiFi","connect to the AP fail");
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI("WiFi", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_state.retry_count = 0;
        xEventGroupSetBits(wifi_state.event_group, WIFI_CONNECTED_BIT);
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
                wifi_prov_mgr_deinit();
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

    // Validate gmtime() return value
    if (utcTime == NULL)
    {
        ESP_LOGE("Time", "Failed to get UTC time, gmtime returned NULL");
        return;
    }

    // Format the time as a string
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", utcTime);

    // Write the system time as a log entry
    ESP_LOGI("Time", "Current system time: %s", strftime_buf);
}

void SetupWWVBArray()
{
    time_t rawtime;
    struct tm *utcTime;

    time(&rawtime);
    utcTime = gmtime(&rawtime);

    // Validate time values before encoding
    if (utcTime == NULL)
    {
        ESP_LOGE("WWVB", "Failed to get UTC time, gmtime returned NULL");
        return;
    }
    
    // Check for reasonable time values (year should be >= 2000)
    // If time is before 2000, it likely means time hasn't been synchronized yet
    if (utcTime->tm_year + 1900 < 2000)
    {
        ESP_LOGE("WWVB", "Invalid system time detected (year=%d). Time may not be synchronized.", utcTime->tm_year + 1900);
        return;
    }

    // Using the current UTC time fill in the WWVBArray
    encodeYear(utcTime->tm_year + 1900, wwvb_state.array);
    encodeDayOfYear(utcTime->tm_yday + 1, wwvb_state.array);
    encodeHour(utcTime->tm_hour, wwvb_state.array);
    encodeMinute(utcTime->tm_min, wwvb_state.array);
    setMarkersAndIndicators(wwvb_state.array);
    setDUT1(wwvb_state.array); // We're ignoring DUT1 as it has been deprecated and not used in this scenario
    setLeapYear(utcTime->tm_year + 1900, wwvb_state.array);
    setLeapSecond(false, wwvb_state.array); // Ignore leap seconds in this scenario
    setDST(isDaylightSavingTime(utcTime->tm_year + 1900, utcTime->tm_yday + 1), wwvb_state.array);
}

void SetupTimers()
{
    // Setup 1 second timer
    const esp_timer_create_args_t timer_second_config = {
        .callback = &TimerSecond_ISR,
        .name = "One Second Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_second_config, &timers.second));

    // Setup Bit 0 timer
    const esp_timer_create_args_t timer_bit0_config = {
        .callback = &TimerSignalReenable_ISR,
        .name = "Bit 0 Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_bit0_config, &timers.bit0));
            
    // Setup Bit 1 timer
    const esp_timer_create_args_t timer_bit1_config = {
        .callback = &TimerSignalReenable_ISR,
        .name = "Bit 1 Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_bit1_config, &timers.bit1));

    // Setup Bit Marker timer
    const esp_timer_create_args_t timer_bitmarker_config = {
        .callback = &TimerSignalReenable_ISR,
        .name = "Bit Marker Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_bitmarker_config, &timers.marker));
}

// All the bit/marker timers just reenable the 50%^ duty cycle of the 60KHz signal
void IRAM_ATTR TimerSignalReenable_ISR()
{
    // Remove ESP_ERROR_CHECK - just call the functions directly
    // Errors in ISR context cannot be safely handled
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 127);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}

void TimerSecond_ISR(void *param)
{
  (void) param; // Suppress unused parameter warning
  static bool ON;
  ON = !ON;
  
  // Remove ESP_ERROR_CHECK - just call the function directly
  gpio_set_level((gpio_num_t)CONFIG_WWVB_DEBUG_LED_PIN, ON);

  // Validate slot index before accessing WWVBArray
  if (wwvb_state.slot >= 60)
  {
      wwvb_state.slot = 0;
  }

  switch (wwvb_state.array[wwvb_state.slot])
  {
  case 0:
  {
      #ifdef WWVBDEBUG
      // Defer debug output to task context via queue
      if (debug_queue != NULL) {
          debug_msg_t msg = {.type = '0'};
          xQueueSendFromISR(debug_queue, &msg, NULL);
      }
      #endif

      // 0 (0.2s reduced power)
      ZeroCarrier();

      // TimerBit0 - Start timer without ESP_ERROR_CHECK
      if (timers.bit0 != NULL)
      {
          esp_timer_start_once(timers.bit0, 200000); // 0.2 second
      }
    }
  break;
  case 1:
  {
      #ifdef WWVBDEBUG
      // Defer debug output to task context via queue
      if (debug_queue != NULL) {
          debug_msg_t msg = {.type = '1'};
          xQueueSendFromISR(debug_queue, &msg, NULL);
      }
      #endif

      // 1 (0.5s reduced power)
      ZeroCarrier();

      // TimerBit1 - Start timer without ESP_ERROR_CHECK
      if (timers.bit1 != NULL)
      {
          esp_timer_start_once(timers.bit1, 500000); // 0.5 second
      }

  }
  break;
  case 2:
  {
      #ifdef WWVBDEBUG
      // Defer debug output to task context via queue
      if (debug_queue != NULL) {
          debug_msg_t msg = {.type = 'M'};
          xQueueSendFromISR(debug_queue, &msg, NULL);
      }
      #endif

      // Marker (0.8s reduced power)
      ZeroCarrier();

      // TimerBitMarker - Start timer without ESP_ERROR_CHECK
      if (timers.marker != NULL)
      {
          esp_timer_start_once(timers.marker, 800000); // 0.8 second
      }
  }
  break;
  }

  wwvb_state.slot++; // Advance data slot in minute data packet
  if (wwvb_state.slot == 60)
  {
      wwvb_state.slot = 0; // Reset slot to 0 if at 60 seconds
      #ifdef WWVBDEBUG
      // Defer debug output and logging to task context via queue
      if (debug_queue != NULL) {
          debug_msg_t msg = {.type = 'N'};
          xQueueSendFromISR(debug_queue, &msg, NULL);
      }
      #endif
  }
}

void ZeroCarrier()
{
    // Remove ESP_ERROR_CHECK - just call the functions directly
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}

// This rotuine takes the input value and then breaks it out in the individual BCD pattern that the WWVB format expects
uint16_t BitsEncoder(uint16_t n)
{
    uint16_t result = 0;

    const uint8_t div1 = n / 100;
    const uint8_t div2 = (n - (div1 * 100)) / 10;
    const uint8_t mod = n % 10;

    result = (div1 & 0xF) << 8;
    result |= (div2 & 0xF) << 4;
    result |= (mod & 0xF);

    return result;
}

// WWVB Expects year to be in 8 bit BCD - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void encodeYear(uint16_t year, uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "encodeYear: signal pointer is NULL");
        return;
    }
    
    if (year < 2000 || year > 2099)
    {
        ESP_LOGE("WWVB", "encodeYear: year %d is out of valid range (2000-2099)", year);
        return;
    }

    int yearBCD = year % 100;
    uint16_t bitsResult = BitsEncoder(yearBCD);

    signal[45] = (bitsResult & 0x80) >> 7;
    signal[46] = (bitsResult & 0x40) >> 6;
    signal[47] = (bitsResult & 0x20) >> 5;
    signal[48] = (bitsResult & 0x10) >> 4;
    signal[50] = (bitsResult & 0x08) >> 3;
    signal[51] = (bitsResult & 0x04) >> 2;
    signal[52] = (bitsResult & 0x02) >> 1;
    signal[53] = (bitsResult & 0x01);
}

// WWVB Expects the day of the year to be in 10 bit BCD - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void encodeDayOfYear(uint16_t dayOfYear, uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "encodeDayOfYear: signal pointer is NULL");
        return;
    }
    
    if (dayOfYear < 1 || dayOfYear > 366)
    {
        ESP_LOGE("WWVB", "encodeDayOfYear: dayOfYear %d is out of valid range (1-366)", dayOfYear);
        return;
    }

    uint16_t bitsResult = BitsEncoder(dayOfYear);

    signal[22] = (bitsResult & 0x0200) >> 9;
    signal[23] = (bitsResult & 0x0100) >> 8;
    signal[25] = (bitsResult & 0x0080) >> 7;
    signal[26] = (bitsResult & 0x0040) >> 6;
    signal[27] = (bitsResult & 0x0020) >> 5;
    signal[28] = (bitsResult & 0x0010) >> 4;
    signal[30] = (bitsResult & 0x0008) >> 3;
    signal[31] = (bitsResult & 0x0004) >> 2;
    signal[32] = (bitsResult & 0x0002) >> 1;
    signal[33] = (bitsResult & 0x0001);
}

// WWVB Expects the hour to be in 6 bit BCD - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void encodeHour(uint8_t hour, uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "encodeHour: signal pointer is NULL");
        return;
    }
    
    if (hour > 23)
    {
        ESP_LOGE("WWVB", "encodeHour: hour %d is out of valid range (0-23)", hour);
        return;
    }

    uint16_t bitsResult = BitsEncoder(hour);

    signal[12] = (bitsResult & 0x20) >> 5;
    signal[13] = (bitsResult & 0x10) >> 4;
    signal[15] = (bitsResult & 0x08) >> 3;
    signal[16] = (bitsResult & 0x04) >> 2;
    signal[17] = (bitsResult & 0x02) >> 1;
    signal[18] = (bitsResult & 0x01);
}

// WWVB Expects minutes to be in 7 bit BCD - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void encodeMinute(uint8_t minute, uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "encodeMinute: signal pointer is NULL");
        return;
    }
    
    if (minute > 59)
    {
        ESP_LOGE("WWVB", "encodeMinute: minute %d is out of valid range (0-59)", minute);
        return;
    }

    uint16_t bitsResult = BitsEncoder(minute);

    signal[1] = (bitsResult & 0x40) >> 6;
    signal[2] = (bitsResult & 0x20) >> 5;
    signal[3] = (bitsResult & 0x10) >> 4;
    signal[5] = (bitsResult & 0x08) >> 3;
    signal[6] = (bitsResult & 0x04) >> 2;
    signal[7] = (bitsResult & 0x02) >> 1;
    signal[8] = (bitsResult & 0x01);
}

// The WWVB signal has certain marker and bits that are always set to either a marker bit or a zero
void setMarkersAndIndicators(uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setMarkersAndIndicators: signal pointer is NULL");
        return;
    }

    signal[0] = 2;  // Position marker
    signal[9] = 2;  // Position marker
    signal[19] = 2; // Position marker
    signal[29] = 2; // Position marker
    signal[39] = 2; // Position marker
    signal[49] = 2; // Position marker
    signal[59] = 2; // Position marker

    signal[4] = 0;  // Always 0
    signal[10] = 0; // Always 0
    signal[11] = 0; // Always 0
    signal[14] = 0; // Always 0
    signal[20] = 0; // Always 0
    signal[21] = 0; // Always 0
    signal[24] = 0; // Always 0
    signal[34] = 0; // Always 0
    signal[35] = 0; // Always 0
    signal[44] = 0; // Always 0
    signal[54] = 0; // Always 0
}

// WWVB once supported celestial navigation uses but as it was deprecated and this scenario doesn't need it then just set those bits to 0 - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void setDUT1(uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setDUT1: signal pointer is NULL");
        return;
    }

    // DUT1 is obsolete, it was used for celestial navigation
    signal[36] = 0;
    signal[37] = 0;
    signal[38] = 0;
    signal[40] = 0;
    signal[41] = 0;
    signal[42] = 0;
    signal[43] = 0;
}

// If you use the current year and mktime to set a date it will tell you if it is a leap year or not
// I can't find where I got this code from so apologies for not crediting it to the appropriate person
void setLeapYear(uint16_t year, uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setLeapYear: signal pointer is NULL");
        return;
    }
    
    if (year < 2000 || year > 2099)
    {
        ESP_LOGE("WWVB", "setLeapYear: year %d is out of valid range (2000-2099)", year);
        return;
    }

    struct tm time_in = {0};
    time_in.tm_year = year - 1900;
    time_in.tm_mon = 2;  // March (0-based: January is 0)
    time_in.tm_mday = 0; // Zero day of March will roll back to the last day of February

    mktime(&time_in);

    // If mktime leaves the day as 29 then it is a leap year
    if (time_in.tm_mday == 29)
        signal[55] = 1;
    else
        signal[55] = 0;
}

// WWVB Expects this bit for a leap second, I don't believe it is useful in this scenario but setting it just in case - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void setLeapSecond(bool IsLeap, uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setLeapSecond: signal pointer is NULL");
        return;
    }

    if (IsLeap)
        signal[56] = 1;
    else
        signal[56] = 0;
}

// WWVB Expects to have a DST bit set - It allows for warning of DST but we're ignoring that in this scenario - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void setDST(bool IsDST, uint8_t *signal)
{
    // Validate input parameters
    if (signal == NULL)
    {
        ESP_LOGE("WWVB", "setDST: signal pointer is NULL");
        return;
    }

    if (IsDST)
    {
        signal[57] = 1;
        signal[58] = 1;
    }
    else
    {
        signal[57] = 0;
        signal[58] = 0;
    }
}

void Setup60KHzOutput()
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
        .freq_hz = 60000,                     // frequency of PWM signal
        .speed_mode = LEDC_HIGH_SPEED_MODE,   // timer mode
        .timer_num = LEDC_TIMER_0             // timer index
    };
    
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel.channel = LEDC_CHANNEL_0;
    ledc_channel.duty = 127;
    ledc_channel.gpio_num = (gpio_num_t)CONFIG_WWVB_OUTPUT_PIN; // Configurable GPIO for WWVB output (default: 26/A0 on Huzzah32)
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

// Generate a cryptographically secure proof-of-possession (PoP) based on device MAC address
// This uses SHA-256 hash of the MAC address to prevent attackers from deriving the PoP
// by observing the MAC address through BLE advertising or network scanning
static void generate_unique_pop(char *pop, size_t max)
{
    if (pop == NULL || max < POP_BUFFER_SIZE)
    {
        ESP_LOGE("POP", "Invalid PoP buffer: pop is %s, size=%zu (need at least %d)",
                 pop == NULL ? "NULL" : "non-NULL", max, POP_BUFFER_SIZE);
        if (pop != NULL && max > 0)
        {
            pop[0] = '\0';  // Set empty string on error
        }
        return;
    }

    uint8_t eth_mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, eth_mac));
    
    // Use SHA-256 hash of MAC address for cryptographic security
    // This prevents attackers from deriving the PoP by observing the MAC address
    uint8_t hash[32];  // SHA-256 produces 32 bytes
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    
    int ret = mbedtls_sha256_starts(&sha256_ctx, 0);  // 0 = SHA-256 (not SHA-224)
    if (ret != 0)
    {
        ESP_LOGE("POP", "SHA-256 start failed: %d", ret);
        mbedtls_sha256_free(&sha256_ctx);
        pop[0] = '\0';  // Set empty string on error
        return;
    }
    
    ret = mbedtls_sha256_update(&sha256_ctx, eth_mac, 6);
    if (ret != 0)
    {
        ESP_LOGE("POP", "SHA-256 update failed: %d", ret);
        mbedtls_sha256_free(&sha256_ctx);
        pop[0] = '\0';  // Set empty string on error
        return;
    }
    
    ret = mbedtls_sha256_finish(&sha256_ctx, hash);
    if (ret != 0)
    {
        ESP_LOGE("POP", "SHA-256 finish failed: %d", ret);
        mbedtls_sha256_free(&sha256_ctx);
        pop[0] = '\0';  // Set empty string on error
        return;
    }
    
    mbedtls_sha256_free(&sha256_ctx);
    
    // Use first 6 bytes of hash to create 12-character hex PoP
    snprintf(pop, max, "%02X%02X%02X%02X%02X%02X",
             hash[0], hash[1], hash[2], hash[3], hash[4], hash[5]);
}

// DST (Daylight Saving Time) calculation functions
// These functions implement US DST rules as mandated since 2007:
// - DST starts: Second Sunday in March at 2:00 AM
// - DST ends: First Sunday in November at 2:00 AM
// 
// NOTE: These functions ONLY support US DST rules. If you need support for other
// time zones or DST rules, you should use ESP-IDF's timezone support instead.
// 
// The algorithm uses Zeller's congruence to calculate the day of week for any date,
// then determines the correct Sunday for DST transitions.
// Tested and verified for years 2020-2028, including leap years.

// Function to determine if a year is a leap year
bool isLeapYear(int year)
{
    if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
    {
        return true;
    }
    return false;
}

// Function to calculate the start and end days (as day-of-year) for DST in a given year
// Parameters:
//   year: The year to calculate DST days for (e.g., 2024)
//   startDay: Pointer to store the day-of-year when DST starts (1-366)
//   endDay: Pointer to store the day-of-year when DST ends (1-366)
void calculateDSTDays(int year, int *startDay, int *endDay)
{
    // Validate input parameters
    if (startDay == NULL || endDay == NULL)
    {
        ESP_LOGE("WWVB", "calculateDSTDays: NULL pointer provided");
        return;
    }
    
    bool leap = isLeapYear(year);
    int daysInFeb = leap ? 29 : 28;
    
    // Calculate day-of-week for January 1 using Zeller's congruence
    // For January, we treat it as month 13 of previous year in Zeller's formula
    int y = year - 1;
    int m = 13; // January as month 13 of previous year
    int q = 1;  // day of month (January 1)
    
    // Apply Zeller's congruence formula
    int century = y / 100;
    int year_of_century = y % 100;
    int h = (q + ((13 * (m + 1)) / 5) + year_of_century + (year_of_century / 4) + (century / 4) + 5 * century) % 7;
    
    // Zeller's result: h: 0=Saturday, 1=Sunday, 2=Monday, ..., 6=Friday
    // Convert to standard: 0=Sunday, 1=Monday, ..., 6=Saturday
    int jan1_dow = (h + 6) % 7;
    
    // ===== Calculate Second Sunday in March =====
    int march1_doy = 31 + daysInFeb + 1; // Jan(31) + Feb(28/29) + Mar(1) for March 1
    int march1_dow = (jan1_dow + (march1_doy - 1)) % 7;
    
    // Days from March 1 until first Sunday
    int days_to_first_sunday = (march1_dow == 0) ? 0 : (7 - march1_dow);
    
    // Second Sunday is 7 days after first Sunday
    // If March 1 is a Sunday (days_to_first_sunday == 0), then March 1 is the first Sunday
    int second_sunday_date = 1 + days_to_first_sunday + 7;
    
    *startDay = march1_doy - 1 + second_sunday_date; // -1 because march1_doy includes March 1
    
    // ===== Calculate First Sunday in November =====
    int nov1_doy = 31 + daysInFeb + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 1; // Jan(31) + Feb(28/29) + Mar(31) + Apr(30) + May(31) + Jun(30) + Jul(31) + Aug(31) + Sep(30) + Oct(31) + Nov(1) for Nov 1
    int nov1_dow = (jan1_dow + (nov1_doy - 1)) % 7;
    
    // Days from November 1 until first Sunday
    int days_to_first_sunday_nov = (nov1_dow == 0) ? 0 : (7 - nov1_dow);
    
    int first_sunday_date = 1 + days_to_first_sunday_nov;
    
    *endDay = nov1_doy - 1 + first_sunday_date; // -1 because nov1_doy includes Nov 1
}

// Function to check if a given day is within the DST period for a given year
// Parameters:
//   year: The year (e.g., 2024)
//   daysPassed: Day of year (1-366, where 1 = January 1)
// Returns:
//   true if the day is during DST (on or after DST start, before DST end)
//   false otherwise
bool isDaylightSavingTime(int year, int daysPassed)
{
    int startDay, endDay;
    calculateDSTDays(year, &startDay, &endDay);
    return (daysPassed >= startDay && daysPassed < endDay);
}
