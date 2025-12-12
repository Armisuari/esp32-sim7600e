#include <string.h>
#include <stdio.h>
#include <time.h>  // Tambahkan ini untuk timestamp
#include <ctype.h>
#include <ctype.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system/tcp_system.h"
#include "system/gnss_system.h"
#include "driver/uart.h"
#include "gsm/gsm.h"

#define TAG "TCP_SIM7600"
#define MODEM_UART_PORT UART_NUM_1

ring_buffer_t uart_rb = { .head = 0, .tail = 0 };
volatile bool echo_received = false;

const char* tcp_ip = "118.99.122.238";
int tcpbin_port = 4242;
const char* msg = "HELLO ESP";

const char* tcp_ip_traccar = "35.213.135.157";
int tcpbin_port_traccar = 5027;
uint8_t imei_packet[] = {
    0x00, 0x0F,                                     // length = 15
    '3','5','6','3','0','7','0','4','2','4','4','1','0','1','5' // ASCII IMEI
};


#define TCP_INIT_MAX_RETRY     5
#define TCP_CONNECT_MAX_RETRY  5
#define TCP_RETRY_DELAY_MS     1000

int uart_rb_read(uint8_t *dst, int max_len)
{
    int count = 0;
    while(uart_rb.tail != uart_rb.head && count < max_len) {
        dst[count++] = uart_rb.buf[uart_rb.tail];
        uart_rb.tail = (uart_rb.tail + 1) % UART_BUF_SIZE;
    }
    return count;
}

bool sim7600_tcp_init_use_queue(void)
{
    char buf[128];

    esp_err_t res = gsm_send_at_command_queue("AT+NETOPEN\r\n", buf, sizeof(buf));

    if (res != ESP_OK) 
    {
        // Check if network is already open
        if (strstr(buf, "Network is already opened") != NULL) 
        {
            ESP_LOGW(TAG, "Network already opened, continue...");
        } 
        else 
        {
            ESP_LOGE(TAG, "Failed to open network: %s", buf);
            return false;
        }
    }

    ESP_LOGI(TAG, "Internet ready");
    return true;
}

// ---------------- TCP CONNECT ----------------
bool sim7600_tcp_connect_use_queue(const char* ip, int port)
{
    char buf[128];

    snprintf(buf, sizeof(buf), "AT+CIPOPEN=%d,\"TCP\",\"%s\",%d\r\n", TCP_CONN_ID, ip, port);
    if (gsm_send_at_command_queue(buf, buf, sizeof(buf)) != ESP_OK) return false;

    ESP_LOGI(TAG, "TCP connected to %s:%d", ip, port);

    // Set push mode (auto receive)
    if (gsm_send_at_command_queue("AT+CIPRXGET=0\r\n", buf, sizeof(buf)) != ESP_OK) return false;

    return true;
}

// ---------------- TCP SEND ----------------
bool sim7600_tcp_send(const char* data, size_t len)
{
    // 1. Start CIPSEND dengan panjang payload
    if (gsm_cipsend_start(TCP_CONN_ID, len) != ESP_OK) 
    {
        ESP_LOGE(TAG, "CIPSEND start failed");
        return false;
    }

    // 2. Kirim payload
    if (gsm_cipsend_payload_use_queue(data, len) != ESP_OK) 
    {
        ESP_LOGE(TAG, "CIPSEND payload failed");
        return false;
    }

    ESP_LOGI(TAG, "Sent %d bytes", len);
    return true;
}

// ---------------- TCP CLOSE ----------------
bool sim7600_tcp_close()
{
    char buf[64];

    snprintf(buf, sizeof(buf), "AT+CIPCLOSE=%d\r\n", TCP_CONN_ID);
    gsm_send_at_command_queue(buf, buf, sizeof(buf));

    gsm_send_at_command_queue("AT+NETCLOSE\r\n", buf, sizeof(buf));
    ESP_LOGI(TAG, "TCP closed");

    return true;
}


bool tcp_init_with_retry(void)
{
    for (int i = 0; i < TCP_INIT_MAX_RETRY; i++)
    {
        if (sim7600_tcp_init_use_queue()) {
            ESP_LOGI(TAG, "TCP init success");
            return true;
        } else {
            ESP_LOGW(TAG, "TCP init failed, retrying (%d/%d)...", i + 1, TCP_INIT_MAX_RETRY);
            vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
        }
    }

    ESP_LOGE(TAG, "TCP init failed after %d retries", TCP_INIT_MAX_RETRY);
    return false;
}

bool tcp_connect_with_retry(const char *ip, int port)
{
    for (int i = 0; i < TCP_CONNECT_MAX_RETRY; i++)
    {
        if (sim7600_tcp_connect_use_queue(ip, port)) {
            ESP_LOGI(TAG, "TCP connect success");
            return true;
        } else {
            ESP_LOGW(TAG, "TCP connect failed, retrying (%d/%d)...", i + 1, TCP_CONNECT_MAX_RETRY);
            vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
        }
    }

    ESP_LOGE(TAG, "TCP connect failed after %d retries", TCP_CONNECT_MAX_RETRY);
    return false;
}

// ---------------- TCP RECEIVE TASK USE QUEUE ----------------
void tcp_receiver_task_use_queue(void *arg)
{
    gsm_msg_t rx;

    for (;;)
    {
        if (xQueueReceive(gsm_urc_queue, &rx, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        char *ipd = strstr(rx.data, "+IPD");
        if (!ipd) {
            ESP_LOGI(TAG, "URC: %s", rx.data);
            continue;
        }

        ipd += 4; // lompat "+IPD"

        // Ambil panjang payload
        int len = atoi(ipd);

        // Lompat digit
        while (*ipd && isdigit((unsigned char)*ipd)) ipd++;
        // Lompat spasi
        while (*ipd == ' ') ipd++;

        char *payload = ipd;

        if (len > 0 && payload)
        {
            bool ascii_only = true;
            for (int i = 0; i < len; i++) {
                if ((unsigned char)payload[i] < 0x20 || (unsigned char)payload[i] > 0x7E) {
                    ascii_only = false;
                    break;
                }
            }

            ESP_LOGI(TAG, "Got IPD len=%d", len);

            if (ascii_only) {
                ESP_LOGI(TAG, "Payload (ASCII): %.*s", len, payload);
            } else {
                // Kumpulin ke buffer HEX
                char hexbuf[512];
                int pos = 0;
                for (int i = 0; i < len && pos < sizeof(hexbuf) - 3; i++) {
                    pos += snprintf(&hexbuf[pos], sizeof(hexbuf) - pos, "%02X ", (uint8_t)payload[i]);
                }
                ESP_LOGI(TAG, "Payload (HEX): %s", hexbuf);
            }
        }
        else {
            ESP_LOGE(TAG, "IPD parse failed: %s", rx.data);
        }
    }
}

void tcp_test_task_hello(void)
{
    // Mulai TCP Echo test
    ESP_LOGI(TAG, "Starting TCP Echo test...");

    // Inisialisasi TCP
    if (!sim7600_tcp_init_use_queue()) 
    {
        ESP_LOGE(TAG, "Failed to init TCP");
        return;
    }

    // Koneksi TCP
    if (!sim7600_tcp_connect_use_queue(tcp_ip, tcpbin_port)) 
    {
        ESP_LOGE(TAG, "Failed to connect TCP server");
        return;
    }

    // Start TCP receiver task (consumes gsm_urc_queue)
    xTaskCreate(tcp_receiver_task_use_queue, "tcp_receiver_task", 4096, NULL, 9, NULL);

    // Start an example sender task (opsional) — kirim sekali atau periodik
    if (!sim7600_tcp_send(msg, strlen(msg))) 
    {
        ESP_LOGE(TAG, "Failed to send TCP data");
        sim7600_tcp_close();
    } 
    else 
    {
        ESP_LOGI(TAG, "'%s' has been sent", msg);
        sim7600_tcp_close();
    }
}


void tcp_test_task_gnss(void *arg)
{
    ESP_LOGI(TAG, "Starting TCP GNSS task...");

    if (!sim7600_tcp_init_use_queue()) {
        ESP_LOGE(TAG, "Failed to init TCP");
        vTaskDelete(NULL);
        return;
    }

    if (!sim7600_tcp_connect_use_queue(tcp_ip, tcpbin_port)) {
        ESP_LOGE(TAG, "Failed to connect TCP server");
        vTaskDelete(NULL);
        return;
    }

    xTaskCreate(tcp_receiver_task_use_queue, "tcp_receiver_task", 4096, NULL, 9, NULL);

    gps_info_t gnss_data;
    gps_info_t last_sent = {0};
    char payload[128];

    const TickType_t delay_ticks = pdMS_TO_TICKS(1000); // 1 detik
    TickType_t start_ticks = xTaskGetTickCount();

    while (1) 
    {
        // Cek timeout 1 menit
        if ((xTaskGetTickCount() - start_ticks) >= pdMS_TO_TICKS(60000)) {
            ESP_LOGI(TAG, "TCP task timeout 1 menit, deleting task...");
            break;
        }

        // Ambil data terbaru, non-blocking
        if (xQueueReceive(gnss_queue, &gnss_data, 0) == pdPASS) 
        {
            // Bandingkan dengan data terakhir
            if (gnss_data.latitude  != last_sent.latitude ||
                gnss_data.longitude != last_sent.longitude ||
                gnss_data.altitude  != last_sent.altitude ||
                gnss_data.speed     != last_sent.speed)
            {
                snprintf(payload, sizeof(payload),
                         "LAT:%.6f,LON:%.6f,ALT:%.2f,SPEED:%.2f,TIME:%s",
                         gnss_data.latitude,
                         gnss_data.longitude,
                         gnss_data.altitude,
                         gnss_data.speed,
                         gnss_data.timestamp);

                if (!sim7600_tcp_send(payload, strlen(payload))) {
                    ESP_LOGE(TAG, "Failed to send TCP data");
                } else {
                    ESP_LOGI(TAG, "Sent: %s", payload);
                    last_sent = gnss_data;
                }
            }
        }

        vTaskDelay(delay_ticks);
    }

    sim7600_tcp_close();
    vTaskDelete(NULL);
}


void tcp_test_task_teltonika(void)
{
    // Mulai TCP Echo test
    ESP_LOGI(TAG, "Starting teltonika TCP test...");

    // Inisialisasi TCP
    if (!sim7600_tcp_init_use_queue()) 
    {
        ESP_LOGE(TAG, "Failed to init TCP");
        return;
    }

    // Koneksi TCP
    if (!sim7600_tcp_connect_use_queue(tcp_ip_traccar, tcpbin_port_traccar)) 
    {
        ESP_LOGE(TAG, "Failed to connect TCP server");
        return;
    }

    // Start TCP receiver task (consumes gsm_urc_queue)
    xTaskCreate(tcp_receiver_task_use_queue, "tcp_receiver_task", 4096, NULL, 9, NULL);

    // Start an example sender task (opsional) — kirim sekali atau periodik
    if (!sim7600_tcp_send((const char*)imei_packet, sizeof(imei_packet))) 
    {
        ESP_LOGE(TAG, "Failed to send TCP data");
        sim7600_tcp_close();
    } 
    else 
    {
        
        ESP_LOGI(TAG, "%04X%s has been sent", (imei_packet[0] << 8) | imei_packet[1], (char*)&imei_packet[2]);
        sim7600_tcp_close();
    }
}


void tcp_test_task_gnss_for_loop(void *arg)
{
    gps_info_t gnss_data;
    gps_info_t last_sent = {0};
    char payload[128];

    if (!tcp_init_with_retry()) 
    {
        vTaskDelete(NULL);
        return;
    }

    if (!tcp_connect_with_retry(tcp_ip, tcpbin_port)) 
    {
        vTaskDelete(NULL);
        return;
    }

    TickType_t start_ticks = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start_ticks) < pdMS_TO_TICKS(600000)) // 1 menit
    {
        // tunggu signal dari GNSS task
        if (xSemaphoreTake(gnss_ready_sem, portMAX_DELAY) == pdPASS)
        {
            // ambil data terbaru dari queue
            if (xQueueReceive(gnss_queue, &gnss_data, 0) == pdPASS)
            {
                // kirim hanya kalau berbeda dari terakhir
                if (gnss_data.latitude  != last_sent.latitude ||
                    gnss_data.longitude != last_sent.longitude ||
                    gnss_data.altitude  != last_sent.altitude ||
                    gnss_data.speed     != last_sent.speed)
                {
                    snprintf(payload, sizeof(payload),
                             "LAT:%.6f,LON:%.6f,ALT:%.2f,SPEED:%.2f,TIME:%s",
                             gnss_data.latitude,
                             gnss_data.longitude,
                             gnss_data.altitude,
                             gnss_data.speed,
                             gnss_data.timestamp);

                    if (sim7600_tcp_send(payload, strlen(payload))) {
                        ESP_LOGI(TAG, "Sent: %s", payload);
                        last_sent = gnss_data;
                    } else {
                        ESP_LOGE(TAG, "Failed to send TCP data");
                    }

                    // delay supaya kirim maksimal 1 detik
                    vTaskDelay(pdMS_TO_TICKS(10000));
                }
            }
        }
    }

    sim7600_tcp_close();
    ESP_LOGI(TAG, "TCP task finished, deleting task...");
    vTaskDelete(NULL);
}
