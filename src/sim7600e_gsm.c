/**
 * @file sim7600e_gsm.c
 * @brief GSM/4G Communication Functions for SIM7600E
 * 
 * This file implements GSM/4G cellular connectivity features including:
 * - Network registration and status monitoring
 * - SMS sending and receiving
 * - Internet connection management
 * - AT command interface
 * 
 * @author ESP32 SIM7600E Component
 */

#include "sim7600e_gsm.h"
#include "sim7600e.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include "driver/uart.h"

static const char *TAG = "SIM7600E_GSM";

// External function declarations (from main sim7600e.c)
extern QueueHandle_t sim7600e_get_urc_queue(void);
extern QueueHandle_t sim7600e_get_resp_queue(void);
extern int sim7600e_get_uart_port(void);
extern SemaphoreHandle_t sim7600e_get_mutex(void);

static esp_err_t send_at_command_internal(const char *cmd, char *response, size_t resp_size, uint32_t timeout_ms);

esp_err_t sim7600e_gsm_check_modem(void)
{
    char response[128];
    esp_err_t ret;
    
    // Basic modem check
    ret = sim7600e_gsm_send_at_command("AT\r\n", response, sizeof(response), 5000);
    if (ret != ESP_OK || !strstr(response, "OK")) {
        ESP_LOGE(TAG, "Modem not responding");
        return ESP_FAIL;
    }
    
    // Configure verbose responses
    sim7600e_gsm_send_at_command("ATV1\r\n", response, sizeof(response), 5000);
    sim7600e_gsm_send_at_command("AT+CMEE=2\r\n", response, sizeof(response), 5000);
    sim7600e_gsm_send_at_command("AT+CREG=2\r\n", response, sizeof(response), 5000);
    sim7600e_gsm_send_at_command("ATE0\r\n", response, sizeof(response), 5000);
    
    // Enable automatic time zone and time update from network (NITZ)
    sim7600e_gsm_send_at_command("AT+CTZU=1\r\n", response, sizeof(response), 5000);
    ESP_LOGI(TAG, "Automatic network time update enabled");
    
    ESP_LOGI(TAG, "Modem initialized successfully");
    return ESP_OK;
}

esp_err_t sim7600e_gsm_check_sim(void)
{
    char response[128];
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+CPIN?\r\n", response, sizeof(response), 5000);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check SIM status");
        return ESP_FAIL;
    }
    
    if (strstr(response, "+CPIN: READY") || strstr(response, "READY")) {
        ESP_LOGI(TAG, "SIM card ready");
        return ESP_OK;
    }
    
    if (strstr(response, "+CPIN: SIM PIN")) {
        ESP_LOGE(TAG, "SIM requires PIN");
        return ESP_FAIL;
    }
    
    // If only OK response, assume SIM is ready
    if (strstr(response, "OK") && strlen(response) <= 10) {
        ESP_LOGI(TAG, "SIM card ready");
        return ESP_OK;
    }
    
    ESP_LOGW(TAG, "Unexpected SIM response: %s", response);
    return ESP_FAIL;
}



esp_err_t sim7600e_gsm_wait_for_network(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Waiting for network registration...");
    
    uint32_t start_time = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    int attempt = 0;
    
    // First, check signal quality
    char response[256];
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+CSQ\r\n", response, sizeof(response), 5000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Signal quality response: %s", response);
    }
    
    // Check operator information
    ret = sim7600e_gsm_send_at_command("AT+COPS?\r\n", response, sizeof(response), 5000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Operator info: %s", response);
    }
    
    // Now wait for registration
    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        attempt++;
        ESP_LOGI(TAG, "Network registration attempt %d", attempt);
        
        // Check circuit-switched registration
        ret = sim7600e_gsm_send_at_command("AT+CREG?\r\n", response, sizeof(response), 5000);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "CS Registration status: %s", response);
            
            // Parse CREG response: +CREG: n,stat[,lac,ci]
            // Look for the status value after the first comma
            char *creg_pos = strstr(response, "+CREG:");
            if (creg_pos) {
                int n, stat;
                // Try parsing with extended format first
                if (sscanf(creg_pos, "+CREG: %d,%d", &n, &stat) >= 2) {
                    ESP_LOGI(TAG, "Parsed CREG - n=%d, stat=%d", n, stat);
                    
                    if (stat == 1) {
                        ESP_LOGI(TAG, "✅ Registered to home network (CS Domain)");
                        
                        // Check packet-switched registration
                        ret = sim7600e_gsm_send_at_command("AT+CEREG?\r\n", response, sizeof(response), 5000);
                        if (ret == ESP_OK) {
                            ESP_LOGI(TAG, "PS Registration status: %s", response);
                            
                            // Parse CEREG response similarly
                            char *cereg_pos = strstr(response, "+CEREG:");
                            if (cereg_pos) {
                                int ps_n, ps_stat;
                                if (sscanf(cereg_pos, "+CEREG: %d,%d", &ps_n, &ps_stat) >= 2) {
                                    ESP_LOGI(TAG, "Parsed CEREG - n=%d, stat=%d", ps_n, ps_stat);
                                    if (ps_stat == 1 || ps_stat == 5) {
                                        ESP_LOGI(TAG, "✅ Registered to packet network (PS Domain)");
                                        return ESP_OK;
                                    }
                                }
                            }
                            
                            // If PS not registered, still continue - CS registration might be sufficient
                            ESP_LOGW(TAG, "PS registration not optimal, but CS registered");
                        }
                        
                        return ESP_OK; // CS registration is sufficient for basic operation
                        
                    } else if (stat == 5) {
                        ESP_LOGI(TAG, "✅ Registered, roaming (CS Domain)");
                        return ESP_OK;
                    } else if (stat == 0) {
                        ESP_LOGW(TAG, "Not searching for new operator (stat=%d)", stat);
                    } else if (stat == 2) {
                        ESP_LOGW(TAG, "Searching for operator (stat=%d)", stat);
                    } else if (stat == 3) {
                        ESP_LOGE(TAG, "Registration denied (stat=%d)", stat);
                    } else {
                        ESP_LOGW(TAG, "Unknown registration status: %d", stat);
                    }
                } else {
                    ESP_LOGW(TAG, "Failed to parse CREG response: %s", creg_pos);
                }
            } else {
                ESP_LOGW(TAG, "No +CREG: found in response: %s", response);
            }
        } else {
            ESP_LOGE(TAG, "Failed to get CS registration status");
        }
        
        // Check signal quality periodically
        if (attempt % 5 == 0) {
            ret = sim7600e_gsm_send_at_command("AT+CSQ\r\n", response, sizeof(response), 5000);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Current signal quality: %s", response);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(3000)); // Check every 3 seconds
    }
    
    ESP_LOGE(TAG, "Network registration timeout after %d attempts", attempt);
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
    
    // Configure network for TCP/IP applications
    ret = sim7600e_gsm_send_at_command("AT+NETOPEN\r\n", response, sizeof(response), 10000);
    if (ret == ESP_OK && strstr(response, "+NETOPEN: 1")) {
        ESP_LOGI(TAG, "Network interface opened successfully");
    } else if (ret == ESP_OK && strstr(response, "+NETOPEN: 0")) {
        ESP_LOGI(TAG, "Network interface already opened");
    } else {
        ESP_LOGW(TAG, "NETOPEN command failed, trying legacy method: %s", response);
        
        // Try alternative TCP/IP initialization for older firmware
        ret = sim7600e_gsm_send_at_command("AT+CIPMODE=0\r\n", response, sizeof(response), 5000);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set transparent mode");
        }
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
    
    // Initialize struct to known values
    memset(info, 0, sizeof(*info));
    info->signal_strength = -999;  // Unknown by default
    
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

esp_err_t sim7600e_gsm_sync_ntp_time(const char *ntp_server)
{
    char response[256];
    char cmd[128];
    
    // Use default NTP server if none provided
    const char *server = (ntp_server != NULL) ? ntp_server : "pool.ntp.org";
    
    // Set NTP server
    snprintf(cmd, sizeof(cmd), "AT+CNTP=\"%s\",0\r\n", server);
    esp_err_t ret = sim7600e_gsm_send_at_command(cmd, response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set NTP server");
        return ret;
    }
    
    // Sync time with NTP server (may take several seconds)
    ret = sim7600e_gsm_send_at_command("AT+CNTP\r\n", response, sizeof(response), 15000);
    if (ret == ESP_OK) {
        if (strstr(response, "+CNTP: 1") || strstr(response, "OK")) {
            ESP_LOGI(TAG, "NTP time synchronized successfully");
            return ESP_OK;
        } else {
            ESP_LOGW(TAG, "NTP sync response: %s", response);
        }
    }
    
    ESP_LOGW(TAG, "Failed to sync with NTP server");
    return ESP_FAIL;
}

esp_err_t sim7600e_gsm_send_at_command(const char *cmd, char *response, size_t resp_size, uint32_t timeout_ms)
{
    if (cmd == NULL || response == NULL || resp_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return send_at_command_internal(cmd, response, resp_size, timeout_ms);
}

esp_err_t download_certificates_to_module(const char *fil_cert, const uint8_t *cert_start, const uint8_t *cert_end)
{
    if (fil_cert == NULL || cert_start == NULL || cert_end == NULL || cert_end <= cert_start) {
        return ESP_ERR_INVALID_ARG;
    }
    
    SemaphoreHandle_t mutex = sim7600e_get_mutex();
    QueueHandle_t resp_queue = sim7600e_get_resp_queue();
    QueueHandle_t urc_queue = sim7600e_get_urc_queue();
    int uart_port = sim7600e_get_uart_port();
    
    if (mutex == NULL || resp_queue == NULL || urc_queue == NULL) {
        ESP_LOGE(TAG, "SIM7600E not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Take mutex
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex for certificate download");
        return ESP_ERR_TIMEOUT;
    }
    
    // Clear both queues
    sim7600e_msg_t dummy_msg;
    while (xQueueReceive(resp_queue, &dummy_msg, 0) == pdTRUE) {}
    while (xQueueReceive(urc_queue, &dummy_msg, 0) == pdTRUE) {}

    char cmd[256];
    size_t cert_size = cert_end - cert_start;
    ESP_LOGI(TAG, "Downloading certificate '%s' of size %d bytes to module", fil_cert, (int)cert_size);
    
    // Send AT+CCERTDOWN command with correct termination
    snprintf(cmd, sizeof(cmd), "AT+CCERTDOWN=\"%s\",%d\r\n", fil_cert, (int)cert_size);
    printf("send command %s", cmd); // cmd already has \n
    
    if (uart_write_bytes(uart_port, cmd, strlen(cmd)) != strlen(cmd)) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Failed to send CCERTDOWN command");
        return ESP_FAIL;
    }
    
    // Wait for prompt '>'
    // We expect the prompt specifically.
    sim7600e_msg_t resp_msg;
    TickType_t timeout_ticks = pdMS_TO_TICKS(5000); // 5s timeout for prompt
    TickType_t start_time = xTaskGetTickCount();
    bool got_prompt = false;
    char combined_response[512] = {0};

    while ((xTaskGetTickCount() - start_time) < timeout_ticks && !got_prompt) {
        // Check queues
        if (xQueueReceive(resp_queue, &resp_msg, 50) == pdTRUE || xQueueReceive(urc_queue, &resp_msg, 50) == pdTRUE) {
            ESP_LOGD(TAG, "Got resp: %s", resp_msg.data);
            if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                if (strlen(combined_response) > 0) strcat(combined_response, " ");
                    strcat(combined_response, resp_msg.data);
            }
            
            if (strstr(resp_msg.data, ">")) {
                got_prompt = true;
            } else if (strstr(resp_msg.data, "ERROR")) {
                // Fail early if ERROR
                xSemaphoreGive(mutex);
                ESP_LOGE(TAG, "CCERTDOWN command failed: %s", resp_msg.data);
                return ESP_FAIL;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!got_prompt) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Timeout waiting for '>' prompt. Resp: %s", combined_response);
        return ESP_ERR_TIMEOUT;
    }

    // Write certificate data in chunks
    const size_t chunk_size = 256;
    size_t offset = 0;
    int total_written = 0;
    
    ESP_LOGI(TAG, "Writing certificate data in chunks...");
    while (offset < cert_size) {
        size_t remaining = cert_size - offset;
        size_t write_size = (remaining < chunk_size) ? remaining : chunk_size;
        
        int written = uart_write_bytes(uart_port, (const char *)(cert_start + offset), write_size);
        if (written < 0) {
            xSemaphoreGive(mutex);
            ESP_LOGE(TAG, "Failed to write certificate chunk at offset %d", (int)offset);
            return ESP_FAIL;
        }
        
        total_written += written;
        offset += written;
        
        vTaskDelay(pdMS_TO_TICKS(20)); // Give slight delay for UART buffer/Module processing
        
        ESP_LOGD(TAG, "Written %d/%d bytes", total_written, (int)cert_size);
    }
    
    // Wait for Final OK
    // Reboot or next commands might happen, but we should just expect OK after download
    timeout_ticks = pdMS_TO_TICKS(10000); // 10s timeout for write confirmation
    start_time = xTaskGetTickCount();
    bool got_ok = false;
    memset(combined_response, 0, sizeof(combined_response));

    while ((xTaskGetTickCount() - start_time) < timeout_ticks && !got_ok) {
        if (xQueueReceive(resp_queue, &resp_msg, 50) == pdTRUE || xQueueReceive(urc_queue, &resp_msg, 50) == pdTRUE) {
             if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                if (strlen(combined_response) > 0) strcat(combined_response, " ");
                strcat(combined_response, resp_msg.data);
            }
            if (strstr(resp_msg.data, "OK")) {
                got_ok = true;
            } else if (strstr(resp_msg.data, "ERROR")) {
                 xSemaphoreGive(mutex);
                 ESP_LOGE(TAG, "Certificate write cleanup failed: %s", resp_msg.data);
                 return ESP_FAIL;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    xSemaphoreGive(mutex);

    if (!got_ok) {
        ESP_LOGE(TAG, "Timeout waiting for upload confirmation. Resp: %s", combined_response);
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Certificate downloaded successfully: %s (%d bytes)", fil_cert, total_written);
    return ESP_OK;
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

esp_err_t sim7600e_gsm_mqtt_subscribe(const char *topic, int qos)
{
    if (topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    SemaphoreHandle_t mutex = sim7600e_get_mutex();
    QueueHandle_t resp_queue = sim7600e_get_resp_queue();
    QueueHandle_t urc_queue = sim7600e_get_urc_queue();
    int uart_port = sim7600e_get_uart_port();
    
    if (mutex == NULL || resp_queue == NULL || urc_queue == NULL) {
        ESP_LOGE(TAG, "SIM7600E not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Take mutex
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex for MQTT subscribe");
        return ESP_ERR_TIMEOUT;
    }
    
    // Clear both queues
    sim7600e_msg_t dummy_msg;
    while (xQueueReceive(resp_queue, &dummy_msg, 0) == pdTRUE) {}
    while (xQueueReceive(urc_queue, &dummy_msg, 0) == pdTRUE) {}
    
    // Step 1: Send subscription command
    char sub_cmd[64];
    snprintf(sub_cmd, sizeof(sub_cmd), "AT+CMQTTSUB=0,%d,%d\r\n", strlen(topic), qos);
    
    ESP_LOGI(TAG, "Sending MQTT subscribe command: %s", sub_cmd);
    if (uart_write_bytes(uart_port, sub_cmd, strlen(sub_cmd)) != strlen(sub_cmd)) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Failed to send MQTT subscribe command");
        return ESP_FAIL;
    }
    
    // Wait for prompt '>'
    sim7600e_msg_t resp_msg;
    TickType_t timeout_ticks = pdMS_TO_TICKS(5000); // 5s timeout for prompt
    TickType_t start_time = xTaskGetTickCount();
    bool got_prompt = false;
    char combined_response[512] = {0};

    while ((xTaskGetTickCount() - start_time) < timeout_ticks && !got_prompt) {
        // Check queues
        if (xQueueReceive(resp_queue, &resp_msg, 50) == pdTRUE || xQueueReceive(urc_queue, &resp_msg, 50) == pdTRUE) {
            ESP_LOGD(TAG, "Got resp: %s", resp_msg.data);
            if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                if (strlen(combined_response) > 0) strcat(combined_response, " ");
                    strcat(combined_response, resp_msg.data);
            }
            
            if (strstr(resp_msg.data, ">")) {
                got_prompt = true;
            } else if (strstr(resp_msg.data, "ERROR")) {
                xSemaphoreGive(mutex);
                ESP_LOGE(TAG, "MQTT subscribe command failed: %s", resp_msg.data);
                return ESP_FAIL;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!got_prompt) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Timeout waiting for '>' prompt. Resp: %s", combined_response);
        return ESP_ERR_TIMEOUT;
    }
    
    // Step 2: Send topic with Ctrl+Z
    char topic_cmd[128];
    snprintf(topic_cmd, sizeof(topic_cmd), "%s\x1A", topic);
    
    ESP_LOGI(TAG, "Sending topic string: %s", topic);
    if (uart_write_bytes(uart_port, topic_cmd, strlen(topic_cmd)) != strlen(topic_cmd)) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Failed to send topic string");
        return ESP_FAIL;
    }
    
    // Wait for subscription confirmation
    memset(combined_response, 0, sizeof(combined_response));
    timeout_ticks = pdMS_TO_TICKS(10000);
    start_time = xTaskGetTickCount();
    bool got_response = false;
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks && !got_response) {
        // Check both queues for response
        if (xQueueReceive(resp_queue, &resp_msg, 100) == pdTRUE) {
            if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                if (strlen(combined_response) > 0) {
                    strcat(combined_response, " ");
                }
                strcat(combined_response, resp_msg.data);
            }
            
            if (strstr(resp_msg.data, "OK") || strstr(resp_msg.data, "CMQTTSUB") || strstr(resp_msg.data, "ERROR")) {
                got_response = true;
            }
        }
        
        if (xQueueReceive(urc_queue, &resp_msg, 100) == pdTRUE) {
            if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                if (strlen(combined_response) > 0) {
                    strcat(combined_response, " ");
                }
                strcat(combined_response, resp_msg.data);
            }
            
            if (strstr(resp_msg.data, "OK") || strstr(resp_msg.data, "CMQTTSUB") || strstr(resp_msg.data, "ERROR")) {
                got_response = true;
            }
        }
        
        if (!got_response) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    xSemaphoreGive(mutex);
    
    if (got_response) {
        ESP_LOGI(TAG, "MQTT subscription response: %s", combined_response);
        if (strstr(combined_response, "ERROR")) {
            ESP_LOGE(TAG, "MQTT subscription failed");
            return ESP_FAIL;
        } else {
            ESP_LOGI(TAG, "MQTT subscription successful");
            return ESP_OK;
        }
    } else {
        ESP_LOGE(TAG, "MQTT subscription timeout");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t sim7600e_gsm_mqtt_set_topic(const char *topic)
{
    if (topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    SemaphoreHandle_t mutex = sim7600e_get_mutex();
    QueueHandle_t resp_queue = sim7600e_get_resp_queue();
    QueueHandle_t urc_queue = sim7600e_get_urc_queue();
    int uart_port = sim7600e_get_uart_port();
    
    if (mutex == NULL || resp_queue == NULL || urc_queue == NULL) {
        ESP_LOGE(TAG, "SIM7600E not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Take mutex
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex for MQTT set topic");
        return ESP_ERR_TIMEOUT;
    }
    
    // Clear both queues
    sim7600e_msg_t dummy_msg;
    while (xQueueReceive(resp_queue, &dummy_msg, 0) == pdTRUE) {}
    while (xQueueReceive(urc_queue, &dummy_msg, 0) == pdTRUE) {}
    
    // Step 1: Send topic command
    char topic_cmd[64];
    snprintf(topic_cmd, sizeof(topic_cmd), "AT+CMQTTTOPIC=0,%d\r\n", strlen(topic));
    
    ESP_LOGI(TAG, "Setting MQTT topic, length: %d", strlen(topic));
    if (uart_write_bytes(uart_port, topic_cmd, strlen(topic_cmd)) != strlen(topic_cmd)) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Failed to send MQTT topic command");
        return ESP_FAIL;
    }
    
    // Wait for prompt '>'
    sim7600e_msg_t resp_msg;
    TickType_t timeout_ticks = pdMS_TO_TICKS(5000); // 5s timeout for prompt
    TickType_t start_time = xTaskGetTickCount();
    bool got_prompt = false;
    char combined_response[256] = {0}; // Use a local buffer for this function

    while ((xTaskGetTickCount() - start_time) < timeout_ticks && !got_prompt) {
        // Check queues
        if (xQueueReceive(resp_queue, &resp_msg, 50) == pdTRUE || xQueueReceive(urc_queue, &resp_msg, 50) == pdTRUE) {
            ESP_LOGD(TAG, "Got resp: %s", resp_msg.data);
            if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                if (strlen(combined_response) > 0) strcat(combined_response, " ");
                    strcat(combined_response, resp_msg.data);
            }
            
            if (strstr(resp_msg.data, ">")) {
                got_prompt = true;
            } else if (strstr(resp_msg.data, "ERROR")) {
                xSemaphoreGive(mutex);
                ESP_LOGE(TAG, "MQTT topic command failed: %s", resp_msg.data);
                return ESP_FAIL;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!got_prompt) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Timeout waiting for '>' prompt. Resp: %s", combined_response);
        return ESP_ERR_TIMEOUT;
    }
    
    // Step 2: Send topic string
    ESP_LOGI(TAG, "Sending topic: %s", topic);
    if (uart_write_bytes(uart_port, topic, strlen(topic)) != strlen(topic)) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Failed to send topic string");
        return ESP_FAIL;
    }
    
    // Wait for OK response
    memset(combined_response, 0, sizeof(combined_response));
    timeout_ticks = pdMS_TO_TICKS(3000);
    start_time = xTaskGetTickCount();
    bool got_response = false;
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks && !got_response) {
        if (xQueueReceive(resp_queue, &resp_msg, 50) == pdTRUE || xQueueReceive(urc_queue, &resp_msg, 50) == pdTRUE) {
            if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                if (strlen(combined_response) > 0) strcat(combined_response, " ");
                strcat(combined_response, resp_msg.data);
            }
            if (strstr(resp_msg.data, "OK") || strstr(resp_msg.data, "ERROR")) {
                got_response = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    xSemaphoreGive(mutex);
    
    if (got_response && !strstr(combined_response, "ERROR")) {
        ESP_LOGI(TAG, "MQTT topic set successfully");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to set MQTT topic: %s", combined_response);
        return ESP_FAIL;
    }
}

esp_err_t sim7600e_gsm_mqtt_set_payload(const char *payload)
{
    if (payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    SemaphoreHandle_t mutex = sim7600e_get_mutex();
    QueueHandle_t resp_queue = sim7600e_get_resp_queue();
    QueueHandle_t urc_queue = sim7600e_get_urc_queue();
    int uart_port = sim7600e_get_uart_port();
    
    if (mutex == NULL || resp_queue == NULL || urc_queue == NULL) {
        ESP_LOGE(TAG, "SIM7600E not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Take mutex
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex for MQTT set payload");
        return ESP_ERR_TIMEOUT;
    }
    
    // Clear both queues
    sim7600e_msg_t dummy_msg;
    while (xQueueReceive(resp_queue, &dummy_msg, 0) == pdTRUE) {}
    while (xQueueReceive(urc_queue, &dummy_msg, 0) == pdTRUE) {}
    
    // Step 1: Send payload length command
    char payload_cmd[64];
    snprintf(payload_cmd, sizeof(payload_cmd), "AT+CMQTTPAYLOAD=0,%d\r\n", strlen(payload));
    
    ESP_LOGI(TAG, "Setting MQTT payload, length: %d", strlen(payload));
    if (uart_write_bytes(uart_port, payload_cmd, strlen(payload_cmd)) != strlen(payload_cmd)) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Failed to send MQTT payload command");
        return ESP_FAIL;
    }
    
    // Wait for prompt '>'
    sim7600e_msg_t resp_msg;
    TickType_t timeout_ticks = pdMS_TO_TICKS(5000); // 5s timeout for prompt
    TickType_t start_time = xTaskGetTickCount();
    bool got_prompt = false;
    char combined_response[256] = {0};

    while ((xTaskGetTickCount() - start_time) < timeout_ticks && !got_prompt) {
        // Check queues
        if (xQueueReceive(resp_queue, &resp_msg, 50) == pdTRUE || xQueueReceive(urc_queue, &resp_msg, 50) == pdTRUE) {
            ESP_LOGD(TAG, "Got resp: %s", resp_msg.data);
            if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                if (strlen(combined_response) > 0) strcat(combined_response, " ");
                    strcat(combined_response, resp_msg.data);
            }
            
            if (strstr(resp_msg.data, ">")) {
                got_prompt = true;
            } else if (strstr(resp_msg.data, "ERROR")) {
                xSemaphoreGive(mutex);
                ESP_LOGE(TAG, "MQTT payload command failed: %s", resp_msg.data);
                return ESP_FAIL;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!got_prompt) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Timeout waiting for '>' prompt. Resp: %s", combined_response);
        return ESP_ERR_TIMEOUT;
    }
    
    // Step 2: Send payload with Ctrl+Z
    char payload_with_terminator[512];
    snprintf(payload_with_terminator, sizeof(payload_with_terminator), "%s\x1A", payload);
    
    ESP_LOGI(TAG, "Sending payload with terminator");
    if (uart_write_bytes(uart_port, payload_with_terminator, strlen(payload_with_terminator)) != strlen(payload_with_terminator)) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Failed to send payload");
        return ESP_FAIL;
    }
    
    // Wait for OK response
    memset(combined_response, 0, sizeof(combined_response));
    timeout_ticks = pdMS_TO_TICKS(3000);
    start_time = xTaskGetTickCount();
    bool got_response = false;
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks && !got_response) {
        if (xQueueReceive(resp_queue, &resp_msg, 50) == pdTRUE || xQueueReceive(urc_queue, &resp_msg, 50) == pdTRUE) {
            if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                if (strlen(combined_response) > 0) strcat(combined_response, " ");
                strcat(combined_response, resp_msg.data);
            }
            if (strstr(resp_msg.data, "OK") || strstr(resp_msg.data, "ERROR")) {
                got_response = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    xSemaphoreGive(mutex);
    
    if (got_response && !strstr(combined_response, "ERROR")) {
        ESP_LOGI(TAG, "MQTT payload set successfully");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to set MQTT payload: %s", combined_response);
        return ESP_FAIL;
    }
}

// Internal function implementation
static esp_err_t send_at_command_internal(const char *cmd, char *response, size_t resp_size, uint32_t timeout_ms)
{
    SemaphoreHandle_t mutex = sim7600e_get_mutex();
    QueueHandle_t resp_queue = sim7600e_get_resp_queue();
    QueueHandle_t urc_queue = sim7600e_get_urc_queue();
    int uart_port = sim7600e_get_uart_port();
    
    if (mutex == NULL || resp_queue == NULL || urc_queue == NULL) {
        ESP_LOGE(TAG, "SIM7600E not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Take mutex with sufficient timeout to wait for any ongoing operations
    // Use at least 15 seconds to accommodate long-running operations like shadow updates
    uint32_t mutex_timeout = (timeout_ms > 15000) ? timeout_ms : 15000;
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(mutex_timeout)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Clear both queues
    sim7600e_msg_t dummy_msg;
    while (xQueueReceive(resp_queue, &dummy_msg, 0) == pdTRUE) {}
    while (xQueueReceive(urc_queue, &dummy_msg, 0) == pdTRUE) {}

    // Send command
    if (uart_write_bytes(uart_port, cmd, strlen(cmd)) != strlen(cmd)) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Failed to send AT command");
        return ESP_FAIL;
    }

    // Wait for response
    sim7600e_msg_t resp_msg;
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    TickType_t start_time = xTaskGetTickCount();
    
    char combined_response[512] = {0};
    bool got_final_response = false;
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks && !got_final_response) {
        TickType_t remaining_time = timeout_ticks - (xTaskGetTickCount() - start_time);
        if (remaining_time <= 0) break;
        
        // Check URC queue for data responses
        if (xQueueReceive(urc_queue, &resp_msg, 50) == pdTRUE) {
            if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                if (strlen(combined_response) > 0) {
                    strcat(combined_response, " ");
                }
                strcat(combined_response, resp_msg.data);
            }
        }
        
        // Check response queue for OK/ERROR
        if (xQueueReceive(resp_queue, &resp_msg, 50) == pdTRUE) {
            if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                if (strlen(combined_response) > 0) {
                    strcat(combined_response, " ");
                }
                strcat(combined_response, resp_msg.data);
            }
            
            if (strstr(resp_msg.data, "OK") || strstr(resp_msg.data, "ERROR")) {
                got_final_response = true;
            } else if (strstr(resp_msg.data, ">")) {
                // Prompt received, continue waiting
            }
        }
        
        if (!got_final_response) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    if (got_final_response || strlen(combined_response) > 0) {
        strncpy(response, combined_response, resp_size - 1);
        response[resp_size - 1] = '\0';
        
        // Clean up extra spaces
        char *src = response, *dst = response;
        char prev = '\0';
        while (*src) {
            if (*src != ' ' || prev != ' ') {
                *dst++ = *src;
            }
            prev = *src++;
        }
        *dst = '\0';
        
        xSemaphoreGive(mutex);
        return ESP_OK;
    } else {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "AT command timeout: %s", cmd);
        return ESP_ERR_TIMEOUT;
    }
}