#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "gsm/gsm.h"
#include "system/gsm_system.h"
#include "system/tcp_system.h"
#include "system/gnss_system.h"

// Log tag for debugging
static const char *TAG = "MAIN";

TaskHandle_t gsm_uart_reader_task_handle = NULL;


// Main function to initialize GSM and run the test
void app_main(void) 
{
    ESP_LOGI(TAG, "Initializing GSM module...");

    // Initialize UART for GSM
    gsm_uart_init();
    
    if (gsm_urc_queue == NULL) 
    {
        gsm_urc_queue = xQueueCreate(10, sizeof(gsm_msg_t));
    }
    if (gsm_resp_queue == NULL) 
    {
        gsm_resp_queue = xQueueCreate(5, sizeof(gsm_msg_t));
    }
    
    if (!gsm_urc_queue || !gsm_resp_queue) 
    {
        ESP_LOGE(TAG, "Failed to create queues");
        return;
    }

    if (gnss_queue == NULL) 
    {
        gnss_queue = xQueueCreate(1, sizeof(gps_info_t));
    }

    if (!gnss_queue) 
    {
        ESP_LOGE(TAG, "Failed to create GPS queue");
    }

    gnss_ready_sem = xSemaphoreCreateBinary();
    if (gnss_ready_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create GNSS semaphore");
        return;
    }

    xTaskCreate(gsm_uart_reader_task, "gsm_uart_reader_task", 4096, NULL, 10, &gsm_uart_reader_task_handle);

    // slight delay for uart reader to start
    vTaskDelay(pdMS_TO_TICKS(200));

    // Run GSM checks
    gsm_modem_check();
    // // gsm_turnoff_echo();
    // gsm_modem_check();
    gsm_get_imei();
    gsm_sim_check();
    // // gsm_wait_for_network_and_time();
    gsm_enable_internet("internet");
    gsm_enable_gnss();

    // Periodically get GPS info
    xTaskCreate(gnss_task, "gnss_task", 6144, NULL, 7, NULL);

    // Start TCP Echo test (once)
    // tcp_test_task_hello();
    // Start Teltonika TCP test  (once)
    // tcp_test_task_teltonika();

    // Start continuous TCP GNSS sender task
    // xTaskCreate(tcp_test_task_gnss_for_loop, "tcp_test_task", 8192, NULL, 8, NULL);
}