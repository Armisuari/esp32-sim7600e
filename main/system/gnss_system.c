#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gsm_system.h"
#include "gnss_system.h"
#include "gsm/gsm.h"
#include "esp_log.h"

static const char *TAG = "GNSS_SYSTEM";

QueueHandle_t gnss_queue  = NULL;
gps_info_t *gps_data;
SemaphoreHandle_t gnss_ready_sem;

void gsm_enable_gnss(void)
{
    char response[256];

    // Cek status GPS
    gsm_send_at_command_queue("AT+CGPS?\r\n", response, sizeof(response));
    if (strstr(response, "+CGPS: 0")) 
    {
        ESP_LOGI(TAG, "GNSS is OFF, enabling...");
        gsm_send_at_command_queue("AT+CGPS=1\r\n", response, sizeof(response));
        vTaskDelay(pdMS_TO_TICKS(2000)); // tunggu modul start
    } 
    else 
    {
        ESP_LOGI(TAG, "GNSS already ON");
    }
}

static double nmea_to_decimal(const char *nmea)
{
    double val = atof(nmea);
    int deg = (int)(val / 100);
    double min = val - (deg * 100);
    return deg + (min / 60.0);
}


static bool gsm_get_gps_info(gps_info_t *gps)
{
    char response[256];

    double lat;
    double lon;
    int year, month, day, hour, min, sec;
    double speed;   // km/h
    double alt;     // meter
    bool valid;
    
    gsm_send_at_command_queue("AT+CGPSINFO\r\n", response, sizeof(response));

    char *line = strstr(response, "+CGPSINFO:");
    if (!line) {
        ESP_LOGW(TAG, "No +CGPSINFO in response: %s", response);
        return false;
    }

    char *ok = strstr(line, "OK");
    if (ok) *ok = '\0';

    // contoh respon: +CGPSINFO: -6.200000,S,106.816666,E,080923,123456.0,100.0,0.0,0
    char buff_lat[16], buff_ns, buff_lon[16], buff_ew;
    char buff_date[7], buff_time[10];
    double buf_alt = 0, buf_speed = 0, buff_course = 0;

    int parsed = sscanf(line,
                        "+CGPSINFO: %15[^,],%c,%15[^,],%c,%6s,%9s,%lf,%lf,%lf",
                        buff_lat, &buff_ns, buff_lon, &buff_ew,
                        buff_date, buff_time, &buf_alt, &buf_speed, &buff_course);
    

    if (parsed < 4) {
        ESP_LOGW(TAG, "GPS no fix yet (parsed=%d), raw=%s", parsed, line);
        return false;
    }

    lat = nmea_to_decimal(buff_lat);
    lon = nmea_to_decimal(buff_lon);
    if (buff_ns == 'S') lat = -lat;
    if (buff_ew == 'W') lon = -lon;

    if (parsed >= 6) 
    {
        sscanf(buff_date, "%2d%2d%2d", &day, &month, &year);
        year += 2000;
        sscanf(buff_time, "%2d%2d%2d", &hour, &min, &sec);
    }

    gps->latitude = lat;
    gps->longitude = lon;
    gps->altitude = buf_alt;
    gps->speed = buf_speed;

    snprintf(gps->timestamp, sizeof(gps->timestamp),
             "%04d-%02d-%02d %02d:%02d:%02d",
             year, month, day, hour+7, min, sec); // GMT+7

    ESP_LOGI(TAG, "GPS: lat=%.6f lon=%.6f alt=%.1f m speed=%.1f km/h time=%s",
             gps->latitude, gps->longitude, gps->altitude, gps->speed, gps->timestamp);
    
    ESP_LOGI(TAG, "Google Maps: https://maps.google.com/?q=%.6f,%.6f", lat, lon);


    return true;
}


void gnss_task(void *arg) 
{
    gps_info_t gps_data;

    while (1) 
    {
        if (gsm_get_gps_info(&gps_data))  // blocking, wajar
        { 
            if (gnss_queue) {
                xQueueOverwrite(gnss_queue, &gps_data);
            }
            // Trigger semaphore supaya TCP task bisa kirim
            xSemaphoreGive(gnss_ready_sem);
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // polling 200 ms
    }
}