#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "wifi_provisioning/manager.h"
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
void TimerSecond_ISR();
void BoardDebugTest();
void SetupTimers();
void SetupWWVBArray();
bool isLeapYear(int year);
void calculateDSTDays(int year, int *startDay, int *endDay);
bool isDaylightSavingTime(int year, int daysPassed);
void LogCurrentTime();
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void Setup60KHzOutput();

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

    ESP_LOGI("WiFi", "Connecting to WiFi");

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
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
    };

    /* Initialize provisioning manager with the configuration parameters set above */
    ESP_ERROR_CHECK( wifi_prov_mgr_init(wifi_prov_config) );

    /* Let's find out if the device is provisioned */
    ESP_ERROR_CHECK( wifi_prov_mgr_is_provisioned(&is_provisioned) );

    ESP_LOGI("WiFI", "Is provisioned: %s", is_provisioned ? "true" : "false");

    // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    // ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );

    // ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("WiFi", "Already provisioned, starting Wi-Fi STA");

    /* We don't need the manager as device is already provisioned, so let's release it's resources */
    wifi_prov_mgr_deinit();

    /* Start Wi-Fi station */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI("WiFi", "Connected");
    
    ESP_LOGI("SNTP", "Initializing SNTP");

    

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntpServer);
    esp_netif_sntp_init(&sntp_config);

    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
        ESP_LOGI("SNTP","Failed to update system time within 10s timeout");
    }
    else {
        ESP_LOGI("SNTP","System time updated");
    }

    SetupWWVBArray();

    SetupTimers();

    Setup60KHzOutput();

    while (1)
    {
        SetupWWVBArray();
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    
    //BoardDebugTest(); // This is just to show the board is working
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

    // Write the system time as a log entry
    ESP_LOGI("Time", "Current system time: %s", strftime_buf);
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
    ESP_ERROR_CHECK(esp_timer_start_periodic(TimerSecond, 1000000)); // 1 second

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

void BoardDebugTest()
{
    printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK)
    {
        printf("Get flash size failed");
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    for (int i = 10; i >= 0; i--)
    {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
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
    bool leap = isLeapYear(year);
    // Calculate the second Sunday in March
    int daysInFeb = leap ? 29 : 28;
    int daysUntilMarch = 31 + daysInFeb;
    *startDay = daysUntilMarch + (14 - ((year + year / 4 - year / 100 + year / 400 + daysUntilMarch) % 7));

    // Calculate the first Sunday in November
    int daysUntilNov = 31 + daysInFeb + 31 + 30 + 31 + 30 + 31 + 31 + 30;
    *endDay = daysUntilNov + (7 - ((year + year / 4 - year / 100 + year / 400 + daysUntilNov) % 7));
}

// Function to check if the current day is within DST period
bool isDaylightSavingTime(int year, int daysPassed)
{
    int startDay, endDay;
    calculateDSTDays(year, &startDay, &endDay);
    return (daysPassed >= startDay && daysPassed < endDay);
}

void Setup60KHzOutput()
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
        .freq_hz = 60000,                     // frequency of PWM signal
        .speed_mode = LEDC_HIGH_SPEED_MODE,   // timer mode
        .timer_num = LEDC_TIMER_0             // timer index
    };
    
    ledc_timer_config(&ledc_timer);

    ledc_channel.channel = LEDC_CHANNEL_0;
    ledc_channel.duty = 127;
    ledc_channel.gpio_num = GPIO_NUM_26; // A0 on the Huzzah32
    ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_channel.timer_sel = LEDC_TIMER_0;

    ledc_channel_config(&ledc_channel);
}

// All the bit/marker timers just reenable the 50%^ duty cycle of the 60KHz signal
void IRAM_ATTR TimerSignalReenable_ISR()
{
    //analogWrite(A0, 127);
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 127);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}

void TimerSecond_ISR(void *param)
{
  static bool ON;
  ON = !ON;
  
  gpio_set_level(GPIO_NUM_13, ON);

  switch (WWVBArray[slot])
  {
  case 0:
  {
      #ifdef WWVBDEBUG
      printf("0");
      #endif

      // 0 (0.2s reduced power)
      ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0);
      ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);

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
      ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0);
      ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);

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
      ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0);
      ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);

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

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI("SNTP", "Notification of a time synchronization event");
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 10) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI("WiFi", "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI("WiFi","connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI("WiFi", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

