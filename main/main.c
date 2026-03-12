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

#define WWVBDEBUG

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
void TimerSecond_ISR();
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
void Setup60KHzOutput();
void SNTP_callback (struct timeval *tv);

// WWVB related
const char *ntpServer = "pool.ntp.org";
uint8_t WWVBArray[60] = {0};
volatile uint8_t slot = 0;

// WiFi Provisioning
bool is_provisioned = false;
bool timer_Enabled = false;
int s_retry_num = 0;
EventGroupHandle_t s_wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;

// // Bit & Marker timers
esp_timer_handle_t TimerBit0 = NULL;
esp_timer_handle_t TimerBit1 = NULL;
esp_timer_handle_t TimerBitMarker = NULL;

// // One Second timer
esp_timer_handle_t TimerSecond = NULL;

// 60KHz output
ledc_channel_config_t ledc_channel;

void app_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    ESP_LOGI("GPIO", "Configuring GPIO");

    gpio_reset_pin(GPIO_NUM_13);
    gpio_set_direction(GPIO_NUM_13, GPIO_MODE_OUTPUT);

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
    ESP_ERROR_CHECK(esp_timer_start_periodic(TimerSecond, 1000000)); // 1 second
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

        char service_name[12];
        get_device_service_name(service_name, sizeof(service_name));

        /* Do we want a proof-of-possession (ignored if Security 0 is selected):
         *      - this should be a string with length > 0
         *      - NULL if not used
         */
        const char *pop = "abcd1234";

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
        if (s_retry_num < 10) 
        {
            ESP_ERROR_CHECK(esp_wifi_connect());
            s_retry_num++;
            ESP_LOGI("WiFi", "retry to connect to the AP");
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
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
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

void SetupWWVBArray()
{
    time_t rawtime;
    struct tm *utcTime;

    time(&rawtime);
    utcTime = gmtime(&rawtime);

    // Using the current UTC time fill in the WWVBArray
    encodeYear(utcTime->tm_year + 1900, WWVBArray);
    encodeDayOfYear(utcTime->tm_yday + 1, WWVBArray);
    encodeHour(utcTime->tm_hour, WWVBArray);
    encodeMinute(utcTime->tm_min, WWVBArray);
    setMarkersAndIndicators(WWVBArray);
    setDUT1(WWVBArray); // We're ignoring DUT1 as it has been deprecated and not used in this scenario
    setLeapYear(utcTime->tm_year + 1900, WWVBArray);
    setLeapSecond(false, WWVBArray); // Ignore leap seconds in this scenario
    setDST(isDaylightSavingTime(utcTime->tm_year + 1900, utcTime->tm_yday + 1), WWVBArray);
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
}

// All the bit/marker timers just reenable the 50%^ duty cycle of the 60KHz signal
void IRAM_ATTR TimerSignalReenable_ISR()
{
    //analogWrite(A0, 127);
    ESP_ERROR_CHECK(ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 127));
    ESP_ERROR_CHECK(ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel));
}

void TimerSecond_ISR(void *param)
{
  static bool ON;
  ON = !ON;
  
  ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_13, ON));

  switch (WWVBArray[slot])
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
  }
}

void ZeroCarrier()
{
    ESP_ERROR_CHECK(ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0));
    ESP_ERROR_CHECK(ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel));
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
    // DUT1 is obselete, it was used for celestial navigation
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
    if (IsLeap)
        signal[56] = 1;
    else
        signal[56] = 0;
}

// WWVB Expects to have a DST bit set - It allows for warning of DST but we're ignoring that in this scenario - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void setDST(bool IsDST, uint8_t *signal)
{
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
    ledc_channel.gpio_num = GPIO_NUM_26; // A0 on the Huzzah32
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

// Function to determine if a year is a leap year
bool isLeapYear(int year)
{
    if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
    {
        return true;
    }
    return false;
}

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
