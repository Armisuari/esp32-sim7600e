/**
 * @file main.c
 * @brief ESP32-S3 SIM7600E MQTT TLS client application
 *
 * This application demonstrates secure MQTT connectivity using:
 * - SIM7600E cellular module with TLS/SSL support
 * - X.509 certificate-based authentication
 * - Modular architecture with separate managers
 * - Robust error handling and reconnection logic
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

// Project modules
#include "mqtt_tls_config.h"
#include "sim_manager.h"
#include "certificate_manager.h"
#include "mqtt_tls_client.h"

static const char *TAG = "MQTT_TLS_MAIN";

// Global client instance
static mqtt_tls_client_t g_mqtt_client;

/**
 * @brief Initialize NVS flash storage
 */
static esp_err_t init_nvs(void)
{
    ESP_LOGI(TAG, "Initializing NVS flash storage");
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "Erasing NVS and retrying initialization");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "NVS flash storage initialized successfully");
    return ESP_OK;
}

/**
 * @brief Initialize all system components
 */
static esp_err_t init_system_components(void)
{
    esp_err_t ret;
    
    // Initialize NVS flash
    ret = init_nvs();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Initialize certificate manager
    ESP_LOGI(TAG, "Initializing certificate manager...");
    ret = certificate_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Certificate manager initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize SIM manager
    ESP_LOGI(TAG, "Initializing SIM manager...");
    ret = sim_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SIM manager initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Connect to cellular network
    ESP_LOGI(TAG, "Connecting to cellular network...");
    ret = sim_manager_connect(CONFIG_CELLULAR_APN, CONFIG_CELLULAR_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cellular connection failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize MQTT TLS client
    ESP_LOGI(TAG, "Initializing MQTT TLS client...");
    ret = mqtt_tls_client_init(&g_mqtt_client,
                               CONFIG_MQTT_TLS_BROKER_HOST,
                               CONFIG_MQTT_TLS_BROKER_PORT,
                               CONFIG_MQTT_TLS_CLIENT_ID);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT TLS client initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "All system components initialized successfully");
    return ESP_OK;
}

/**
 * @brief Monitor system status and network health
 */
static void system_monitor_task(void *pvParameters)
{
    const TickType_t monitor_interval = pdMS_TO_TICKS(30000); // 30 seconds
    uint32_t loop_count = 0;
    
    ESP_LOGI(TAG, "System monitor task started");
    
    while (1) {
        loop_count++;
        
        // Log system status
        ESP_LOGI(TAG, "=== System Status [Loop: %lu] ===", loop_count);
        ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "Uptime: %llu ms", esp_timer_get_time() / 1000);
        
        // Check cellular connection
        if (sim_manager_is_connected()) {
            sim7600e_network_info_t net_info;
            if (sim_manager_get_network_info(&net_info) == ESP_OK) {
                ESP_LOGI(TAG, "Cellular: Connected to %s (Signal: %d dBm)", 
                         net_info.operator_name, net_info.signal_strength);
            }
        } else {
            ESP_LOGW(TAG, "Cellular: Disconnected");
        }
        
        // Check MQTT connection
        if (mqtt_tls_client_is_connected(&g_mqtt_client)) {
            ESP_LOGI(TAG, "MQTT TLS: Connected to %s:%d", 
                     CONFIG_MQTT_TLS_BROKER_HOST, CONFIG_MQTT_TLS_BROKER_PORT);
        } else {
            ESP_LOGW(TAG, "MQTT TLS: Disconnected");
        }
        
        ESP_LOGI(TAG, "================================");
        
        vTaskDelay(monitor_interval);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "ESP32-S3 SIM7600E MQTT TLS Client Starting...");
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "Broker: %s:%d", CONFIG_MQTT_TLS_BROKER_HOST, CONFIG_MQTT_TLS_BROKER_PORT);
    ESP_LOGI(TAG, "Client: %s", CONFIG_MQTT_TLS_CLIENT_ID);
    ESP_LOGI(TAG, "APN: %s", CONFIG_CELLULAR_APN);
    ESP_LOGI(TAG, "================================================");
    
    // Initialize all system components
    esp_err_t ret = init_system_components();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "System initialization failed, restarting in 30 seconds...");
        vTaskDelay(pdMS_TO_TICKS(30000));
        esp_restart();
        return;
    }
    
    // Connect to MQTT broker
    ESP_LOGI(TAG, "Connecting to MQTT broker over TLS...");
    ret = mqtt_tls_client_connect(&g_mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT TLS connection failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Restarting in 30 seconds...");
        vTaskDelay(pdMS_TO_TICKS(30000));
        esp_restart();
        return;
    }
    
    // Start MQTT client task
    ESP_LOGI(TAG, "Starting MQTT TLS client task...");
    ret = mqtt_tls_client_start_task(&g_mqtt_client, CONFIG_PUBLISH_INTERVAL_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT task: %s", esp_err_to_name(ret));
        return;
    }
    
    // Start system monitor task
    xTaskCreate(system_monitor_task, 
                "system_monitor", 
                4096, 
                NULL, 
                3, 
                NULL);
    
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "ESP32-S3 SIM7600E MQTT TLS Client Started!");
    ESP_LOGI(TAG, "Publishing telemetry every %d seconds", CONFIG_PUBLISH_INTERVAL_MS / 1000);
    ESP_LOGI(TAG, "Monitoring system health every 30 seconds");
    ESP_LOGI(TAG, "================================================");
    
    // Main application loop - keep alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000)); // Sleep for 1 minute
    }
}