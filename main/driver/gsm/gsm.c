#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gsm.h"

static const char *TAG = "GSM";

static SemaphoreHandle_t gsm_mutex;
QueueHandle_t gsm_urc_queue  = NULL;
QueueHandle_t gsm_resp_queue = NULL;

typedef enum {
    GSM_IDLE = 0,
    GSM_BUSY
} gsm_state_t;

static gsm_state_t gsm_state = GSM_IDLE;

// Function to initialize UART for SIM7600E
void gsm_uart_init()
{
    gsm_mutex = xSemaphoreCreateMutex();

    if (gsm_mutex == NULL) {
        ESP_LOGE("GSM", "Failed to create GSM mutex");
    }

    uart_config_t uart_config = 
    {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,  // No RTS/CTS flow control
        .rx_flow_ctrl_thresh = 122,
    };

    ESP_ERROR_CHECK(uart_driver_install(MODEM_UART_PORT, UART_BUF_SIZE * 2, UART_BUF_SIZE * 2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MODEM_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(MODEM_UART_PORT, MODEM_TX_PIN, MODEM_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

// Function to power on the SIM7600E by controlling PWRKEY
void gsm_power_on() 
{
    gpio_reset_pin(PWRKEY_PIN);
    gpio_set_direction(PWRKEY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PWRKEY_PIN, 0); // Pull PWRKEY low to power ON
    vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for 1.1 seconds
    gpio_set_level(PWRKEY_PIN, 1); // Release PWRKEY (set high)
}

// Fungsi bantu: trim CRLF dari string
static void normalize_response(char *str) 
{
    int i, j = 0;
    for (i = 0; str[i] != '\0'; i++) 
    {
        if (str[i] == '\r' || str[i] == '\n') 
        {
            // Ganti CR/LF dengan spasi (hanya jika bukan duplikat spasi)
            if (j > 0 && str[j-1] != ' ') 
            {
                str[j++] = ' ';
            }
        } 
        else 
        {
            str[j++] = str[i];
        }
    }
    str[j] = '\0'; // null-terminate
}

esp_err_t gsm_cipsend_start(int link_id, int len)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%d\r\n", link_id, len);

    char resp[128];
    esp_err_t ret = gsm_send_at_command_queue(cmd, resp, sizeof(resp));
    if (ret == ESP_OK && strstr(resp, ">")) 
    {
        ESP_LOGI(TAG, "CIPSEND prompt received");
        return ESP_OK;
    }
    ESP_LOGE(TAG, "CIPSEND failed, response: %s", resp);
    return ESP_FAIL;
}

esp_err_t gsm_cipsend_payload_use_queue(const char *data, int len) 
{
    // 1. Kirim payload mentah
    int written = uart_write_bytes(MODEM_UART_PORT, data, len);
    if (written != len) {
        ESP_LOGE(TAG, "UART write mismatch (written=%d, expected=%d)", written, len);
        return ESP_FAIL;
    }

    // 2. Kirim CTRL+Z untuk akhir data
    uint8_t ctrlz = 0x1A;
    uart_write_bytes(MODEM_UART_PORT, (const char *)&ctrlz, 1);

    // 3. Tunggu respon dari modem
    gsm_msg_t rx;
    if (xQueueReceive(gsm_resp_queue, &rx, pdMS_TO_TICKS(10000)) == pdTRUE) {
        if (strstr(rx.data, "SEND OK") || strstr(rx.data, "+CIPSEND")) {
            ESP_LOGI(TAG, "Payload sent OK");
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Unexpected response after payload: %s", rx.data);
        }
    } else {
        ESP_LOGE(TAG, "Timeout waiting SEND OK");
    }

    return ESP_FAIL;
}


esp_err_t gsm_send_at_command_queue(const char *cmd, char *response, size_t resp_size)
{
    if (!gsm_mutex) {
        ESP_LOGE(TAG, "gsm_mutex not initialized!");
        return ESP_FAIL;
    }

    // Ambil mutex sebelum kirim command
    if (xSemaphoreTake(gsm_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take GSM mutex for command %s", cmd);
        return ESP_FAIL;
    }

    if (gsm_state == GSM_BUSY) {
        ESP_LOGW(TAG, "Modem busy, skip command: %s", cmd);
        xSemaphoreGive(gsm_mutex);
        return ESP_FAIL;
    }

    gsm_state = GSM_BUSY;   // tandai modem sibuk

    // --- Bagian asli kamu ---
    char clean_cmd[64];
    strncpy(clean_cmd, cmd, sizeof(clean_cmd)-1);
    clean_cmd[sizeof(clean_cmd)-1] = '\0';
    for (int i=0; clean_cmd[i]; i++) {
        if (clean_cmd[i] == '\r' || clean_cmd[i] == '\n') { clean_cmd[i] = '\0'; break; }
    }
    ESP_LOGI(TAG, "AT+COMMAND: %s", clean_cmd);

    // write to UART
    uart_write_bytes(MODEM_UART_PORT, cmd, strlen(cmd));

    // tunggu response dari gsm_resp_queue
    gsm_msg_t rx;
    esp_err_t ret = ESP_FAIL;
    if (xQueueReceive(gsm_resp_queue, &rx, pdMS_TO_TICKS(5000)) == pdTRUE) {
        if (response && resp_size > 0) {
            strncpy(response, rx.data, resp_size-1);
            response[resp_size-1] = '\0';
        }
        if (strstr(rx.data, "OK")) {
            ret = ESP_OK;
        } else if (strstr(rx.data, "ERROR")) {
            ret = ESP_FAIL;
        } else {
            // default masih dianggap OK, biar bisa diproses lebih lanjut
            ret = ESP_OK;
        }
    } else {
        ESP_LOGW(TAG, "Timeout waiting AT response");
        if (response && resp_size > 0) response[0] = '\0';
    }


    gsm_state = GSM_IDLE;
    // Lepas mutex
    xSemaphoreGive(gsm_mutex);

    return ret;
}

static void parse_tcp_payload(const char *input, char *out, size_t out_size) 
{
    // Cari substring "+IPD"
    const char *p = strstr(input, "+IPD");
    if (!p) {
        out[0] = '\0';
        return;
    }

    p += 4; // skip "+IPD"

    // Ambil panjang payload
    int len = atoi(p);
    if (len <= 0) {
        out[0] = '\0';
        return;
    }

    // Skip angka
    while (*p >= '0' && *p <= '9') p++;
    while (*p == ' ') p++;

    // <<< FIX: skip \r\n kalau ada
    if (*p == '\r') p++;
    if (*p == '\n') p++;

    // Copy payload sesuai len
    size_t copy_len = (len < (int)(out_size - 1)) ? len : (out_size - 1);
    memcpy(out, p, copy_len);
    out[copy_len] = '\0';

    ESP_LOGI("TCP", "TCP Payload extracted (len=%d)", len);

    // HEX print
    // printf("HEX: ");
    // for (int i = 0; i < copy_len; i++) {
    //     printf("%02X ", (uint8_t)out[i]);
    // }
    // printf("\n");

    // ASCII print
    char ascii_buf[256];
    int pos = 0;
    for (int i = 0; i < copy_len && pos < sizeof(ascii_buf) - 1; i++) {
        uint8_t c = (uint8_t)out[i];
        if (c >= 0x20 && c <= 0x7E) {
            ascii_buf[pos++] = c;
        } else {
            ascii_buf[pos++] = '.';
        }
    }
    ascii_buf[pos] = '\0';
    // ESP_LOGI("TCP", "ASCII: %s", ascii_buf);
}


void gsm_uart_reader_task(void *arg)
{
    uint8_t raw[512];
    while (1) 
    {
        int len = uart_read_bytes(MODEM_UART_PORT, raw, sizeof(raw)-1, pdMS_TO_TICKS(500));
        if (len > 0) 
        {
            // 1. Null-terminate the received data
            if (len >= (int)sizeof(raw)) len = sizeof(raw)-1;
            raw[len] = '\0';

            // 2. create gsm_msg_t and copy data
            gsm_msg_t msg;
            memset(&msg, 0, sizeof(msg));
            strncpy(msg.data, (char*)raw, sizeof(msg.data)-1);

            // cek kalau ada payload +IPD
            char payload[128];
            parse_tcp_payload(msg.data, payload, sizeof(payload));
            if (strlen(payload) > 0) // Jika ada payload    
            {
                // Tampilkan payload TCP
                int plen = strlen(payload); // panjang payload

                // Cek apakah semua karakter adalah ASCII
                bool all_ascii = true;
                for (int i = 0; i < plen; i++) 
                {
                    uint8_t c = (uint8_t)payload[i];
                    if (!(c >= 0x20 && c <= 0x7E)) 
                    {
                        all_ascii = false;
                        break;
                    }
                }

                // Tampilkan payload TCP
                if (all_ascii) 
                {
                    // Tampilkan ASCII
                    ESP_LOGI(TAG, "TCP Payload (ASCII): %.*s", plen, payload);
                } 
                else 
                {
                    // Tampilkan HEX
                    char hexbuf[512];
                    int pos = 0;
                    for (int i = 0; i < plen && pos < sizeof(hexbuf) - 3; i++) 
                    {
                        pos += snprintf(hexbuf + pos, sizeof(hexbuf) - pos, "%02X ", (uint8_t)payload[i]);
                    }
                    ESP_LOGI(TAG, "TCP Payload (HEX): %s", hexbuf);
                }
            }

            // classify: if starts with '+' => URC; if contains OK/ERROR => response
            if (msg.data[0] == '+') 
            {
                if (gsm_urc_queue) 
                {
                    xQueueSend(gsm_urc_queue, &msg, 0);
                }
                ESP_LOGI(TAG, "URC received: %s", msg.data);
            } else if (strstr(msg.data, "OK") || strstr(msg.data, "ERROR") || 
                        strstr(msg.data, ">") || strstr(msg.data, "CONNECT")) {
                if (gsm_resp_queue) 
                {
                    xQueueSend(gsm_resp_queue, &msg, 0);
                }
                normalize_response(msg.data);
                ESP_LOGI(TAG, "AT response: %s", msg.data);
            } 
            else 
            {
                normalize_response(msg.data);
                ESP_LOGI(TAG, "Other raw: %s", msg.data);
            }
        }
    }
}