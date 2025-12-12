#ifndef SIM7600E_GNSS_H
#define SIM7600E_GNSS_H

#include "esp_err.h"
#include "sim7600e.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GNSS fix status
 */
typedef enum {
    SIM7600E_GNSS_NO_FIX = 0,
    SIM7600E_GNSS_2D_FIX = 1,
    SIM7600E_GNSS_3D_FIX = 2
} sim7600e_gnss_fix_status_t;

/**
 * @brief GNSS constellation types
 */
typedef enum {
    SIM7600E_GNSS_GPS = 0x01,
    SIM7600E_GNSS_GLONASS = 0x02,
    SIM7600E_GNSS_GALILEO = 0x04,
    SIM7600E_GNSS_BEIDOU = 0x08,
    SIM7600E_GNSS_ALL = 0x0F
} sim7600e_gnss_constellation_t;

/**
 * @brief Extended GNSS information structure
 */
typedef struct {
    double latitude;
    double longitude;
    double altitude;
    float speed;
    float heading;
    float hdop;  // Horizontal dilution of precision
    float vdop;  // Vertical dilution of precision
    int satellites_used;
    int satellites_visible;
    sim7600e_gnss_fix_status_t fix_status;
    char timestamp[32]; // YYYY-MM-DD HH:MM:SS
    bool valid_fix;
} sim7600e_gnss_info_t;

/**
 * @brief GNSS configuration structure
 */
typedef struct {
    sim7600e_gnss_constellation_t constellations; /*!< Enabled GNSS constellations */
    uint32_t update_rate_ms;                       /*!< Position update rate in milliseconds */
    bool cold_start;                               /*!< Perform cold start on enable */
} sim7600e_gnss_config_t;

/**
 * @brief GNSS event callback function type
 * 
 * @param info Pointer to GNSS information structure
 * @param user_data User data passed during callback registration
 */
typedef void (*sim7600e_gnss_event_cb_t)(const sim7600e_gnss_info_t *info, void *user_data);

/**
 * @brief Enable GNSS functionality
 * 
 * @param config GNSS configuration (NULL for default)
 * @return 
 *     - ESP_OK: GNSS enabled successfully
 *     - ESP_FAIL: Failed to enable GNSS
 */
esp_err_t sim7600e_gnss_enable(const sim7600e_gnss_config_t *config);

/**
 * @brief Disable GNSS functionality
 * 
 * @return 
 *     - ESP_OK: GNSS disabled successfully
 *     - ESP_FAIL: Failed to disable GNSS
 */
esp_err_t sim7600e_gnss_disable(void);

/**
 * @brief Get current GNSS information
 * 
 * @param info Pointer to GNSS info structure to fill
 * @param timeout_ms Timeout in milliseconds to wait for valid fix
 * @return 
 *     - ESP_OK: Success, valid GNSS data retrieved
 *     - ESP_ERR_TIMEOUT: Timeout waiting for valid fix
 *     - ESP_ERR_INVALID_ARG: Invalid arguments
 *     - ESP_FAIL: GNSS not enabled or other error
 */
esp_err_t sim7600e_gnss_get_info(sim7600e_gnss_info_t *info, uint32_t timeout_ms);

/**
 * @brief Start GNSS task for continuous position updates
 * 
 * @param task_priority FreeRTOS task priority
 * @param stack_size Task stack size in bytes
 * @return 
 *     - ESP_OK: GNSS task started successfully
 *     - ESP_ERR_INVALID_STATE: Task already running
 *     - ESP_FAIL: Failed to start task
 */
esp_err_t sim7600e_gnss_start_task(uint32_t task_priority, uint32_t stack_size);

/**
 * @brief Stop GNSS task
 * 
 * @return 
 *     - ESP_OK: GNSS task stopped successfully
 *     - ESP_ERR_INVALID_STATE: Task not running
 */
esp_err_t sim7600e_gnss_stop_task(void);

/**
 * @brief Register callback for GNSS events
 * 
 * @param callback Callback function to register
 * @param user_data User data to pass to callback
 * @return 
 *     - ESP_OK: Callback registered successfully
 *     - ESP_ERR_INVALID_ARG: Invalid callback
 */
esp_err_t sim7600e_gnss_register_callback(sim7600e_gnss_event_cb_t callback, void *user_data);

/**
 * @brief Unregister GNSS event callback
 * 
 * @return 
 *     - ESP_OK: Callback unregistered successfully
 */
esp_err_t sim7600e_gnss_unregister_callback(void);

/**
 * @brief Perform GNSS cold start (clear all assistance data)
 * 
 * @return 
 *     - ESP_OK: Cold start performed successfully
 *     - ESP_FAIL: Failed to perform cold start
 */
esp_err_t sim7600e_gnss_cold_start(void);

/**
 * @brief Get default GNSS configuration
 * 
 * @return sim7600e_gnss_config_t Default GNSS configuration
 */
sim7600e_gnss_config_t sim7600e_gnss_get_default_config(void);

#ifdef __cplusplus
}
#endif

#endif // SIM7600E_GNSS_H