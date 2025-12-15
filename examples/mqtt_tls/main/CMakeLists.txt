idf_component_register(
    SRCS "main.c"
         "sim_manager.c"
         "certificate_manager.c"
         "mqtt_tls_client.c"
    INCLUDE_DIRS "."
    REQUIRES esp32-sim7600e json mbedtls nvs_flash
    EMBED_TXTFILES "../certificates/root_ca.pem"
                   "../certificates/client_cert.pem"
                   "../certificates/client_key.pem"
)