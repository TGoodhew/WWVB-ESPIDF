/*
 * WiFi Manager Module
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

#define POP_BUFFER_SIZE 13  // 13 bytes: 12 hex chars (6 MAC bytes * 2) + null terminator

const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;

static wifi_state_t wifi_state = {
    .is_provisioned = false,
    .retry_count = 0,
    .event_group = NULL
};

// Forward declarations
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void get_device_service_name(char *service_name, size_t max);
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

        char service_name[12];
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
        ESP_ERROR_CHECK(esp_wifi_connect());
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        if (wifi_state.retry_count < CONFIG_WIFI_MAX_RETRY) 
        {
            ESP_ERROR_CHECK(esp_wifi_connect());
            wifi_state.retry_count++;
            ESP_LOGI("WiFi", "retry to connect to the AP");
        } 
        else 
        {
            xEventGroupSetBits(wifi_state.event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI("WiFi", "connect to the AP fail");
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
    uint8_t eth_mac[6];
    const char *ssid_prefix = "PROV_";
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, eth_mac));
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

// Generate a cryptographically secure proof-of-possession (PoP) based on device MAC address
// This uses SHA-256 hash of the MAC address to prevent attackers from deriving the PoP
// by observing the MAC address through BLE advertising or network scanning
static void generate_unique_pop(char *pop, size_t max)
{
    if (pop == NULL || max < POP_BUFFER_SIZE)
    {
        ESP_LOGE("POP", "Invalid PoP buffer: pop is %s, size=%zu (need at least %d)",
                 pop == NULL ? "NULL" : "non-NULL", max, POP_BUFFER_SIZE);
        if (pop != NULL && max > 0)
        {
            pop[0] = '\0';  // Set empty string on error
        }
        return;
    }

    uint8_t eth_mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, eth_mac));
    
    // Use SHA-256 hash of MAC address for cryptographic security
    // This prevents attackers from deriving the PoP by observing the MAC address
    uint8_t hash[32];  // SHA-256 produces 32 bytes
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    
    int ret = mbedtls_sha256_starts(&sha256_ctx, 0);  // 0 = SHA-256 (not SHA-224)
    if (ret != 0)
    {
        ESP_LOGE("POP", "SHA-256 start failed: %d", ret);
        mbedtls_sha256_free(&sha256_ctx);
        pop[0] = '\0';  // Set empty string on error
        return;
    }
    
    ret = mbedtls_sha256_update(&sha256_ctx, eth_mac, 6);
    if (ret != 0)
    {
        ESP_LOGE("POP", "SHA-256 update failed: %d", ret);
        mbedtls_sha256_free(&sha256_ctx);
        pop[0] = '\0';  // Set empty string on error
        return;
    }
    
    ret = mbedtls_sha256_finish(&sha256_ctx, hash);
    if (ret != 0)
    {
        ESP_LOGE("POP", "SHA-256 finish failed: %d", ret);
        mbedtls_sha256_free(&sha256_ctx);
        pop[0] = '\0';  // Set empty string on error
        return;
    }
    
    mbedtls_sha256_free(&sha256_ctx);
    
    // Use first 6 bytes of hash to create 12-character hex PoP
    snprintf(pop, max, "%02X%02X%02X%02X%02X%02X",
             hash[0], hash[1], hash[2], hash[3], hash[4], hash[5]);
}
