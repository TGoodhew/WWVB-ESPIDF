/*
 * WWVB Main Module Header
 * 
 * Exported functions from main.c for use by other modules.
 */

#ifndef WWVB_MAIN_H
#define WWVB_MAIN_H

/*
 * Setup WWVB signal array with current time data
 * 
 * Encodes the current UTC time into the staging WWVB signal array.
 * Must be called after SNTP synchronization to ensure valid time.
 * This function writes to the staging buffer and sets the swap_pending flag.
 * 
 * Called from:
 * - SNTP_callback() - Initial setup after time sync, before timer starts
 * - Main loop - Periodic updates when signaled by ISR
 */
void SetupWWVBArray(void);

#endif // WWVB_MAIN_H
