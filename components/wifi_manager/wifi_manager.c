/**
 * @file wifi_manager.c
 * @brief WiFi manager component - credential storage and state machine
 */

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "wifi_manager.h"
#include "dns_server.h"
#include "http_server.h"

static const char *TAG = "wifi_manager";

// NVS namespace and keys
#define NVS_NAMESPACE       "wifi_creds"
#define NVS_KEY_SSID        "wifi_ssid"
#define NVS_KEY_PASS        "wifi_pass"
#define NVS_KEY_CONFIGURED  "wifi_configured"

// Maximum lengths (including null terminator)
#define MAX_SSID_LEN        32
#define MAX_PASS_LEN        64

// NVS handle
static nvs_handle_t s_nvs_handle = 0;
static bool s_nvs_initialized = false;

// WiFi state
static wifi_state_t s_wifi_state = WIFI_STATE_INIT;

// STA connection timeout timer (30 seconds)
static TimerHandle_t s_sta_timeout_timer = NULL;
static const int STA_TIMEOUT_SEC = 30;

// Forward declarations
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void sta_timeout_callback(TimerHandle_t timer);

/**
 * @brief Initialize NVS storage
 * @return ESP_OK on success
 */
static esp_err_t nvs_init(void)
{
    if (s_nvs_initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    s_nvs_initialized = true;
    ESP_LOGI(TAG, "NVS initialized");
    return ESP_OK;
}

esp_err_t wifi_creds_load(char *ssid, size_t ssid_size, char *pass, size_t pass_size)
{
    if (!s_nvs_initialized) {
        esp_err_t err = nvs_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    // Check if configured
    uint8_t configured = 0;
    esp_err_t err = nvs_get_u8(s_nvs_handle, NVS_KEY_CONFIGURED, &configured);
    if (err != ESP_OK || !configured) {
        ESP_LOGD(TAG, "Credentials not configured");
        return ESP_ERR_NOT_FOUND;
    }

    // Load SSID
    size_t ssid_len = ssid_size;
    err = nvs_get_str(s_nvs_handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load SSID: %s", esp_err_to_name(err));
        return err;
    }

    // Load password
    size_t pass_len = pass_size;
    err = nvs_get_str(s_nvs_handle, NVS_KEY_PASS, pass, &pass_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load password: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGD(TAG, "Credentials loaded successfully");
    return ESP_OK;
}

esp_err_t wifi_creds_save(const char *ssid, const char *password)
{
    // Validate parameters
    if (ssid == NULL || password == NULL) {
        ESP_LOGE(TAG, "NULL parameters");
        return ESP_ERR_INVALID_ARG;
    }

    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(password);

    // Validate lengths
    if (ssid_len == 0) {
        ESP_LOGE(TAG, "Empty SSID");
        return ESP_ERR_INVALID_ARG;
    }
    if (ssid_len >= MAX_SSID_LEN) {
        ESP_LOGE(TAG, "SSID too long: %zu (max %d)", ssid_len, MAX_SSID_LEN - 1);
        return ESP_ERR_INVALID_ARG;
    }
    if (pass_len >= MAX_PASS_LEN) {
        ESP_LOGE(TAG, "Password too long: %zu (max %d)", pass_len, MAX_PASS_LEN - 1);
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_nvs_initialized) {
        esp_err_t err = nvs_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    // Save SSID
    esp_err_t err = nvs_set_str(s_nvs_handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSID: %s", esp_err_to_name(err));
        return err;
    }

    // Save password
    err = nvs_set_str(s_nvs_handle, NVS_KEY_PASS, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(err));
        return err;
    }

    // Set configured flag
    err = nvs_set_u8(s_nvs_handle, NVS_KEY_CONFIGURED, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configured flag: %s", esp_err_to_name(err));
        return err;
    }

    // Commit changes
    err = nvs_commit(s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "WiFi credentials saved: SSID=%s", ssid);
    return ESP_OK;
}

bool wifi_creds_is_configured(void)
{
    if (!s_nvs_initialized) {
        esp_err_t err = nvs_init();
        if (err != ESP_OK) {
            return false;
        }
    }

    uint8_t configured = 0;
    esp_err_t err = nvs_get_u8(s_nvs_handle, NVS_KEY_CONFIGURED, &configured);
    if (err != ESP_OK) {
        return false;
    }

    return configured != 0;
}

esp_err_t wifi_creds_clear(void)
{
    if (!s_nvs_initialized) {
        esp_err_t err = nvs_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    // Erase all keys in namespace
    esp_err_t err = nvs_erase_key(s_nvs_handle, NVS_KEY_SSID);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase SSID: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(s_nvs_handle, NVS_KEY_PASS);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase password: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(s_nvs_handle, NVS_KEY_CONFIGURED);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase configured flag: %s", esp_err_to_name(err));
        return err;
    }

    // Commit changes
    err = nvs_commit(s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "WiFi credentials cleared");
    return ESP_OK;
}

esp_err_t wifi_manager_init(void)
{
    return nvs_init();
}

wifi_state_t wifi_manager_get_state(void)
{
    return s_wifi_state;
}

/**
 * @brief Set WiFi state (internal use)
 * @param state New state
 */
void wifi_manager_set_state(wifi_state_t state)
{
    const char *state_names[] = {
        "INIT",
        "STA_CONNECTING",
        "STA_CONNECTED",
        "AP_ACTIVE",
        "SWITCHING"
    };
    
    if (state != s_wifi_state) {
        ESP_LOGI(TAG, "WiFi state: %s -> %s", 
                 state_names[s_wifi_state], 
                 state_names[state]);
        s_wifi_state = state;
    }
}

/**
 * @brief STA connection timeout callback
 */
static void sta_timeout_callback(TimerHandle_t timer)
{
    ESP_LOGW(TAG, "STA connection timeout after %d seconds", STA_TIMEOUT_SEC);
    
    if (s_wifi_state == WIFI_STATE_STA_CONNECTING) {
        ESP_LOGI(TAG, "Transitioning to AP_ACTIVE due to timeout");
        wifi_manager_set_state(WIFI_STATE_AP_ACTIVE);
        // TODO: Start AP mode here
    }
}

/**
 * @brief Start STA connection timeout timer
 */
static void start_sta_timeout_timer(void)
{
    if (s_sta_timeout_timer == NULL) {
        s_sta_timeout_timer = xTimerCreate(
            "sta_timeout",
            pdMS_TO_TICKS(STA_TIMEOUT_SEC * 1000),
            pdFALSE,  // One-shot timer
            NULL,
            sta_timeout_callback
        );
    }
    
    if (s_sta_timeout_timer != NULL) {
        xTimerStart(s_sta_timeout_timer, 0);
        ESP_LOGD(TAG, "STA timeout timer started (%ds)", STA_TIMEOUT_SEC);
    }
}

/**
 * @brief Stop STA connection timeout timer
 */
static void stop_sta_timeout_timer(void)
{
    if (s_sta_timeout_timer != NULL) {
        xTimerStop(s_sta_timeout_timer, 0);
        ESP_LOGD(TAG, "STA timeout timer stopped");
    }
}

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START: {
                ESP_LOGI(TAG, "STA started, connecting...");
                esp_wifi_connect();
                break;
            }
            
            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
                ESP_LOGI(TAG, "Connected to SSID: %s", (char *)event->ssid);
                // Timer will be stopped when we get IP
                break;
            }
            
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "Disconnected from SSID: %s, reason: %d", 
                         (char *)event->ssid, event->reason);
                
                if (s_wifi_state == WIFI_STATE_STA_CONNECTED) {
                    ESP_LOGI(TAG, "Auto-reconnect enabled, attempting reconnection");
                    esp_wifi_connect();
                    // State remains STA_CONNECTED during reconnection attempts
                }
                break;
            }
            
            case WIFI_EVENT_AP_START: {
                ESP_LOGI(TAG, "AP started");
                break;
            }
            
            case WIFI_EVENT_AP_STOP: {
                ESP_LOGI(TAG, "AP stopped");
                break;
            }
            
            default:
                break;
        }
    } 
    else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
                ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
                
                // Stop timeout timer
                stop_sta_timeout_timer();
                
                // Transition to connected state
                wifi_manager_set_state(WIFI_STATE_STA_CONNECTED);
                break;
            }
            
            default:
                break;
        }
    }
}

/**
 * @brief Initialize WiFi hardware and event handlers
 */
static esp_err_t wifi_init(void)
{
    // Create default network interface
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Register event handlers (using default loop)
    esp_err_t err = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        NULL
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(err));
        return err;
    }
    
    err = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL,
        NULL
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(err));
        return err;
    }
    
    // Initialize WiFi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "WiFi initialized");
    return ESP_OK;
}

/**
 * @brief Start STA connection
 */
static esp_err_t wifi_start_sta(void)
{
    char ssid[32] = {0};
    char pass[64] = {0};
    
    esp_err_t err = wifi_creds_load(ssid, sizeof(ssid), pass, sizeof(pass));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load credentials");
        return err;
    }
    
    // Configure STA
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
    
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA mode: %s", esp_err_to_name(err));
        return err;
    }
    
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(err));
        return err;
    }
    
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
    wifi_manager_set_state(WIFI_STATE_STA_CONNECTING);
    
    // Start timeout timer
    start_sta_timeout_timer();
    
    return ESP_OK;
}

/**
 * @brief Start AP mode
 */
static esp_err_t wifi_start_ap(void)
{
    // Get MAC address for AP SSID
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "MyraMon_%02X%02X", mac[4], mac[5]);
    
    // Configure AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ap_ssid),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)wifi_config.ap.ssid, ap_ssid, sizeof(wifi_config.ap.ssid) - 1);
    
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP mode: %s", esp_err_to_name(err));
        return err;
    }
    
    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
        return err;
    }
    
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "AP started: %s", ap_ssid);
    wifi_manager_set_state(WIFI_STATE_AP_ACTIVE);
    
    // Start DNS server for captive portal
    dns_server_init();
    dns_server_start();
    
    // Start HTTP server for captive portal
    http_server_init();
    http_server_start();
    
    return ESP_OK;
}

/**
 * @brief Start WiFi based on credential status
 */
esp_err_t wifi_manager_start(void)
{
    esp_err_t err = wifi_init();
    if (err != ESP_OK) {
        return err;
    }
    
    if (wifi_creds_is_configured()) {
        ESP_LOGI(TAG, "Credentials found, starting STA connection");
        err = wifi_start_sta();
    } else {
        ESP_LOGI(TAG, "No credentials, starting AP mode");
        err = wifi_start_ap();
    }
    
    return err;
}
