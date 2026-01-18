/*
 * Time Synchronization Module
 * 
 * Handles SNTP synchronization and time management.
 */

#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <sys/time.h>

/*
 * Setup SNTP synchronization
 * Configures SNTP client and waits for initial synchronization
 */
void SetupSNTP(void);

/*
 * SNTP synchronization callback
 * Called when time is synchronized via SNTP
 * 
 * @param tv The synchronized time value
 */
void SNTP_callback(struct timeval *tv);

/*
 * Log the current system time
 */
void LogCurrentTime(void);

#endif // TIME_SYNC_H
