/**
 * @file sim_manager.h
 * @brief SIM7600E cellular connection management
 */

#ifndef SIM_MANAGER_H
#define SIM_MANAGER_H

#include "esp_err.h"
#include "sim7600e.h"

/**
 * @brief Initialize SIM7600E manager
 * @return ESP_OK on success
 */
esp_err_t sim_manager_init(void);

/**
 * @brief Connect to cellular network
 * @param apn Access Point Name
 * @param timeout_ms Connection timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t sim_manager_connect(const char *apn, uint32_t timeout_ms);

/**
 * @brief Check if cellular connection is active
 * @return true if connected, false otherwise
 */
bool sim_manager_is_connected(void);

/**
 * @brief Get network information
 * @param info Pointer to network info structure
 * @return ESP_OK on success
 */
esp_err_t sim_manager_get_network_info(sim7600e_network_info_t *info);

/**
 * @brief Wait for cellular connection to be established
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t sim_manager_wait_for_connection(uint32_t timeout_ms);

/**
 * @brief Cleanup SIM manager
 */
void sim_manager_cleanup(void);

#endif // SIM_MANAGER_H