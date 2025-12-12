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
            
            // Enable verbose responses with multiple approaches
            ESP_LOGI(TAG, "Configuring verbose responses...");
            
            // Method 1: ATV1 (verbose responses)
            ret = sim7600e_gsm_send_at_command("ATV1\r\n", response, sizeof(response), 5000);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "ATV1 verbose responses enabled: %s", response);
            } else {
                ESP_LOGW(TAG, "Failed ATV1 command");
            }
            
            // Method 2: AT+CMEE=2 (verbose error reporting)
            ret = sim7600e_gsm_send_at_command("AT+CMEE=2\r\n", response, sizeof(response), 5000);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Verbose error reporting enabled: %s", response);
            } else {
                ESP_LOGW(TAG, "Failed to enable verbose errors");
            }
            
            // Method 3: Ensure network registration reporting
            ret = sim7600e_gsm_send_at_command("AT+CREG=2\r\n", response, sizeof(response), 5000);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Network registration reporting enabled: %s", response);
            } else {
                ESP_LOGW(TAG, "Failed to enable CREG reporting");
            }
            
            // Method 4: Test verbose mode immediately with a query command
            ESP_LOGI(TAG, "Testing verbose mode with CREG query...");
            ret = sim7600e_gsm_send_at_command("AT+CREG?\r\n", response, sizeof(response), 5000);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "CREG test response: '%s'", response);
                if (strstr(response, "+CREG:")) {
                    ESP_LOGI(TAG, "✅ Verbose mode working - getting detailed responses");
                } else {
                    ESP_LOGW(TAG, "❌ Verbose mode not working - still getting minimal responses");
                }
            }
            
            // Disable echo mode (good practice)
            ESP_LOGI(TAG, "Turning off echo mode...");
            ret = sim7600e_gsm_send_at_command("ATE0\r\n", response, sizeof(response), 5000);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Echo mode disabled");
            } else {
                ESP_LOGW(TAG, "Failed to disable echo");
            }
            
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
    
    // For AT+CPIN?, the SIM7600E sends two responses:
    // 1. "+CPIN: READY" (or similar status message)
    // 2. "OK" (command completion)
    // We need to capture both and parse them properly
    
    // Send AT+CPIN? command and wait for the status response
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+CPIN?\r\n", response, sizeof(response), 5000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Full SIM response: '%s'", response);  // Add debug logging
        ESP_LOGI(TAG, "Response length: %d", strlen(response));  // Show response length
        
        // Check for +CPIN status in the response
        if (strstr(response, "+CPIN: READY")) {
            ESP_LOGI(TAG, "SIM card is ready");
            return ESP_OK;
        } else if (strstr(response, "+CPIN: SIM PIN")) {
            ESP_LOGE(TAG, "SIM card requires PIN");
            return ESP_FAIL;
        } else if (strstr(response, "READY")) {
            // Sometimes the response might not include the +CPIN: prefix
            ESP_LOGI(TAG, "SIM card is ready");
            return ESP_OK;
        } else if (strstr(response, "OK") && strlen(response) <= 10) {
            // If we only get "OK" without +CPIN response, try a different approach
            ESP_LOGW(TAG, "Only received OK response, trying alternative check...");
            
            // Some SIM7600E modules may not send the +CPIN response properly
            // In that case, if we get OK, it usually means the SIM is detected
            // But let's try a simple AT command to verify the module is working
            char test_response[64];
            esp_err_t test_ret = sim7600e_gsm_send_at_command("AT\r\n", test_response, sizeof(test_response), 3000);
            if (test_ret == ESP_OK && strstr(test_response, "OK")) {
                ESP_LOGI(TAG, "Module responsive, assuming SIM is ready");
                return ESP_OK;
            } else {
                ESP_LOGW(TAG, "SIM card not detected - only got OK response");
                return ESP_FAIL;
            }
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
    QueueHandle_t urc_queue = sim7600e_get_urc_queue();
    int uart_port = sim7600e_get_uart_port();
    
    if (mutex == NULL || resp_queue == NULL || urc_queue == NULL) {
        ESP_LOGE(TAG, "SIM7600E not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Take mutex
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Clear both queues
    sim7600e_msg_t dummy_msg;
    while (xQueueReceive(resp_queue, &dummy_msg, 0) == pdTRUE) {
        // Clear response queue
    }
    while (xQueueReceive(urc_queue, &dummy_msg, 0) == pdTRUE) {
        // Clear URC queue
    }

    // Send command
    int written = uart_write_bytes(uart_port, cmd, strlen(cmd));
    if (written != strlen(cmd)) {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "Failed to send AT command");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sent command: %s", cmd);

    // Wait for response - collect all data until we get OK/ERROR
    sim7600e_msg_t resp_msg;
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    TickType_t start_time = xTaskGetTickCount();
    
    char combined_response[512] = {0};
    bool got_final_response = false;
    int response_count = 0;
    
    // Keep collecting responses until timeout or final response
    while ((xTaskGetTickCount() - start_time) < timeout_ticks && !got_final_response) {
        TickType_t remaining_time = timeout_ticks - (xTaskGetTickCount() - start_time);
        if (remaining_time <= 0) break;
        
        // Check both queues for responses with better timing
        bool got_urc = false, got_resp = false;
        
        // Check URC queue first (for data responses like +CREG:, +CSQ:, etc.)
        if (xQueueReceive(urc_queue, &resp_msg, 50) == pdTRUE) {
            got_urc = true;
            response_count++;
            ESP_LOGI(TAG, "Received URC #%d: '%s'", response_count, resp_msg.data);
            
            // Add URC to combined response
            if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                if (strlen(combined_response) > 0) {
                    strcat(combined_response, " ");
                }
                strcat(combined_response, resp_msg.data);
            }
        }
        
        // Check response queue (for OK/ERROR) with longer timeout if no URC
        if (xQueueReceive(resp_queue, &resp_msg, got_urc ? 50 : 200) == pdTRUE) {
            got_resp = true;
            response_count++;
            ESP_LOGI(TAG, "Received response #%d: '%s'", response_count, resp_msg.data);
            
            // Add response to combined response
            if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                if (strlen(combined_response) > 0) {
                    strcat(combined_response, " ");
                }
                strcat(combined_response, resp_msg.data);
            }
            
            // Check if this contains OK or ERROR (final response)
            if (strstr(resp_msg.data, "OK") || strstr(resp_msg.data, "ERROR")) {
                ESP_LOGI(TAG, "Got final response, breaking");
                got_final_response = true;
            }
        }
        
        // If we didn't get any message, continue waiting
        if (!got_urc && !got_resp) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    
    if (got_final_response || response_count > 0) {
        // Copy the combined response to the output buffer
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
        
        ESP_LOGI(TAG, "Final combined response: '%s'", response);
        xSemaphoreGive(mutex);
        return ESP_OK;
    } else {
        xSemaphoreGive(mutex);
        ESP_LOGE(TAG, "AT command timeout");
        return ESP_ERR_TIMEOUT;
    }
}