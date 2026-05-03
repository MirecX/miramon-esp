#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize DNS server component
 * @return ESP_OK on success
 */
esp_err_t dns_server_init(void);

/**
 * @brief Start DNS server task
 * @return ESP_OK on success
 */
esp_err_t dns_server_start(void);

/**
 * @brief Stop DNS server task
 * @return ESP_OK on success
 */
esp_err_t dns_server_stop(void);

#ifdef __cplusplus
}
#endif
