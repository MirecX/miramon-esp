#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "wifi_manager.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "MyraMon starting...");
    
    // Initialize WiFi manager
    esp_err_t err = wifi_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi manager: %s", esp_err_to_name(err));
        return;
    }
    
    // Start WiFi (STA or AP based on credentials)
    err = wifi_manager_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return;
    }
    
    ESP_LOGI(TAG, "MyraMon initialization complete");
    
    // WiFi runs independently via event handlers
    // Task will be deleted when initialization is complete
    vTaskDelete(NULL);
}
