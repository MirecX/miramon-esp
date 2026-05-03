#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi state machine states
 */
typedef enum {
    WIFI_STATE_INIT = 0,
    WIFI_STATE_STA_CONNECTING,
    WIFI_STATE_STA_CONNECTED,
    WIFI_STATE_AP_ACTIVE,
    WIFI_STATE_SWITCHING
} wifi_state_t;

/**
 * @brief Initialize the WiFi manager component
 * @return ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Load WiFi credentials from NVS
 * @param ssid Buffer to store SSID (must be at least 32 bytes)
 * @param ssid_size Size of ssid buffer
 * @param pass Buffer to store password (must be at least 64 bytes)
 * @param pass_size Size of pass buffer
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not configured
 */
esp_err_t wifi_creds_load(char *ssid, size_t ssid_size, char *pass, size_t pass_size);

/**
 * @brief Save WiFi credentials to NVS
 * @param ssid WiFi SSID (max 31 characters)
 * @param password WiFi password (max 63 characters)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if validation fails
 */
esp_err_t wifi_creds_save(const char *ssid, const char *password);

/**
 * @brief Check if WiFi credentials are configured
 * @return true if configured, false otherwise
 */
bool wifi_creds_is_configured(void);

/**
 * @brief Clear WiFi credentials from NVS
 * @return ESP_OK on success
 */
esp_err_t wifi_creds_clear(void);

/**
 * @brief Get current WiFi state
 * @return Current wifi_state_t
 */
wifi_state_t wifi_manager_get_state(void);

/**
 * @brief Check for STA timeout and start AP if needed
 * Call periodically from main loop
 */
void wifi_manager_check_timeout(void);

/**
 * @brief Start WiFi (STA or AP based on credentials)
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Set WiFi state (internal use)
 * @param state New state
 */
void wifi_manager_set_state(wifi_state_t state);

#ifdef __cplusplus
}
#endif
