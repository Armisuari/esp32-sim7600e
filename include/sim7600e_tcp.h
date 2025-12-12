#ifndef SIM7600E_TCP_H
#define SIM7600E_TCP_H

#include "esp_err.h"
#include "sim7600e.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TCP connection status
 */
typedef enum {
    SIM7600E_TCP_DISCONNECTED = 0,
    SIM7600E_TCP_CONNECTING = 1,
    SIM7600E_TCP_CONNECTED = 2,
    SIM7600E_TCP_DISCONNECTING = 3,
    SIM7600E_TCP_ERROR = 4
} sim7600e_tcp_status_t;

/**
 * @brief TCP connection configuration
 */
typedef struct {
    char host[128];          /*!< Remote host address */
    uint16_t port;           /*!< Remote port number */
    uint32_t timeout_ms;     /*!< Connection timeout in milliseconds */
    bool keep_alive;         /*!< Enable TCP keep-alive */
    uint32_t keep_alive_idle; /*!< Keep-alive idle time in seconds */
    uint32_t keep_alive_interval; /*!< Keep-alive probe interval in seconds */
    uint32_t keep_alive_count;    /*!< Keep-alive probe count */
} sim7600e_tcp_config_t;

/**
 * @brief TCP data received callback function type
 * 
 * @param data Pointer to received data buffer
 * @param len Length of received data
 * @param user_data User data passed during callback registration
 */
typedef void (*sim7600e_tcp_recv_cb_t)(const uint8_t *data, size_t len, void *user_data);

/**
 * @brief TCP connection status callback function type
 * 
 * @param status Current connection status
 * @param user_data User data passed during callback registration
 */
typedef void (*sim7600e_tcp_status_cb_t)(sim7600e_tcp_status_t status, void *user_data);

/**
 * @brief Open TCP connection
 * 
 * @param config Pointer to TCP configuration
 * @return 
 *     - ESP_OK: Connection opened successfully
 *     - ESP_ERR_INVALID_ARG: Invalid configuration
 *     - ESP_ERR_TIMEOUT: Connection timeout
 *     - ESP_FAIL: Failed to open connection
 */
esp_err_t sim7600e_tcp_connect(const sim7600e_tcp_config_t *config);

/**
 * @brief Close TCP connection
 * 
 * @return 
 *     - ESP_OK: Connection closed successfully
 *     - ESP_FAIL: Failed to close connection
 */
esp_err_t sim7600e_tcp_disconnect(void);

/**
 * @brief Send data over TCP connection
 * 
 * @param data Pointer to data buffer to send
 * @param len Length of data to send
 * @param timeout_ms Send timeout in milliseconds
 * @return 
 *     - ESP_OK: Data sent successfully
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_ERR_INVALID_STATE: TCP not connected
 *     - ESP_ERR_TIMEOUT: Send timeout
 *     - ESP_FAIL: Failed to send data
 */
esp_err_t sim7600e_tcp_send(const uint8_t *data, size_t len, uint32_t timeout_ms);

/**
 * @brief Send string data over TCP connection
 * 
 * @param str String to send
 * @param timeout_ms Send timeout in milliseconds
 * @return 
 *     - ESP_OK: String sent successfully
 *     - ESP_ERR_INVALID_ARG: Invalid string
 *     - ESP_ERR_INVALID_STATE: TCP not connected
 *     - ESP_ERR_TIMEOUT: Send timeout
 *     - ESP_FAIL: Failed to send string
 */
esp_err_t sim7600e_tcp_send_string(const char *str, uint32_t timeout_ms);

/**
 * @brief Receive data from TCP connection
 * 
 * @param data Pointer to buffer to store received data
 * @param len Maximum length of data to receive
 * @param received Pointer to store actual received length
 * @param timeout_ms Receive timeout in milliseconds
 * @return 
 *     - ESP_OK: Data received successfully
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_ERR_INVALID_STATE: TCP not connected
 *     - ESP_ERR_TIMEOUT: Receive timeout
 *     - ESP_FAIL: Failed to receive data
 */
esp_err_t sim7600e_tcp_receive(uint8_t *data, size_t len, size_t *received, uint32_t timeout_ms);

/**
 * @brief Get current TCP connection status
 * 
 * @return sim7600e_tcp_status_t Current connection status
 */
sim7600e_tcp_status_t sim7600e_tcp_get_status(void);

/**
 * @brief Register callback for received TCP data
 * 
 * @param callback Callback function to register
 * @param user_data User data to pass to callback
 * @return 
 *     - ESP_OK: Callback registered successfully
 *     - ESP_ERR_INVALID_ARG: Invalid callback
 */
esp_err_t sim7600e_tcp_register_recv_callback(sim7600e_tcp_recv_cb_t callback, void *user_data);

/**
 * @brief Register callback for TCP connection status changes
 * 
 * @param callback Callback function to register
 * @param user_data User data to pass to callback
 * @return 
 *     - ESP_OK: Callback registered successfully
 *     - ESP_ERR_INVALID_ARG: Invalid callback
 */
esp_err_t sim7600e_tcp_register_status_callback(sim7600e_tcp_status_cb_t callback, void *user_data);

/**
 * @brief Unregister TCP receive callback
 * 
 * @return 
 *     - ESP_OK: Callback unregistered successfully
 */
esp_err_t sim7600e_tcp_unregister_recv_callback(void);

/**
 * @brief Unregister TCP status callback
 * 
 * @return 
 *     - ESP_OK: Callback unregistered successfully
 */
esp_err_t sim7600e_tcp_unregister_status_callback(void);

/**
 * @brief Get default TCP configuration
 * 
 * @return sim7600e_tcp_config_t Default TCP configuration
 */
sim7600e_tcp_config_t sim7600e_tcp_get_default_config(void);

#ifdef __cplusplus
}
#endif

#endif // SIM7600E_TCP_H