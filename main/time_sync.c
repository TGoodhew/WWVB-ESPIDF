/*
 * Time Synchronization Module
 */

#include "time_sync.h"
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

// External timer handle (defined in signal_output module)
extern void StartSecondTimer(void);

void SetupSNTP(void)
{
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntpServer);
    sntp_config.sync_cb = SNTP_callback;

    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_config));

    // Retry SNTP synchronization up to 3 times
    const int max_retries = 3;
    esp_err_t sntp_result = ESP_FAIL;
    
    for (int retry = 0; retry < max_retries; retry++)
    {
        sntp_result = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SNTP_SYNC_TIMEOUT_MS));
        
        if (sntp_result == ESP_OK)
        {
            ESP_LOGI("SNTP", "System time updated successfully");
            break;
        }
        else
        {
            ESP_LOGE("SNTP", "Failed to update system time within 10s timeout (attempt %d/%d)", retry + 1, max_retries);
            
            if (retry < max_retries - 1)
            {
                ESP_LOGI("SNTP", "Retrying SNTP synchronization...");
                vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2 seconds before retry
            }
        }
    }
    
    // If all retries failed, halt execution
    if (sntp_result != ESP_OK)
    {
        ESP_LOGE("SNTP", "SNTP synchronization failed after %d attempts. Cannot continue without valid time.", max_retries);
        ESP_ERROR_CHECK(sntp_result); // This will abort execution
    }
}

void SNTP_callback(struct timeval *tv)
{
    ESP_LOGI("SNTP", "SNTP Synchronized");
    
    // Start the second timer (defined in signal_output module)
    StartSecondTimer();
}

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
