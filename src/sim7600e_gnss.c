/**
 * @file sim7600e_gnss.c
 * @brief GNSS/GPS Functions for SIM7600E
 * 
 * This file implements GNSS positioning functionality including:
 * - GPS/GLONASS satellite positioning
 * - NMEA data parsing
 * - Position data callbacks
 * - Multi-constellation support
 * 
 * @author ESP32 SIM7600E Component
 */

#include "sim7600e_gnss.h"
#include "sim7600e.h"
#include "sim7600e_gsm.h"
#include "esp_log.h"
#include "driver/uart.h"
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
extern SemaphoreHandle_t sim7600e_get_mutex(void);
extern QueueHandle_t sim7600e_get_resp_queue(void);
extern QueueHandle_t sim7600e_get_urc_queue(void);
extern int sim7600e_get_uart_port(void);

// Internal function declarations
static void gnss_task(void *arg);
static double nmea_to_decimal(const char *nmea);
static bool parse_gps_response(const char *response, sim7600e_gnss_info_t *info);
static esp_err_t gnss_get_info_non_blocking(sim7600e_gnss_info_t *info, uint32_t mutex_timeout_ms);

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

/**
 * @brief Non-blocking GNSS info retrieval for background task
 * 
 * This function tries to get GNSS info with a short mutex timeout to avoid
 * blocking when MQTT operations are in progress. If the mutex is not available
 * within mutex_timeout_ms, it returns ESP_ERR_TIMEOUT immediately.
 * 
 * IMPORTANT: This function does NOT clear the URC queue to avoid destroying
 * pending MQTT URCs. It only looks for CGPSINFO responses.
 * 
 * @param info Pointer to store GNSS info
 * @param mutex_timeout_ms Maximum time to wait for mutex (in milliseconds)
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if mutex busy or no fix, ESP_FAIL on error
 */
static esp_err_t gnss_get_info_non_blocking(sim7600e_gnss_info_t *info, uint32_t mutex_timeout_ms)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_gnss_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    
    SemaphoreHandle_t mutex = sim7600e_get_mutex();
    QueueHandle_t resp_queue = sim7600e_get_resp_queue();
    int uart_port = sim7600e_get_uart_port();
    
    if (mutex == NULL || resp_queue == NULL) {
        ESP_LOGE(TAG, "GNSS: INVALID STATE - mutex or queue is NULL");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Try to take mutex with short timeout - if MQTT is busy, we skip this cycle
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(mutex_timeout_ms)) != pdTRUE) {
        ESP_LOGD(TAG, "GNSS: Mutex busy (MQTT active?), skipping cycle");
        return ESP_ERR_TIMEOUT;
    }
    
    // Clear the response queue before sending command
    sim7600e_msg_t dummy_msg;
    while (xQueueReceive(resp_queue, &dummy_msg, 0) == pdTRUE) {}
    
    // Send command
    const char *cmd = "AT+CGPSINFO\r\n";
    int bytes_written = uart_write_bytes(uart_port, cmd, strlen(cmd));
    
    if (bytes_written != (int)strlen(cmd)) {
        ESP_LOGE(TAG, "GNSS: UART write failed");
        xSemaphoreGive(mutex);
        return ESP_FAIL;
    }
    
    // Wait for response with 2 second timeout
    // CGPSINFO is now routed to resp_queue by the UART reader
    sim7600e_msg_t resp_msg;
    TickType_t timeout_ticks = pdMS_TO_TICKS(2000);
    TickType_t start_time = xTaskGetTickCount();
    
    char combined_response[512] = {0};
    bool got_final_response = false;
    bool got_cgpsinfo = false;
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks && !got_final_response) {
        // Read from resp_queue - both +CGPSINFO and OK/ERROR now go there
        if (xQueueReceive(resp_queue, &resp_msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Check if this is a CGPSINFO response
            if (strstr(resp_msg.data, "+CGPSINFO") || strstr(resp_msg.data, "CGPSINFO")) {
                got_cgpsinfo = true;
                if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                    if (strlen(combined_response) > 0) strcat(combined_response, " ");
                    strcat(combined_response, resp_msg.data);
                }
            }
            
            // Check for OK/ERROR
            if (strstr(resp_msg.data, "OK") || strstr(resp_msg.data, "ERROR")) {
                if (strlen(combined_response) + strlen(resp_msg.data) < sizeof(combined_response) - 1) {
                    if (strlen(combined_response) > 0) strcat(combined_response, " ");
                    strcat(combined_response, resp_msg.data);
                }
                got_final_response = true;
            }
        }
        
        if (!got_final_response) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    xSemaphoreGive(mutex);
    
    if (!got_final_response || !got_cgpsinfo) {
        ESP_LOGD(TAG, "GNSS: No response received");
        return ESP_ERR_TIMEOUT;
    }
    
    // Parse the response
    if (parse_gps_response(combined_response, info)) {
        return ESP_OK;
    } else {
        return ESP_ERR_TIMEOUT;
    }
}

// Static variable to store last valid fix for caching
static sim7600e_gnss_info_t s_last_valid_fix = {0};
static bool s_has_valid_fix = false;
static TickType_t s_last_fix_time = 0;

static void gnss_task(void *arg)
{
    sim7600e_gnss_info_t gnss_info;
    QueueHandle_t gnss_queue = sim7600e_get_gnss_queue();
    uint32_t consecutive_mutex_failures = 0;
    uint32_t no_fix_count = 0;
    
    ESP_LOGI(TAG, "GNSS task started, update rate: %lu ms", s_gnss_config.update_rate_ms);
    
    // Initial delay to let system stabilize and MQTT connect first
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    ESP_LOGI(TAG, "GNSS task starting main loop");
    
    while (1) {
        // Use non-blocking version with 500ms mutex timeout
        // This allows GNSS task to coexist with MQTT without blocking
        esp_err_t ret = gnss_get_info_non_blocking(&gnss_info, 500);
        
        if (ret == ESP_OK) {
            consecutive_mutex_failures = 0;
            
            if (gnss_info.valid_fix) {
                // Got a valid fix - cache it and update queue
                no_fix_count = 0;
                s_last_valid_fix = gnss_info;
                s_has_valid_fix = true;
                s_last_fix_time = xTaskGetTickCount();
                
                if (gnss_queue != NULL) {
                    xQueueOverwrite(gnss_queue, &gnss_info);
                }
                
                if (s_gnss_callback != NULL) {
                    s_gnss_callback(&gnss_info, s_gnss_user_data);
                }
                
                ESP_LOGI(TAG, "GPS fix: Lat=%.6f, Lon=%.6f, Alt=%.1fm", 
                         gnss_info.latitude, gnss_info.longitude, gnss_info.altitude);
                
                // After getting a valid fix, use longer delay (5 seconds) to reduce modem contention
                // Location doesn't change that fast, so we don't need to poll every second
                vTaskDelay(pdMS_TO_TICKS(5000));
            } else {
                // No fix yet - use cached data if recent enough (< 60 seconds old)
                no_fix_count++;
                TickType_t now = xTaskGetTickCount();
                TickType_t age_ticks = now - s_last_fix_time;
                uint32_t age_seconds = age_ticks / configTICK_RATE_HZ;
                
                if (s_has_valid_fix && age_seconds < 60) {
                    // Use cached fix but mark as slightly stale
                    s_last_valid_fix.hdop = 2.0 + (age_seconds / 30.0);  // Degrade accuracy over time
                    if (gnss_queue != NULL) {
                        xQueueOverwrite(gnss_queue, &s_last_valid_fix);
                    }
                    ESP_LOGD(TAG, "GPS: Using cached fix (age=%lus)", age_seconds);
                } else {
                    // No valid cached data - put no-fix info in queue so consumers know status
                    gnss_info.valid_fix = false;
                    gnss_info.fix_status = SIM7600E_GNSS_NO_FIX;
                    if (gnss_queue != NULL) {
                        xQueueOverwrite(gnss_queue, &gnss_info);
                    }
                    
                    if (no_fix_count % 10 == 1) {  // Log every 10th attempt
                        ESP_LOGW(TAG, "GPS: Acquiring satellites (no fix for %lu attempts)", no_fix_count);
                    }
                }
                
                // Still acquiring satellites, use configured rate
                vTaskDelay(pdMS_TO_TICKS(s_gnss_config.update_rate_ms));
            }
        } else {
            consecutive_mutex_failures++;
            
            // On timeout (likely mutex contention with MQTT), back off significantly
            if (ret == ESP_ERR_TIMEOUT) {
                if (consecutive_mutex_failures == 1) {
                    ESP_LOGD(TAG, "GNSS: Modem busy, backing off");
                }
                // Aggressive backoff: 2s, 4s, 6s, max 10s to give MQTT time to complete
                uint32_t backoff_ms = (consecutive_mutex_failures < 5) ? 
                    (consecutive_mutex_failures * 2000) : 10000;
                vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            } else {
                ESP_LOGW(TAG, "GNSS query failed: %s", esp_err_to_name(ret));
                vTaskDelay(pdMS_TO_TICKS(s_gnss_config.update_rate_ms * 2));
            }
        }
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
    
    // Initialize structure with defaults
    memset(info, 0, sizeof(sim7600e_gnss_info_t));
    info->fix_status = SIM7600E_GNSS_NO_FIX;
    info->valid_fix = false;
    info->hdop = 99.9;  // Invalid HDOP
    info->vdop = 99.9;
    
    ESP_LOGD(TAG, "Parsing GPS response: '%s'", response);
    
    // Look for +CGPSINFO response
    char *cgps_start = strstr(response, "+CGPSINFO:");
    if (cgps_start == NULL) {
        ESP_LOGD(TAG, "No +CGPSINFO in response");
        return false;
    }
    
    // Skip "+CGPSINFO:" and any leading whitespace
    cgps_start += 10;
    while (*cgps_start == ' ') cgps_start++;
    
    ESP_LOGD(TAG, "CGPSINFO data: '%s'", cgps_start);
    
    // Check for empty response (no fix): +CGPSINFO: ,,,,,,,,
    if (*cgps_start == ',' || strncmp(cgps_start, ",,", 2) == 0) {
        ESP_LOGD(TAG, "GNSS acquiring satellites (no fix yet)");
        return true;
    }
    
    // Parse the response: +CGPSINFO: lat,latNS,lon,lonEW,date,time,alt,speed,course
    char lat_str[16] = {0}, lat_ns[2] = {0}, lon_str[16] = {0}, lon_ew[2] = {0};
    char date_str[16] = {0}, time_str[16] = {0}, alt_str[16] = {0}, speed_str[16] = {0};
    
    int parsed = sscanf(cgps_start, "%15[^,],%1[^,],%15[^,],%1[^,],%15[^,],%15[^,],%15[^,],%15[^,]",
                       lat_str, lat_ns, lon_str, lon_ew, date_str, time_str, alt_str, speed_str);
    
    ESP_LOGD(TAG, "Parsed %d fields", parsed);
    
    // Need at least lat, lat_ns, lon, lon_ew for a valid position
    if (parsed < 4 || strlen(lat_str) == 0 || strlen(lon_str) == 0) {
        ESP_LOGD(TAG, "Incomplete GPS data (parsed=%d)", parsed);
        return true;
    }
    
    // Validate coordinate strings contain digits
    bool lat_valid = false, lon_valid = false;
    for (int i = 0; lat_str[i]; i++) {
        if (lat_str[i] >= '0' && lat_str[i] <= '9') { lat_valid = true; break; }
    }
    for (int i = 0; lon_str[i]; i++) {
        if (lon_str[i] >= '0' && lon_str[i] <= '9') { lon_valid = true; break; }
    }
    
    if (!lat_valid || !lon_valid) {
        ESP_LOGD(TAG, "Invalid coordinate format");
        return true;
    }
    
    // Convert latitude (NMEA format: DDMM.MMMM)
    info->latitude = nmea_to_decimal(lat_str);
    if (lat_ns[0] == 'S' || lat_ns[0] == 's') {
        info->latitude = -info->latitude;
    }
    
    // Convert longitude (NMEA format: DDDMM.MMMM)
    info->longitude = nmea_to_decimal(lon_str);
    if (lon_ew[0] == 'W' || lon_ew[0] == 'w') {
        info->longitude = -info->longitude;
    }
    
    // Sanity check coordinates
    if (info->latitude < -90.0 || info->latitude > 90.0 ||
        info->longitude < -180.0 || info->longitude > 180.0) {
        ESP_LOGW(TAG, "Coordinates out of range: %.6f, %.6f", info->latitude, info->longitude);
        info->latitude = 0;
        info->longitude = 0;
        return true;
    }
    
    // Convert altitude (meters) if available
    if (parsed >= 7 && strlen(alt_str) > 0) {
        info->altitude = atof(alt_str);
    }
    
    // Convert speed (km/h to m/s) if available
    if (parsed >= 8 && strlen(speed_str) > 0) {
        info->speed = atof(speed_str) * 0.277778;
    }
    
    // Parse timestamp if available
    if (parsed >= 6 && strlen(date_str) >= 6 && strlen(time_str) >= 6) {
        int day = 0, month = 0, year = 0, hour = 0, min = 0, sec = 0;
        if (sscanf(date_str, "%2d%2d%2d", &day, &month, &year) == 3 &&
            sscanf(time_str, "%2d%2d%2d", &hour, &min, &sec) == 3) {
            snprintf(info->timestamp, sizeof(info->timestamp), 
                    "20%02d-%02d-%02d %02d:%02d:%02d", 
                    year, month, day, hour, min, sec);
        }
    }
    
    // We have valid coordinates - mark as valid fix
    info->fix_status = SIM7600E_GNSS_3D_FIX;
    info->valid_fix = true;
    info->hdop = 1.5;  // Reasonable default
    info->vdop = 2.0;
    info->satellites_used = 4;  // Minimum for 3D fix
    info->satellites_visible = 8;
    
    ESP_LOGD(TAG, "Valid fix: %.6f, %.6f, alt=%.1f", 
             info->latitude, info->longitude, info->altitude);
    
    return true;
}