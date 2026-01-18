/*
 * Time Synchronization Module
 * 
 * Handles SNTP synchronization and time management.
 */

#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <sys/time.h>

// Maximum number of SNTP retry attempts
#define SNTP_MAX_RETRY_ATTEMPTS 3

// Delay between SNTP retry attempts (milliseconds)
#define SNTP_RETRY_DELAY_MS 2000

/*
 * Setup SNTP synchronization
 * Configures SNTP client and waits for initial synchronization.
 * Retries up to SNTP_MAX_RETRY_ATTEMPTS times if synchronization fails.
 * Aborts execution if all retry attempts fail.
 */
void SetupSNTP(void);

/*
 * SNTP synchronization callback
 * Called when time is successfully synchronized via SNTP.
 * Starts the second timer for WWVB signal generation.
 * 
 * @param tv The synchronized time value
 */
void SNTP_callback(struct timeval *tv);

/*
 * Log the current system time
 * Formats and logs the current UTC time to console.
 * Returns silently if time is not available.
 */
void LogCurrentTime(void);

#endif // TIME_SYNC_H
