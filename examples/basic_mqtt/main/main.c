#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "cJSON.h"
#include "sim7600e.h"
#include "sim7600e_gsm.h"

static const char *TAG = "MQTT_BASIC";

// Configuration
#define PUBLISH_INTERVAL_MS 60000  // 1 minute

// Variables
static char device_mac[13];
static char publish_topic[64];
static char subscribe_topic[32];
static const char client_id[] = "esp32s3";
static uint32_t heartbeat_counter = 0;

// Functions
static void setup_topics(void);
static esp_err_t mqtt_connect(void);
static esp_err_t publish_data(void);
static void mqtt_task(void *pvParameters);

static void setup_topics(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_mac, sizeof(device_mac), "%02X%02X%02X%02X%02X%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(publish_topic, sizeof(publish_topic), "DEME25/08/INPUTS/%s", device_mac);
    snprintf(subscribe_topic, sizeof(subscribe_topic), "DEME25/08/OUTPUT");
    ESP_LOGI(TAG, "Device: %s", device_mac);
}

static esp_err_t mqtt_connect(void) {
    char response[256], command[128];
    
    // Cleanup existing connections
    sim7600e_gsm_send_at_command("AT+CMQTTDISC=0,60\r\n", response, sizeof(response), 2000);
    sim7600e_gsm_send_at_command("AT+CMQTTREL=0\r\n", response, sizeof(response), 2000);
    sim7600e_gsm_send_at_command("AT+CMQTTSTOP\r\n", response, sizeof(response), 2000);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Start MQTT and connect
    if (sim7600e_gsm_send_at_command("AT+CMQTTSTART\r\n", response, sizeof(response), 5000) != ESP_OK) {
        return ESP_FAIL;
    }
    
    snprintf(command, sizeof(command), "AT+CMQTTACCQ=0,\"%s\",0\r\n", client_id);
    if (sim7600e_gsm_send_at_command(command, response, sizeof(response), 5000) != ESP_OK) {
        return ESP_FAIL;
    }
    
    snprintf(command, sizeof(command), "AT+CMQTTCONNECT=0,\"tcp://test.mosquitto.org:1883\",60,0\r\n");
    if (sim7600e_gsm_send_at_command(command, response, sizeof(response), 15000) != ESP_OK) {
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "MQTT connected");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Subscribe to control topic
    return sim7600e_gsm_mqtt_subscribe(subscribe_topic, 1);
}

static esp_err_t publish_data(void) {
    // Get current signal strength
    sim7600e_network_info_t net_info = {0};
    int signal_strength = -999;  // Default value if unavailable
    if (sim7600e_gsm_get_network_info(&net_info) == ESP_OK) {
        signal_strength = net_info.signal_strength;
    }
    
    // Increment heartbeat counter
    heartbeat_counter++;
    
    // Create comprehensive device status JSON
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "mac", device_mac);
    cJSON_AddStringToObject(json, "client_id", client_id);
    cJSON_AddNumberToObject(json, "signal_strength", signal_strength);
    cJSON_AddNumberToObject(json, "heartbeat", heartbeat_counter);
    
    // Add sensor data
    cJSON *sensors = cJSON_CreateObject();
    cJSON_AddNumberToObject(sensors, "D0", 0);
    cJSON_AddNumberToObject(sensors, "D1", 0);
    cJSON_AddNumberToObject(sensors, "D2", 0);
    cJSON_AddNumberToObject(sensors, "D3", 0);
    cJSON_AddItemToObject(json, "sensors", sensors);
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    if (!json_string) return ESP_FAIL;
    
    // Set topic and payload, then publish
    esp_err_t ret = ESP_FAIL;
    if (sim7600e_gsm_mqtt_set_topic(publish_topic) == ESP_OK &&
        sim7600e_gsm_mqtt_set_payload(json_string) == ESP_OK) {
        
        char response[256];
        ret = sim7600e_gsm_send_at_command("AT+CMQTTPUB=0,1,60\r\n", response, sizeof(response), 10000);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Published [#%lu]: %s", heartbeat_counter, json_string);
        }
    }
    
    free(json_string);
    return ret;
}

static void mqtt_task(void *pvParameters) {
    TickType_t last_publish = 0;
    const TickType_t interval = pdMS_TO_TICKS(PUBLISH_INTERVAL_MS);
    
    while (1) {
        if ((xTaskGetTickCount() - last_publish) >= interval) {
            last_publish = xTaskGetTickCount();
            if (publish_data() != ESP_OK) {
                ESP_LOGE(TAG, "Publish failed");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting MQTT Example");
    
    setup_topics();
    
    // Initialize SIM7600E
    sim7600e_config_t config = sim7600e_get_default_config();
    if (sim7600e_init(&config) != ESP_OK || 
        sim7600e_power_on() != ESP_OK) {
        ESP_LOGE(TAG, "SIM7600E init failed");
        return;
    }
    
    vTaskDelay(pdMS_TO_TICKS(10000));  // Wait for initialization
    
    // Check modem and SIM
    if (sim7600e_gsm_check_modem() != ESP_OK || 
        sim7600e_gsm_check_sim() != ESP_OK) {
        ESP_LOGE(TAG, "Modem/SIM check failed");
        return;
    }
    
    // Wait for network and enable internet
    if (sim7600e_gsm_wait_for_network(60000) != ESP_OK ||
        sim7600e_gsm_enable_internet("internet") != ESP_OK) {
        ESP_LOGE(TAG, "Network setup failed");
        return;
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Connect to MQTT and start publishing
    if (mqtt_connect() == ESP_OK) {
        ESP_LOGI(TAG, "MQTT ready - starting publisher");
        xTaskCreate(mqtt_task, "mqtt_task", 8192, NULL, 5, NULL);
    } else {
        ESP_LOGE(TAG, "MQTT connection failed");
        return;
    }
    
    // Monitor loop
    int count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Running: %d", ++count);
        
        if (count % 5 == 0) {
            sim7600e_network_info_t info;
            if (sim7600e_gsm_get_network_info(&info) == ESP_OK) {
                ESP_LOGI(TAG, "Signal: %d dBm", info.signal_strength);
            }
        }
    }
}