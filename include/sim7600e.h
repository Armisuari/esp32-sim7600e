/**
 * @file sim7600e.h
 * @brief SIM7600E 4G LTE Module Driver for ESP32
 * 
 * This component provides a high-level interface for the SIM7600E cellular module,
 * including GSM/4G connectivity, GNSS positioning, and TCP communication.
 * 
 * @author ESP32 SIM7600E Component
 * @version 1.0.0
 */

#ifndef SIM7600E_H
#define SIM7600E_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SIM7600E component version
 */
#define SIM7600E_VERSION_MAJOR 1
#define SIM7600E_VERSION_MINOR 0
#define SIM7600E_VERSION_PATCH 0

/**
 * @brief Default configuration values
 */
#define SIM7600E_DEFAULT_UART_PORT    UART_NUM_2
#define SIM7600E_DEFAULT_TX_PIN       2
#define SIM7600E_DEFAULT_RX_PIN       1
#define SIM7600E_DEFAULT_PWRKEY_PIN   41
#define SIM7600E_DEFAULT_BAUD_RATE    115200
#define SIM7600E_DEFAULT_UART_BUF_SIZE 1024

/**
 * @brief Message structure for communication
 */
#define SIM7600E_MSG_LEN 512

typedef struct {
    char data[SIM7600E_MSG_LEN];
} sim7600e_msg_t;

/**
 * @brief GPS/GNSS information structure
 */
typedef struct {
    double latitude;
    double longitude;
    double altitude;
    float speed;
    char timestamp[32]; // YYYY-MM-DD HH:MM:SS
} sim7600e_gps_info_t;

/**
 * @brief Configuration structure for SIM7600E module
 */
typedef struct {
    int uart_port;          /*!< UART port number (UART_NUM_0, UART_NUM_1, UART_NUM_2) */
    int tx_pin;             /*!< GPIO pin for UART TX */
    int rx_pin;             /*!< GPIO pin for UART RX */
    int pwrkey_pin;         /*!< GPIO pin for power key control */
    uint32_t baud_rate;     /*!< UART baud rate */
    uint32_t uart_buf_size; /*!< UART buffer size */
    
    struct {
        size_t urc_queue_size;   /*!< Size of URC (Unsolicited Result Code) queue */
        size_t resp_queue_size;  /*!< Size of response queue */
        size_t gnss_queue_size;  /*!< Size of GNSS data queue */
    } queue_config;
} sim7600e_config_t;

/**
 * @brief Get default configuration
 * 
 * @return sim7600e_config_t Default configuration structure
 */
sim7600e_config_t sim7600e_get_default_config(void);

/**
 * @brief Initialize the SIM7600E module
 * 
 * @param config Pointer to configuration structure
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_FAIL: Initialization failed
 */
esp_err_t sim7600e_init(const sim7600e_config_t *config);

/**
 * @brief Deinitialize the SIM7600E module
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_FAIL: Deinitialization failed
 */
esp_err_t sim7600e_deinit(void);

/**
 * @brief Power on the SIM7600E module
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_FAIL: Power on failed
 */
esp_err_t sim7600e_power_on(void);

/**
 * @brief Power off the SIM7600E module
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_FAIL: Power off failed
 */
esp_err_t sim7600e_power_off(void);

/**
 * @brief Get module information (IMEI, etc.)
 * 
 * @param imei Buffer to store IMEI (minimum 16 bytes)
 * @param imei_len Length of IMEI buffer
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_FAIL: Failed to get module info
 */
esp_err_t sim7600e_get_module_info(char *imei, size_t imei_len);

/**
 * @brief Get message queues for advanced usage
 * 
 * @param urc_queue Pointer to store URC queue handle
 * @param resp_queue Pointer to store response queue handle
 * @param gnss_queue Pointer to store GNSS queue handle
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_STATE: Module not initialized
 */
esp_err_t sim7600e_get_queues(QueueHandle_t *urc_queue, QueueHandle_t *resp_queue, QueueHandle_t *gnss_queue);

#ifdef __cplusplus
}
#endif

#endif // SIM7600E_H