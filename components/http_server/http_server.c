/**
 * @file http_server.c
 * @brief HTTP server for captive portal
 * 
 * Provides:
 * - Welcome page (GET /)
 * - Catch-all redirect
 * - API endpoints (temperature, scan, config, status)
 */

#include <string.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "driver/temperature_sensor.h"
#include "esp_wifi.h"
#include "wifi_manager.h"
#include "http_server.h"

static const char *TAG = "http_server";

// Server configuration
#define HTTP_PORT           80
#define HTTP_MAX_CONNECTIONS 6

// Server handle
static httpd_handle_t s_server = NULL;

// Temperature sensor
static temperature_sensor_handle_t s_temp_handle = NULL;
static bool s_temp_sensor_enabled = false;

/**
 * @brief Read temperature sensor
 * @return Temperature in Celsius, or -999.0 on error
 */
static float read_temperature(void)
{
    if (!s_temp_sensor_enabled) {
        if (s_temp_handle == NULL) {
            // Install with range -10 to 80°C (most accurate)
            temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
            esp_err_t err = temperature_sensor_install(&temp_config, &s_temp_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to install temp sensor: %s", esp_err_to_name(err));
                return -999.0f;
            }
        }
        
        esp_err_t err = temperature_sensor_enable(s_temp_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable temp sensor: %s", esp_err_to_name(err));
            return -999.0f;
        }
        
        s_temp_sensor_enabled = true;
        ESP_LOGI(TAG, "Temperature sensor enabled");
    }
    
    float temp;
    esp_err_t err = temperature_sensor_get_celsius(s_temp_handle, &temp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature: %s", esp_err_to_name(err));
        return -999.0f;
    }
    
    return temp;
}

/**
 * @brief Welcome page HTML
 */
static const char WELCOME_PAGE[] = 
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"    <title>MyraMon</title>\n"
"    <style>\n"
"        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #fff; }\n"
"        h1 { color: #0066cc; border-bottom: 2px solid #0066cc; padding-bottom: 10px; }\n"
"        .status { padding: 15px; margin: 10px 0; border-radius: 5px; }\n"
"        .success { background: #d4edda; color: #155724; }\n"
"        .error { background: #f8d7da; color: #721c24; }\n"
"        .info { background: #d1ecf1; color: #0c5460; }\n"
"        .temp { font-size: 24px; font-weight: bold; color: #0066cc; }\n"
"        form { margin: 20px 0; }\n"
"        label { display: block; margin: 10px 0 5px; font-weight: bold; }\n"
"        select, input[type=\"password\"] { width: 100%; padding: 10px; margin: 5px 0; box-sizing: border-box; }\n"
"        button { background: #0066cc; color: white; padding: 12px 24px; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; }\n"
"        button:hover { background: #0052a3; }\n"
"        #password-container { position: relative; }\n"
"        #toggle-password { position: absolute; right: 10px; top: 38px; cursor: pointer; color: #666; }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <h1>MyraMon</h1>\n"
"    \n"
"    <div id=\"status\" class=\"status info\">Loading status...</div>\n"
"    \n"
"    <div class=\"temp\">Internal Temp: <span id=\"temperature\">--.-</span> &deg;C</div>\n"
"    \n"
"    <div id=\"config-section\" style=\"display:none;\">\n"
"        <h2>WiFi Configuration</h2>\n"
"        <form id=\"wifi-form\">\n"
"            <label for=\"ssid\">Select Network:</label>\n"
"            <select id=\"ssid\" name=\"ssid\" required>\n"
"                <option value=\"\">Scanning...</option>\n"
"            </select>\n"
"            <button type=\"button\" onclick=\"scanNetworks()\">Scan Again</button>\n"
"            \n"
"            <label for=\"password\">Password:</label>\n"
"            <div id=\"password-container\">\n"
"                <input type=\"password\" id=\"password\" name=\"password\" required>\n"
"                <span id=\"toggle-password\" onclick=\"togglePassword()\">Show</span>\n"
"            </div>\n"
"            \n"
"            <button type=\"submit\">Save & Connect</button>\n"
"        </form>\n"
"    </div>\n"
"    \n"
"    <script>\n"
"        // Temperature polling (every 10 seconds)\n"
"        function updateTemperature() {\n"
"            fetch('/api/temp')\n"
"                .then(r => r.json())\n"
"                .then(d => {\n"
"                    document.getElementById('temperature').textContent = d.temp.toFixed(1);\n"
"                })\n"
"                .catch(e => {\n"
"                    document.getElementById('temperature').textContent = 'N/A';\n"
"                });\n"
"        }\n"
"        \n"
"        // Track if we've already scanned\n"
"        let hasScanned = false;\n"
"        \n"
"        // Status polling (every 5 seconds)\n"
"        function updateStatus() {\n"
"            fetch('/api/status')\n"
"                .then(r => r.json())\n"
"                .then(d => {\n"
"                    const statusEl = document.getElementById('status');\n"
"                    const configSection = document.getElementById('config-section');\n"
"                    \n"
"                    if (d.state === 'ap_active') {\n"
"                        statusEl.className = 'status info';\n"
"                        statusEl.textContent = 'Setting up AP...';\n"
"                        configSection.style.display = 'block';\n"
"                        // Only scan once on first status update\n"
"                        if (!hasScanned) {\n"
"                            hasScanned = true;\n"
"                            scanNetworks();\n"
"                        }\n"
"                    } else if (d.state === 'sta_connected') {\n"
"                        statusEl.className = 'status success';\n"
"                        statusEl.textContent = 'Connected to ' + d.ssid + ' (' + d.ip + ')';\n"
"                        configSection.style.display = 'none';\n"
"                    } else if (d.state === 'sta_connecting') {\n"
"                        statusEl.className = 'status info';\n"
"                        statusEl.textContent = 'Connecting to ' + d.ssid + '...';\n"
"                        configSection.style.display = 'none';\n"
"                    } else {\n"
"                        statusEl.className = 'status info';\n"
"                        statusEl.textContent = 'Status: ' + d.state;\n"
"                    }\n"
"                })\n"
"                .catch(e => {\n"
"                    console.error('Status error:', e);\n"
"                });\n"
"        }\n"
"        \n"
"        // WiFi scan\n"
"        function scanNetworks() {\n"
"            const select = document.getElementById('ssid');\n"
"            select.innerHTML = '<option value=\"\">Scanning...</option>';\n"
"            \n"
"            fetch('/api/scan')\n"
"                .then(r => r.json())\n"
"                .then(d => {\n"
"                    select.innerHTML = '';\n"
"                    if (d.ssids.length === 0) {\n"
"                        select.innerHTML = '<option value=\"\">No networks found</option>';\n"
"                    } else {\n"
"                        d.ssids.forEach(net => {\n"
"                            const opt = document.createElement('option');\n"
"                            opt.value = net.ssid;\n"
"                            opt.textContent = net.ssid + ' (' + net.rssi + ' dBm)';\n"
"                            select.appendChild(opt);\n"
"                        });\n"
"                    }\n"
"                })\n"
"                .catch(e => {\n"
"                    select.innerHTML = '<option value=\"\">Scan failed</option>';\n"
"                });\n"
"        }\n"
"        \n"
"        // Toggle password visibility\n"
"        function togglePassword() {\n"
"            const input = document.getElementById('password');\n"
"            const toggle = document.getElementById('toggle-password');\n"
"            if (input.type === 'password') {\n"
"                input.type = 'text';\n"
"                toggle.textContent = 'Hide';\n"
"            } else {\n"
"                input.type = 'password';\n"
"                toggle.textContent = 'Show';\n"
"            }\n"
"        }\n"
"        \n"
"        // Form submission\n"
"        document.getElementById('wifi-form').addEventListener('submit', function(e) {\n"
"            e.preventDefault();\n"
"            \n"
"            const ssid = document.getElementById('ssid').value;\n"
"            const password = document.getElementById('password').value;\n"
"            \n"
"            if (!ssid) {\n"
"                alert('Please select a network');\n"
"                return;\n"
"            }\n"
"            \n"
"            fetch('/api/config', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({ssid: ssid, password: password})\n"
"            })\n"
"            .then(r => r.json())\n"
"            .then(d => {\n"
"                if (d.status === 'ok') {\n"
"                    const statusEl = document.getElementById('status');\n"
"                    statusEl.className = 'status success';\n"
"                    statusEl.textContent = 'Credentials saved! Connecting...';\n"
"                    document.getElementById('wifi-form').style.display = 'none';\n"
"                } else {\n"
"                    alert('Error: ' + d.message);\n"
"                }\n"
"            })\n"
"            .catch(e => {\n"
"                alert('Error saving credentials');\n"
"            });\n"
"        });\n"
"        \n"
"        // Start polling\n"
"        updateTemperature();\n"
"        updateStatus();\n"
"        setInterval(updateTemperature, 10000);  // 10 seconds\n"
"        setInterval(updateStatus, 5000);        // 5 seconds\n"
"    </script>\n"
"</body>\n"
"</html>\n";

/**
 * @brief GET / handler - Welcome page
 */
static esp_err_t welcome_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET /");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, WELCOME_PAGE, strlen(WELCOME_PAGE));
    return ESP_OK;
}

/**
 * @brief Catch-all handler - Redirect to /
 */
static esp_err_t redirect_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "Redirect %s -> /", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// URI handlers
static const httpd_uri_t uri_welcome = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = welcome_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_redirect = {
    .uri      = "/*",
    .method   = HTTP_GET,
    .handler  = redirect_handler,
    .user_ctx = NULL
};

/**
 * @brief GET /api/temp handler - Temperature reading
 */
static esp_err_t temp_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET /api/temp");
    
    float temp = read_temperature();
    
    char json[64];
    if (temp > -900) {
        snprintf(json, sizeof(json), "{\"temp\": %.1f}", temp);
    } else {
        snprintf(json, sizeof(json), "{\"temp\": null}");
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static const httpd_uri_t uri_temp = {
    .uri      = "/api/temp",
    .method   = HTTP_GET,
    .handler  = temp_handler,
    .user_ctx = NULL
};

/**
 * @brief GET /api/scan handler - WiFi scan
 */
static esp_err_t scan_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET /api/scan");
    
    // Start scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };
    
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);  // Blocking scan
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
        // Return empty array on scan failure
        const char *json = "{\"ssids\": []}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    // Get results
    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK || ap_count == 0) {
        const char *json = "{\"ssids\": []}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    wifi_ap_record_t *ap_records = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (ap_records == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\": \"no memory\"}", 24);
        return ESP_OK;
    }
    
    err = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (err != ESP_OK) {
        free(ap_records);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ssids\": []}", 13);
        return ESP_OK;
    }
    
    // Build JSON response
    char json[2048] = "{\"ssids\": [";
    size_t offset = strlen(json);
    
    for (int i = 0; i < ap_count && offset < sizeof(json) - 50; i++) {
        char entry[100];
        snprintf(entry, sizeof(entry), 
                 "{\"ssid\": \"%s\", \"rssi\": %d}%s",
                 ap_records[i].ssid,
                 ap_records[i].rssi,
                 (i < ap_count - 1) ? "," : "");
        offset += snprintf(json + offset, sizeof(json) - offset, "%s", entry);
    }
    
    strcat(json, "]}");
    free(ap_records);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static const httpd_uri_t uri_scan = {
    .uri      = "/api/scan",
    .method   = HTTP_GET,
    .handler  = scan_handler,
    .user_ctx = NULL
};

/**
 * @brief POST /api/config handler - Save WiFi credentials
 */
static esp_err_t config_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "POST /api/config");
    
    // Read request body
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"status\": \"error\", \"message\": \"empty request\"}", 48);
        return ESP_OK;
    }
    buf[ret] = '\0';
    
    // Simple JSON parsing (find ssid and password)
    char *ssid_start = strstr(buf, "\"ssid\"");
    char *pass_start = strstr(buf, "\"password\"");
    
    if (!ssid_start || !pass_start) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"status\": \"error\", \"message\": \"invalid json\"}", 49);
        return ESP_OK;
    }
    
    // Extract values (simple parsing)
    char ssid[32] = {0};
    char password[64] = {0};
    
    char *ssid_val = strchr(ssid_start + 7, ':');
    if (ssid_val) {
        ssid_val = strchr(ssid_val, '"');
        if (ssid_val) {
            ssid_val++;
            char *ssid_end = strchr(ssid_val, '"');
            if (ssid_end) {
                size_t len = ssid_end - ssid_val;
                if (len < sizeof(ssid)) {
                    strncpy(ssid, ssid_val, len);
                }
            }
        }
    }
    
    char *pass_val = strchr(pass_start + 10, ':');
    if (pass_val) {
        pass_val = strchr(pass_val, '"');
        if (pass_val) {
            pass_val++;
            char *pass_end = strchr(pass_val, '"');
            if (pass_end) {
                size_t len = pass_end - pass_val;
                if (len < sizeof(password)) {
                    strncpy(password, pass_val, len);
                }
            }
        }
    }
    
    // Validate
    if (strlen(ssid) == 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"status\": \"error\", \"message\": \"empty ssid\"}", 47);
        return ESP_OK;
    }
    
    // Save credentials
    esp_err_t err = wifi_creds_save(ssid, password);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"status\": \"error\", \"message\": \"save failed\"}", 48);
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Credentials saved: SSID=%s", ssid);
    
    // TODO: Transition to STA_CONNECTING state
    // For now, just return success
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\": \"ok\"}", 16);
    return ESP_OK;
}

static const httpd_uri_t uri_config = {
    .uri      = "/api/config",
    .method   = HTTP_POST,
    .handler  = config_handler,
    .user_ctx = NULL
};

/**
 * @brief GET /api/status handler - Connection status
 */
static esp_err_t status_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET /api/status");
    
    wifi_state_t state = wifi_manager_get_state();
    const char *state_str;
    char ssid[32] = "";
    char ip[16] = "";
    float temp = read_temperature();
    
    switch (state) {
        case WIFI_STATE_INIT:
            state_str = "init";
            break;
        case WIFI_STATE_STA_CONNECTING:
            state_str = "sta_connecting";
            // Try to get SSID from credentials
            if (wifi_creds_is_configured()) {
                char pass[64];
                wifi_creds_load(ssid, sizeof(ssid), pass, sizeof(pass));
            }
            break;
        case WIFI_STATE_STA_CONNECTED:
            state_str = "sta_connected";
            if (wifi_creds_is_configured()) {
                char pass[64];
                wifi_creds_load(ssid, sizeof(ssid), pass, sizeof(pass));
            }
            // TODO: Get actual IP address
            strcpy(ip, "192.168.x.x");
            break;
        case WIFI_STATE_AP_ACTIVE:
            state_str = "ap_active";
            strcpy(ssid, "MyraMon_XX");
            strcpy(ip, "192.168.4.1");
            break;
        case WIFI_STATE_SWITCHING:
            state_str = "switching";
            break;
        default:
            state_str = "unknown";
    }
    
    char json[256];
    if (temp > -900) {
        snprintf(json, sizeof(json), 
                 "{\"state\": \"%s\", \"ssid\": \"%s\", \"ip\": \"%s\", \"temp\": %.1f}",
                 state_str, ssid, ip, temp);
    } else {
        snprintf(json, sizeof(json), 
                 "{\"state\": \"%s\", \"ssid\": \"%s\", \"ip\": \"%s\", \"temp\": null}",
                 state_str, ssid, ip);
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static const httpd_uri_t uri_status = {
    .uri      = "/api/status",
    .method   = HTTP_GET,
    .handler  = status_handler,
    .user_ctx = NULL
};

/**
 * @brief Redirect handler for captive portal detection
 */
static esp_err_t cp_redirect_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "Captive portal detection: %s", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief 204 No Content handler for captive portal detection
 */
static esp_err_t cp_204_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "Captive portal 204: %s", req->uri);
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Captive portal detection URLs
static const httpd_uri_t uri_library_test = {
    .uri      = "/library/test/success.html",
    .method   = HTTP_GET,
    .handler  = cp_redirect_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_hotspot_detect = {
    .uri      = "/hotspot-detect.html",
    .method   = HTTP_GET,
    .handler  = cp_redirect_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_generate_204 = {
    .uri      = "/generate_204",
    .method   = HTTP_GET,
    .handler  = cp_204_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_gen_204 = {
    .uri      = "/gen_204",
    .method   = HTTP_GET,
    .handler  = cp_204_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_connecttest = {
    .uri      = "/connecttest.txt",
    .method   = HTTP_GET,
    .handler  = cp_redirect_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_ncsi = {
    .uri      = "/ncsi.txt",
    .method   = HTTP_GET,
    .handler  = cp_redirect_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_canonical = {
    .uri      = "/canonical.html",
    .method   = HTTP_GET,
    .handler  = cp_redirect_handler,
    .user_ctx = NULL
};

esp_err_t http_server_init(void)
{
    ESP_LOGI(TAG, "HTTP server initialized");
    return ESP_OK;
}

esp_err_t http_server_start(void)
{
    if (s_server != NULL) {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;  // Need space for all captive portal URLs
    config.max_open_sockets = HTTP_MAX_CONNECTIONS;
    config.server_port = HTTP_PORT;
    
    ESP_LOGI(TAG, "Starting HTTP server on port %d", HTTP_PORT);
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }
    
    // Register handlers
    httpd_register_uri_handler(s_server, &uri_welcome);
    httpd_register_uri_handler(s_server, &uri_redirect);
    httpd_register_uri_handler(s_server, &uri_temp);
    httpd_register_uri_handler(s_server, &uri_scan);
    httpd_register_uri_handler(s_server, &uri_config);
    httpd_register_uri_handler(s_server, &uri_status);
    
    // Captive portal detection URLs
    httpd_register_uri_handler(s_server, &uri_library_test);
    httpd_register_uri_handler(s_server, &uri_hotspot_detect);
    httpd_register_uri_handler(s_server, &uri_generate_204);
    httpd_register_uri_handler(s_server, &uri_gen_204);
    httpd_register_uri_handler(s_server, &uri_connecttest);
    httpd_register_uri_handler(s_server, &uri_ncsi);
    httpd_register_uri_handler(s_server, &uri_canonical);
    
    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping HTTP server");
    httpd_stop(s_server);
    s_server = NULL;
    
    return ESP_OK;
}
