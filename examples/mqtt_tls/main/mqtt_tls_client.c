/**
 * @file mqtt_tls_client.c
 * @brief MQTT TLS client using SIM7600E AT commands
 */

#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "mqtt_tls_client.h"
#include "mqtt_tls_config.h"
#include "certificate_manager.h"
#include "sim_manager.h"
#include "sim7600e_gsm.h"

static const char *TAG = "MQTT_TLS_CLIENT";

// Task parameters
#define MQTT_TLS_TASK_STACK_SIZE 8192
#define MQTT_TLS_TASK_PRIORITY 5

// Static function declarations
static void mqtt_tls_task(void *pvParameters);
static esp_err_t setup_device_topics(mqtt_tls_client_t *client);
static esp_err_t create_telemetry_payload(mqtt_tls_client_t *client, char **payload);

esp_err_t mqtt_tls_client_init(mqtt_tls_client_t *client, 
                               const char *broker_host, 
                               int broker_port, 
                               const char *client_id)
{
    if (client == NULL || broker_host == NULL || client_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing MQTT TLS client");
    
    // Initialize client structure
    memset(client, 0, sizeof(mqtt_tls_client_t));
    
    strncpy(client->broker_host, broker_host, sizeof(client->broker_host) - 1);
    client->broker_port = broker_port;
    strncpy(client->client_id, client_id, sizeof(client->client_id) - 1);
    client->heartbeat_counter = 0;
    client->connected = false;
    client->task_handle = NULL;
    
    // Setup device-specific topics
    esp_err_t ret = setup_device_topics(client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup device topics");
        return ret;
    }
    
    ESP_LOGI(TAG, "MQTT TLS client initialized for broker: %s:%d", broker_host, broker_port);
    return ESP_OK;
}

static esp_err_t setup_device_topics(mqtt_tls_client_t *client)
{
    // Get device MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(client->device_mac, sizeof(client->device_mac), 
             "%02X%02X%02X%02X%02X%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // Create MQTT topics
    snprintf(client->publish_topic, sizeof(client->publish_topic), 
             CONFIG_MQTT_INPUT_TOPIC_FMT, client->device_mac);
    strncpy(client->subscribe_topic, CONFIG_MQTT_OUTPUT_TOPIC, 
            sizeof(client->subscribe_topic) - 1);
    
    ESP_LOGI(TAG, "Device MAC: %s", client->device_mac);
    ESP_LOGI(TAG, "Publish topic: %s", client->publish_topic);
    ESP_LOGI(TAG, "Subscribe topic: %s", client->subscribe_topic);
    
    return ESP_OK;
}

esp_err_t mqtt_tls_client_connect(mqtt_tls_client_t *client)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Connecting to MQTT broker over TLS...");
    
    char response[512], command[256];
    esp_err_t ret;
    
    // Cleanup existing connections
    sim7600e_gsm_send_at_command("AT+CMQTTDISC=0,60\\r\\n", response, sizeof(response), 2000);
    sim7600e_gsm_send_at_command("AT+CMQTTREL=0\\r\\n", response, sizeof(response), 2000);
    sim7600e_gsm_send_at_command("AT+CMQTTSTOP\\r\\n", response, sizeof(response), 2000);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Configure SSL context with certificates
    ret = certificate_manager_configure_ssl_context(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SSL configuration failed");
        return ret;
    }
    
    // Start MQTT service
    ret = sim7600e_gsm_send_at_command("AT+CMQTTSTART\\r\\n", response, sizeof(response), 10000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT service");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Acquire MQTT client with SSL context
    snprintf(command, sizeof(command), "AT+CMQTTACCQ=0,\\\"%s\\\",1,0\\r\\n", client->client_id);
    ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), 10000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire MQTT client with TLS");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Connect to MQTT broker over TLS
    snprintf(command, sizeof(command), 
             "AT+CMQTTCONNECT=0,\\\"ssl://%s:%d\\\",%d,1\\r\\n",
             client->broker_host, client->broker_port, CONFIG_MQTT_KEEPALIVE);
    ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), 20000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to MQTT broker over TLS");
        return ret;
    }
    
    client->connected = true;
    ESP_LOGI(TAG, "MQTT TLS connected successfully to %s:%d", 
             client->broker_host, client->broker_port);
    
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Subscribe to control topic
    ret = mqtt_tls_client_subscribe(client, client->subscribe_topic, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to subscribe to control topic");
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t mqtt_tls_client_disconnect(mqtt_tls_client_t *client)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Disconnecting from MQTT broker");
    
    char response[256];
    
    // Disconnect MQTT
    sim7600e_gsm_send_at_command("AT+CMQTTDISC=0,60\\r\\n", response, sizeof(response), 5000);
    
    // Release client
    sim7600e_gsm_send_at_command("AT+CMQTTREL=0\\r\\n", response, sizeof(response), 3000);
    
    // Stop MQTT service
    sim7600e_gsm_send_at_command("AT+CMQTTSTOP\\r\\n", response, sizeof(response), 3000);
    
    client->connected = false;
    ESP_LOGI(TAG, "MQTT TLS disconnected");
    
    return ESP_OK;
}

static esp_err_t create_telemetry_payload(mqtt_tls_client_t *client, char **payload)
{
    // Get current network signal strength
    sim7600e_network_info_t net_info = {0};
    int signal_strength = -999;
    if (sim_manager_get_network_info(&net_info) == ESP_OK) {
        signal_strength = net_info.signal_strength;
    }
    
    // Increment heartbeat counter
    client->heartbeat_counter++;
    
    // Create telemetry JSON payload
    cJSON *telemetry = cJSON_CreateObject();
    if (telemetry == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // Device identification
    cJSON_AddStringToObject(telemetry, "device_mac", client->device_mac);
    cJSON_AddStringToObject(telemetry, "client_id", client->client_id);
    
    // Network information
    cJSON_AddNumberToObject(telemetry, "signal_strength_dbm", signal_strength);
    cJSON_AddStringToObject(telemetry, "operator", net_info.operator_name);
    
    // Connection information
    cJSON_AddStringToObject(telemetry, "transport", "TLS");
    cJSON_AddNumberToObject(telemetry, "port", client->broker_port);
    cJSON_AddNumberToObject(telemetry, "heartbeat", client->heartbeat_counter);
    
    // System information
    cJSON_AddNumberToObject(telemetry, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(telemetry, "uptime_ms", esp_timer_get_time() / 1000);
    
    // Sensor data (placeholder)
    cJSON *sensors = cJSON_CreateObject();
    cJSON_AddNumberToObject(sensors, "D0", 0);
    cJSON_AddNumberToObject(sensors, "D1", 0);
    cJSON_AddNumberToObject(sensors, "D2", 0);
    cJSON_AddNumberToObject(sensors, "D3", 0);
    cJSON_AddItemToObject(telemetry, "sensors", sensors);
    
    *payload = cJSON_Print(telemetry);
    cJSON_Delete(telemetry);
    
    return (*payload != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t mqtt_tls_client_publish_telemetry(mqtt_tls_client_t *client)
{
    if (client == NULL || !client->connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
    char *payload = NULL;
    esp_err_t ret = create_telemetry_payload(client, &payload);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create telemetry payload");
        return ret;
    }
    
    ESP_LOGI(TAG, "Publishing telemetry data: %s", payload);
    
    // Set topic and payload, then publish over TLS
    if (sim7600e_gsm_mqtt_set_topic(client->publish_topic) == ESP_OK &&
        sim7600e_gsm_mqtt_set_payload(payload) == ESP_OK) {
        
        char response[256];
        ret = sim7600e_gsm_send_at_command("AT+CMQTTPUB=0,1,90\\r\\n", response, sizeof(response), 15000);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Telemetry published successfully [#%lu]", client->heartbeat_counter);
        } else {
            ESP_LOGE(TAG, "TLS publish failed");
        }
    } else {
        ESP_LOGE(TAG, "Failed to set topic/payload");
        ret = ESP_FAIL;
    }
    
    free(payload);
    return ret;
}

esp_err_t mqtt_tls_client_subscribe(mqtt_tls_client_t *client, const char *topic, int qos)
{
    if (client == NULL || topic == NULL || !client->connected) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Subscribing to topic: %s (QoS: %d)", topic, qos);
    
    esp_err_t ret = sim7600e_gsm_mqtt_subscribe(topic, qos);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Successfully subscribed to: %s", topic);
    } else {
        ESP_LOGE(TAG, "Failed to subscribe to: %s", topic);
    }
    
    return ret;
}

static void mqtt_tls_task(void *pvParameters)
{
    mqtt_tls_client_t *client = (mqtt_tls_client_t *)pvParameters;
    uint32_t publish_interval_ms = CONFIG_PUBLISH_INTERVAL_MS;
    
    TickType_t last_publish = 0;
    const TickType_t interval = pdMS_TO_TICKS(publish_interval_ms);
    
    ESP_LOGI(TAG, "MQTT TLS task started (interval: %lu ms)", publish_interval_ms);
    
    while (1) {
        if ((xTaskGetTickCount() - last_publish) >= interval) {
            last_publish = xTaskGetTickCount();
            
            if (client->connected) {
                esp_err_t ret = mqtt_tls_client_publish_telemetry(client);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Telemetry publish failed, attempting reconnect...");
                    
                    // Attempt to reconnect
                    client->connected = false;
                    vTaskDelay(pdMS_TO_TICKS(5000)); // Wait before reconnect
                    
                    if (mqtt_tls_client_connect(client) == ESP_OK) {
                        ESP_LOGI(TAG, "Reconnected successfully");
                    }
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t mqtt_tls_client_start_task(mqtt_tls_client_t *client, uint32_t publish_interval_ms)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (client->task_handle != NULL) {
        ESP_LOGW(TAG, "Task already running");
        return ESP_ERR_INVALID_STATE;
    }
    
    BaseType_t ret = xTaskCreate(mqtt_tls_task, 
                                "mqtt_tls_task", 
                                MQTT_TLS_TASK_STACK_SIZE, 
                                client, 
                                MQTT_TLS_TASK_PRIORITY, 
                                &client->task_handle);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT TLS task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "MQTT TLS task started successfully");
    return ESP_OK;
}

esp_err_t mqtt_tls_client_stop_task(mqtt_tls_client_t *client)
{
    if (client == NULL || client->task_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vTaskDelete(client->task_handle);
    client->task_handle = NULL;
    
    ESP_LOGI(TAG, "MQTT TLS task stopped");
    return ESP_OK;
}

bool mqtt_tls_client_is_connected(mqtt_tls_client_t *client)
{
    return (client != NULL) ? client->connected : false;
}

void mqtt_tls_client_cleanup(mqtt_tls_client_t *client)
{
    if (client == NULL) {
        return;
    }
    
    // Stop task if running
    if (client->task_handle != NULL) {
        mqtt_tls_client_stop_task(client);
    }
    
    // Disconnect if connected
    if (client->connected) {
        mqtt_tls_client_disconnect(client);
    }
    
    // Clear client structure
    memset(client, 0, sizeof(mqtt_tls_client_t));
    
    ESP_LOGI(TAG, "MQTT TLS client cleanup completed");
}