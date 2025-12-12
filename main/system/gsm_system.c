#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gsm_system.h"
#include "gsm/gsm.h"
#include "esp_log.h"

static const char *TAG = "GSM_SYSTEM";

// Function to test the GSM modem
void gsm_turnoff_echo() 
{
    char response[128];
    memset(response, 0, sizeof(response)); // Clear response buffer 
    // Send "AT" command to check if the modem is responding
    esp_err_t ret = gsm_send_at_command_queue("ATE0\r\n", response, sizeof(response));
    if (ret == ESP_OK) 
    {
        if (!strstr(response, "OK")) 
        {
            ESP_LOGW(TAG, "Unexpected response: %s", response);
        }
    } 
    else 
    {
        ESP_LOGE(TAG, "Failed to send AT command");
    }
}

// Function to test the GSM modem
void gsm_modem_check() 
{
    ESP_LOGI(TAG, "Checking SIM A760C modem...");
    char response[128];
    memset(response, 0, sizeof(response)); // Clear response buffer 
    // Send "AT" command to check if the modem is responding
    esp_err_t ret = gsm_send_at_command_queue("AT\r\n", response, sizeof(response));
    if (ret == ESP_OK) 
    {
        if (strstr(response, "OK")) 
        {
            ESP_LOGI(TAG, "SIM A760C is working fine!");
        } 
        else 
        {
            ESP_LOGW(TAG, "Unexpected response: %s", response);
        }
    } 
    else 
    {
        ESP_LOGE(TAG, "Failed to send AT command");
    }
}

// Function to check the SIM card ready or not
bool gsm_sim_check() 
{
    char response[128];
    memset(response, 0, sizeof(response)); // Clear response buffer 
    // Send "AT" command to check if the modem is responding
    esp_err_t ret = gsm_send_at_command_queue("AT+CPIN?\r\n", response, sizeof(response));
    if (ret == ESP_OK) 
    {
        if (!strstr(response, "OK")) 
        {
            ESP_LOGW(TAG, "Unexpected response: %s", response);
            return false;
        }
    } 
    else 
    {
        ESP_LOGE(TAG, "Failed to send AT command");
        return false;
    }

    return true;
}

static bool gsm_check_network(void)
{
    char response[128];
    bool registered = true;
    bool gprs_attached = true;

    // ---- Cek CREG (circuit-switched) ----
    gsm_send_at_command_queue("AT+CREG?\r\n", response, sizeof(response));

    if (strstr(response, "0,1"))
        ESP_LOGI("GSM", "Registered to home network (CS Domain)");
    else if (strstr(response, "0,5"))
        ESP_LOGI("GSM", "Registered, roaming (CS Domain)");
    else
    {
        ESP_LOGW(TAG, "Not registered to packet-switched network.");
        registered = false;
    }
    // ---- Cek CEREG (LTE/packet service) ----
    memset(response, 0, sizeof(response));
    gsm_send_at_command_queue("AT+CEREG?\r\n", response, sizeof(response));

    if (strstr(response, "0,1"))
        ESP_LOGI("GSM", "Registered to LTE network (EPS/Packet Domain)");
    else if (strstr(response, "0,5"))
        ESP_LOGI("GSM", "Registered, roaming (LTE EPS)");
    else
    {
        ESP_LOGW("GSM", "Not registered to LTE EPS yet.");
        registered = false;
    }

    // ---- Cek GPRS Attach ----
    memset(response, 0, sizeof(response));
    gsm_send_at_command_queue("AT+CGATT?\r\n", response, sizeof(response));

    if (strstr(response, "1"))
        ESP_LOGI("GSM", "Data service attached (Internet Ready)");
    else
    {
        ESP_LOGW("GSM", "Data service NOT attached yet.");
        gprs_attached = false;
    }

    // ---- Cek RSSI kalau sudah register ----
    if (registered) {
        memset(response, 0, sizeof(response));
        gsm_send_at_command_queue("AT+CSQ\r\n", response, sizeof(response));
    }

    return (registered && gprs_attached);
}


static bool gsm_get_time(void)
{
    char response[128];
    gsm_send_at_command_queue("AT+CCLK?\r\n", response, sizeof(response));

    // Ambil isi di dalam tanda kutip
    char *time_str = strstr(response, "\"");
    if (!time_str) return false;

    time_str++; // skip tanda "
    char *end = strchr(time_str, '"');
    if (end) *end = '\0';

    ESP_LOGI(TAG, "Parsed Time: %s", time_str);

    // Kalau masih default "80/..." berarti waktu belum valid
    if (strncmp(time_str, "80/", 3) == 0) {
        ESP_LOGW(TAG, "Time not valid yet...");
        return false;
    }

    ESP_LOGI(TAG, "Time is valid.");
    return true;
}

void gsm_wait_for_network_and_time(void)
{
    ESP_LOGI(TAG, "Waiting for network registration and valid time...");

    bool network_ready = false;
    bool time_ready    = false;

    while (!(network_ready && time_ready)) 
    {
        network_ready = gsm_check_network();
        time_ready    = gsm_get_time();

        if (!(network_ready && time_ready)) 
        {
            ESP_LOGI(TAG, "Retrying in 2s...");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    ESP_LOGI(TAG, "Network registered and time is valid!");
}

void gsm_get_imei(void)
{
    char response[128];
    char gsm_imei[20] = {0};
    int gsm_imei_len = 0;
    memset(response, 0, sizeof(response)); 
    memset(gsm_imei, 0, sizeof(gsm_imei));
    gsm_imei_len = 0;

    // Kirim AT command
    esp_err_t ret = gsm_send_at_command_queue("AT+CGSN\r\n", response, sizeof(response));
    if (ret == ESP_OK) 
    {
        // Cari baris pertama (IMEI ada di line pertama)
        char *line = strtok(response, "\r\n");
        while (line != NULL) {
            if (strspn(line, "0123456789") >= 14) {  
                // kemungkinan besar IMEI
                strncpy(gsm_imei, line, sizeof(gsm_imei) - 1);
                gsm_imei[sizeof(gsm_imei) - 1] = '\0';
                gsm_imei_len = strlen(gsm_imei);
                ESP_LOGI(TAG, "IMEI: %s (len=%d)", gsm_imei, gsm_imei_len);
                return;
            }
            line = strtok(NULL, "\r\n");
        }

        ESP_LOGW(TAG, "IMEI not found, raw response: %s", response);
    } 
    else 
    {
        ESP_LOGE(TAG, "Failed to send AT command");
    }
}

bool gsm_enable_internet(const char *apn)
{
    char response[256];

    ESP_LOGI(TAG, "Enabling internet with APN: %s", apn);

    // 1. Set APN (PDP context 1, IPv4, APN sesuai operator)
    snprintf(response, sizeof(response), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", apn);
    gsm_send_at_command_queue(response, response, sizeof(response));    

    if (strstr(response, "ERROR")) 
    {
        ESP_LOGE(TAG, "Failed to set APN");
        return false;
    }
    memset(response, 0, sizeof(response)); // Clear response buffer

    // 2. Activate PDP context (context 1)
    gsm_send_at_command_queue("AT+CGACT=1,1\r\n", response, sizeof(response));

    if (strstr(response, "ERROR")) 
    {
        ESP_LOGE(TAG, "Failed to activate PDP context");
        return false;
    }
    memset(response, 0, sizeof(response));

    // 3. Verify PDP is active
    gsm_send_at_command_queue("AT+CGACT?\r\n", response, sizeof(response));

    if (!strstr(response, "1,1")) {
        ESP_LOGW(TAG, "PDP context not active yet...");
        return false;
    }
    memset(response, 0, sizeof(response));

    // 4. CEK OPERATOR
    gsm_send_at_command_queue("AT+COPS?\r\n", response, sizeof(response));

    if (strstr(response, "ERROR")) {
        ESP_LOGW(TAG, "Not registered to any card");
        return false;
    }
    memset(response, 0, sizeof(response));

    // 5. CEK NETWORK INFO
    gsm_send_at_command_queue("AT+CPSI?\r\n", response, sizeof(response));

    if (strstr(response, "ERROR")) {
        ESP_LOGW(TAG, "Not registered to any card");
        return false;
    }
    memset(response, 0, sizeof(response));

    ESP_LOGI(TAG, "Internet is enabled and reachable.");
    return true;
}