/**
 * @file mqtt_tls_client.h
 * @brief MQTT TLS client using SIM7600E AT commands
 */

#ifndef MQTT_TLS_CLIENT_H
#define MQTT_TLS_CLIENT_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// MQTT TLS client handle structure
typedef struct {
    char broker_host[64];
    int broker_port;
    char client_id[32];
    char device_mac[13];
    char publish_topic[64];
    char subscribe_topic[64];
    uint32_t heartbeat_counter;
    bool connected;
    TaskHandle_t task_handle;
} mqtt_tls_client_t;

/**
 * @brief Initialize MQTT TLS client
 * @param client Pointer to client structure
 * @param broker_host MQTT broker hostname
 * @param broker_port MQTT broker port (usually 8883 for TLS)
 * @param client_id MQTT client ID
 * @return ESP_OK on success
 */
esp_err_t mqtt_tls_client_init(mqtt_tls_client_t *client, 
                               const char *broker_host, 
                               int broker_port, 
                               const char *client_id);

/**
 * @brief Connect to MQTT broker over TLS
 * @param client Pointer to client structure
 * @return ESP_OK on success
 */
esp_err_t mqtt_tls_client_connect(mqtt_tls_client_t *client);

/**
 * @brief Disconnect from MQTT broker
 * @param client Pointer to client structure
 * @return ESP_OK on success
 */
esp_err_t mqtt_tls_client_disconnect(mqtt_tls_client_t *client);

/**
 * @brief Publish telemetry data over TLS
 * @param client Pointer to client structure
 * @return ESP_OK on success
 */
esp_err_t mqtt_tls_client_publish_telemetry(mqtt_tls_client_t *client);

/**
 * @brief Subscribe to control topic
 * @param client Pointer to client structure
 * @param topic Topic to subscribe to
 * @param qos Quality of Service level
 * @return ESP_OK on success
 */
esp_err_t mqtt_tls_client_subscribe(mqtt_tls_client_t *client, const char *topic, int qos);

/**
 * @brief Start MQTT TLS client task
 * @param client Pointer to client structure
 * @param publish_interval_ms Publish interval in milliseconds
 * @return ESP_OK on success
 */
esp_err_t mqtt_tls_client_start_task(mqtt_tls_client_t *client, uint32_t publish_interval_ms);

/**
 * @brief Stop MQTT TLS client task
 * @param client Pointer to client structure
 * @return ESP_OK on success
 */
esp_err_t mqtt_tls_client_stop_task(mqtt_tls_client_t *client);

/**
 * @brief Check if client is connected
 * @param client Pointer to client structure
 * @return true if connected, false otherwise
 */
bool mqtt_tls_client_is_connected(mqtt_tls_client_t *client);

/**
 * @brief Cleanup MQTT TLS client
 * @param client Pointer to client structure
 */
void mqtt_tls_client_cleanup(mqtt_tls_client_t *client);

#endif // MQTT_TLS_CLIENT_H