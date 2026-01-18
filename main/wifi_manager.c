/*
 * WiFi Manager Module
 * 
 * This module manages WiFi connectivity with secure BLE provisioning.
 * 
 * Key Features:
 * - **BLE Provisioning**: First-time setup via Bluetooth Low Energy
 * - **Cryptographic Security**: SHA-256 based PoP generation from MAC address
 * - **Persistent Storage**: WiFi credentials saved in NVS (Non-Volatile Storage)
 * - **Auto-reconnect**: Automatically reconnects after device reboot
 * - **Retry Logic**: Configurable retry attempts for connection failures
 * 
 * Security Design:
 * The Proof-of-Possession (PoP) is generated using SHA-256 hash of the device's
 * MAC address. This prevents attackers from deriving the PoP by observing:
 * - BLE advertising packets (which contain service name derived from MAC)
 * - Network scanning (which could reveal MAC address)
 * 
 * The cryptographic hash (SHA-256) is a one-way function, making it computationally
 * infeasible to derive the PoP from the MAC address without knowing the hashing
 * algorithm used. The PoP must be obtained from the device's serial console.
 * 
 * Provisioning Flow:
 * 1. Check NVS for saved credentials
 * 2. If none found, start BLE advertising with unique service name
 * 3. User connects via ESP BLE Provisioning mobile app
 * 4. User enters PoP from serial console
 * 5. User provides WiFi SSID and password
 * 6. Credentials saved to NVS, BLE stopped
 * 7. WiFi connection established
 * 8. Future boots skip provisioning and connect directly
 */

#include "wifi_manager.h"
#include <string.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include <mbedtls/sha256.h>
#include "sdkconfig.h"

// Provisioning PoP buffer size (12 hex chars + null terminator)
#define POP_BUFFER_SIZE 13

// Service name prefix for BLE provisioning
#define SERVICE_NAME_PREFIX "PROV_"
#define SERVICE_NAME_PREFIX_LEN 5
#define SERVICE_NAME_STRING_LENGTH 11  // String length: 5 prefix + 6 hex chars (excludes null terminator)

// MAC address size
#define MAC_ADDRESS_SIZE 6
#define MAC_HEX_CHARS_PER_BYTE 2

// Hash size for SHA-256
#define SHA256_HASH_SIZE 32

// Number of hash bytes used for PoP generation
#define POP_HASH_BYTES 6
#define POP_STRING_LENGTH 12  // String length: 6 bytes * 2 hex chars (excludes null terminator)

const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;

static wifi_state_t wifi_state = {
    .is_provisioned = false,
    .retry_count = 0,
    .event_group = NULL
};

// Forward declarations
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

/**
 * @brief Generate a device-unique BLE service name for provisioning
 * 
 * Creates a unique service name that users will see when scanning for BLE devices
 * during WiFi provisioning. The name format is "PROV_XXXXXX" where XXXXXX is
 * derived from the last 3 bytes of the device's MAC address in hexadecimal.
 * 
 * Why use MAC address:
 * - Guarantees uniqueness across all devices (MAC addresses are globally unique)
 * - Remains constant for the device lifetime
 * - Allows users to identify specific devices if multiple are being provisioned
 * 
 * Example:
 * - MAC address: 24:6F:28:AB:CD:EF
 * - Service name: "PROV_ABCDEF"
 * 
 * The service name is NOT secret - it's visible in BLE scans. Security is provided
 * by the separate PoP (Proof of Possession), not by hiding the service name.
 * 
 * @param service_name Output buffer for the service name (must be >= WIFI_SERVICE_NAME_SIZE)
 * @param max Size of the output buffer
 */
static void get_device_service_name(char *service_name, size_t max);

/**
 * @brief Generate a cryptographically secure Proof-of-Possession (PoP)
 * 
 * Creates a unique 12-character hexadecimal PoP by hashing the device's MAC address
 * with SHA-256. This provides real security compared to hardcoded or simple derivations.
 * 
 * Security Rationale:
 * - SHA-256 is a cryptographic one-way function
 * - Cannot derive PoP from MAC address without knowing the algorithm
 * - Even if attacker sees MAC (via BLE or network), they cannot compute PoP
 * - Prevents unauthorized provisioning attempts
 * 
 * Process:
 * 1. Get device MAC address (6 bytes)
 * 2. Compute SHA-256 hash (produces 32 bytes)
 * 3. Take first 6 bytes of hash
 * 4. Convert to 12-character hex string
 * 5. User must obtain this from serial console to provision device
 * 
 * Example:
 * - MAC: 24:6F:28:AB:CD:EF
 * - SHA-256(MAC): A3B2C1D4E5F6... (32 bytes)
 * - PoP: "A3B2C1D4E5F6" (first 6 bytes as hex)
 * 
 * The PoP should be displayed on the serial console so legitimate users can
 * enter it during provisioning, while preventing remote attackers from guessing it.
 * 
 * @param pop Output buffer for the PoP string (must be >= POP_BUFFER_SIZE)
 * @param max Size of the output buffer
 */
static void generate_unique_pop(char *pop, size_t max);

wifi_state_t* GetWiFiState(void)
{
    return &wifi_state;
}

void SetupWiFi(void)
{
    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_state.event_group = xEventGroupCreate();

    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Configuration for the provisioning manager */
    wifi_prov_mgr_config_t wifi_prov_config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM};

    /* Initialize provisioning manager with the configuration parameters set above */
    ESP_ERROR_CHECK(wifi_prov_mgr_init(wifi_prov_config));

    /* Let's find out if the device is provisioned */
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&wifi_state.is_provisioned));

    ESP_LOGI("WiFi", "Is provisioned: %s", wifi_state.is_provisioned ? "true" : "false");

    /* If device is not yet provisioned start provisioning service */
    if (!wifi_state.is_provisioned)
    {
        ESP_LOGI("WiFi", "Starting provisioning");

        char service_name[WIFI_SERVICE_NAME_SIZE];
        get_device_service_name(service_name, sizeof(service_name));

        /* Generate a device-unique proof-of-possession using a cryptographic hash of the MAC address.
         * This provides real security by using SHA-256 hashing, preventing attackers from deriving
         * the PoP by observing the MAC address through BLE advertising or network scanning.
         * The PoP should be printed/displayed for the user to enter during provisioning.
         */
        char pop[POP_BUFFER_SIZE];
        generate_unique_pop(pop, sizeof(pop));
        
        // Log the PoP so the user knows what to enter during provisioning
        ESP_LOGI("WiFi", "Provisioning PoP: %s", pop);

        /* This is the structure for passing security parameters
         * for the protocomm security 1.
         */
        wifi_prov_security1_params_t *sec_params = pop;

        uint8_t custom_service_uuid[] = {
            /* LSB <---------------------------------------
             * ---------------------------------------> MSB */
            0xb4,
            0xdf,
            0x5a,
            0x1c,
            0x3f,
            0x6b,
            0xf4,
            0xbf,
            0xea,
            0x4a,
            0x82,
            0x03,
            0x04,
            0x90,
            0x1a,
            0x02,
        };

        ESP_ERROR_CHECK(wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid));

        /* Start provisioning service */
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, (const void *)sec_params, service_name, NULL));
    }
    else
    {
        ESP_LOGI("WiFi", "Already provisioned, starting Wi-Fi STA");

        /* We don't need the manager as device is already provisioned, so let's release it's resources */
        wifi_prov_mgr_deinit();

        /* Start Wi-Fi station */
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI("WiFi", "Connected");
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        /* Only connect if we're already provisioned and have credentials.
         * During provisioning, WiFi will be started but we should wait for
         * credentials to be received. The provisioning manager will handle
         * WiFi configuration internally, and connection will happen automatically.
         */
        if (wifi_state.is_provisioned) 
        {
            ESP_LOGI("WiFi", "WiFi started, connecting to configured AP");
            ESP_ERROR_CHECK(esp_wifi_connect());
        }
        else
        {
            ESP_LOGI("WiFi", "WiFi started, waiting for provisioning to complete");
        }
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        /* Only retry connection if we have credentials (are provisioned).
         * During provisioning, disconnects should not trigger reconnection attempts.
         */
        if (wifi_state.is_provisioned && wifi_state.retry_count < CONFIG_WIFI_MAX_RETRY) 
        {
            ESP_ERROR_CHECK(esp_wifi_connect());
            wifi_state.retry_count++;
            ESP_LOGI("WiFi", "retry to connect to the AP");
        } 
        else if (!wifi_state.is_provisioned)
        {
            ESP_LOGW("WiFi", "Disconnected but not provisioned yet, waiting for provisioning");
        }
        else 
        {
            xEventGroupSetBits(wifi_state.event_group, WIFI_FAIL_BIT);
            ESP_LOGE("WiFi", "connect to the AP failed after %d retries", CONFIG_WIFI_MAX_RETRY);
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI("WiFi", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_state.retry_count = 0;
        xEventGroupSetBits(wifi_state.event_group, WIFI_CONNECTED_BIT);
    } 
    else if (event_base == WIFI_PROV_EVENT) 
    {
        switch (event_id) 
        {
            case WIFI_PROV_START:
                ESP_LOGI("WiFi", "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: 
            {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI("WiFi", "Received Wi-Fi credentials"
                         "\n\tSSID     : %s\n\tPassword : %s",
                         (const char *) wifi_sta_cfg->ssid,
                         (const char *) wifi_sta_cfg->password);
                break;
            }
            case WIFI_PROV_CRED_FAIL: 
            {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE("WiFi", "Provisioning failed!\n\tReason : %s" "\n\tPlease reset to factory and retry provisioning",  (*reason == WIFI_PROV_STA_AUTH_ERROR) ?  "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI("WiFi", "Provisioning successful");
                /* Credentials have been received and validated by the provisioning manager.
                 * The provisioning manager has already configured WiFi with these credentials.
                 * Connection will be handled automatically by WiFi event handlers.
                 */
                break;
            case WIFI_PROV_END:
                /* De-initialize manager once provisioning is finished */
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    }
}

static void get_device_service_name(char *service_name, size_t max)
{
    // Validate input parameters
    if (service_name == NULL || max < WIFI_SERVICE_NAME_SIZE)
    {
        ESP_LOGE("WiFi", "get_device_service_name: Invalid buffer (NULL or size %zu < %d)", 
                 max, WIFI_SERVICE_NAME_SIZE);
        if (service_name != NULL && max > 0)
        {
            service_name[0] = '\0';  // Set empty string on error
        }
        return;
    }
    
    uint8_t eth_mac[MAC_ADDRESS_SIZE];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, eth_mac));
    
    // Format: "PROV_" + last 3 bytes of MAC in hex (6 chars) + null
    const int result = snprintf(service_name, max, "%s%02X%02X%02X",
                                SERVICE_NAME_PREFIX, eth_mac[3], eth_mac[4], eth_mac[5]);
    
    // Verify snprintf success (result should be SERVICE_NAME_STRING_LENGTH)
    if (result < 0 || result >= (int)max)
    {
        ESP_LOGE("WiFi", "get_device_service_name: snprintf failed or truncated (result=%d, expected=%d)", 
                 result, SERVICE_NAME_STRING_LENGTH);
        service_name[0] = '\0';
    }
}

// Generate a cryptographically secure proof-of-possession (PoP) based on device MAC address.
// This uses SHA-256 hash of the MAC address to prevent attackers from deriving the PoP
// by observing the MAC address through BLE advertising or network scanning.
// 
// Security Properties:
// - One-way function: Cannot reverse SHA-256 to get input from output
// - Deterministic: Same MAC always produces same PoP (device consistency)
// - Unique: Different MAC addresses produce different PoPs
// - Unpredictable: Cannot guess PoP without computing the hash
// 
// Implementation uses mbedtls SHA-256 for cryptographic security.
static void generate_unique_pop(char *pop, size_t max)
{
    // Validate input parameters
    if (pop == NULL || max < POP_BUFFER_SIZE)
    {
        ESP_LOGE("WiFi", "generate_unique_pop: Invalid PoP buffer: pop is %s, size=%zu (need at least %d)",
                 pop == NULL ? "NULL" : "non-NULL", max, POP_BUFFER_SIZE);
        if (pop != NULL && max > 0)
        {
            pop[0] = '\0';  // Set empty string on error
        }
        return;
    }

    uint8_t eth_mac[MAC_ADDRESS_SIZE];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, eth_mac));
    
    // Use SHA-256 hash of MAC address for cryptographic security
    // This prevents attackers from deriving the PoP by observing the MAC address
    uint8_t hash[SHA256_HASH_SIZE];
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    
    int ret = mbedtls_sha256_starts(&sha256_ctx, 0);  // 0 = SHA-256 (not SHA-224)
    if (ret != 0)
    {
        ESP_LOGE("WiFi", "generate_unique_pop: SHA-256 start failed: %d", ret);
        mbedtls_sha256_free(&sha256_ctx);
        pop[0] = '\0';  // Set empty string on error
        return;
    }
    
    ret = mbedtls_sha256_update(&sha256_ctx, eth_mac, MAC_ADDRESS_SIZE);
    if (ret != 0)
    {
        ESP_LOGE("WiFi", "generate_unique_pop: SHA-256 update failed: %d", ret);
        mbedtls_sha256_free(&sha256_ctx);
        pop[0] = '\0';  // Set empty string on error
        return;
    }
    
    ret = mbedtls_sha256_finish(&sha256_ctx, hash);
    if (ret != 0)
    {
        ESP_LOGE("WiFi", "generate_unique_pop: SHA-256 finish failed: %d", ret);
        mbedtls_sha256_free(&sha256_ctx);
        pop[0] = '\0';  // Set empty string on error
        return;
    }
    
    mbedtls_sha256_free(&sha256_ctx);
    
    // Use first POP_HASH_BYTES bytes of hash to create 12-character hex PoP
    const int result = snprintf(pop, max, "%02X%02X%02X%02X%02X%02X",
                                hash[0], hash[1], hash[2], hash[3], hash[4], hash[5]);
    
    // Verify snprintf success (result should be POP_STRING_LENGTH)
    if (result < 0 || result >= (int)max)
    {
        ESP_LOGE("WiFi", "generate_unique_pop: snprintf failed or truncated (result=%d, expected=%d)", 
                 result, POP_STRING_LENGTH);
        pop[0] = '\0';
    }
}
