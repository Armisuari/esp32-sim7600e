/**
 * @file certificate_manager.h
 * @brief TLS certificate management for SIM7600E MQTT
 */

#ifndef CERTIFICATE_MANAGER_H
#define CERTIFICATE_MANAGER_H

#include "esp_err.h"

/**
 * @brief Initialize certificate manager and load embedded certificates
 * @return ESP_OK on success
 */
esp_err_t certificate_manager_init(void);

/**
 * @brief Configure SIM7600E SSL context with certificates
 * @param ssl_context_id SSL context ID (usually 0)
 * @return ESP_OK on success
 */
esp_err_t certificate_manager_configure_ssl_context(int ssl_context_id);

/**
 * @brief Get root CA certificate data
 * @return Pointer to root CA certificate string
 */
const char* certificate_manager_get_root_ca(void);

/**
 * @brief Get client certificate data  
 * @return Pointer to client certificate string
 */
const char* certificate_manager_get_client_cert(void);

/**
 * @brief Get client private key data
 * @return Pointer to private key string
 */
const char* certificate_manager_get_client_key(void);

/**
 * @brief Cleanup certificate manager and free memory
 */
void certificate_manager_cleanup(void);

#endif // CERTIFICATE_MANAGER_H