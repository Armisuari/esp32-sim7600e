#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "cJSON.h"

// Include the SIM7600E component headers
#include "sim7600e.h"
#include "sim7600e_gsm.h"

static const char *TAG = "MQTT_AT_EXAMPLE";

// MQTT and Network Configuration (based on Arduino reference)
#define MQTT_BROKER_HOST    "test.mosquitto.org"
#define MQTT_BROKER_PORT    "1883"
#define APN_NAME            "internet"  // Change to your carrier's APN
#define MQTT_CLIENT_ID      "esp32s3_client_test"
#define MQTT_USERNAME       ""  // Update with your credentials
#define MQTT_PASSWORD       ""  // Update with your credentials

// GPIO Configuration (matching Arduino reference)
#define GPIO_D0    GPIO_NUM_36
#define GPIO_D1    GPIO_NUM_35  
#define GPIO_D2    GPIO_NUM_32
#define GPIO_D3    GPIO_NUM_33
#define GPIO_R0    GPIO_NUM_4

// Timing configuration
#define PUBLISH_INTERVAL_MS     60000  // 1 minute
#define MQTT_TIMEOUT_MS         10000

// Device specific
static char device_mac_str[13];  // 12 chars + null terminator
static char uplink_topic[64];
static char downlink_topic[64];

// Function declarations
static esp_err_t init_network(void);
static esp_err_t connect_to_gprs(void);
static esp_err_t connect_to_mqtt(void);
static esp_err_t publish_sensor_data(void);
static esp_err_t handle_incoming_messages(void);
static esp_err_t gsm_send_at_command(const char* command, char* response, size_t response_size, uint32_t timeout_ms);
static void get_mac_address_string(void);
static void init_gpio(void);
static void mqtt_task(void *pvParameters);
static bool is_network_connected(void);
static bool is_gprs_connected(void);
static void handle_mqtt_callback(const char* topic, const char* payload, int payload_len);

// Get ESP32 MAC address as string (similar to Arduino WiFi.macAddress)
static void get_mac_address_string(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    snprintf(device_mac_str, sizeof(device_mac_str), 
             "%02X%02X%02X%02X%02X%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // Create topic strings
    snprintf(uplink_topic, sizeof(uplink_topic), "NORVI/INPUTS/%s", device_mac_str);
    snprintf(downlink_topic, sizeof(downlink_topic), "NORVI/+/OUTPUT");
    
    ESP_LOGI(TAG, "Device MAC: %s", device_mac_str);
    ESP_LOGI(TAG, "Uplink topic: %s", uplink_topic);
    ESP_LOGI(TAG, "Downlink topic: %s", downlink_topic);
}

// Initialize GPIO pins
static void init_gpio(void) {
    // Configure input pins
    gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << GPIO_D0) | (1ULL << GPIO_D1) | (1ULL << GPIO_D2) | (1ULL << GPIO_D3),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&input_config);
    
    // Configure output pin
    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << GPIO_R0),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&output_config);
    
    // Set initial output state
    gpio_set_level(GPIO_R0, 0);
    
    ESP_LOGI(TAG, "GPIO initialized");
}

// Send AT command and get response
static esp_err_t gsm_send_at_command(const char* command, char* response, size_t response_size, uint32_t timeout_ms) {
    ESP_LOGI(TAG, "Send -> %s", command);
    
    esp_err_t ret = sim7600e_gsm_send_at_command(command, response, response_size, timeout_ms);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Response: %s", response);
    } else {
        ESP_LOGE(TAG, "AT command failed: %s", command);
    }
    
    return ret;
}

// Initialize network connection (equivalent to Arduino Init function)
static esp_err_t init_network(void) {
    ESP_LOGI(TAG, "Initializing network connection...");
    
    char response[256];
    esp_err_t ret;
    
    // Wait for module to be ready
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Set phone functionality
    ret = gsm_send_at_command("AT+CFUN=1\r\n", response, sizeof(response), 10000);
    if (ret != ESP_OK) return ret;
    
    // Check PIN status
    ret = gsm_send_at_command("AT+CPIN?\r\n", response, sizeof(response), 10000);
    if (ret != ESP_OK) return ret;
    
    // Signal quality report
    ret = gsm_send_at_command("AT+CSQ\r\n", response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Network registration status
    ret = gsm_send_at_command("AT+CREG?\r\n", response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Operator selection
    ret = gsm_send_at_command("AT+COPS?\r\n", response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // GPRS attach status
    ret = gsm_send_at_command("AT+CGATT?\r\n", response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Serving cell information
    ret = gsm_send_at_command("AT+CPSI?\r\n", response, sizeof(response), 500);
    if (ret != ESP_OK) return ret;
    
    // Define PDP context
    char pdp_cmd[128];
    snprintf(pdp_cmd, sizeof(pdp_cmd), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", APN_NAME);
    ret = gsm_send_at_command(pdp_cmd, response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Activate PDP context
    ret = gsm_send_at_command("AT+CGACT=1,1\r\n", response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Check GPRS attach status again
    ret = gsm_send_at_command("AT+CGATT?\r\n", response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Show PDP address
    ret = gsm_send_at_command("AT+CGPADDR=1\r\n", response, sizeof(response), 500);
    if (ret != ESP_OK) return ret;
    
    // Open network service
    ret = gsm_send_at_command("AT+NETOPEN\r\n", response, sizeof(response), 500);
    if (ret != ESP_OK) return ret;
    
    // Query network state
    ret = gsm_send_at_command("AT+NETSTATE\r\n", response, sizeof(response), 500);
    if (ret != ESP_OK) return ret;
    
    ESP_LOGI(TAG, "Network initialization completed");
    return ESP_OK;
}

// Connect to GPRS (additional step if needed)
static esp_err_t connect_to_gprs(void) {
    ESP_LOGI(TAG, "Connecting to GPRS...");
    
    char response[256];
    esp_err_t ret;
    
    ret = gsm_send_at_command("AT+CGATT=1\r\n", response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    char pdp_cmd[128];
    snprintf(pdp_cmd, sizeof(pdp_cmd), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", APN_NAME);
    ret = gsm_send_at_command(pdp_cmd, response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    ret = gsm_send_at_command("AT+CGACT=1,1\r\n", response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    ret = gsm_send_at_command("AT+CGPADDR=1\r\n", response, sizeof(response), 500);
    if (ret != ESP_OK) return ret;
    
    ret = gsm_send_at_command("AT+NETOPEN\r\n", response, sizeof(response), 500);
    if (ret != ESP_OK) return ret;
    
    ret = gsm_send_at_command("AT+NETSTATE\r\n", response, sizeof(response), 500);
    if (ret != ESP_OK) return ret;
    
    ESP_LOGI(TAG, "GPRS connection established");
    return ESP_OK;
}

// Connect to MQTT broker using AT commands (equivalent to Arduino connectToMQTT)
static esp_err_t connect_to_mqtt(void) {
    ESP_LOGI(TAG, "Connecting to MQTT broker...");
    
    char response[512];
    char command[256];
    esp_err_t ret;
    
    // Start MQTT service
    ret = gsm_send_at_command("AT+CMQTTSTART\r\n", response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Acquire a client
    snprintf(command, sizeof(command), "AT+CMQTTACCQ=0,\"%s\",0\r\n", MQTT_CLIENT_ID);
    ret = gsm_send_at_command(command, response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Set will topic
    ret = gsm_send_at_command("AT+CMQTTWILLTOPIC=0,2\r\n", response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Send will topic data
    ret = gsm_send_at_command("01\x1A", response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Set will message
    ret = gsm_send_at_command("AT+CMQTTWILLMSG=0,6,1\r\n", response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Send will message data
    ret = gsm_send_at_command("qwerty\x1A", response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Connect to MQTT server
    snprintf(command, sizeof(command), 
             "AT+CMQTTCONNECT=0,\"tcp://%s:%s\",60,1,\"%s\",\"%s\"\r\n",
             MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_USERNAME, MQTT_PASSWORD);
    ret = gsm_send_at_command(command, response, sizeof(response), 10000);
    if (ret != ESP_OK) return ret;
    
    // Wait for connection to establish
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Subscribe to downlink topic
    int topic_len = strlen(downlink_topic);
    snprintf(command, sizeof(command), "AT+CMQTTSUB=0,%d,1\r\n", topic_len);
    ret = gsm_send_at_command(command, response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Send subscription topic
    snprintf(command, sizeof(command), "%s\x1A", downlink_topic);
    ret = gsm_send_at_command(command, response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "MQTT connection established and subscribed to %s", downlink_topic);
    return ESP_OK;
}

// Publish sensor data (equivalent to Arduino main loop publishing)
static esp_err_t publish_sensor_data(void) {
    char response[512];
    char command[256];
    char json_payload[256];
    esp_err_t ret;
    
    // Read GPIO inputs
    int d0_state = gpio_get_level(GPIO_D0);
    int d1_state = gpio_get_level(GPIO_D1);
    int d2_state = gpio_get_level(GPIO_D2);
    int d3_state = gpio_get_level(GPIO_D3);
    
    // Create JSON payload
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_FAIL;
    }
    
    cJSON_AddNumberToObject(json, "D0", d0_state);
    cJSON_AddNumberToObject(json, "D1", d1_state);
    cJSON_AddNumberToObject(json, "D2", d2_state);
    cJSON_AddNumberToObject(json, "D3", d3_state);
    
    char *json_string = cJSON_Print(json);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to convert JSON to string");
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    
    // Remove formatting from JSON (make it compact)
    strncpy(json_payload, json_string, sizeof(json_payload) - 1);
    json_payload[sizeof(json_payload) - 1] = '\0';
    
    free(json_string);
    cJSON_Delete(json);
    
    ESP_LOGI(TAG, "Publishing sensor data: %s", json_payload);
    
    // Set MQTT topic
    int topic_len = strlen(uplink_topic);
    snprintf(command, sizeof(command), "AT+CMQTTTOPIC=0,%d\r\n", topic_len);
    ret = gsm_send_at_command(command, response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Send topic
    ret = gsm_send_at_command(uplink_topic, response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Set MQTT payload
    int payload_len = strlen(json_payload);
    snprintf(command, sizeof(command), "AT+CMQTTPAYLOAD=0,%d\r\n", payload_len);
    ret = gsm_send_at_command(command, response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Send payload
    snprintf(command, sizeof(command), "%s\x1A", json_payload);
    ret = gsm_send_at_command(command, response, sizeof(response), 1000);
    if (ret != ESP_OK) return ret;
    
    // Publish message
    ret = gsm_send_at_command("AT+CMQTTPUB=0,1,60\r\n", response, sizeof(response), 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT publish failed, trying to reconnect...");
        // Try to reconnect
        if (!is_gprs_connected()) {
            ESP_LOGI(TAG, "GPRS connection lost, reconnecting...");
            connect_to_gprs();
            connect_to_mqtt();
        }
        if (!is_network_connected()) {
            ESP_LOGI(TAG, "Network connection lost, reconnecting...");
            init_network();
            connect_to_gprs();
            connect_to_mqtt();
        }
        return ret;
    }
    
    ESP_LOGI(TAG, "Sensor data published successfully");
    return ESP_OK;
}

// Handle incoming MQTT messages
static esp_err_t handle_incoming_messages(void) {
    // This would require parsing the SIM7600E UART buffer for incoming MQTT messages
    // For now, we'll implement a simple version that checks for incoming data
    
    // In a full implementation, you would:
    // 1. Check UART for incoming data
    // 2. Parse +CMQTTRXTOPIC and +CMQTTRXPAYLOAD responses
    // 3. Extract topic and payload
    // 4. Call handle_mqtt_callback
    
    // For this example, we'll just return OK
    return ESP_OK;
}

// Handle MQTT callback (equivalent to Arduino mqttCallback)
static void handle_mqtt_callback(const char* topic, const char* payload, int payload_len) {
    ESP_LOGI(TAG, "Message arrived [%s]: %.*s", topic, payload_len, payload);
    
    // Extract MAC ID from topic (NORVI/<MAC>/OUTPUT)
    char mac_id[16] = {0};
    const char* first_slash = strchr(topic, '/');
    if (first_slash != NULL) {
        const char* second_slash = strchr(first_slash + 1, '/');
        if (second_slash != NULL) {
            int mac_len = second_slash - first_slash - 1;
            if (mac_len > 0 && mac_len < sizeof(mac_id)) {
                strncpy(mac_id, first_slash + 1, mac_len);
                mac_id[mac_len] = '\0';
            }
        }
    }
    
    ESP_LOGI(TAG, "MAC ID: %s", mac_id);
    
    // Check if message is for this device
    if (strcmp(mac_id, device_mac_str) == 0) {
        // Parse JSON payload
        cJSON *json = cJSON_ParseWithLength(payload, payload_len);
        if (json == NULL) {
            ESP_LOGE(TAG, "Failed to parse JSON payload");
            return;
        }
        
        cJSON *state_item = cJSON_GetObjectItem(json, "state");
        if (cJSON_IsNumber(state_item)) {
            int state = (int)cJSON_GetNumberValue(state_item);
            ESP_LOGI(TAG, "Setting relay state to: %d", state);
            
            gpio_set_level(GPIO_R0, state ? 1 : 0);
        }
        
        cJSON_Delete(json);
    } else {
        ESP_LOGI(TAG, "Received message for a different device");
    }
}

// Check if network is connected
static bool is_network_connected(void) {
    char response[256];
    esp_err_t ret = gsm_send_at_command("AT+CREG?\r\n", response, sizeof(response), 3000);
    if (ret == ESP_OK) {
        return (strstr(response, "+CREG: 0,1") != NULL || strstr(response, "+CREG: 0,5") != NULL);
    }
    return false;
}

// Check if GPRS is connected
static bool is_gprs_connected(void) {
    char response[256];
    esp_err_t ret = gsm_send_at_command("AT+CGATT?\r\n", response, sizeof(response), 3000);
    if (ret == ESP_OK) {
        return (strstr(response, "+CGATT: 1") != NULL);
    }
    return false;
}

// MQTT task (equivalent to Arduino loop)
static void mqtt_task(void *pvParameters) {
    TickType_t last_publish_time = 0;
    const TickType_t publish_interval = pdMS_TO_TICKS(PUBLISH_INTERVAL_MS);
    
    while (1) {
        TickType_t current_time = xTaskGetTickCount();
        
        // Publish sensor data every PUBLISH_INTERVAL_MS
        if (current_time - last_publish_time >= publish_interval) {
            last_publish_time = current_time;
            
            esp_err_t ret = publish_sensor_data();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to publish sensor data");
            }
        }
        
        // Handle incoming messages
        handle_incoming_messages();
        
        // Small delay to prevent busy waiting
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting ESP32-S3 SIM7600E MQTT AT Commands Example");
    
    // Get MAC address and setup topics
    get_mac_address_string();
    
    // Initialize GPIO
    init_gpio();
    
    // Initialize SIM7600E with default config
    sim7600e_config_t config = sim7600e_get_default_config();
    esp_err_t ret = sim7600e_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SIM7600E: %s", esp_err_to_name(ret));
        return;
    }
    
    // Power on the module
    ESP_LOGI(TAG, "Powering on SIM7600E module...");
    ret = sim7600e_power_on();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to power on SIM7600E: %s", esp_err_to_name(ret));
        return;
    }
    
    // Wait for module initialization
    ESP_LOGI(TAG, "Waiting for module initialization...");
    vTaskDelay(pdMS_TO_TICKS(10000));
    
    // Check modem
    ret = sim7600e_gsm_check_modem();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Modem not responding: %s", esp_err_to_name(ret));
        return;
    }
    
    // Check SIM card
    ESP_LOGI(TAG, "Checking SIM card...");
    ret = sim7600e_gsm_check_sim();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SIM card not detected: %s", esp_err_to_name(ret));
        return;
    }
    
    // Initialize network connection
    ret = init_network();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize network: %s", esp_err_to_name(ret));
        return;
    }
    
    // Connect to GPRS
    ret = connect_to_gprs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to GPRS: %s", esp_err_to_name(ret));
        return;
    }
    
    // Connect to MQTT
    ret = connect_to_mqtt();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to MQTT: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "MQTT connection established successfully!");
    ESP_LOGI(TAG, "Publishing to: %s", uplink_topic);
    ESP_LOGI(TAG, "Subscribed to: %s", downlink_topic);
    
    // Start MQTT task
    xTaskCreate(mqtt_task, "mqtt_task", 8192, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "MQTT AT Commands example running!");
    
    // Main loop - keep the application running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Main loop - MQTT example running...");
    }
}