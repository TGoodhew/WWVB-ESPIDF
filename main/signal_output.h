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
#include <esp_timer.h>

// WWVB Signal Constants
#define WWVB_CARRIER_FREQUENCY_HZ 60000  // 60 kHz carrier frequency for WWVB signal

// PWM Constants
#define PWM_RESOLUTION_BITS 8                               // 8-bit PWM resolution (0-255)
#define PWM_MAX_VALUE ((1 << PWM_RESOLUTION_BITS) - 1)     // Maximum PWM value (255)
#define PWM_DUTY_CYCLE_50_PERCENT (PWM_MAX_VALUE / 2)      // 50% duty cycle (127 for 8-bit)

// Timer Duration Constants (in microseconds)
#define TIMER_ONE_SECOND_US 1000000      // 1 second in microseconds
#define TIMER_BIT0_DURATION_US 200000    // 0.2 second - WWVB bit '0' reduced power duration
#define TIMER_BIT1_DURATION_US 500000    // 0.5 second - WWVB bit '1' reduced power duration
#define TIMER_MARKER_DURATION_US 800000  // 0.8 second - WWVB marker reduced power duration

// Debug queue for ISR to task communication
#define DEBUG_QUEUE_SIZE 10

// Debug message type for ISR to task communication
typedef struct {
    char type;  // '0', '1', 'M', or 'N' for newline/time log
} debug_msg_t;

/*
 * Setup the 60 kHz PWM output
 * Configures LEDC timer and channel for carrier generation.
 * Uses 8-bit resolution PWM at 60 kHz with 50% duty cycle.
 */
void Setup60KHzOutput(void);

/*
 * Setup all timers for WWVB signal generation
 * Creates timers for bit0 (0.2s), bit1 (0.5s), marker (0.8s), and second intervals.
 * These timers control the reduced-power periods in the WWVB signal.
 */
void SetupTimers(void);

/*
 * Start the second timer
 * Called after SNTP synchronization to begin WWVB signal transmission.
 * The timer triggers once per second to advance through the 60-second frame.
 */
void StartSecondTimer(void);

/*
 * Set carrier to zero (turn off)
 * Used during reduced power periods (bit encoding).
 * Sets PWM duty cycle to 0% to reduce carrier amplitude.
 */
void ZeroCarrier(void);

/*
 * Timer ISR to re-enable the carrier signal
 * Called after bit/marker reduced power period completes.
 * Restores PWM duty cycle to 50% for full carrier power.
 * 
 * @param param Timer parameter (unused)
 */
void TimerSignalReenable_ISR(void *param);

/*
 * Debug task to handle logging from ISR context
 * Receives debug messages from ISR via queue and outputs them.
 * This prevents blocking operations in ISR context.
 * 
 * @param pvParameters Task parameters (unused)
 */
void debug_task(void *pvParameters);

/*
 * Get the debug queue handle
 * Returns the queue used for ISR-to-task communication.
 * 
 * @return Handle to the debug queue, or NULL if not initialized
 */
QueueHandle_t GetDebugQueue(void);

/*
 * Create the debug queue and task
 * Initializes the message queue and creates the debug task.
 * Must be called before starting signal generation.
 */
void InitDebugQueue(void);

/*
 * Get timer handles for ISR use
 * These accessors allow ISR code to access timer handles safely.
 * 
 * @return Timer handle, or NULL if not initialized
 */
esp_timer_handle_t GetBit0Timer(void);
esp_timer_handle_t GetBit1Timer(void);
esp_timer_handle_t GetMarkerTimer(void);

#endif // SIGNAL_OUTPUT_H
