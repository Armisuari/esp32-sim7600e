#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"

// Include the SIM7600E component headers
#include "sim7600e.h"
#include "sim7600e_gsm.h"
#include "sim7600e_gnss.h"
#include "sim7600e_tcp.h"

static const char *TAG = "SIM7600E_EXAMPLE";

// GNSS callback
static void gnss_event_handler(const sim7600e_gnss_info_t *info, void *user_data)
{
    if (info->valid_fix) {
        ESP_LOGI(TAG, "GNSS: Lat=%.6f, Lon=%.6f, Alt=%.1fm, Sats=%d",
                 info->latitude, info->longitude, info->altitude, info->satellites_used);
    }
}

void app_main(void) 
{
    ESP_LOGI(TAG, "Starting SIM7600E Basic Example");
    
    // Initialize SIM7600E with default config
    sim7600e_config_t config = sim7600e_get_default_config();
    esp_err_t ret = sim7600e_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SIM7600E");
        return;
    }
    
    // Power on and wait for initialization
    sim7600e_power_on();
    ESP_LOGI(TAG, "Waiting for module initialization...");
    vTaskDelay(pdMS_TO_TICKS(10000));
    
    // Check modem status
    ret = sim7600e_gsm_check_modem();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Modem check failed");
        return;
    }
    
    // Check SIM card
    ESP_LOGI(TAG, "Checking SIM card...");
    ret = sim7600e_gsm_check_sim();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SIM card check failed");
        return;
    }
    
    // Get module information
    char imei[32] = {0};
    ret = sim7600e_get_module_info(imei, sizeof(imei));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Module IMEI: %s", imei);
    }
    
    // Wait for network registration
    ESP_LOGI(TAG, "Waiting for network registration...");
    ret = sim7600e_gsm_wait_for_network(60000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Network registration failed");
        return;
    }
    
    // Get network information
    sim7600e_network_info_t net_info = {0};
    ret = sim7600e_gsm_get_network_info(&net_info);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Operator: %s, Signal: %d dBm", 
                 net_info.operator_name, net_info.signal_strength);
        ESP_LOGI(TAG, "Network time: %s", net_info.network_time);
    }
    
    // Enable internet connection
    ESP_LOGI(TAG, "Enabling internet connection...");
    ret = sim7600e_gsm_enable_internet("internet");
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Internet connection established");
        
        // Wait a bit for network to stabilize
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // TCP connection demo
        ESP_LOGI(TAG, "Connecting to TCP server...");
        sim7600e_tcp_config_t tcp_config = sim7600e_tcp_get_default_config();
        
        // First try Google DNS server (should be widely accessible)
        strcpy(tcp_config.host, "8.8.8.8");
        tcp_config.port = 53;
        
        ret = sim7600e_tcp_connect(&tcp_config);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "TCP Connected to %s:%d", tcp_config.host, tcp_config.port);
            sim7600e_tcp_disconnect();
        } else {
            ESP_LOGW(TAG, "First TCP attempt failed, trying HTTP server...");
            
            // Try a different server
            strcpy(tcp_config.host, "httpbin.org");
            tcp_config.port = 80;
            ret = sim7600e_tcp_connect(&tcp_config);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "TCP Connected to %s:%d", tcp_config.host, tcp_config.port);
                sim7600e_tcp_disconnect();
            } else {
                ESP_LOGE(TAG, "TCP Error - both connection attempts failed");
            }
        }
    }
    
    // Enable GNSS
    ESP_LOGI(TAG, "Enabling GNSS...");
    sim7600e_gnss_config_t gnss_config = sim7600e_gnss_get_default_config();
    ret = sim7600e_gnss_enable(&gnss_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "GNSS enabled successfully");
        sim7600e_gnss_register_callback(gnss_event_handler, NULL);
        sim7600e_gnss_start_task(5, 8192);
    }
    
    // Main loop
    ESP_LOGI(TAG, "Starting main loop...");
    int loop_count = 0;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        loop_count++;
        ESP_LOGI(TAG, "Main loop iteration: %d", loop_count);
        
        // Check signal every 5 iterations
        if (loop_count % 5 == 0) {
            sim7600e_network_info_t net_info;
            if (sim7600e_gsm_get_network_info(&net_info) == ESP_OK) {
                ESP_LOGI(TAG, "Signal: %d dBm", net_info.signal_strength);
            }
        }
    }
}