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
#include "sdkconfig.h"

// Timer handles structure (only second timer needed)
typedef struct {
    esp_timer_handle_t second;
    esp_timer_handle_t reenable;
} timer_handles_t;

static timer_handles_t timers = {
    .second = NULL,
    .reenable = NULL
};

// 60KHz output
static ledc_channel_config_t ledc_channel;

// External references to WWVB state (defined in main.c)
extern void TimerSecond_ISR(void *param);

/**
 * @brief Re-enable the 60 kHz carrier after the reduced-power interval.
 * 
 * This callback runs in esp_timer task context and restores the PWM duty cycle
 * to 50%, re-enabling the carrier at a precise offset from the second timer edge.
 * 
 * @param param Timer parameter (unused)
 */
static void CarrierReenableTimerCallback(void *param)
{
    (void)param;

    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, PWM_DUTY_CYCLE_50_PERCENT);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
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
 * @brief Create and configure timers for WWVB signal generation
 * 
 * This function creates the 1 Hz frame timer and a one-shot timer used to
 * restore the carrier after each reduced-power symbol interval.
 * 
 * Architecture:
 * 1. Second Timer (1 Hz, periodic, default dispatch):
 *    - Triggers every 1 second in timer task context
 *    - Advances through the 60-second WWVB frame
 *    - Reads the current bit value (0, 1, or marker)
 *    - Reduces carrier power (sets duty cycle to 0%)
 *    - Starts the one-shot re-enable timer for the symbol duration
 * 
 * 2. Re-enable Timer (one-shot, default dispatch):
 *    - Fires 200 ms, 500 ms, or 800 ms after symbol start
 *    - Restores the carrier to full power with hardware-timer timing
 * 
 * Signal Coordination Example (for a '1' bit):
 * ```
 * t=0.0s:  Second Timer callback fires
 *          └─> Carrier power reduced (0% duty)
 *          └─> One-shot timer scheduled for 500 ms
 * t=0.5s:  One-shot timer callback fires
 *          └─> Carrier power restored (50% duty)
 * t=1.0s:  Second Timer callback fires (next bit)
 *          └─> Repeat for next bit...
 * ```
 */
void SetupTimers(void)
{
    // Create the 1 Hz second timer that drives frame transmission.
    const esp_timer_create_args_t timer_second_config = {
        .callback = &TimerSecond_ISR,
        .name = "One Second Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_second_config, &timers.second));

    // Create the one-shot timer that re-enables the carrier after each symbol.
    const esp_timer_create_args_t timer_reenable_config = {
        .callback = &CarrierReenableTimerCallback,
        .name = "Carrier Reenable Timer"};

    esp_err_t err = esp_timer_create(&timer_reenable_config, &timers.reenable);
    if (err != ESP_OK) {
        ESP_LOGE("SignalOutput", "Failed to create carrier re-enable timer: %s", esp_err_to_name(err));
        if (timers.second != NULL) {
            ESP_ERROR_CHECK(esp_timer_delete(timers.second));
            timers.second = NULL;
        }
        return;
    }

    ESP_LOGI("SignalOutput", "Signal timers created successfully");
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

void ScheduleCarrierReenable(uint64_t delay_us)
{
    if (timers.reenable == NULL)
    {
        ESP_LOGE("SignalOutput", "Carrier re-enable timer is NULL, cannot schedule restore");
        return;
    }

    esp_err_t err = esp_timer_start_once(timers.reenable, delay_us);
    if (err == ESP_ERR_INVALID_STATE)
    {
        ESP_ERROR_CHECK(esp_timer_stop(timers.reenable));
        err = esp_timer_start_once(timers.reenable, delay_us);
    }

    if (err != ESP_OK)
    {
        ESP_LOGE("SignalOutput", "Failed to schedule carrier restore: %s", esp_err_to_name(err));
    }
}
