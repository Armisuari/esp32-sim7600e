#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

// Include the SIM7600E component headers
#include "sim7600e.h"
#include "sim7600e_gsm.h"
#include "sim7600e_tcp.h"

static const char *TAG = "MQTT_EXAMPLE";

// MQTT Configuration
#define MQTT_BROKER_HOST    "test.mosquitto.org"
#define MQTT_BROKER_PORT    1883
#define MQTT_CLIENT_ID      "esp32s3_sim7600e"
#define MQTT_PUBLISH_TOPIC  "deme25/esp32s3/sim7600e/data"
#define MQTT_SUBSCRIBE_TOPIC "deme25/esp32s3/sim7600e/cmd"

// Event group bits for MQTT events
#define MQTT_CONNECTED_BIT    BIT0
#define MQTT_SUBSCRIBED_BIT   BIT1

// Simple MQTT implementation over TCP
typedef struct {
    sim7600e_tcp_config_t tcp_config;
    char client_id[64];
    char publish_topic[128];
    char subscribe_topic[128];
    bool connected;
} mqtt_simple_client_t;

static mqtt_simple_client_t mqtt_simple_client = {0};

// Simple MQTT packet types
#define MQTT_CONNECT     0x10
#define MQTT_CONNACK     0x20
#define MQTT_PUBLISH     0x30
#define MQTT_SUBSCRIBE   0x82
#define MQTT_SUBACK      0x90
#define MQTT_PINGREQ     0xC0
#define MQTT_PINGRESP    0xD0

// Helper function to encode remaining length
static int encode_remaining_length(uint8_t *buf, int length) {
    int encoded = 0;
    do {
        uint8_t byte = length % 128;
        length /= 128;
        if (length > 0) {
            byte |= 0x80;
        }
        buf[encoded++] = byte;
    } while (length > 0);
    return encoded;
}

// Helper function to create MQTT CONNECT packet
static int create_connect_packet(uint8_t *buf, const char *client_id) {
    int pos = 0;
    
    // Fixed header
    buf[pos++] = MQTT_CONNECT;
    
    // Calculate payload length
    int client_id_len = strlen(client_id);
    int payload_len = 10 + 2 + client_id_len; // Protocol name + version + flags + keep alive + client ID
    
    // Remaining length
    pos += encode_remaining_length(&buf[pos], payload_len);
    
    // Variable header
    // Protocol name "MQTT"
    buf[pos++] = 0x00; buf[pos++] = 0x04;
    buf[pos++] = 'M'; buf[pos++] = 'Q'; buf[pos++] = 'T'; buf[pos++] = 'T';
    
    // Protocol level (4 for MQTT 3.1.1)
    buf[pos++] = 0x04;
    
    // Connect flags (clean session)
    buf[pos++] = 0x02;
    
    // Keep alive (60 seconds)
    buf[pos++] = 0x00; buf[pos++] = 0x3C;
    
    // Payload - Client ID
    buf[pos++] = (client_id_len >> 8) & 0xFF;
    buf[pos++] = client_id_len & 0xFF;
    memcpy(&buf[pos], client_id, client_id_len);
    pos += client_id_len;
    
    return pos;
}

// Helper function to create MQTT PUBLISH packet
static int create_publish_packet(uint8_t *buf, const char *topic, const char *payload) {
    int pos = 0;
    
    // Fixed header
    buf[pos++] = MQTT_PUBLISH;
    
    // Calculate payload length
    int topic_len = strlen(topic);
    int payload_len = strlen(payload);
    int packet_len = 2 + topic_len + payload_len;
    
    // Remaining length
    pos += encode_remaining_length(&buf[pos], packet_len);
    
    // Variable header - Topic name
    buf[pos++] = (topic_len >> 8) & 0xFF;
    buf[pos++] = topic_len & 0xFF;
    memcpy(&buf[pos], topic, topic_len);
    pos += topic_len;
    
    // Payload
    memcpy(&buf[pos], payload, payload_len);
    pos += payload_len;
    
    return pos;
}

// Helper function to create MQTT SUBSCRIBE packet
static int create_subscribe_packet(uint8_t *buf, const char *topic, uint16_t packet_id) {
    int pos = 0;
    
    // Fixed header
    buf[pos++] = MQTT_SUBSCRIBE;
    
    // Calculate payload length
    int topic_len = strlen(topic);
    int packet_len = 2 + 2 + topic_len + 1; // packet ID + topic length + topic + QoS
    
    // Remaining length
    pos += encode_remaining_length(&buf[pos], packet_len);
    
    // Variable header - Packet ID
    buf[pos++] = (packet_id >> 8) & 0xFF;
    buf[pos++] = packet_id & 0xFF;
    
    // Payload - Topic filter
    buf[pos++] = (topic_len >> 8) & 0xFF;
    buf[pos++] = topic_len & 0xFF;
    memcpy(&buf[pos], topic, topic_len);
    pos += topic_len;
    
    // QoS level
    buf[pos++] = 0x00; // QoS 0
    
    return pos;
}

// Simple MQTT connect function
static esp_err_t mqtt_simple_connect(const char *broker_host, uint16_t broker_port, const char *client_id) {
    // Configure TCP connection with longer timeout
    strcpy(mqtt_simple_client.tcp_config.host, broker_host);
    mqtt_simple_client.tcp_config.port = broker_port;
    mqtt_simple_client.tcp_config.timeout_ms = 30000; // Increased timeout
    
    strcpy(mqtt_simple_client.client_id, client_id);
    strcpy(mqtt_simple_client.publish_topic, MQTT_PUBLISH_TOPIC);
    strcpy(mqtt_simple_client.subscribe_topic, MQTT_SUBSCRIBE_TOPIC);
    
    ESP_LOGI(TAG, "Connecting to MQTT broker %s:%d", broker_host, broker_port);
    
    // Establish TCP connection
    esp_err_t ret = sim7600e_tcp_connect(&mqtt_simple_client.tcp_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to MQTT broker");
        return ret;
    }
    
    ESP_LOGI(TAG, "TCP connection established, sending MQTT CONNECT");
    
    // Create and send CONNECT packet
    uint8_t connect_packet[256];
    int packet_len = create_connect_packet(connect_packet, client_id);
    
    ret = sim7600e_tcp_send(connect_packet, packet_len, 10000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send MQTT CONNECT packet");
        sim7600e_tcp_disconnect();
        return ret;
    }
    
    ESP_LOGI(TAG, "MQTT CONNECT packet sent (%d bytes)", packet_len);
    
    // Wait for CONNACK
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // For this simple example, we'll assume connection is successful
    // In a full implementation, you would parse the CONNACK response
    mqtt_simple_client.connected = true;
    
    ESP_LOGI(TAG, "MQTT connection established!");
    return ESP_OK;
}

// Simple MQTT publish function
static esp_err_t mqtt_simple_publish(const char *topic, const char *payload) {
    if (!mqtt_simple_client.connected) {
        ESP_LOGE(TAG, "MQTT not connected");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Publishing to topic '%s': %s", topic, payload);
    
    uint8_t publish_packet[512];
    int packet_len = create_publish_packet(publish_packet, topic, payload);
    
    esp_err_t ret = sim7600e_tcp_send(publish_packet, packet_len, 10000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send MQTT PUBLISH packet");
        return ret;
    }
    
    ESP_LOGI(TAG, "MQTT PUBLISH packet sent (%d bytes)", packet_len);
    return ESP_OK;
}

// Simple MQTT subscribe function
static esp_err_t mqtt_simple_subscribe(const char *topic) {
    if (!mqtt_simple_client.connected) {
        ESP_LOGE(TAG, "MQTT not connected");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Subscribing to topic '%s'", topic);
    
    uint8_t subscribe_packet[256];
    static uint16_t packet_id = 1;
    int packet_len = create_subscribe_packet(subscribe_packet, topic, packet_id++);
    
    esp_err_t ret = sim7600e_tcp_send(subscribe_packet, packet_len, 10000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send MQTT SUBSCRIBE packet");
        return ret;
    }
    
    ESP_LOGI(TAG, "MQTT SUBSCRIBE packet sent (%d bytes)", packet_len);
    return ESP_OK;
}

// MQTT task to handle periodic publishing and message receiving
static void mqtt_task(void *pvParameters) {
    int message_count = 0;
    char message[256];
    esp_err_t ret;
    
    while (1) {
        // Check if MQTT is connected, if not try to reconnect
        if (!mqtt_simple_client.connected) {
            ESP_LOGI(TAG, "MQTT not connected, attempting reconnection...");
            ret = mqtt_simple_connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_CLIENT_ID);
            if (ret == ESP_OK) {
                // Subscribe to command topic after successful connection
                ret = mqtt_simple_subscribe(MQTT_SUBSCRIBE_TOPIC);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Successfully reconnected and subscribed");
                }
            } else {
                ESP_LOGW(TAG, "MQTT reconnection failed, will retry in 30 seconds");
                vTaskDelay(pdMS_TO_TICKS(30000));
                continue;
            }
        }
        
        if (mqtt_simple_client.connected) {
            // Get current network info for richer message content
            sim7600e_network_info_t network_info;
            ret = sim7600e_gsm_get_network_info(&network_info);
            
            if (ret == ESP_OK) {
                snprintf(message, sizeof(message), 
                         "{\"msg_id\":%d,\"operator\":\"%s\",\"signal\":%d,\"time\":\"%s\",\"status\":\"online\"}", 
                         ++message_count, network_info.operator_name, 
                         network_info.signal_strength, network_info.network_time);
            } else {
                snprintf(message, sizeof(message), 
                         "{\"msg_id\":%d,\"status\":\"online\",\"uptime\":%lu}", 
                         ++message_count, (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000));
            }
            
            ESP_LOGI(TAG, "Publishing: %s", message);
            ret = mqtt_simple_publish(mqtt_simple_client.publish_topic, message);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to publish message, marking as disconnected");
                mqtt_simple_client.connected = false;
            }
            
            // In a full implementation, you would also check for incoming messages here
            ESP_LOGI(TAG, "Listening for commands on '%s'", mqtt_simple_client.subscribe_topic);
        }
        
        vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds
    }
}

void app_main(void) 
{
    ESP_LOGI(TAG, "Starting ESP32-S3 SIM7600E MQTT Example");
    
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
        ESP_LOGE(TAG, "Failed to check modem: %s", esp_err_to_name(ret));
        return;
    }
    
    // Check SIM card
    ESP_LOGI(TAG, "Checking SIM card...");
    ret = sim7600e_gsm_check_sim();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SIM card check failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Get IMEI (if available)
    char imei[20];
    ret = sim7600e_gsm_send_at_command("AT+GSN", imei, sizeof(imei), 5000);
    if (ret == ESP_OK && strlen(imei) > 0) {
        ESP_LOGI(TAG, "Module IMEI: %s", imei);
    } else {
        ESP_LOGW(TAG, "Failed to get IMEI: %s", esp_err_to_name(ret));
    }
    
    // Wait for network registration
    ESP_LOGI(TAG, "Waiting for network registration...");
    ret = sim7600e_gsm_wait_for_network(120000); // 2 minutes timeout
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Network registration failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Get network information
    sim7600e_network_info_t network_info;
    ret = sim7600e_gsm_get_network_info(&network_info);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Operator: %s, Signal: %d dBm", 
                 network_info.operator_name, network_info.signal_strength);
        ESP_LOGI(TAG, "Network time: %s", network_info.network_time);
    }
    
    // Enable internet connection
    ESP_LOGI(TAG, "Enabling internet connection...");
    ret = sim7600e_gsm_enable_internet("internet"); // Common APN for many carriers
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable internet: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Internet connection established");
    
    // Wait longer for network to stabilize (cellular connections need more time)
    ESP_LOGI(TAG, "Waiting for network stabilization...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Check connection status before MQTT
    char ip_info[128];
    ret = sim7600e_gsm_send_at_command("AT+CGPADDR=1\r\n", ip_info, sizeof(ip_info), 5000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Current IP address: %s", ip_info);
    }
    
    // Connect to MQTT broker
    ESP_LOGI(TAG, "Connecting to MQTT broker...");
    ret = mqtt_simple_connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_CLIENT_ID);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to MQTT broker: %s", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Retrying MQTT connection in 10 seconds...");
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        // Try once more with different timeout
        ESP_LOGI(TAG, "Attempting second MQTT connection...");
        ret = mqtt_simple_connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_CLIENT_ID);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Second MQTT connection attempt also failed: %s", esp_err_to_name(ret));
            ESP_LOGI(TAG, "Continuing with periodic reconnect attempts...");
        }
    }
    
    // Subscribe to command topic
    ESP_LOGI(TAG, "Subscribing to command topic...");
    ret = mqtt_simple_subscribe(MQTT_SUBSCRIBE_TOPIC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to subscribe to topic: %s", esp_err_to_name(ret));
    }
    
    // Publish initial message
    ESP_LOGI(TAG, "Publishing initial message...");
    ret = mqtt_simple_publish(MQTT_PUBLISH_TOPIC, "ESP32-S3 SIM7600E MQTT Example Started!");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to publish initial message: %s", esp_err_to_name(ret));
    }
    
    // Start MQTT task for periodic publishing
    xTaskCreate(mqtt_task, "mqtt_task", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "MQTT example running!");
    ESP_LOGI(TAG, "Publishing to: %s", MQTT_PUBLISH_TOPIC);
    ESP_LOGI(TAG, "Subscribed to: %s", MQTT_SUBSCRIBE_TOPIC);
    ESP_LOGI(TAG, "You can monitor messages using:");
    ESP_LOGI(TAG, "  mosquitto_sub -h %s -t \"%s\"", MQTT_BROKER_HOST, MQTT_PUBLISH_TOPIC);
    ESP_LOGI(TAG, "  mosquitto_pub -h %s -t \"%s\" -m \"Your message\"", MQTT_BROKER_HOST, MQTT_SUBSCRIBE_TOPIC);
    
    // Main loop
    while (1) {
        // Display network status periodically
        ret = sim7600e_gsm_get_network_info(&network_info);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Status: %s, Signal: %d dBm, Time: %s", 
                     network_info.operator_name, network_info.signal_strength, network_info.network_time);
        }
        
        vTaskDelay(pdMS_TO_TICKS(60000)); // Status update every minute
    }
}