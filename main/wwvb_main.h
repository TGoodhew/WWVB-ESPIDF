/*
 * WWVB Main Module Header
 * 
 * Exported functions from main.c for use by other modules.
 */

#ifndef WWVB_MAIN_H
#define WWVB_MAIN_H

#include <stdbool.h>

/*
 * Setup WWVB signal array with current time data
 * 
 * Encodes the current UTC time into the staging WWVB signal array.
 * Must be called after SNTP synchronization to ensure valid time.
 * This function writes to the staging buffer and sets the swap_pending flag.
 * 
 * Called from:
 * - Main loop - Periodic updates when signaled by ISR
 * 
 * @return true if encoding succeeded, false if time validation failed
 */
bool SetupWWVBArray(void);

/*
 * Initialize WWVB active buffer before timer starts
 * 
 * This function is called once after SNTP synchronization to prepare the
 * initial WWVB signal data before the timer starts transmitting. It:
 * 1. Encodes the current time into the staging buffer
 * 2. Copies staging buffer to active buffer for immediate transmission
 * 3. Resets the swap_pending flag
 * 
 * This ensures the first transmitted frame contains valid time data rather
 * than all zeros. Must be called after SNTP sync but before StartSecondTimer().
 * 
 * Called from:
 * - SNTP_callback() - Initial setup after time sync, before timer starts
 * 
 * @return true if initialization succeeded, false if time encoding failed
 */
bool InitializeWWVBBuffer(void);

#endif // WWVB_MAIN_H
