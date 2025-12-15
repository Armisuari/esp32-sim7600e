/**
 * @file sim_manager.c
 * @brief SIM7600E cellular connection management
 */

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sim_manager.h"
#include "sim7600e_gsm.h"

static const char *TAG = "SIM_MANAGER";

esp_err_t sim_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing SIM7600E manager");
    
    // Initialize SIM7600E with default configuration
    sim7600e_config_t config = sim7600e_get_default_config();
    esp_err_t ret = sim7600e_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SIM7600E: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Power on the module
    ESP_LOGI(TAG, "Powering on SIM7600E module...");
    ret = sim7600e_power_on();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to power on SIM7600E: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for module initialization
    ESP_LOGI(TAG, "Waiting for module initialization...");
    vTaskDelay(pdMS_TO_TICKS(10000));
    
    // Check modem
    ret = sim7600e_gsm_check_modem();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Modem check failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Check SIM card
    ESP_LOGI(TAG, "Checking SIM card...");
    ret = sim7600e_gsm_check_sim();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SIM card check failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "SIM7600E manager initialized successfully");
    return ESP_OK;
}

esp_err_t sim_manager_connect(const char *apn, uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Connecting to cellular network with APN: %s", apn);
    
    // Wait for network registration
    ESP_LOGI(TAG, "Waiting for network registration...");
    esp_err_t ret = sim7600e_gsm_wait_for_network(timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Network registration failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Enable internet connection
    ESP_LOGI(TAG, "Enabling internet connection...");
    ret = sim7600e_gsm_enable_internet(apn);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Internet connection failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Get and log network information
    sim7600e_network_info_t net_info = {0};
    ret = sim7600e_gsm_get_network_info(&net_info);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Connected to operator: %s", net_info.operator_name);
        ESP_LOGI(TAG, "Signal strength: %d dBm", net_info.signal_strength);
        ESP_LOGI(TAG, "Network time: %s", net_info.network_time);
    }
    
    ESP_LOGI(TAG, "Cellular connection established successfully");
    return ESP_OK;
}

bool sim_manager_is_connected(void)
{
    sim7600e_network_info_t net_info;
    if (sim7600e_gsm_get_network_info(&net_info) == ESP_OK) {
        // Consider connected if signal strength is reasonable
        return (net_info.signal_strength > -120);
    }
    return false;
}

esp_err_t sim_manager_get_network_info(sim7600e_network_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return sim7600e_gsm_get_network_info(info);
}

esp_err_t sim_manager_wait_for_connection(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Waiting for cellular connection...");
    
    uint32_t elapsed = 0;
    const uint32_t check_interval = 1000; // Check every second
    
    while (elapsed < timeout_ms) {
        if (sim_manager_is_connected()) {
            ESP_LOGI(TAG, "Cellular connection established after %lu ms", elapsed);
            return ESP_OK;
        }
        
        vTaskDelay(pdMS_TO_TICKS(check_interval));
        elapsed += check_interval;
    }
    
    ESP_LOGE(TAG, "Timeout waiting for cellular connection");
    return ESP_ERR_TIMEOUT;
}

void sim_manager_cleanup(void)
{
    ESP_LOGI(TAG, "SIM manager cleanup completed");
    // Additional cleanup if needed
}