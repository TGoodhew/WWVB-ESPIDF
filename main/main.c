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
    0.9   Refactored into modular architecture
*/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <nvs_flash.h>

// Common configuration
#include "wwvb_config.h"

// Application modules
#include "wwvb_encoder.h"
#include "dst_calc.h"
#include "wifi_manager.h"
#include "time_sync.h"
#include "signal_output.h"

// WWVB state structure with double-buffering using pointer swapping
// We use two arrays: one active (being transmitted) and one staging (being prepared)
// This ensures the ISR always reads a complete, consistent 60-second frame
typedef struct {
    uint8_t buffer0[WWVB_SIGNAL_ARRAY_SIZE];  // First buffer
    uint8_t buffer1[WWVB_SIGNAL_ARRAY_SIZE];  // Second buffer
    volatile uint8_t *active;      // Pointer to array being transmitted by ISR
    volatile uint8_t *staging;     // Pointer to array being prepared for next minute
    volatile uint8_t slot;
    volatile bool swap_pending;  // Flag to indicate staging array is ready to become active
} wwvb_state_t;

static wwvb_state_t wwvb_state = {
    .buffer0 = {0},
    .buffer1 = {0},
    .active = NULL,   // Will be initialized in app_main
    .staging = NULL,  // Will be initialized in app_main
    .slot = 0,
    .swap_pending = false
};

// Flag for signaling WWVB array updates from ISR to task
static volatile bool update_wwvb_array = false;

// Spinlock for protecting pointer swap in ISR
static portMUX_TYPE wwvb_spinlock = portMUX_INITIALIZER_UNLOCKED;

// Function prototypes

/*
 * Setup WWVB array with current time data
 * Encodes current UTC time into the staging WWVB signal array.
 * Includes year, day of year, hour, minute, DST status, and other indicators.
 * The staging array will be swapped to active at the next minute boundary.
 */
void SetupWWVBArray(void);

/*
 * Timer ISR called once per second
 * Advances through the 60-second WWVB frame, transmitting each bit.
 * Manages double-buffer pointer swapping at minute boundaries.
 * Triggers array updates at slots 30 and 60.
 * 
 * @param param Timer parameter (unused)
 */
void TimerSecond_ISR(void *param);

void app_main(void)
{
    // Disable output buffering for immediate console output
    setvbuf(stdout, NULL, _IONBF, 0);

    ESP_LOGI("GPIO", "Configuring GPIO");

    // Configure debug LED GPIO
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

    // Initialize the double-buffer pointers
    // buffer0 is initially active, buffer1 is initially staging
    wwvb_state.active = wwvb_state.buffer0;
    wwvb_state.staging = wwvb_state.buffer1;
    
    SetupWWVBArray();
    
    // Copy staging to active for initial data before timer starts
    // Use a loop instead of memcpy to respect volatile qualifier
    for (int i = 0; i < WWVB_SIGNAL_ARRAY_SIZE; i++) {
        wwvb_state.active[i] = wwvb_state.staging[i];
    }
    wwvb_state.swap_pending = false; // Reset flag after manual copy

    ESP_LOGI("SNTP", "Initializing Timers");

    SetupTimers();

    ESP_LOGI("SNTP", "Initializing Signal Output");

    // Setup 60 kHz carrier output using LEDC PWM
    Setup60KHzOutput();

    // Create debug queue and task for ISR-to-task logging
    InitDebugQueue();

    // Main loop: Check for WWVB array update requests from ISR
    while (1)
    {
        // Check if the ISR signaled that we need to update the array
        if (update_wwvb_array)
        {
            update_wwvb_array = false;
            SetupWWVBArray();  // Encode current time into staging array
        }
        // Sleep for 500ms to ensure flag is checked frequently
        // Updates happen when signaled by ISR (at slot 30 and slot 60, i.e., twice per minute)
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void SetupWWVBArray(void)
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
    
    // Check for reasonable time values (year should be >= WWVB_MIN_YEAR)
    // If time is before WWVB_MIN_YEAR, it likely means time hasn't been synchronized yet
    if (utcTime->tm_year + YEAR_OFFSET_1900 < WWVB_MIN_YEAR)
    {
        ESP_LOGE("WWVB", "Invalid system time detected (year=%d). Time may not be synchronized.", 
                 utcTime->tm_year + YEAR_OFFSET_1900);
        return;
    }

    // Write to the staging array (not the active array being transmitted)
    // The staging array will become active at the next minute boundary
    encodeYear(utcTime->tm_year + YEAR_OFFSET_1900, wwvb_state.staging);
    encodeDayOfYear(utcTime->tm_yday + 1, wwvb_state.staging);
    encodeHour(utcTime->tm_hour, wwvb_state.staging);
    encodeMinute(utcTime->tm_min, wwvb_state.staging);
    setMarkersAndIndicators(wwvb_state.staging);
    setDUT1(wwvb_state.staging); // We're ignoring DUT1 as it has been deprecated and not used in this scenario
    setLeapYear(utcTime->tm_year + YEAR_OFFSET_1900, wwvb_state.staging);
    setLeapSecond(false, wwvb_state.staging); // Ignore leap seconds in this scenario
    setDST(isDaylightSavingTime(utcTime->tm_year + YEAR_OFFSET_1900, utcTime->tm_yday + 1), wwvb_state.staging);
    
    // Signal that the staging array is ready to be swapped at the next minute boundary
    wwvb_state.swap_pending = true;
}

void IRAM_ATTR TimerSecond_ISR(void *param)
{
  (void)param; // Suppress unused parameter warning
  static bool ON;
  static QueueHandle_t debug_queue_cached = NULL;
  
  // Cache the debug queue handle on first call to avoid repeated lookups
  if (debug_queue_cached == NULL) {
      debug_queue_cached = GetDebugQueue();
  }
  
  // Toggle debug LED state
  ON = !ON;
  
  // Set debug LED without error checking (ISR context)
  gpio_set_level((gpio_num_t)CONFIG_WWVB_DEBUG_LED_PIN, ON);

  // At the start of a new minute (slot 0), swap the pointers if a swap is pending
  // This ensures we always transmit a complete, consistent 60-second frame
  // Pointer swapping is much faster than copying 60 bytes in ISR context
  if (wwvb_state.slot == 0 && wwvb_state.swap_pending)
  {
      // Use critical section to ensure atomic pointer swap
      // This prevents race conditions if main task reads pointers during swap
      portENTER_CRITICAL_ISR(&wwvb_spinlock);
      
      // Swap pointers: staging becomes active, old active becomes staging
      volatile uint8_t *temp = wwvb_state.active;
      wwvb_state.active = wwvb_state.staging;
      wwvb_state.staging = temp;
      wwvb_state.swap_pending = false;
      
      portEXIT_CRITICAL_ISR(&wwvb_spinlock);
  }

  // Validate slot index before accessing active array
  if (wwvb_state.slot >= WWVB_SIGNAL_ARRAY_SIZE)
  {
      wwvb_state.slot = 0;
  }

  // Always read from the active array (never from staging)
  switch (wwvb_state.active[wwvb_state.slot])
  {
  case WWVB_BIT_ZERO:
  {
      #ifdef WWVBDEBUG
      // Defer debug output to task context via queue
      if (debug_queue_cached != NULL) {
          debug_msg_t msg = {.type = '0'};
          xQueueSendFromISR(debug_queue_cached, &msg, NULL);
      }
      #endif

      // 0 (0.2s reduced power)
      ZeroCarrier();

      // TimerBit0 - Start timer without ESP_ERROR_CHECK
      esp_timer_handle_t bit0_timer = GetBit0Timer();
      if (bit0_timer != NULL)
      {
          esp_timer_start_once(bit0_timer, TIMER_BIT0_DURATION_US); // 0.2 second
      }
    }
  break;
  case WWVB_BIT_ONE:
  {
      #ifdef WWVBDEBUG
      // Defer debug output to task context via queue
      if (debug_queue_cached != NULL) {
          debug_msg_t msg = {.type = '1'};
          xQueueSendFromISR(debug_queue_cached, &msg, NULL);
      }
      #endif

      // 1 (0.5s reduced power)
      ZeroCarrier();

      // TimerBit1 - Start timer without ESP_ERROR_CHECK
      esp_timer_handle_t bit1_timer = GetBit1Timer();
      if (bit1_timer != NULL)
      {
          esp_timer_start_once(bit1_timer, TIMER_BIT1_DURATION_US); // 0.5 second
      }

  }
  break;
  case WWVB_BIT_MARKER:
  {
      #ifdef WWVBDEBUG
      // Defer debug output to task context via queue
      if (debug_queue_cached != NULL) {
          debug_msg_t msg = {.type = 'M'};
          xQueueSendFromISR(debug_queue_cached, &msg, NULL);
      }
      #endif

      // Marker (0.8s reduced power)
      ZeroCarrier();

      // TimerBitMarker - Start timer without ESP_ERROR_CHECK
      esp_timer_handle_t marker_timer = GetMarkerTimer();
      if (marker_timer != NULL)
      {
          esp_timer_start_once(marker_timer, TIMER_MARKER_DURATION_US); // 0.8 second
      }
  }
  break;
  }

  wwvb_state.slot++; // Advance data slot in minute data packet
  if (wwvb_state.slot == WWVB_SIGNAL_ARRAY_SIZE)
  {
      wwvb_state.slot = 0; // Reset slot to 0 if at 60 seconds
      update_wwvb_array = true; // Signal that array needs updating
      #ifdef WWVBDEBUG
      // Defer debug output and logging to task context via queue
      if (debug_queue_cached != NULL) {
          debug_msg_t msg = {.type = 'N'};
          xQueueSendFromISR(debug_queue_cached, &msg, NULL);
      }
      #endif
  }
  else if (wwvb_state.slot == WWVB_SIGNAL_ARRAY_SIZE / 2)
  {
      // Update at 30 seconds as well to ensure at least 2 updates per minute
      update_wwvb_array = true;
  }
}
