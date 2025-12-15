#ifndef MQTT_TLS_CONFIG_H
#define MQTT_TLS_CONFIG_H

// MQTT TLS Configuration
#define CONFIG_MQTT_TLS_BROKER_HOST "test.mosquitto.org"
#define CONFIG_MQTT_TLS_BROKER_PORT 8883
#define CONFIG_MQTT_TLS_CLIENT_ID "esp32s3_tls_client"

// Cellular Configuration
#define CONFIG_CELLULAR_APN "internet"
#define CONFIG_CELLULAR_TIMEOUT_MS 60000

// Certificate Configuration
#define CONFIG_ROOT_CA_FILENAME "root_ca.pem"
#define CONFIG_CLIENT_CERT_FILENAME "client_cert.pem" 
#define CONFIG_CLIENT_KEY_FILENAME "client_key.pem"

// MQTT Configuration
#define CONFIG_MQTT_KEEPALIVE 90
#define CONFIG_MQTT_TIMEOUT_MS 15000
#define CONFIG_PUBLISH_INTERVAL_MS 60000

// Topics Configuration
#define CONFIG_MQTT_TOPIC_PREFIX "DEME25/08"
#define CONFIG_MQTT_INPUT_TOPIC_FMT CONFIG_MQTT_TOPIC_PREFIX "/INPUTS/%s"
#define CONFIG_MQTT_OUTPUT_TOPIC CONFIG_MQTT_TOPIC_PREFIX "/OUTPUT"

// Logging Configuration
#define CONFIG_LOG_LEVEL ESP_LOG_INFO

// TLS Configuration
#define CONFIG_TLS_VERSION_1_2 3
#define CONFIG_TLS_AUTH_MODE_VERIFY_PEER 1
#define CONFIG_TLS_SNI_ENABLED 1

#endif // MQTT_TLS_CONFIG_H