/**
 * @file certificate_manager.c
 * @brief TLS certificate management for SIM7600E MQTT
 */

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "certificate_manager.h"
#include "sim7600e_gsm.h"

static const char *TAG = "CERT_MANAGER";

// Certificate storage
static char *root_ca_cert = NULL;
static char *client_cert = NULL;
static char *client_private_key = NULL;

// Embedded certificate files
extern const uint8_t root_ca_pem_start[] asm("_binary_root_ca_pem_start");
extern const uint8_t root_ca_pem_end[] asm("_binary_root_ca_pem_end");

extern const uint8_t client_cert_pem_start[] asm("_binary_client_cert_pem_start");
extern const uint8_t client_cert_pem_end[] asm("_binary_client_cert_pem_end");

extern const uint8_t client_key_pem_start[] asm("_binary_client_key_pem_start");
extern const uint8_t client_key_pem_end[] asm("_binary_client_key_pem_end");

esp_err_t certificate_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing certificate manager");
    
    // Load root CA certificate
    size_t root_ca_len = root_ca_pem_end - root_ca_pem_start;
    root_ca_cert = malloc(root_ca_len + 1);
    if (root_ca_cert == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for root CA");
        return ESP_ERR_NO_MEM;
    }
    memcpy(root_ca_cert, root_ca_pem_start, root_ca_len);
    root_ca_cert[root_ca_len] = '\\0';
    ESP_LOGI(TAG, "Root CA loaded, size: %zu bytes", root_ca_len);
    
    // Load client certificate
    size_t client_cert_len = client_cert_pem_end - client_cert_pem_start;
    client_cert = malloc(client_cert_len + 1);
    if (client_cert == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for client cert");
        return ESP_ERR_NO_MEM;
    }
    memcpy(client_cert, client_cert_pem_start, client_cert_len);
    client_cert[client_cert_len] = '\\0';
    ESP_LOGI(TAG, "Client certificate loaded, size: %zu bytes", client_cert_len);
    
    // Load client private key
    size_t client_key_len = client_key_pem_end - client_key_pem_start;
    client_private_key = malloc(client_key_len + 1);
    if (client_private_key == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for private key");
        return ESP_ERR_NO_MEM;
    }
    memcpy(client_private_key, client_key_pem_start, client_key_len);
    client_private_key[client_key_len] = '\\0';
    ESP_LOGI(TAG, "Private key loaded, size: %zu bytes", client_key_len);
    
    return ESP_OK;
}

esp_err_t certificate_manager_configure_ssl_context(int ssl_context_id)
{
    char response[512];
    char command[1024];
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Configuring SSL context %d with certificates", ssl_context_id);
    
    // Configure SSL version (TLS 1.2)
    snprintf(command, sizeof(command), "AT+CSSLCFG=\\\"sslversion\\\",%d,3\\r\\n", ssl_context_id);
    ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set SSL version");
        return ret;
    }
    
    // Set authentication mode (verify peer)
    snprintf(command, sizeof(command), "AT+CSSLCFG=\\\"authmode\\\",%d,1\\r\\n", ssl_context_id);
    ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), 3000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set SSL auth mode");
        return ret;
    }
    
    // Enable SNI (Server Name Indication)
    snprintf(command, sizeof(command), "AT+CSSLCFG=\\\"enableSNI\\\",%d,1\\r\\n", ssl_context_id);
    ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), 3000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SNI configuration failed, continuing...");
    }
    
    // Configure cipher suites (optional - modern secure suites)
    snprintf(command, sizeof(command), 
             "AT+CSSLCFG=\\\"ciphersuite\\\",%d,\\\"ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384\\\"\\r\\n", 
             ssl_context_id);
    ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Cipher suite configuration failed, using defaults");
    }
    
    ESP_LOGI(TAG, "SSL context %d configured successfully", ssl_context_id);
    return ESP_OK;
}

const char* certificate_manager_get_root_ca(void)
{
    return root_ca_cert;
}

const char* certificate_manager_get_client_cert(void)
{
    return client_cert;
}

const char* certificate_manager_get_client_key(void)
{
    return client_private_key;
}

void certificate_manager_cleanup(void)
{
    if (root_ca_cert) {
        free(root_ca_cert);
        root_ca_cert = NULL;
    }
    if (client_cert) {
        free(client_cert);
        client_cert = NULL;
    }
    if (client_private_key) {
        free(client_private_key);
        client_private_key = NULL;
    }
    ESP_LOGI(TAG, "Certificate manager cleanup completed");
}