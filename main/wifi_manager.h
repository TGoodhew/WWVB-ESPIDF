/*
 * WiFi Manager Module
 * 
 * Handles WiFi provisioning via BLE and connection management.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <stdbool.h>

// Service name buffer size (prefix + 6 hex chars + null terminator)
#define WIFI_SERVICE_NAME_SIZE 12

// WiFi state structure
typedef struct {
    bool is_provisioned;
    int retry_count;
    EventGroupHandle_t event_group;
} wifi_state_t;

// WiFi event group bits
extern const int WIFI_CONNECTED_BIT;
extern const int WIFI_FAIL_BIT;

/*
 * Initialize and setup WiFi with provisioning support
 * Sets up WiFi stack, provisioning manager, and starts connection
 */
void SetupWiFi(void);

/*
 * Get the WiFi state
 * 
 * @return Pointer to the WiFi state structure
 */
wifi_state_t* GetWiFiState(void);

#endif // WIFI_MANAGER_H
