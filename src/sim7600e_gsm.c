#include "sim7600e_gsm.h"
#include "sim7600e.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "SIM7600E_GSM";

// External function declarations (from main sim7600e.c)
extern QueueHandle_t sim7600e_get_urc_queue(void);
extern QueueHandle_t sim7600e_get_resp_queue(void);
extern int sim7600e_get_uart_port(void);
extern SemaphoreHandle_t sim7600e_get_mutex(void);

static esp_err_t send_at_command_internal(const char *cmd, char *response, size_t resp_size, uint32_t timeout_ms);

esp_err_t sim7600e_gsm_check_modem(void)
{
    ESP_LOGI(TAG, "Checking SIM7600E modem...");
    char response[128];
    
    esp_err_t ret = sim7600e_gsm_send_at_command("AT\r\n", response, sizeof(response), 5000);
    if (ret == ESP_OK) {
        if (strstr(response, "OK")) {
            ESP_LOGI(TAG, "SIM7600E is working fine!");
            return ESP_OK;
        } else {
            ESP_LOGW(TAG, "Unexpected response: %s", response);
            return ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Failed to send AT command");
        return ESP_FAIL;
    }
}

esp_err_t sim7600e_gsm_check_sim(void)
{
    ESP_LOGI(TAG, "Checking SIM card status...");
    char response[128];
    
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+CPIN?\r\n", response, sizeof(response), 5000);
    if (ret == ESP_OK) {
        if (strstr(response, "READY")) {
            ESP_LOGI(TAG, "SIM card is ready");
            return ESP_OK;
        } else if (strstr(response, "SIM PIN")) {
            ESP_LOGE(TAG, "SIM card requires PIN");
            return ESP_FAIL;
        } else {
            ESP_LOGW(TAG, "Unexpected SIM response: %s", response);
            return ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Failed to check SIM card status");
        return ESP_FAIL;
    }
}

esp_err_t sim7600e_gsm_turn_off_echo(void)
{
    ESP_LOGI(TAG, "Turning off echo mode...");
    char response[128];
    
    esp_err_t ret = sim7600e_gsm_send_at_command("ATE0\r\n", response, sizeof(response), 5000);
    if (ret == ESP_OK) {
        if (strstr(response, "OK")) {
            ESP_LOGI(TAG, "Echo mode disabled");
            return ESP_OK;
        } else {
            ESP_LOGW(TAG, "Unexpected response: %s", response);
            return ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Failed to turn off echo");
        return ESP_FAIL;
    }
}

esp_err_t sim7600e_gsm_wait_for_network(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Waiting for network registration...");
    
    uint32_t start_time = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        char response[128];
        
        // Check circuit-switched registration
        esp_err_t ret = sim7600e_gsm_send_at_command("AT+CREG?\r\n", response, sizeof(response), 5000);
        if (ret == ESP_OK) {
            if (strstr(response, "0,1")) {
                ESP_LOGI(TAG, "Registered to home network (CS Domain)");
                
                // Check packet-switched registration
                ret = sim7600e_gsm_send_at_command("AT+CEREG?\r\n", response, sizeof(response), 5000);
                if (ret == ESP_OK && (strstr(response, "0,1") || strstr(response, "0,5"))) {
                    ESP_LOGI(TAG, "Registered to packet network (PS Domain)");
                    return ESP_OK;
                }
            } else if (strstr(response, "0,5")) {
                ESP_LOGI(TAG, "Registered, roaming (CS Domain)");
                return ESP_OK;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // Check every 2 seconds
    }
    
    ESP_LOGE(TAG, "Network registration timeout");
    return ESP_ERR_TIMEOUT;
}

esp_err_t sim7600e_gsm_enable_internet(const char *apn)
{
    if (apn == NULL) {
        ESP_LOGE(TAG, "APN cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Enabling internet connection with APN: %s", apn);
    
    char command[128];
    char response[256];
    
    // Configure APN
    snprintf(command, sizeof(command), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", apn);
    esp_err_t ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), 10000);
    if (ret != ESP_OK || !strstr(response, "OK")) {
        ESP_LOGE(TAG, "Failed to configure APN");
        return ESP_FAIL;
    }
    
    // Activate PDP context
    ret = sim7600e_gsm_send_at_command("AT+CGACT=1,1\r\n", response, sizeof(response), 30000);
    if (ret != ESP_OK || !strstr(response, "OK")) {
        ESP_LOGE(TAG, "Failed to activate PDP context");
        return ESP_FAIL;
    }
    
    // Get IP address
    ret = sim7600e_gsm_send_at_command("AT+CGPADDR=1\r\n", response, sizeof(response), 10000);
    if (ret == ESP_OK && strstr(response, "+CGPADDR")) {
        ESP_LOGI(TAG, "Internet connection established: %s", response);
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Failed to get IP address");
    return ESP_FAIL;
}

esp_err_t sim7600e_gsm_get_network_info(sim7600e_network_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char response[256];
    
    // Get signal strength
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+CSQ\r\n", response, sizeof(response), 5000);
    if (ret == ESP_OK) {
        char *csq_start = strstr(response, "+CSQ:");
        if (csq_start) {
            int rssi, ber;
            if (sscanf(csq_start, "+CSQ: %d,%d", &rssi, &ber) == 2) {
                if (rssi != 99) {
                    info->signal_strength = -113 + (rssi * 2); // Convert to dBm
                } else {
                    info->signal_strength = -999; // Unknown
                }
            }
        }
    }
    
    // Get operator name
    ret = sim7600e_gsm_send_at_command("AT+COPS?\r\n", response, sizeof(response), 5000);
    if (ret == ESP_OK) {
        char *cops_start = strstr(response, "+COPS:");
        if (cops_start) {
            // Parse operator name (simplified parsing)
            char *quote_start = strchr(cops_start, '\"');
            if (quote_start) {
                quote_start++; // Skip opening quote
                char *quote_end = strchr(quote_start, '\"');
                if (quote_end) {
                    int len = quote_end - quote_start;
                    if (len < sizeof(info->operator_name)) {
                        strncpy(info->operator_name, quote_start, len);
                        info->operator_name[len] = '\0';
                    }
                }
            }
        }
    }
    
    // Get network time
    ret = sim7600e_gsm_send_at_command("AT+CCLK?\r\n", response, sizeof(response), 5000);
    if (ret == ESP_OK) {
        char *cclk_start = strstr(response, "+CCLK:");
        if (cclk_start) {
            char *quote_start = strchr(cclk_start, '\"');
            if (quote_start) {
                quote_start++; // Skip opening quote
                char *quote_end = strchr(quote_start, '\"');
                if (quote_end) {
                    int len = quote_end - quote_start;
                    if (len < sizeof(info->network_time)) {
                        strncpy(info->network_time, quote_start, len);
                        info->network_time[len] = '\0';
                    }
                }
            }
        }
    }
    
    return ESP_OK;
}

esp_err_t sim7600e_gsm_send_at_command(const char *cmd, char *response, size_t resp_size, uint32_t timeout_ms)
{
    if (cmd == NULL || response == NULL || resp_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return send_at_command_internal(cmd, response, resp_size, timeout_ms);
}

esp_err_t sim7600e_gsm_send_sms(const char *phone_number, const char *message)
{
    if (phone_number == NULL || message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char command[64];
    char response[128];
    
    // Set text mode
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+CMGF=1\r\n", response, sizeof(response), 5000);
    if (ret != ESP_OK || !strstr(response, "OK")) {
        ESP_LOGE(TAG, "Failed to set SMS text mode");
        return ESP_FAIL;
    }
    
    // Set phone number
    snprintf(command, sizeof(command), "AT+CMGS=\"%s\"\r\n", phone_number);
    ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), 5000);
    if (ret != ESP_OK || !strstr(response, ">")) {
        ESP_LOGE(TAG, "Failed to set SMS recipient");
        return ESP_FAIL;
    }
    
    // Send message with Ctrl+Z terminator
    char sms_data[256];
    snprintf(sms_data, sizeof(sms_data), "%s\x1A", message);
    ret = sim7600e_gsm_send_at_command(sms_data, response, sizeof(response), 30000);
    if (ret != ESP_OK || !strstr(response, "+CMGS")) {
        ESP_LOGE(TAG, "Failed to send SMS");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "SMS sent successfully");
    return ESP_OK;
}

esp_err_t sim7600e_gsm_make_call(const char *phone_number)
{
    if (phone_number == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char command[64];
    char response[128];
    
    snprintf(command, sizeof(command), "ATD%s;\r\n", phone_number);
    esp_err_t ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), 30000);
    
    if (ret == ESP_OK && strstr(response, "OK")) {
        ESP_LOGI(TAG, "Call initiated to %s", phone_number);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to make call");
        return ESP_FAIL;
    }
}

esp_err_t sim7600e_gsm_hang_up(void)
{
    char response[128];
    
    esp_err_t ret = sim7600e_gsm_send_at_command("ATH\r\n", response, sizeof(response), 5000);
    
    if (ret == ESP_OK && strstr(response, "OK")) {
        ESP_LOGI(TAG, "Call ended");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to end call");
        return ESP_FAIL;
    }
}

// Internal function implementation
static esp_err_t send_at_command_internal(const char *cmd, char *response, size_t resp_size, uint32_t timeout_ms)
{
    SemaphoreHandle_t mutex = sim7600e_get_mutex();
    QueueHandle_t resp_queue = sim7600e_get_resp_queue();
    int uart_port = sim7600e_get_uart_port();
    
    if (mutex == NULL || resp_queue == NULL) {
        ESP_LOGE(TAG, "SIM7600E not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Take mutex
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    // Clear response queue
    sim7600e_msg_t dummy_msg;
    while (xQueueReceive(resp_queue, &dummy_msg, 0) == pdTRUE) {
        // Clear queue
    }
    
    // Send command
    int written = uart_write_bytes(uart_port, cmd, strlen(cmd));
    if (written != strlen(cmd)) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Failed to send AT command");
        return ESP_FAIL;
    }
    
    // Wait for response
    sim7600e_msg_t resp_msg;
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    if (xQueueReceive(resp_queue, &resp_msg, timeout_ticks) == pdTRUE) {
        strncpy(response, resp_msg.data, resp_size - 1);
        response[resp_size - 1] = '\0';
        xSemaphoreGive(mutex);
        return ESP_OK;
    } else {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "AT command timeout");
        return ESP_ERR_TIMEOUT;
    }
}