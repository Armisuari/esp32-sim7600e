#ifndef GSM_H
#define GSM_H

#include "esp_err.h"

// #define MODEM_UART_PORT    UART_NUM_1
#define MODEM_UART_PORT    UART_NUM_2
#define MODEM_TX_PIN       2 // 13
#define MODEM_RX_PIN       1 // 12
#define PWRKEY_PIN         41

#define UART_BAUD_RATE     115200
#define UART_BUF_SIZE      1024

#define GSM_MSG_LEN 512

typedef struct {
    char data[GSM_MSG_LEN];
} gsm_msg_t;



// Function to initialize UART for GSM communication
void gsm_uart_init(void);

// Function to power on the SIM7600E by controlling PWRKEY
void gsm_power_on(void);

esp_err_t gsm_cipsend_start(int link_id, int len);

esp_err_t gsm_cipsend_payload_use_queue(const char *data, int len);

esp_err_t gsm_send_at_command_queue(const char *cmd, char *response, size_t resp_size);

void gsm_uart_reader_task(void *arg);

bool gsm_dial_ppp(void);

extern QueueHandle_t gsm_urc_queue;
extern QueueHandle_t gsm_resp_queue;

#endif // GSM_H
