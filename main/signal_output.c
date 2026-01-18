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

void Setup60KHzOutput(void)
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
        .freq_hz = WWVB_CARRIER_FREQUENCY_HZ, // frequency of PWM signal
        .speed_mode = LEDC_HIGH_SPEED_MODE,   // timer mode
        .timer_num = LEDC_TIMER_0             // timer index
    };
    
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel.channel = LEDC_CHANNEL_0;
    ledc_channel.duty = PWM_DUTY_CYCLE_50_PERCENT;
    ledc_channel.gpio_num = (gpio_num_t)CONFIG_WWVB_OUTPUT_PIN; // Configurable GPIO for WWVB output (default: 26/A0 on Huzzah32)
    ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_channel.timer_sel = LEDC_TIMER_0;

    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void SetupTimers(void)
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

void ZeroCarrier(void)
{
    // Remove ESP_ERROR_CHECK - just call the functions directly
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}

// All the bit/marker timers just reenable the 50% duty cycle of the 60KHz signal
void IRAM_ATTR TimerSignalReenable_ISR(void *param)
{
    (void)param; // Suppress unused parameter warning
    // Remove ESP_ERROR_CHECK - just call the functions directly
    // Errors in ISR context cannot be safely handled
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
