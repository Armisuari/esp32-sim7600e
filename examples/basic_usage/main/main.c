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

// GNSS callback function
static void gnss_event_handler(const sim7600e_gnss_info_t *info, void *user_data)
{
    if (info->valid_fix) {
        ESP_LOGI(TAG, "GNSS Fix: Lat=%.6f, Lon=%.6f, Alt=%.2f m, Speed=%.2f m/s, Sats=%d",
                 info->latitude, info->longitude, info->altitude,
                 info->speed, info->satellites_used);
        ESP_LOGI(TAG, "Timestamp: %s", info->timestamp);
    } else {
        ESP_LOGD(TAG, "No valid GNSS fix");
    }
}

// TCP status callback function
static void tcp_status_handler(sim7600e_tcp_status_t status, void *user_data)
{
    switch (status) {
        case SIM7600E_TCP_CONNECTED:
            ESP_LOGI(TAG, "TCP Connected");
            break;
        case SIM7600E_TCP_DISCONNECTED:
            ESP_LOGI(TAG, "TCP Disconnected");
            break;
        case SIM7600E_TCP_CONNECTING:
            ESP_LOGI(TAG, "TCP Connecting...");
            break;
        case SIM7600E_TCP_DISCONNECTING:
            ESP_LOGI(TAG, "TCP Disconnecting...");
            break;
        case SIM7600E_TCP_ERROR:
            ESP_LOGE(TAG, "TCP Error");
            break;
    }
}

// TCP receive callback function
static void tcp_recv_handler(const uint8_t *data, size_t len, void *user_data)
{
    ESP_LOGI(TAG, "TCP Received %d bytes: %.*s", len, len, data);
}

void app_main(void) 
{
    ESP_LOGI(TAG, "Starting SIM7600E Basic Example");
    
    // Initialize the SIM7600E component
    sim7600e_config_t config = sim7600e_get_default_config();
    
    // You can customize the configuration here if needed
    // Example pin assignments for different ESP32 boards:
    // config.tx_pin = 17;      // GPIO17 for UART TX
    // config.rx_pin = 16;      // GPIO16 for UART RX  
    // config.pwrkey_pin = 4;   // GPIO4 for power key control
    // config.baud_rate = 115200;
    
    esp_err_t ret = sim7600e_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SIM7600E: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "SIM7600E initialized successfully");
    
    // Power on the module
    ret = sim7600e_power_on();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to power on SIM7600E");
        return;
    }
    
    // Wait a moment for the module to boot
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Check modem status
    ESP_LOGI(TAG, "Checking modem status...");
    ret = sim7600e_gsm_check_modem();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Modem check failed");
        return;
    }
    
    // Turn off echo mode
    ret = sim7600e_gsm_turn_off_echo();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to turn off echo mode");
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
    ret = sim7600e_gsm_wait_for_network(60000); // 60 second timeout
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
    // Replace "internet" with your carrier's APN (e.g., "internet", "data", "fast.t-mobile.com")
    ret = sim7600e_gsm_enable_internet("internet");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable internet connection");
    } else {
        ESP_LOGI(TAG, "Internet connection established");
        
        // Demonstrate TCP connectivity
        sim7600e_tcp_config_t tcp_config = sim7600e_tcp_get_default_config();
        strcpy(tcp_config.host, "httpbin.org");
        tcp_config.port = 80;
        
        // Register TCP callbacks
        sim7600e_tcp_register_status_callback(tcp_status_handler, NULL);
        sim7600e_tcp_register_recv_callback(tcp_recv_handler, NULL);
        
        // Connect to TCP server
        ESP_LOGI(TAG, "Connecting to TCP server...");
        ret = sim7600e_tcp_connect(&tcp_config);
        if (ret == ESP_OK) {
            // Send HTTP GET request
            const char *http_request = "GET /get HTTP/1.1\r\nHost: httpbin.org\r\nConnection: close\r\n\r\n";
            ret = sim7600e_tcp_send_string(http_request, 10000);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "HTTP request sent successfully");
            }
            
            // Wait a bit then disconnect
            vTaskDelay(pdMS_TO_TICKS(5000));
            sim7600e_tcp_disconnect();
        }
    }
    
    // Enable GNSS
    ESP_LOGI(TAG, "Enabling GNSS...");
    sim7600e_gnss_config_t gnss_config = sim7600e_gnss_get_default_config();
    gnss_config.update_rate_ms = 2000; // Update every 2 seconds
    
    ret = sim7600e_gnss_enable(&gnss_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable GNSS");
    } else {
        ESP_LOGI(TAG, "GNSS enabled successfully");
        
        // Register GNSS callback
        sim7600e_gnss_register_callback(gnss_event_handler, NULL);
        
        // Start GNSS task
        ret = sim7600e_gnss_start_task(5, 4096);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start GNSS task");
        }
    }
    
    // Main application loop - demonstrate periodic operations
    ESP_LOGI(TAG, "Starting main loop...");
    
    int loop_count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // Wait 10 seconds
        loop_count++;
        
        ESP_LOGI(TAG, "Main loop iteration: %d", loop_count);
        
        // Check network signal strength every 5 iterations (50 seconds)
        if (loop_count % 5 == 0) {
            ret = sim7600e_gsm_get_network_info(&net_info);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Signal strength: %d dBm", net_info.signal_strength);
            }
        }
        
        // Get GNSS info directly every 3 iterations (30 seconds)
        if (loop_count % 3 == 0) {
            sim7600e_gnss_info_t gnss_info;
            ret = sim7600e_gnss_get_info(&gnss_info, 5000);
            if (ret == ESP_OK && gnss_info.valid_fix) {
                ESP_LOGI(TAG, "Direct GNSS query - Lat: %.6f, Lon: %.6f", 
                         gnss_info.latitude, gnss_info.longitude);
            }
        }
        
        // Send SMS demonstration every 20 iterations (200 seconds)
        // Uncomment and modify the phone number to test SMS functionality
        /*
        if (loop_count % 20 == 0) {
            ret = sim7600e_gsm_send_sms("+1234567890", "Hello from ESP32!");
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "SMS sent successfully");
            }
        }
        */
    }
}