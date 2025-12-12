#ifndef TCP_SYSTEM_H
#define TCP_SYSTEM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define TCP_CONN_ID 0
#define UART_BUF_SIZE 1024

typedef struct {
    uint8_t buf[UART_BUF_SIZE];
    volatile int head;
    volatile int tail;
} ring_buffer_t;

extern ring_buffer_t uart_rb;
extern volatile bool echo_received;

// UART ring buffer
void uart_rb_write(uint8_t *data, int len);
int uart_rb_read(uint8_t *dst, int max_len);

// TCP tasks
bool sim7600_tcp_close(void);
bool sim7600_tcp_init_use_queue(void);
bool sim7600_tcp_connect_use_queue(const char* ip, int port);
void tcp_receiver_task_use_queue(void *arg);
void tcp_test_task_hello(void);
void tcp_test_task_gnss(void *arg);
void tcp_test_task_teltonika(void);
void tcp_test_task_gnss_for_loop(void *arg);

#endif
