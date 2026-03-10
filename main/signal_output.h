/*
 * Signal Output Module
 * 
 * Handles PWM generation for 60 kHz WWVB carrier signal and
 * timer management for signal modulation.
 */

#ifndef SIGNAL_OUTPUT_H
#define SIGNAL_OUTPUT_H

#include <freertos/FreeRTOS.h>
#include <esp_timer.h>

// WWVB Signal Constants
#define WWVB_CARRIER_FREQUENCY_HZ 60000  // 60 kHz carrier frequency for WWVB signal

// PWM Constants
#define PWM_RESOLUTION_BITS 8                               // 8-bit PWM resolution (0-255)
#define PWM_MAX_VALUE ((1 << PWM_RESOLUTION_BITS) - 1)     // Maximum PWM value (255)
#define PWM_DUTY_CYCLE_50_PERCENT (1 << (PWM_RESOLUTION_BITS - 1))  // 50% duty cycle (128 for 8-bit)

// Timer Duration Constants (in microseconds)
#define TIMER_ONE_SECOND_US 1000000      // 1 second in microseconds
#define TIMER_BIT0_DURATION_US 200000    // 0.2 second - WWVB bit '0' reduced power duration
#define TIMER_BIT1_DURATION_US 500000    // 0.5 second - WWVB bit '1' reduced power duration
#define TIMER_MARKER_DURATION_US 800000  // 0.8 second - WWVB marker reduced power duration

/*
 * Setup the 60 kHz PWM output
 * Configures LEDC timer and channel for carrier generation.
 * Uses 8-bit resolution PWM at 60 kHz with 50% duty cycle.
 */
void Setup60KHzOutput(void);

/*
 * Setup timers for WWVB signal generation.
 * Creates the 1 Hz frame timer and a one-shot carrier re-enable timer.
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
 * Schedule carrier re-enable after a precise delay.
 * Uses a one-shot esp_timer to restore the PWM duty cycle.
 *
 * @param delay_us Delay in microseconds before the carrier is re-enabled
 */
void ScheduleCarrierReenable(uint64_t delay_us);


#endif // SIGNAL_OUTPUT_H
