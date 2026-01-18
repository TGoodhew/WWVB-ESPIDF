/*
 * Signal Output Module
 * 
 * Handles PWM generation for 60 kHz WWVB carrier signal and
 * timer management for signal modulation.
 */

#ifndef SIGNAL_OUTPUT_H
#define SIGNAL_OUTPUT_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// WWVB Signal Constants
#define WWVB_CARRIER_FREQUENCY_HZ 60000  // 60 kHz carrier frequency for WWVB signal
#define PWM_DUTY_CYCLE_50_PERCENT (255 / 2)  // 50% duty cycle for 8-bit PWM resolution

// Timer Duration Constants (in microseconds)
#define TIMER_ONE_SECOND_US 1000000      // 1 second in microseconds
#define TIMER_BIT0_DURATION_US 200000    // 0.2 second - WWVB bit '0' reduced power duration
#define TIMER_BIT1_DURATION_US 500000    // 0.5 second - WWVB bit '1' reduced power duration
#define TIMER_MARKER_DURATION_US 800000  // 0.8 second - WWVB marker reduced power duration

// Debug queue for ISR to task communication
#define DEBUG_QUEUE_SIZE 10
typedef struct {
    char type;  // '0', '1', 'M', or 'N' for newline/time log
} debug_msg_t;

/*
 * Setup the 60 kHz PWM output
 * Configures LEDC timer and channel for carrier generation
 */
void Setup60KHzOutput(void);

/*
 * Setup all timers for WWVB signal generation
 * Creates timers for bit0, bit1, marker, and second intervals
 */
void SetupTimers(void);

/*
 * Start the second timer
 * Called after SNTP synchronization
 */
void StartSecondTimer(void);

/*
 * Set carrier to zero (turn off)
 * Used during reduced power periods
 */
void ZeroCarrier(void);

/*
 * Debug task to handle logging from ISR context
 * 
 * @param pvParameters Task parameters (unused)
 */
void debug_task(void *pvParameters);

/*
 * Get the debug queue handle
 * 
 * @return Handle to the debug queue
 */
QueueHandle_t GetDebugQueue(void);

/*
 * Create the debug queue and task
 */
void InitDebugQueue(void);

/*
 * Get timer handles for ISR use
 */
esp_timer_handle_t GetBit0Timer(void);
esp_timer_handle_t GetBit1Timer(void);
esp_timer_handle_t GetMarkerTimer(void);

#endif // SIGNAL_OUTPUT_H
