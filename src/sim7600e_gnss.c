#include "sim7600e_gnss.h"
#include "sim7600e.h"
#include "sim7600e_gsm.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "SIM7600E_GNSS";

// Static variables for GNSS functionality
static TaskHandle_t s_gnss_task_handle = NULL;
static sim7600e_gnss_event_cb_t s_gnss_callback = NULL;
static void *s_gnss_user_data = NULL;
static bool s_gnss_enabled = false;
static sim7600e_gnss_config_t s_gnss_config;

// External function declarations
extern QueueHandle_t sim7600e_get_gnss_queue(void);
extern SemaphoreHandle_t sim7600e_get_gnss_semaphore(void);

// Internal function declarations
static void gnss_task(void *arg);
static double nmea_to_decimal(const char *nmea);
static bool parse_gps_response(const char *response, sim7600e_gnss_info_t *info);

sim7600e_gnss_config_t sim7600e_gnss_get_default_config(void)
{
    sim7600e_gnss_config_t config = {
        .constellations = SIM7600E_GNSS_GPS | SIM7600E_GNSS_GLONASS,
        .update_rate_ms = 1000,
        .cold_start = false
    };
    return config;
}

esp_err_t sim7600e_gnss_enable(const sim7600e_gnss_config_t *config)
{
    char response[256];
    
    // Use default config if none provided
    if (config == NULL) {
        s_gnss_config = sim7600e_gnss_get_default_config();
    } else {
        s_gnss_config = *config;
    }
    
    ESP_LOGI(TAG, "Enabling GNSS...");
    
    // Check current GNSS status
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+CGPS?\r\n", response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check GNSS status");
        return ESP_FAIL;
    }
    
    if (strstr(response, "+CGPS: 0")) {
        ESP_LOGI(TAG, "GNSS is OFF, enabling...");
        
        // Enable GNSS
        ret = sim7600e_gsm_send_at_command("AT+CGPS=1\r\n", response, sizeof(response), 10000);
        if (ret != ESP_OK || !strstr(response, "OK")) {
            ESP_LOGE(TAG, "Failed to enable GNSS");
            return ESP_FAIL;
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for GNSS to start
    } else {
        ESP_LOGI(TAG, "GNSS already enabled");
    }
    
    // Perform cold start if requested
    if (s_gnss_config.cold_start) {
        ret = sim7600e_gnss_cold_start();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Cold start failed, continuing anyway");
        }
    }
    
    s_gnss_enabled = true;
    ESP_LOGI(TAG, "GNSS enabled successfully");
    
    return ESP_OK;
}

esp_err_t sim7600e_gnss_disable(void)
{
    ESP_LOGI(TAG, "Disabling GNSS...");
    
    // Stop task if running
    if (s_gnss_task_handle != NULL) {
        sim7600e_gnss_stop_task();
    }
    
    char response[128];
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+CGPS=0\r\n", response, sizeof(response), 10000);
    if (ret != ESP_OK || !strstr(response, "OK")) {
        ESP_LOGE(TAG, "Failed to disable GNSS");
        return ESP_FAIL;
    }
    
    s_gnss_enabled = false;
    ESP_LOGI(TAG, "GNSS disabled successfully");
    
    return ESP_OK;
}

esp_err_t sim7600e_gnss_get_info(sim7600e_gnss_info_t *info, uint32_t timeout_ms)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_gnss_enabled) {
        ESP_LOGE(TAG, "GNSS not enabled");
        return ESP_ERR_INVALID_STATE;
    }
    
    char response[512];
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+CGPSINFO\r\n", response, sizeof(response), timeout_ms);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get GNSS info");
        return ESP_FAIL;
    }
    
    if (parse_gps_response(response, info)) {
        return ESP_OK;
    } else {
        ESP_LOGI(TAG, "No valid GNSS fix available");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t sim7600e_gnss_start_task(uint32_t task_priority, uint32_t stack_size)
{
    if (!s_gnss_enabled) {
        ESP_LOGE(TAG, "GNSS not enabled");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_gnss_task_handle != NULL) {
        ESP_LOGW(TAG, "GNSS task already running");
        return ESP_ERR_INVALID_STATE;
    }
    
    BaseType_t ret = xTaskCreate(
        gnss_task,
        "sim7600e_gnss_task",
        stack_size,
        NULL,
        task_priority,
        &s_gnss_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GNSS task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "GNSS task started");
    return ESP_OK;
}

esp_err_t sim7600e_gnss_stop_task(void)
{
    if (s_gnss_task_handle != NULL) {
        vTaskDelete(s_gnss_task_handle);
        s_gnss_task_handle = NULL;
        ESP_LOGI(TAG, "GNSS task stopped");
    }
    
    return ESP_OK;
}

esp_err_t sim7600e_gnss_register_callback(sim7600e_gnss_event_cb_t callback, void *user_data)
{
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_gnss_callback = callback;
    s_gnss_user_data = user_data;
    
    return ESP_OK;
}

esp_err_t sim7600e_gnss_unregister_callback(void)
{
    s_gnss_callback = NULL;
    s_gnss_user_data = NULL;
    
    return ESP_OK;
}

esp_err_t sim7600e_gnss_cold_start(void)
{
    ESP_LOGI(TAG, "Performing GNSS cold start...");
    
    char response[128];
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+CGPSRST=0\r\n", response, sizeof(response), 10000);
    
    if (ret == ESP_OK && strstr(response, "OK")) {
        ESP_LOGI(TAG, "GNSS cold start completed");
        vTaskDelay(pdMS_TO_TICKS(5000)); // Wait for restart
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to perform cold start");
        return ESP_FAIL;
    }
}

// Internal function implementations

static void gnss_task(void *arg)
{
    sim7600e_gnss_info_t gnss_info;
    QueueHandle_t gnss_queue = sim7600e_get_gnss_queue();
    
    ESP_LOGI(TAG, "GNSS task started");
    
    while (1) {
        esp_err_t ret = sim7600e_gnss_get_info(&gnss_info, 5000);
        
        if (ret == ESP_OK) {
            // Send to queue if available
            if (gnss_queue != NULL) {
                xQueueOverwrite(gnss_queue, &gnss_info);
            }
            
            // Call callback if registered
            if (s_gnss_callback != NULL) {
                s_gnss_callback(&gnss_info, s_gnss_user_data);
            }
            
            ESP_LOGI(TAG, "GPS: Lat=%.6f, Lon=%.6f, Alt=%.2f, Speed=%.2f, Sats=%d", 
                     gnss_info.latitude, gnss_info.longitude, gnss_info.altitude, 
                     gnss_info.speed, gnss_info.satellites_used);
        } else {
            ESP_LOGD(TAG, "No valid GPS fix");
        }
        
        vTaskDelay(pdMS_TO_TICKS(s_gnss_config.update_rate_ms));
    }
}

static double nmea_to_decimal(const char *nmea)
{
    double val = atof(nmea);
    int deg = (int)(val / 100);
    double min = val - (deg * 100);
    return deg + (min / 60.0);
}

static bool parse_gps_response(const char *response, sim7600e_gnss_info_t *info)
{
    if (response == NULL || info == NULL) {
        return false;
    }
    
    // Initialize structure
    memset(info, 0, sizeof(sim7600e_gnss_info_t));
    
    // Look for +CGPSINFO response
    char *cgps_start = strstr(response, "+CGPSINFO:");
    if (cgps_start == NULL) {
        return false;
    }
    
    // Skip "+CGPSINFO:"
    cgps_start += 10;
    
    // Parse the response: +CGPSINFO: lat,latNS,lon,lonEW,date,time,alt,speed,course
    char lat_str[16], lat_ns[2], lon_str[16], lon_ew[2];
    char date_str[16], time_str[16], alt_str[16], speed_str[16];
    
    int parsed = sscanf(cgps_start, "%15[^,],%1[^,],%15[^,],%1[^,],%15[^,],%15[^,],%15[^,],%15[^,]",
                       lat_str, lat_ns, lon_str, lon_ew, date_str, time_str, alt_str, speed_str);
    
    if (parsed < 8) {
        ESP_LOGD(TAG, "Failed to parse GPS response");
        return false;
    }
    
    // Check if we have valid coordinates
    if (strlen(lat_str) == 0 || strlen(lon_str) == 0 || 
        strcmp(lat_str, ",,,,,,,,") == 0) {
        ESP_LOGD(TAG, "No GPS fix available");
        return false;
    }
    
    // Convert latitude
    info->latitude = nmea_to_decimal(lat_str);
    if (lat_ns[0] == 'S') {
        info->latitude = -info->latitude;
    }
    
    // Convert longitude
    info->longitude = nmea_to_decimal(lon_str);
    if (lon_ew[0] == 'W') {
        info->longitude = -info->longitude;
    }
    
    // Convert altitude (meters)
    info->altitude = atof(alt_str);
    
    // Convert speed (km/h to m/s)
    info->speed = atof(speed_str) * 0.277778;
    
    // Parse timestamp
    if (strlen(date_str) >= 6 && strlen(time_str) >= 6) {
        // Date format: DDMMYY, Time format: HHMMSS.sss
        int day, month, year, hour, min, sec;
        if (sscanf(date_str, "%2d%2d%2d", &day, &month, &year) == 3 &&
            sscanf(time_str, "%2d%2d%2d", &hour, &min, &sec) == 3) {
            
            snprintf(info->timestamp, sizeof(info->timestamp), 
                    "20%02d-%02d-%02d %02d:%02d:%02d", 
                    year, month, day, hour, min, sec);
        }
    }
    
    // Set fix status
    info->fix_status = SIM7600E_GNSS_3D_FIX; // Simplified - assume 3D fix if we have data
    info->valid_fix = true;
    
    // Set some default values for fields not available in basic response
    info->hdop = 1.0;
    info->vdop = 1.0;
    info->satellites_used = 4; // Minimum for 3D fix
    info->satellites_visible = 8; // Reasonable default
    
    return true;
}