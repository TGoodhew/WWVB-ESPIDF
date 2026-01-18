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
    1.0   Added comprehensive documentation for algorithms, signal format, and architecture
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

/**
 * @brief Setup WWVB signal array with current time data
 * 
 * Encodes the current UTC time into the staging WWVB signal array. This includes:
 * - Year (2-digit, positions 45-48, 50-53)
 * - Day of year (Julian day 1-366, positions 22-23, 25-28, 30-33)
 * - Hour (0-23, positions 12-13, 15-18)
 * - Minute (0-59, positions 1-3, 5-8)
 * - DST status (positions 57-58)
 * - Leap year indicator (position 55)
 * - Leap second warning (position 56) - always 0 in this implementation
 * - DUT1 bits (positions 36-38, 40-43) - always 0 (deprecated)
 * - Position markers (positions 0, 9, 19, 29, 39, 49, 59)
 * - Reserved bits (always 0)
 * 
 * The encoded data is written to the staging buffer (not the active buffer).
 * It will become active at the next minute boundary when the ISR swaps pointers.
 * 
 * This function is called from the main task context (not ISR) when signaled
 * by the TimerSecond_ISR at slots 30 and 60.
 */
void SetupWWVBArray(void);

/**
 * @brief Main per-second ISR that drives WWVB signal transmission
 * 
 * This Interrupt Service Routine (ISR) is the heart of the WWVB emulator. It's called
 * precisely once per second by the ESP32 high-resolution timer and is responsible for:
 * 
 * 1. **Double-Buffer Management**: At the start of each minute (slot 0), atomically
 *    swaps the active and staging buffer pointers if new data is ready. This ensures
 *    the ISR always reads a complete, consistent 60-second frame.
 * 
 * 2. **Bit Transmission**: Reads the current bit from the active buffer and modulates
 *    the carrier accordingly:
 *    - Bit '0': Reduce power for 0.2s (200ms)
 *    - Bit '1': Reduce power for 0.5s (500ms)
 *    - Marker: Reduce power for 0.8s (800ms)
 * 
 * 3. **Frame Advancement**: Increments the slot counter (0-59) to track position
 *    within the current minute. Resets to 0 after slot 59.
 * 
 * 4. **Update Signaling**: Sets flags to tell the main task when to encode the next
 *    minute's data:
 *    - At slot 30 (mid-minute): Prepare next frame early
 *    - At slot 60→0 (end of minute): Definitely prepare next frame
 * 
 * ISR Design Considerations:
 * - **IRAM_ATTR**: Function stored in fast instruction RAM for consistent timing
 * - **Minimal execution time**: All operations are simple and deterministic
 * - **No blocking**: Never waits for anything, returns quickly
 * - **Atomic operations**: Uses spinlock for pointer swap to prevent race conditions
 * - **Deferred logging**: Debug output is queued to a task, not printed in ISR
 * 
 * Timing is critical: This ISR must complete in well under 1 millisecond to avoid
 * jitter in the signal timing. Typical execution time is <50 microseconds.
 * 
 * @param param Timer parameter (unused, but required by esp_timer callback signature)
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
  static bool ON;  // Debug LED state
  static QueueHandle_t debug_queue_cached = NULL;
  
  // Cache the debug queue handle on first call to avoid repeated function calls
  if (debug_queue_cached == NULL) {
      debug_queue_cached = GetDebugQueue();
  }
  
  // Toggle debug LED to provide visual indication of ISR execution (1 Hz blink)
  ON = !ON;
  gpio_set_level((gpio_num_t)CONFIG_WWVB_DEBUG_LED_PIN, ON);

  // === Double-Buffer Pointer Swap (Critical Section) ===
  // At the start of each minute (slot 0), swap active and staging buffers if new data is ready.
  // This ensures we transmit a complete, consistent 60-second frame without glitches.
  // Pointer swap is used instead of memcpy because it's atomic and extremely fast (<1µs).
  if (wwvb_state.slot == 0 && wwvb_state.swap_pending)
  {
      // Enter critical section to make pointer swap atomic
      // This prevents race conditions if main task reads pointers during swap
      portENTER_CRITICAL_ISR(&wwvb_spinlock);
      
      // Swap pointers: staging becomes active (transmitted), active becomes staging (writable)
      volatile uint8_t *temp = wwvb_state.active;
      wwvb_state.active = wwvb_state.staging;
      wwvb_state.staging = temp;
      wwvb_state.swap_pending = false;  // Clear flag
      
      portEXIT_CRITICAL_ISR(&wwvb_spinlock);
  }

  // === Slot Validation ===
  // Paranoid check: ensure slot is in valid range [0-59]
  // This should never trigger in normal operation, but prevents buffer overflow if it does
  if (wwvb_state.slot >= WWVB_SIGNAL_ARRAY_SIZE)
  {
      wwvb_state.slot = 0;
  }

  // === Bit Transmission ===
  // Read current bit from active buffer and modulate carrier accordingly
  // The switch statement handles three cases: '0', '1', and position marker
  switch (wwvb_state.active[wwvb_state.slot])
  {
  case WWVB_BIT_ZERO:
  {
      #ifdef WWVBDEBUG
      // Defer debug output to task context via queue (cannot printf in ISR)
      if (debug_queue_cached != NULL) {
          debug_msg_t msg = {.type = '0'};
          xQueueSendFromISR(debug_queue_cached, &msg, NULL);
      }
      #endif

      // Bit '0': Reduce carrier power for 0.2 seconds
      ZeroCarrier();

      // Start one-shot timer to restore carrier after 200ms
      esp_timer_handle_t bit0_timer = GetBit0Timer();
      if (bit0_timer != NULL)
      {
          esp_timer_start_once(bit0_timer, TIMER_BIT0_DURATION_US); // 200,000 microseconds
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

      // Bit '1': Reduce carrier power for 0.5 seconds
      ZeroCarrier();

      // Start one-shot timer to restore carrier after 500ms
      esp_timer_handle_t bit1_timer = GetBit1Timer();
      if (bit1_timer != NULL)
      {
          esp_timer_start_once(bit1_timer, TIMER_BIT1_DURATION_US); // 500,000 microseconds
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

      // Marker: Reduce carrier power for 0.8 seconds
      ZeroCarrier();

      // Start one-shot timer to restore carrier after 800ms
      esp_timer_handle_t marker_timer = GetMarkerTimer();
      if (marker_timer != NULL)
      {
          esp_timer_start_once(marker_timer, TIMER_MARKER_DURATION_US); // 800,000 microseconds
      }
  }
  break;
  }

  // === Frame Advancement and Update Signaling ===
  wwvb_state.slot++; // Advance to next bit position in the 60-second frame
  
  if (wwvb_state.slot == WWVB_SIGNAL_ARRAY_SIZE)
  {
      // End of minute reached (slot 60 → 0)
      wwvb_state.slot = 0; // Reset to start of next minute
      update_wwvb_array = true; // Signal main task to encode next minute's data
      
      #ifdef WWVBDEBUG
      // Log minute boundary in debug output
      if (debug_queue_cached != NULL) {
          debug_msg_t msg = {.type = 'N'};  // 'N' = Newline + timestamp
          xQueueSendFromISR(debug_queue_cached, &msg, NULL);
      }
      #endif
  }
  else if (wwvb_state.slot == WWVB_SIGNAL_ARRAY_SIZE / 2)
  {
      // Mid-minute checkpoint (slot 30)
      // Request early update so staging buffer is ready well before minute boundary
      // This provides a 30-second buffer for the main task to encode the data
      update_wwvb_array = true;
  }
}
