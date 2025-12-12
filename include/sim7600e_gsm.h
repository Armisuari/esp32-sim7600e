#ifndef SIM7600E_GSM_H
#define SIM7600E_GSM_H

#include "esp_err.h"
#include "sim7600e.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GSM network status
 */
typedef enum {
    SIM7600E_GSM_STATUS_NOT_REGISTERED = 0,
    SIM7600E_GSM_STATUS_REGISTERED_HOME = 1,
    SIM7600E_GSM_STATUS_SEARCHING = 2,
    SIM7600E_GSM_STATUS_DENIED = 3,
    SIM7600E_GSM_STATUS_UNKNOWN = 4,
    SIM7600E_GSM_STATUS_REGISTERED_ROAMING = 5
} sim7600e_gsm_status_t;

/**
 * @brief Network information structure
 */
typedef struct {
    sim7600e_gsm_status_t status;
    char operator_name[32];
    int signal_strength;  // RSSI in dBm
    char network_time[32]; // Network time string
} sim7600e_network_info_t;

/**
 * @brief Check if GSM modem is responding
 * 
 * @return 
 *     - ESP_OK: Modem is responding
 *     - ESP_FAIL: Modem not responding
 */
esp_err_t sim7600e_gsm_check_modem(void);

/**
 * @brief Check SIM card status
 * 
 * @return 
 *     - ESP_OK: SIM card is ready
 *     - ESP_FAIL: SIM card error or not present
 */
esp_err_t sim7600e_gsm_check_sim(void);

/**
 * @brief Turn off echo mode
 * 
 * @return 
 *     - ESP_OK: Success
 *     - ESP_FAIL: Failed to turn off echo
 */
esp_err_t sim7600e_gsm_turn_off_echo(void);

/**
 * @brief Wait for network registration and time synchronization
 * 
 * @param timeout_ms Timeout in milliseconds
 * @return 
 *     - ESP_OK: Network registered and time synchronized
 *     - ESP_ERR_TIMEOUT: Timeout waiting for network
 *     - ESP_FAIL: Failed to register to network
 */
esp_err_t sim7600e_gsm_wait_for_network(uint32_t timeout_ms);

/**
 * @brief Enable internet connection with APN
 * 
 * @param apn Access Point Name string
 * @return 
 *     - ESP_OK: Internet connection established
 *     - ESP_ERR_INVALID_ARG: Invalid APN
 *     - ESP_FAIL: Failed to establish internet connection
 */
esp_err_t sim7600e_gsm_enable_internet(const char *apn);

/**
 * @brief Get network information
 * 
 * @param info Pointer to network info structure
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_FAIL: Failed to get network info
 */
esp_err_t sim7600e_gsm_get_network_info(sim7600e_network_info_t *info);

/**
 * @brief Send raw AT command
 * 
 * @param cmd AT command string
 * @param response Buffer to store response
 * @param resp_size Size of response buffer
 * @param timeout_ms Timeout in milliseconds
 * @return 
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_ERR_TIMEOUT: Timeout waiting for response
 *     - ESP_FAIL: Command failed
 */
esp_err_t sim7600e_gsm_send_at_command(const char *cmd, char *response, size_t resp_size, uint32_t timeout_ms);

/**
 * @brief Send SMS message
 * 
 * @param phone_number Destination phone number
 * @param message SMS message text
 * @return 
 *     - ESP_OK: SMS sent successfully
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_FAIL: Failed to send SMS
 */
esp_err_t sim7600e_gsm_send_sms(const char *phone_number, const char *message);

/**
 * @brief Make a voice call
 * 
 * @param phone_number Phone number to call
 * @return 
 *     - ESP_OK: Call initiated successfully
 *     - ESP_ERR_INVALID_ARG: Invalid phone number
 *     - ESP_FAIL: Failed to initiate call
 */
esp_err_t sim7600e_gsm_make_call(const char *phone_number);

/**
 * @brief Hang up current call
 * 
 * @return 
 *     - ESP_OK: Call ended successfully
 *     - ESP_FAIL: Failed to end call
 */
esp_err_t sim7600e_gsm_hang_up(void);

#ifdef __cplusplus
}
#endif

#endif // SIM7600E_GSM_H