/**
 * @file sim7600e.c
 * @brief SIM7600E 4G LTE Module Driver Implementation
 * 
 * Core functionality for the SIM7600E cellular module including:
 * - Module initialization and configuration
 * - UART communication setup
 * - Task and queue management
 * - Power control
 * 
 * @author ESP32 SIM7600E Component
 */

#include "sim7600e.h"
#include "sim7600e_gsm.h"
#include "sim7600e_gnss.h"
#include "sim7600e_tcp.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "SIM7600E";

// Internal state structure
typedef struct {
    bool initialized;
    sim7600e_config_t config;
    QueueHandle_t urc_queue;
    QueueHandle_t resp_queue;
    QueueHandle_t gnss_queue;
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t gnss_ready_sem;
    TaskHandle_t uart_reader_task_handle;
} sim7600e_context_t;

static sim7600e_context_t s_sim7600e_ctx = {0};

// Internal function declarations
static void uart_reader_task(void *arg);
static esp_err_t setup_uart(const sim7600e_config_t *config);
static esp_err_t create_queues(const sim7600e_config_t *config);
static void cleanup_resources(void);

sim7600e_config_t sim7600e_get_default_config(void)
{
    sim7600e_config_t config = {
        .uart_port = SIM7600E_DEFAULT_UART_PORT,
        .tx_pin = SIM7600E_DEFAULT_TX_PIN,
        .rx_pin = SIM7600E_DEFAULT_RX_PIN,
        .pwrkey_pin = SIM7600E_DEFAULT_PWRKEY_PIN,
        .baud_rate = SIM7600E_DEFAULT_BAUD_RATE,
        .uart_buf_size = SIM7600E_DEFAULT_UART_BUF_SIZE,
        .queue_config = {
            .urc_queue_size = 10,
            .resp_queue_size = 5,
            .gnss_queue_size = 1
        }
    };
    return config;
}

esp_err_t sim7600e_init(const sim7600e_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_sim7600e_ctx.initialized) {
        ESP_LOGW(TAG, "SIM7600E already initialized");
        return ESP_OK;
    }

    // Create mutex
    s_sim7600e_ctx.mutex = xSemaphoreCreateMutex();
    if (s_sim7600e_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    // Copy configuration
    memcpy(&s_sim7600e_ctx.config, config, sizeof(sim7600e_config_t));

    // Setup UART
    esp_err_t ret = setup_uart(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup UART");
        cleanup_resources();
        return ret;
    }

    // Create queues
    ret = create_queues(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create queues");
        cleanup_resources();
        return ret;
    }

    // Create GNSS ready semaphore
    s_sim7600e_ctx.gnss_ready_sem = xSemaphoreCreateBinary();
    if (s_sim7600e_ctx.gnss_ready_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create GNSS semaphore");
        cleanup_resources();
        return ESP_FAIL;
    }

    // Start UART reader task
    BaseType_t task_ret = xTaskCreate(
        uart_reader_task, 
        "sim7600e_uart_reader", 
        4096, 
        NULL, 
        10, 
        &s_sim7600e_ctx.uart_reader_task_handle
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UART reader task");
        cleanup_resources();
        return ESP_FAIL;
    }

    s_sim7600e_ctx.initialized = true;
    ESP_LOGI(TAG, "SIM7600E initialized successfully");
    
    return ESP_OK;
}

esp_err_t sim7600e_deinit(void)
{
    if (!s_sim7600e_ctx.initialized) {
        return ESP_OK;
    }

    cleanup_resources();
    s_sim7600e_ctx.initialized = false;
    ESP_LOGI(TAG, "SIM7600E deinitialized");
    
    return ESP_OK;
}

esp_err_t sim7600e_power_on(void)
{
    if (!s_sim7600e_ctx.initialized) {
        ESP_LOGE(TAG, "SIM7600E not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    gpio_reset_pin(s_sim7600e_ctx.config.pwrkey_pin);
    gpio_set_direction(s_sim7600e_ctx.config.pwrkey_pin, GPIO_MODE_OUTPUT);
    
    // Pull PWRKEY low for 1 second to power on
    gpio_set_level(s_sim7600e_ctx.config.pwrkey_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(s_sim7600e_ctx.config.pwrkey_pin, 1);
    
    ESP_LOGI(TAG, "Power on sequence completed");
    
    return ESP_OK;
}

esp_err_t sim7600e_power_off(void)
{
    if (!s_sim7600e_ctx.initialized) {
        ESP_LOGE(TAG, "SIM7600E not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Pull PWRKEY low for 3 seconds to power off
    gpio_set_level(s_sim7600e_ctx.config.pwrkey_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(3000));
    gpio_set_level(s_sim7600e_ctx.config.pwrkey_pin, 1);
    
    ESP_LOGI(TAG, "Power off sequence completed");
    
    return ESP_OK;
}

esp_err_t sim7600e_get_module_info(char *imei, size_t imei_len)
{
    if (!s_sim7600e_ctx.initialized) {
        ESP_LOGE(TAG, "SIM7600E not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (imei == NULL || imei_len < 16) {
        ESP_LOGE(TAG, "Invalid IMEI buffer");
        return ESP_ERR_INVALID_ARG;
    }

    // This will be implemented in the GSM module
    return sim7600e_gsm_send_at_command("AT+CGSN\r\n", imei, imei_len, 5000);
}

esp_err_t sim7600e_get_queues(QueueHandle_t *urc_queue, QueueHandle_t *resp_queue, QueueHandle_t *gnss_queue)
{
    if (!s_sim7600e_ctx.initialized) {
        ESP_LOGE(TAG, "SIM7600E not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (urc_queue != NULL) {
        *urc_queue = s_sim7600e_ctx.urc_queue;
    }
    
    if (resp_queue != NULL) {
        *resp_queue = s_sim7600e_ctx.resp_queue;
    }
    
    if (gnss_queue != NULL) {
        *gnss_queue = s_sim7600e_ctx.gnss_queue;
    }

    return ESP_OK;
}

// Internal function implementations

static esp_err_t setup_uart(const sim7600e_config_t *config)
{
    uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };

    esp_err_t ret = uart_driver_install(
        config->uart_port, 
        config->uart_buf_size * 2, 
        config->uart_buf_size * 2, 
        0, 
        NULL, 
        0
    );
    if (ret != ESP_OK) {
        return ret;
    }

    ret = uart_param_config(config->uart_port, &uart_config);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = uart_set_pin(
        config->uart_port, 
        config->tx_pin, 
        config->rx_pin, 
        UART_PIN_NO_CHANGE, 
        UART_PIN_NO_CHANGE
    );
    
    return ret;
}

static esp_err_t create_queues(const sim7600e_config_t *config)
{
    s_sim7600e_ctx.urc_queue = xQueueCreate(
        config->queue_config.urc_queue_size, 
        sizeof(sim7600e_msg_t)
    );
    if (s_sim7600e_ctx.urc_queue == NULL) {
        return ESP_FAIL;
    }

    s_sim7600e_ctx.resp_queue = xQueueCreate(
        config->queue_config.resp_queue_size, 
        sizeof(sim7600e_msg_t)
    );
    if (s_sim7600e_ctx.resp_queue == NULL) {
        return ESP_FAIL;
    }

    s_sim7600e_ctx.gnss_queue = xQueueCreate(
        config->queue_config.gnss_queue_size, 
        sizeof(sim7600e_gps_info_t)
    );
    if (s_sim7600e_ctx.gnss_queue == NULL) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void cleanup_resources(void)
{
    // Stop UART reader task
    if (s_sim7600e_ctx.uart_reader_task_handle != NULL) {
        vTaskDelete(s_sim7600e_ctx.uart_reader_task_handle);
        s_sim7600e_ctx.uart_reader_task_handle = NULL;
    }

    // Delete queues
    if (s_sim7600e_ctx.urc_queue != NULL) {
        vQueueDelete(s_sim7600e_ctx.urc_queue);
        s_sim7600e_ctx.urc_queue = NULL;
    }

    if (s_sim7600e_ctx.resp_queue != NULL) {
        vQueueDelete(s_sim7600e_ctx.resp_queue);
        s_sim7600e_ctx.resp_queue = NULL;
    }

    if (s_sim7600e_ctx.gnss_queue != NULL) {
        vQueueDelete(s_sim7600e_ctx.gnss_queue);
        s_sim7600e_ctx.gnss_queue = NULL;
    }

    // Delete semaphores
    if (s_sim7600e_ctx.gnss_ready_sem != NULL) {
        vSemaphoreDelete(s_sim7600e_ctx.gnss_ready_sem);
        s_sim7600e_ctx.gnss_ready_sem = NULL;
    }

    if (s_sim7600e_ctx.mutex != NULL) {
        vSemaphoreDelete(s_sim7600e_ctx.mutex);
        s_sim7600e_ctx.mutex = NULL;
    }

    // Deinitialize UART
    uart_driver_delete(s_sim7600e_ctx.config.uart_port);
}

// UART reader task - handles URCs and responses including multi-line MQTT data
static void uart_reader_task(void *arg)
{
    uint8_t data[128];
    char line_buffer[SIM7600E_MSG_LEN];
    int line_pos = 0;
    
    // State machine for MQTT RX multi-line data
    // 0 = normal, 1 = expecting topic data, 2 = expecting payload data
    int mqtt_rx_state = 0;

    while (1) {
        int len = uart_read_bytes(s_sim7600e_ctx.config.uart_port, data, sizeof(data), pdMS_TO_TICKS(100));
        
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = (char)data[i];
                
                // Handle prompt character '>' specially
                // Prompts often don't end with \r\n, so we must detect and forward them immediately
                if (c == '>') {
                     if (line_pos < sizeof(line_buffer) - 1) {
                        line_buffer[line_pos++] = c;
                     }
                     line_buffer[line_pos] = '\0';
                     
                     sim7600e_msg_t msg; 
                     memset(&msg, 0, sizeof(msg));
                     strncpy(msg.data, line_buffer, sizeof(msg.data) - 1);
                     
                     if (s_sim7600e_ctx.resp_queue) {
                        xQueueSend(s_sim7600e_ctx.resp_queue, &msg, 0);
                     }
                     line_pos = 0; 
                     continue;
                }

                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        
                        // Create message
                        sim7600e_msg_t msg;
                        memset(&msg, 0, sizeof(msg));
                        strncpy(msg.data, line_buffer, sizeof(msg.data) - 1);
                        
                        // Check for MQTT RX state transitions
                        if (strstr(msg.data, "+CMQTTRXTOPIC:") != NULL) {
                            // Queue the header
                            if (s_sim7600e_ctx.urc_queue) {
                                xQueueSend(s_sim7600e_ctx.urc_queue, &msg, 0);
                            }
                            mqtt_rx_state = 1; // Next line is topic data
                            line_pos = 0;
                            continue;
                        } else if (strstr(msg.data, "+CMQTTRXPAYLOAD:") != NULL) {
                            // Queue the header
                            if (s_sim7600e_ctx.urc_queue) {
                                xQueueSend(s_sim7600e_ctx.urc_queue, &msg, 0);
                            }
                            mqtt_rx_state = 2; // Next line is payload data
                            line_pos = 0;
                            continue;
                        } else if (strstr(msg.data, "+CMQTTRXEND:") != NULL) {
                            mqtt_rx_state = 0; // Reset state
                            // Queue the end marker
                            if (s_sim7600e_ctx.urc_queue) {
                                xQueueSend(s_sim7600e_ctx.urc_queue, &msg, 0);
                            }
                            line_pos = 0;
                            continue;
                        }
                        
                        // Handle MQTT RX data lines (topic or payload content)
                        if (mqtt_rx_state == 1) {
                            // This is topic data - prefix it for identification
                            sim7600e_msg_t topic_msg;
                            memset(&topic_msg, 0, sizeof(topic_msg));
                            snprintf(topic_msg.data, sizeof(topic_msg.data), "+CMQTTRXTOPIC_DATA:%s", msg.data);
                            if (s_sim7600e_ctx.urc_queue) {
                                xQueueSend(s_sim7600e_ctx.urc_queue, &topic_msg, 0);
                            }
                            mqtt_rx_state = 0; // Topic received
                            line_pos = 0;
                            continue;
                        } else if (mqtt_rx_state == 2) {
                            // This is payload data - prefix it for identification
                            sim7600e_msg_t payload_msg;
                            memset(&payload_msg, 0, sizeof(payload_msg));
                            snprintf(payload_msg.data, sizeof(payload_msg.data), "+CMQTTRXPAYLOAD_DATA:%s", msg.data);
                            if (s_sim7600e_ctx.urc_queue) {
                                xQueueSend(s_sim7600e_ctx.urc_queue, &payload_msg, 0);
                            }
                            mqtt_rx_state = 0; // Payload received
                            line_pos = 0;
                            continue;
                        }
                        
                        // Normal message classification
                        // Special handling for GNSS responses - route to resp_queue
                        // so GNSS task can receive them without MQTT URC task interference
                        if (strstr(msg.data, "+CGPSINFO") != NULL) {
                            // GNSS response goes to response queue, not URC queue
                            if (s_sim7600e_ctx.resp_queue) {
                                xQueueSend(s_sim7600e_ctx.resp_queue, &msg, 0);
                            }
                        } else if (msg.data[0] == '+') {
                            // Other URC messages
                            if (s_sim7600e_ctx.urc_queue) {
                                xQueueSend(s_sim7600e_ctx.urc_queue, &msg, 0);
                            }
                        } else if (strstr(msg.data, "OK") || strstr(msg.data, "ERROR") || 
                                   strstr(msg.data, ">") || strstr(msg.data, "CONNECT")) {
                            // Response message
                            if (s_sim7600e_ctx.resp_queue) {
                                xQueueSend(s_sim7600e_ctx.resp_queue, &msg, 0);
                            }
                        }
                        
                        line_pos = 0;
                    }
                } else if (line_pos < sizeof(line_buffer) - 1) {
                    line_buffer[line_pos++] = c;
                }
            }
        }
    }
}

// Global access functions for backward compatibility
QueueHandle_t sim7600e_get_urc_queue(void)
{
    return s_sim7600e_ctx.urc_queue;
}

QueueHandle_t sim7600e_get_resp_queue(void)
{
    return s_sim7600e_ctx.resp_queue;
}

QueueHandle_t sim7600e_get_gnss_queue(void)
{
    return s_sim7600e_ctx.gnss_queue;
}

SemaphoreHandle_t sim7600e_get_gnss_semaphore(void)
{
    return s_sim7600e_ctx.gnss_ready_sem;
}

int sim7600e_get_uart_port(void)
{
    return s_sim7600e_ctx.config.uart_port;
}

SemaphoreHandle_t sim7600e_get_mutex(void)
{
    return s_sim7600e_ctx.mutex;
}