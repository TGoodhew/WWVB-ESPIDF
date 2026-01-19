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
#define SIGNAL_TASK_STACK_SIZE 3072      // Stack size for signal modulation task

// Signal modulation task notification values
#define SIGNAL_NOTIF_BIT0 (1 << 0)       // Bit 0: 200ms delay
#define SIGNAL_NOTIF_BIT1 (1 << 1)       // Bit 1: 500ms delay
#define SIGNAL_NOTIF_MARKER (1 << 2)     // Marker: 800ms delay

// Timer handles structure (only second timer needed)
typedef struct {
    esp_timer_handle_t second;
} timer_handles_t;

static timer_handles_t timers = {
    .second = NULL
};

// Task handle for signal modulation task
static TaskHandle_t signal_task_handle = NULL;

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
 * @brief Signal modulation task
 * 
 * This task handles carrier re-enable operations after reduced-power periods.
 * In ESP-IDF v5.5.2, calling esp_timer_start_once() from within an ESP_TIMER_ISR
 * callback can cause spinlock contention with WiFi's timer operations.
 * 
 * This task uses FreeRTOS delays instead of nested timers, completely avoiding
 * the timer system for re-enable operations and eliminating spinlock conflicts.
 * 
 * Architecture:
 * - Second timer (ESP_TIMER_ISR) notifies this task instead of starting nested timers
 * - Task uses vTaskDelay() for timing (separate from timer subsystem)
 * - Runs at high priority to ensure timely carrier re-enable
 * 
 * @param pvParameters Task parameters (unused)
 */
static void signal_modulation_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t notification_value;
    
    ESP_LOGI("SignalOutput", "Signal modulation task started");
    
    while (1) {
        // Wait for notification from second timer ISR
        // ISR will send notification instead of starting nested timers
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &notification_value, portMAX_DELAY) == pdTRUE) {
            // Determine delay based on bit type
            TickType_t delay_ticks;
            
            if (notification_value & SIGNAL_NOTIF_BIT0) {
                // Bit 0: Wait 200ms then re-enable carrier
                delay_ticks = pdMS_TO_TICKS(200);
            } else if (notification_value & SIGNAL_NOTIF_BIT1) {
                // Bit 1: Wait 500ms then re-enable carrier
                delay_ticks = pdMS_TO_TICKS(500);
            } else if (notification_value & SIGNAL_NOTIF_MARKER) {
                // Marker: Wait 800ms then re-enable carrier
                delay_ticks = pdMS_TO_TICKS(800);
            } else {
                // Unknown notification, skip
                continue;
            }
            
            // Wait for the appropriate duration
            vTaskDelay(delay_ticks);
            
            // Re-enable carrier (restore to full power)
            ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, PWM_DUTY_CYCLE_50_PERCENT);
            ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
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
 * @brief Create and configure timer and task for WWVB signal generation
 * 
 * This function creates the second timer and a dedicated signal modulation task.
 * In ESP-IDF v5.5.2, this architecture avoids spinlock issues by eliminating
 * nested timer operations.
 * 
 * Architecture:
 * 1. Second Timer (1 Hz, periodic, ESP_TIMER_ISR dispatch):
 *    - Triggers every 1 second in ISR context
 *    - Advances through the 60-second WWVB frame
 *    - Reads the current bit value (0, 1, or marker)
 *    - Reduces carrier power (sets duty cycle to 0%)
 *    - Notifies signal modulation task (instead of starting nested timers)
 * 
 * 2. Signal Modulation Task:
 *    - Waits for notification from second timer ISR
 *    - Uses FreeRTOS delay (vTaskDelay) for appropriate duration
 *    - Restores carrier to full power after delay
 *    - Runs independently of timer subsystem, avoiding spinlock conflicts with WiFi
 * 
 * Signal Coordination Example (for a '1' bit):
 * ```
 * t=0.0s:  Second Timer ISR fires
 *          └─> Carrier power reduced (0% duty)
 *          └─> Task notified with BIT1 flag
 * t=0.5s:  Task delay expires
 *          └─> Carrier power restored (50% duty)
 * t=1.0s:  Second Timer ISR fires (next bit)
 *          └─> Repeat for next bit...
 * ```
 * 
 * This approach eliminates spinlock issues by avoiding esp_timer_start_once()
 * calls from within ISR context, which can conflict with WiFi's timer usage.
 */
void SetupTimers(void)
{
    // Create the 1 Hz second timer (periodic) - drives the entire signal generation
    // Use ESP_TIMER_ISR dispatch method to run callback from ISR context (ESP-IDF v5.5.2)
    // This provides precise timing and avoids task scheduling delays
    const esp_timer_create_args_t timer_second_config = {
        .callback = &TimerSecond_ISR,
        .dispatch_method = ESP_TIMER_ISR,
        .name = "One Second Timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_second_config, &timers.second));

    // Create signal modulation task to handle carrier re-enable
    // This task uses FreeRTOS delays instead of nested timers, avoiding
    // spinlock contention with WiFi's timer operations
    BaseType_t task_created = xTaskCreate(
        signal_modulation_task,
        "signal_mod",
        SIGNAL_TASK_STACK_SIZE,
        NULL,
        6,  // Priority 6 (higher than debug task, ensures timely carrier re-enable)
        &signal_task_handle
    );
    
    if (task_created != pdPASS || signal_task_handle == NULL) {
        ESP_LOGE("SignalOutput", "Failed to create signal modulation task");
    } else {
        ESP_LOGI("SignalOutput", "Signal modulation task created successfully");
    }
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

// Accessor for signal task handle (for ISR use)
TaskHandle_t GetSignalTaskHandle(void)
{
    return signal_task_handle;
}
