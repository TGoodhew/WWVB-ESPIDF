/*
 * Signal Output Module
 */

#include "signal_output.h"
#include "time_sync.h"
#include "wwvb_config.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <freertos/task.h>
#include "sdkconfig.h"

// Task and Buffer Size Constants
#define DEBUG_TASK_STACK_SIZE 2048       // Stack size for debug task

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

// Debug queue for ISR to task communication
static QueueHandle_t debug_queue = NULL;

// External references to WWVB state (defined in main.c)
extern void TimerSecond_ISR(void *param);

QueueHandle_t GetDebugQueue(void)
{
    return debug_queue;
}

void InitDebugQueue(void)
{
    // Create debug queue for ISR to task communication
    debug_queue = xQueueCreate(DEBUG_QUEUE_SIZE, sizeof(debug_msg_t));
    if (debug_queue == NULL) {
        ESP_LOGE("SignalOutput", "Failed to create debug queue");
    } else {
        // Create debug task to handle logging from ISR
        BaseType_t task_created = xTaskCreate(debug_task, "debug_task", DEBUG_TASK_STACK_SIZE, NULL, 5, NULL);
        if (task_created != pdPASS) {
            ESP_LOGE("SignalOutput", "Failed to create debug task");
        }
    }
}

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

/**
 * @brief Initialize and configure the 60 kHz PWM carrier output
 * 
 * This function configures the ESP32's LEDC (LED Control) peripheral to generate
 * a 60 kHz square wave carrier signal for WWVB emulation. The LEDC peripheral
 * is normally used for LED PWM control but works perfectly for RF carrier generation.
 * 
 * Configuration:
 * - Frequency: 60,000 Hz (60 kHz) - WWVB carrier frequency
 * - Resolution: 8-bit (0-255 duty cycle values)
 * - Initial Duty Cycle: 50% (128 out of 255) - square wave for full carrier power
 * - Speed Mode: High-speed mode for accurate frequency generation
 * - GPIO: Configurable via CONFIG_WWVB_OUTPUT_PIN (default: GPIO 26/A0)
 * 
 * The carrier is modulated by changing the duty cycle:
 * - Full power: 50% duty cycle (128/255) - normal carrier transmission
 * - Reduced power: 0% duty cycle (0/255) - represents the "reduced power" in WWVB encoding
 * 
 * Why 60 kHz?
 * - This is the official WWVB carrier frequency
 * - Low frequency (LF band) allows signal to penetrate buildings
 * - Standardized frequency that atomic clocks are designed to receive
 * 
 * Hardware output:
 * - Without antenna: GPIO pin oscillates at 60 kHz, detectable with oscilloscope
 * - With antenna: Creates electromagnetic field that propagates to nearby receivers
 */
void Setup60KHzOutput(void)
{
    // Configure LEDC timer for 60 kHz carrier generation
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT,         // 8-bit resolution (0-255)
        .freq_hz = WWVB_CARRIER_FREQUENCY_HZ,        // 60,000 Hz carrier frequency
        .speed_mode = LEDC_HIGH_SPEED_MODE,          // High-speed mode for accuracy
        .timer_num = LEDC_TIMER_0                    // Use timer 0
    };
    
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Configure LEDC channel connected to the output GPIO
    ledc_channel.channel = LEDC_CHANNEL_0;
    ledc_channel.duty = PWM_DUTY_CYCLE_50_PERCENT;   // Start with 50% duty (full power)
    ledc_channel.gpio_num = (gpio_num_t)CONFIG_WWVB_OUTPUT_PIN; // Configurable GPIO
    ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_channel.timer_sel = LEDC_TIMER_0;           // Bind to timer 0

    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

/**
 * @brief Create and configure all ESP32 timers for WWVB signal generation
 * 
 * This function creates four high-resolution timers that work together to generate
 * the WWVB amplitude-modulated signal:
 * 
 * 1. Second Timer (1 Hz, periodic):
 *    - Triggers every 1 second
 *    - Advances through the 60-second WWVB frame
 *    - Reads the current bit value (0, 1, or marker)
 *    - Reduces carrier power (sets duty cycle to 0%)
 *    - Starts the appropriate bit timer based on the bit value
 * 
 * 2. Bit0 Timer (200ms, one-shot):
 *    - Fired by Second Timer when bit value is '0'
 *    - Waits 200ms (0.2 seconds)
 *    - Restores carrier to full power (50% duty cycle)
 * 
 * 3. Bit1 Timer (500ms, one-shot):
 *    - Fired by Second Timer when bit value is '1'
 *    - Waits 500ms (0.5 seconds)
 *    - Restores carrier to full power (50% duty cycle)
 * 
 * 4. Marker Timer (800ms, one-shot):
 *    - Fired by Second Timer when bit is a position marker
 *    - Waits 800ms (0.8 seconds)
 *    - Restores carrier to full power (50% duty cycle)
 * 
 * Timer Coordination Example (for a '1' bit):
 * ```
 * t=0.0s:  Second Timer ISR fires
 *          └─> Carrier power reduced (0% duty)
 *          └─> Bit1 Timer started (500ms)
 * t=0.5s:  Bit1 Timer ISR fires
 *          └─> Carrier power restored (50% duty)
 * t=1.0s:  Second Timer ISR fires (next bit)
 *          └─> Repeat for next bit...
 * ```
 * 
 * All timers use ESP32's high-resolution timer API (esp_timer) which provides
 * microsecond accuracy, essential for proper WWVB signal timing.
 */
void SetupTimers(void)
{
    // Create the 1 Hz second timer (periodic) - drives the entire signal generation
    const esp_timer_create_args_t timer_second_config = {
        .callback = &TimerSecond_ISR,
        .name = "One Second Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_second_config, &timers.second));

    // Create Bit 0 timer (one-shot) - restores carrier after 0.2s reduced power
    const esp_timer_create_args_t timer_bit0_config = {
        .callback = &TimerSignalReenable_ISR,
        .name = "Bit 0 Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_bit0_config, &timers.bit0));
            
    // Create Bit 1 timer (one-shot) - restores carrier after 0.5s reduced power
    const esp_timer_create_args_t timer_bit1_config = {
        .callback = &TimerSignalReenable_ISR,
        .name = "Bit 1 Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_bit1_config, &timers.bit1));

    // Create Marker timer (one-shot) - restores carrier after 0.8s reduced power
    const esp_timer_create_args_t timer_bitmarker_config = {
        .callback = &TimerSignalReenable_ISR,
        .name = "Bit Marker Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_bitmarker_config, &timers.marker));
}

void StartSecondTimer(void)
{
    // Validate timer handle before starting
    if (timers.second == NULL)
    {
        ESP_LOGE("SignalOutput", "TimerSecond handle is NULL, cannot start timer");
        return;
    }
    
    ESP_ERROR_CHECK(esp_timer_start_periodic(timers.second, TIMER_ONE_SECOND_US)); // 1 second
}

/**
 * @brief Reduce carrier power to 0% (turn off carrier)
 * 
 * This function is called at the start of each second to begin the reduced-power
 * period that encodes the bit value. In WWVB amplitude modulation:
 * - Full power = 100% carrier amplitude (50% PWM duty cycle)
 * - Reduced power = 0% carrier amplitude (0% PWM duty cycle)
 * 
 * The duration of reduced power encodes the bit:
 * - 0.2s reduced power = bit '0'
 * - 0.5s reduced power = bit '1'  
 * - 0.8s reduced power = position marker
 * 
 * Note: We don't use ESP_ERROR_CHECK here because:
 * 1. This is often called from ISR context where error handling is limited
 * 2. LEDC functions rarely fail in normal operation
 * 3. A failed carrier update would be detected on oscilloscope but not critical
 */
void ZeroCarrier(void)
{
    // Set PWM duty cycle to 0% to reduce carrier power
    // This must be followed by update_duty to take effect
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}

/**
 * @brief Timer ISR to restore carrier to full power
 * 
 * This ISR is called by the Bit0, Bit1, or Marker timers after the appropriate
 * reduced-power duration has elapsed. It restores the carrier to full power
 * (50% duty cycle) for the remainder of the second.
 * 
 * Called from ISR context, so:
 * - Must be marked IRAM_ATTR (stored in fast instruction RAM)
 * - Cannot use blocking operations
 * - Should minimize execution time
 * - Cannot safely use ESP_ERROR_CHECK (would abort on error)
 * 
 * The carrier remains at full power until the next second timer fires, when
 * the cycle repeats for the next bit in the WWVB frame.
 * 
 * @param param Timer parameter (unused, but required by esp_timer API)
 */
void IRAM_ATTR TimerSignalReenable_ISR(void *param)
{
    (void)param; // Suppress unused parameter warning
    
    // Restore carrier to full power (50% duty cycle = 128 out of 255)
    // This creates a square wave at 60 kHz with equal high and low periods
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, PWM_DUTY_CYCLE_50_PERCENT);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}

// Timer handles accessors for ISR use
esp_timer_handle_t GetBit0Timer(void)
{
    return timers.bit0;
}

esp_timer_handle_t GetBit1Timer(void)
{
    return timers.bit1;
}

esp_timer_handle_t GetMarkerTimer(void)
{
    return timers.marker;
}
