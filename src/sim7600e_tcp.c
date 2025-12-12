/**
 * @file sim7600e_tcp.c
 * @brief TCP Communication Functions for SIM7600E
 * 
 * This file implements TCP/IP connectivity features including:
 * - TCP client connections
 * - Data transmission and reception
 * - Connection status management
 * - Callback handling for network events
 * 
 * @author ESP32 SIM7600E Component
 */

#include "sim7600e_tcp.h"
#include "sim7600e.h"
#include "sim7600e_gsm.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "SIM7600E_TCP";

// Static variables for TCP functionality
static sim7600e_tcp_status_t s_tcp_status = SIM7600E_TCP_DISCONNECTED;
static sim7600e_tcp_config_t s_tcp_config;
static sim7600e_tcp_recv_cb_t s_recv_callback = NULL;
static sim7600e_tcp_status_cb_t s_status_callback = NULL;
static void *s_recv_user_data = NULL;
static void *s_status_user_data = NULL;

// External function declarations
extern int sim7600e_get_uart_port(void);
extern QueueHandle_t sim7600e_get_resp_queue(void);
extern SemaphoreHandle_t sim7600e_get_mutex(void);

// Internal function declarations
static esp_err_t tcp_send_data_internal(const uint8_t *data, size_t len);
static void set_tcp_status(sim7600e_tcp_status_t new_status);

sim7600e_tcp_config_t sim7600e_tcp_get_default_config(void)
{
    sim7600e_tcp_config_t config = {
        .host = "",
        .port = 80,
        .timeout_ms = 30000,
        .keep_alive = false,
        .keep_alive_idle = 7200,
        .keep_alive_interval = 30,
        .keep_alive_count = 9
    };
    return config;
}

esp_err_t sim7600e_tcp_connect(const sim7600e_tcp_config_t *config)
{
    if (config == NULL || strlen(config->host) == 0 || config->port == 0) {
        ESP_LOGE(TAG, "Invalid TCP configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_tcp_status == SIM7600E_TCP_CONNECTED) {
        ESP_LOGW(TAG, "TCP already connected");
        return ESP_OK;
    }
    
    // Copy configuration
    memcpy(&s_tcp_config, config, sizeof(sim7600e_tcp_config_t));
    
    ESP_LOGI(TAG, "Connecting to %s:%d", config->host, config->port);
    set_tcp_status(SIM7600E_TCP_CONNECTING);
    
    char command[256];
    char response[512];
    
    // Check network interface status first
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+NETOPEN?\r\n", response, sizeof(response), 5000);
    if (ret == ESP_OK) {
        if (strstr(response, "+NETOPEN: 0")) {
            ESP_LOGW(TAG, "Network interface not open, attempting to open");
            ret = sim7600e_gsm_send_at_command("AT+NETOPEN\r\n", response, sizeof(response), 10000);
            if (ret != ESP_OK || (!strstr(response, "+NETOPEN: 1") && !strstr(response, "already opened"))) {
                ESP_LOGE(TAG, "Failed to open network interface: %s", response);
                set_tcp_status(SIM7600E_TCP_ERROR);
                return ESP_FAIL;
            }
        }
    }
    
    // Try new NETOPEN API first
    snprintf(command, sizeof(command), "AT+CIPOPEN=0,\"TCP\",\"%s\",%d\r\n", 
             config->host, config->port);
    
    ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), config->timeout_ms);
    
    // Check for success with new API responses
    if (ret == ESP_OK) {
        if (strstr(response, "+CIPOPEN: 0,0") || strstr(response, "CONNECT OK")) {
            set_tcp_status(SIM7600E_TCP_CONNECTED);
            ESP_LOGI(TAG, "TCP connection established");
            return ESP_OK;
        } 
        // Check if we got a specific error code
        else if (strstr(response, "+CIPOPEN: 0,")) {
            char *error_start = strstr(response, "+CIPOPEN: 0,");
            if (error_start) {
                int error_code = 0;
                char temp_ip[32] = {0};
                int temp_port = 0;
                // Try to parse: +CIPOPEN: 0,"TCP","IP",port,error_code
                if (sscanf(error_start, "+CIPOPEN: 0,\"TCP\",\"%31[^\"]\",%d,%d", temp_ip, &temp_port, &error_code) >= 3 && error_code < 0) {
                    ESP_LOGW(TAG, "CIPOPEN failed with error code %d for %s:%d", error_code, temp_ip, temp_port);
                } else {
                    ESP_LOGW(TAG, "CIPOPEN response unclear: %s", response);
                }
            }
        }
        else if (strstr(response, "OK") && !strstr(response, "+CIPOPEN:")) {
            // For new API, just "OK" might mean success - check connection status
            vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for connection to establish
            
            ret = sim7600e_gsm_send_at_command("AT+CIPOPEN?\r\n", response, sizeof(response), 5000);
            if (ret == ESP_OK && strstr(response, "+CIPOPEN: 0,0")) {
                set_tcp_status(SIM7600E_TCP_CONNECTED);
                ESP_LOGI(TAG, "TCP connection established (verified)");
                return ESP_OK;
            }
        }
    }
    
    // If new API failed, try legacy CIPSTART
    ESP_LOGW(TAG, "CIPOPEN failed, trying CIPSTART: %s", response);
    
    // First ensure we're in non-transparent mode for legacy API
    ret = sim7600e_gsm_send_at_command("AT+CIPMODE=0\r\n", response, sizeof(response), 5000);
    vTaskDelay(pdMS_TO_TICKS(500)); // Small delay after mode change
    
    snprintf(command, sizeof(command), "AT+CIPSTART=0,\"TCP\",\"%s\",%d\r\n", 
             config->host, config->port);
    
    ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), config->timeout_ms);
    
    if (ret == ESP_OK && (strstr(response, "CONNECT OK") || strstr(response, "ALREADY CONNECT"))) {
        set_tcp_status(SIM7600E_TCP_CONNECTED);
        ESP_LOGI(TAG, "TCP connection established (legacy)");
        return ESP_OK;
    } else {
        set_tcp_status(SIM7600E_TCP_ERROR);
        ESP_LOGE(TAG, "Failed to establish TCP connection: %s", response);
        return ESP_FAIL;
    }
}

esp_err_t sim7600e_tcp_disconnect(void)
{
    if (s_tcp_status != SIM7600E_TCP_CONNECTED) {
        ESP_LOGW(TAG, "TCP not connected");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Disconnecting TCP connection");
    set_tcp_status(SIM7600E_TCP_DISCONNECTING);
    
    char response[128];
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+CIPCLOSE=0\r\n", response, sizeof(response), 10000);
    
    if (ret == ESP_OK && (strstr(response, "CLOSE OK") || strstr(response, "+CIPCLOSE: 0,0"))) {
        set_tcp_status(SIM7600E_TCP_DISCONNECTED);
        ESP_LOGI(TAG, "TCP connection closed");
        return ESP_OK;
    } else {
        set_tcp_status(SIM7600E_TCP_ERROR);
        ESP_LOGE(TAG, "Failed to close TCP connection");
        return ESP_FAIL;
    }
}

esp_err_t sim7600e_tcp_send(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_tcp_status != SIM7600E_TCP_CONNECTED) {
        ESP_LOGE(TAG, "TCP not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG, "Sending %d bytes over TCP", len);
    
    char command[64];
    char response[128];
    
    // Start send operation
    snprintf(command, sizeof(command), "AT+CIPSEND=0,%d\r\n", len);
    esp_err_t ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), timeout_ms);
    
    if (ret != ESP_OK || !strstr(response, ">")) {
        ESP_LOGE(TAG, "Failed to initiate TCP send");
        return ESP_FAIL;
    }
    
    // Send actual data
    ret = tcp_send_data_internal(data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send TCP data");
        return ESP_FAIL;
    }
    
    // Wait for send confirmation
    ret = sim7600e_gsm_send_at_command("", response, sizeof(response), timeout_ms);
    if (ret == ESP_OK && strstr(response, "DATA ACCEPT")) {
        ESP_LOGI(TAG, "TCP data sent successfully");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "TCP send failed: %s", response);
        return ESP_FAIL;
    }
}

esp_err_t sim7600e_tcp_send_string(const char *str, uint32_t timeout_ms)
{
    if (str == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return sim7600e_tcp_send((const uint8_t*)str, strlen(str), timeout_ms);
}

esp_err_t sim7600e_tcp_receive(uint8_t *data, size_t len, size_t *received, uint32_t timeout_ms)
{
    if (data == NULL || len == 0 || received == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_tcp_status != SIM7600E_TCP_CONNECTED) {
        ESP_LOGE(TAG, "TCP not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    // This is a simplified implementation
    // In a real implementation, you would need to parse +RECEIVE: URC messages
    // For now, return that no data was received
    *received = 0;
    ESP_LOGW(TAG, "TCP receive not fully implemented");
    
    return ESP_ERR_TIMEOUT;
}

sim7600e_tcp_status_t sim7600e_tcp_get_status(void)
{
    return s_tcp_status;
}

esp_err_t sim7600e_tcp_register_recv_callback(sim7600e_tcp_recv_cb_t callback, void *user_data)
{
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_recv_callback = callback;
    s_recv_user_data = user_data;
    
    return ESP_OK;
}

esp_err_t sim7600e_tcp_register_status_callback(sim7600e_tcp_status_cb_t callback, void *user_data)
{
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_status_callback = callback;
    s_status_user_data = user_data;
    
    return ESP_OK;
}

esp_err_t sim7600e_tcp_unregister_recv_callback(void)
{
    s_recv_callback = NULL;
    s_recv_user_data = NULL;
    
    return ESP_OK;
}

esp_err_t sim7600e_tcp_unregister_status_callback(void)
{
    s_status_callback = NULL;
    s_status_user_data = NULL;
    
    return ESP_OK;
}

// Internal function implementations

static esp_err_t tcp_send_data_internal(const uint8_t *data, size_t len)
{
    int uart_port = sim7600e_get_uart_port();
    
    int written = uart_write_bytes(uart_port, data, len);
    if (written != len) {
        ESP_LOGE(TAG, "Failed to write all data to UART");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

static void set_tcp_status(sim7600e_tcp_status_t new_status)
{
    if (s_tcp_status != new_status) {
        s_tcp_status = new_status;
        
        // Call status callback if registered
        if (s_status_callback != NULL) {
            s_status_callback(new_status, s_status_user_data);
        }
    }
}