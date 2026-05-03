/**
 * @file dns_server.c
 * @brief Minimal DNS responder for captive portal
 * 
 * Responds to ANY DNS query with A record: 192.168.4.1
 * Uses raw lwIP UDP socket on port 53
 */

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "dns_server.h"

static const char *TAG = "dns_server";

// DNS server configuration
#define DNS_PORT            53
#define DNS_TTL             60
#define DNS_TASK_PRIORITY   1
#define DNS_TASK_STACK_SIZE 512
#define DNS_MAX_PACKET_SIZE 256

// DNS response IP: 192.168.4.1
static const uint8_t DNS_RESPONSE_IP[4] = {192, 168, 4, 1};

// Task handle
static TaskHandle_t s_dns_task_handle = NULL;
static int s_dns_socket = -1;
static bool s_running = false;

// DNS header structure (12 bytes)
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

/**
 * @brief Parse DNS query and generate response
 * @param request Request packet
 * @param req_len Request length
 * @param response Response buffer
 * @return Response length
 */
static int build_dns_response(const uint8_t *request, size_t req_len, 
                               uint8_t *response, size_t resp_size)
{
    if (req_len < sizeof(dns_header_t)) {
        return 0;
    }

    const dns_header_t *req_hdr = (const dns_header_t *)request;
    
    // Copy header
    memcpy(response, request, sizeof(dns_header_t));
    
    // Modify flags: QR=1 (response), AA=1 (authoritative), RCODE=0 (no error)
    dns_header_t *resp_hdr = (dns_header_t *)response;
    resp_hdr->flags = htons(0x8180);  // Response, no error
    resp_hdr->ancount = htons(1);     // One answer
    resp_hdr->qdcount = req_hdr->qdcount;  // Same questions
    
    // Copy question section
    size_t offset = sizeof(dns_header_t);
    size_t qname_len = 0;
    
    // Find end of question name (null terminator)
    while (offset + qname_len < req_len && request[offset + qname_len] != 0) {
        qname_len++;
    }
    qname_len++;  // Include null terminator
    
    // Copy question name
    memcpy(response + offset, request + offset, qname_len);
    offset += qname_len;
    
    // Copy question type and class (4 bytes)
    if (offset + 4 <= req_len) {
        memcpy(response + offset, request + offset, 4);
        offset += 4;
    }
    
    // Build answer section
    if (offset + 16 <= resp_size) {
        // Name pointer to question (0xC00C = pointer to offset 12)
        response[offset++] = 0xC0;
        response[offset++] = 0x0C;
        
        // Type: A (1)
        response[offset++] = 0x00;
        response[offset++] = 0x01;
        
        // Class: IN (1)
        response[offset++] = 0x00;
        response[offset++] = 0x01;
        
        // TTL: 60 seconds
        response[offset++] = 0x00;
        response[offset++] = 0x00;
        response[offset++] = 0x00;
        response[offset++] = DNS_TTL;
        
        // Data length: 4 (IPv4)
        response[offset++] = 0x00;
        response[offset++] = 0x04;
        
        // IP address: 192.168.4.1
        memcpy(response + offset, DNS_RESPONSE_IP, 4);
        offset += 4;
    }
    
    return offset;
}

/**
 * @brief DNS server task
 */
static void dns_server_task(void *pvParameters)
{
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(DNS_PORT)
    };
    
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    uint8_t recv_buf[DNS_MAX_PACKET_SIZE];
    uint8_t send_buf[DNS_MAX_PACKET_SIZE];
    
    ESP_LOGI(TAG, "DNS server starting on port %d", DNS_PORT);
    
    // Create UDP socket
    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", errno);
        s_running = false;
        vTaskDelete(NULL);
        return;
    }
    
    // Bind to port 53
    if (bind(s_dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: %d", errno);
        close(s_dns_socket);
        s_dns_socket = -1;
        s_running = false;
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "DNS server listening");
    s_running = true;
    
    while (s_running) {
        // Receive DNS query
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(s_dns_socket, &readfds);
        
        struct timeval timeout = {1, 0};  // 1 second timeout
        int ready = select(s_dns_socket + 1, &readfds, NULL, NULL, &timeout);
        
        if (ready > 0 && FD_ISSET(s_dns_socket, &readfds)) {
            memset(recv_buf, 0, sizeof(recv_buf));
            int recv_len = recvfrom(s_dns_socket, recv_buf, sizeof(recv_buf), 0,
                                    (struct sockaddr *)&client_addr, &client_addr_len);
            
            if (recv_len > 0) {
                ESP_LOGD(TAG, "DNS query received (%d bytes)", recv_len);
                
                // Build response
                int resp_len = build_dns_response(recv_buf, recv_len, 
                                                   send_buf, sizeof(send_buf));
                
                if (resp_len > 0) {
                    // Send response
                    sendto(s_dns_socket, send_buf, resp_len, 0,
                           (struct sockaddr *)&client_addr, client_addr_len);
                    ESP_LOGD(TAG, "DNS response sent (%d bytes)", resp_len);
                }
            }
        }
    }
    
    // Cleanup
    if (s_dns_socket >= 0) {
        close(s_dns_socket);
        s_dns_socket = -1;
    }
    
    ESP_LOGI(TAG, "DNS server stopped");
    vTaskDelete(NULL);
}

esp_err_t dns_server_init(void)
{
    ESP_LOGI(TAG, "DNS server initialized");
    return ESP_OK;
}

esp_err_t dns_server_start(void)
{
    if (s_dns_task_handle != NULL) {
        ESP_LOGW(TAG, "DNS server already running");
        return ESP_OK;
    }
    
    xTaskCreate(dns_server_task, "dns_server", DNS_TASK_STACK_SIZE, 
                NULL, DNS_TASK_PRIORITY, &s_dns_task_handle);
    
    if (s_dns_task_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create DNS server task");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "DNS server task started");
    return ESP_OK;
}

esp_err_t dns_server_stop(void)
{
    s_running = false;
    
    // Wait for task to exit
    if (s_dns_task_handle != NULL) {
        // Give it time to clean up
        vTaskDelay(pdMS_TO_TICKS(100));
        s_dns_task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "DNS server stopped");
    return ESP_OK;
}
