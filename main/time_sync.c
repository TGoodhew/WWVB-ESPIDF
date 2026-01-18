/*
 * Time Synchronization Module
 * 
 * This module handles Network Time Protocol (NTP) synchronization via SNTP
 * (Simple Network Time Protocol), the client subset of NTP. It ensures the
 * ESP32 system clock is accurately set to UTC time before WWVB signal generation begins.
 * 
 * SNTP Operation:
 * 1. Connects to configured NTP server (default: pool.ntp.org)
 * 2. Sends time request over UDP port 123
 * 3. Receives timestamp from NTP server (accurate to milliseconds)
 * 4. Updates system clock
 * 5. Triggers callback to start WWVB signal generation
 * 
 * The module implements retry logic with configurable attempts and delays to
 * handle temporary network issues or unresponsive NTP servers.
 */

#include "time_sync.h"
#include "signal_output.h"
#include <string.h>
#include <time.h>
#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"

// SNTP Constants
#define SNTP_SYNC_TIMEOUT_MS 10000       // 10 second timeout for SNTP synchronization
#define TIME_STRING_BUFFER_SIZE 64       // Buffer size for time string formatting

// WWVB related
static const char *ntpServer = CONFIG_WWVB_NTP_SERVER;

/**
 * @brief Initialize and configure SNTP client with retry logic
 * 
 * This function sets up the SNTP client to synchronize the ESP32's system clock
 * with an NTP server. It implements robust retry logic to handle network issues:
 * 
 * Process:
 * 1. Initialize SNTP with configured server (from CONFIG_WWVB_NTP_SERVER)
 * 2. Register callback for successful synchronization
 * 3. Attempt synchronization with timeout
 * 4. Retry up to SNTP_MAX_RETRY_ATTEMPTS times if it fails
 * 5. Abort execution if all retries fail (cannot generate accurate signal without time)
 * 
 * Why accurate time is critical:
 * - WWVB encodes the current time in each 60-second frame
 * - Atomic clocks use this to set their time
 * - Even 1 second error would cause all synced clocks to be wrong
 * - UTC time is required (not local time) for WWVB encoding
 * 
 * The function blocks until synchronization succeeds or all retries are exhausted.
 * After successful sync, the SNTP_callback is invoked to start signal generation.
 */
void SetupSNTP(void)
{
    // Configure SNTP with callback for synchronization notification
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntpServer);
    sntp_config.sync_cb = SNTP_callback;  // Will be called when time is synced

    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_config));

    // Implement retry logic for SNTP synchronization
    // Network issues, server downtime, or DNS failures can cause initial attempts to fail
    esp_err_t sntp_result = ESP_FAIL;
    
    for (int retry = 0; retry < SNTP_MAX_RETRY_ATTEMPTS; retry++)
    {
        // Wait for synchronization with timeout
        sntp_result = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SNTP_SYNC_TIMEOUT_MS));
        
        if (sntp_result == ESP_OK)
        {
            ESP_LOGI("SNTP", "System time updated successfully");
            break;  // Success! Exit retry loop
        }
        else
        {
            ESP_LOGE("SNTP", "Failed to update system time within %dms timeout (attempt %d/%d)", 
                     SNTP_SYNC_TIMEOUT_MS, retry + 1, SNTP_MAX_RETRY_ATTEMPTS);
            
            // Wait before retrying (unless this was the last attempt)
            if (retry < SNTP_MAX_RETRY_ATTEMPTS - 1)
            {
                ESP_LOGI("SNTP", "Retrying SNTP synchronization in %dms...", SNTP_RETRY_DELAY_MS);
                vTaskDelay(pdMS_TO_TICKS(SNTP_RETRY_DELAY_MS));
            }
        }
    }
    
    // If all retries failed, we cannot continue
    // Generating WWVB signal with incorrect time would cause synced clocks to be wrong
    if (sntp_result != ESP_OK)
    {
        ESP_LOGE("SNTP", "SNTP synchronization failed after %d attempts. Cannot continue without valid time.", 
                 SNTP_MAX_RETRY_ATTEMPTS);
        ESP_ERROR_CHECK(sntp_result); // This will abort execution
    }
}

/**
 * @brief SNTP synchronization success callback
 * 
 * This callback is invoked by the SNTP subsystem when time synchronization
 * completes successfully. It's the trigger point that starts the WWVB signal
 * generation, ensuring we only begin transmitting after we have accurate time.
 * 
 * The callback:
 * 1. Logs the successful synchronization event
 * 2. Starts the 1 Hz second timer (defined in signal_output module)
 * 3. Initiates the continuous WWVB signal transmission cycle
 * 
 * After this callback, the system enters its steady-state operation:
 * - Second timer fires every second
 * - ISR reads and transmits bits from the WWVB frame
 * - Main task updates the frame buffer twice per minute
 * - SNTP continues periodic re-synchronization in the background
 * 
 * @param tv Pointer to timeval structure with the synchronized time
 */
void SNTP_callback(struct timeval *tv)
{
    ESP_LOGI("SNTP", "SNTP Synchronized - System time is now accurate");
    
    // Start the second timer to begin WWVB signal generation
    // This is the moment when the emulator starts transmitting
    StartSecondTimer();
}

/**
 * @brief Log the current system time in human-readable format
 * 
 * This utility function formats and logs the current UTC time to the console.
 * It's used for debugging and verification that time synchronization is working.
 * 
 * The function:
 * 1. Gets current time from system clock (set by SNTP)
 * 2. Converts to UTC using gmtime()
 * 3. Formats as human-readable string using strftime()
 * 4. Logs to console
 * 
 * Called from the debug task when a minute boundary is reached (slot 60→0)
 * to provide periodic confirmation that time is advancing correctly.
 */
void LogCurrentTime(void)
{
    time_t rawtime;
    struct tm *utcTime;

    time(&rawtime);
    utcTime = gmtime(&rawtime);

    // Validate gmtime() return value
    if (utcTime == NULL)
    {
        ESP_LOGE("Time", "Failed to get UTC time, gmtime returned NULL");
        return;
    }

    // Format the time as a string
    char strftime_buf[TIME_STRING_BUFFER_SIZE];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", utcTime);

    // Write the system time as a log entry
    ESP_LOGI("Time", "Current system time: %s", strftime_buf);
}
