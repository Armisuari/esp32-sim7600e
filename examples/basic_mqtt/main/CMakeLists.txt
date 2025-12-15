idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp32-sim7600e
    PRIV_REQUIRES mqtt esp_netif esp_event json
)
