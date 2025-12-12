# ESP32 SIM7600E Component CMakeLists.txt
# Registers the SIM7600E driver component for ESP-IDF

idf_component_register(
    SRCS
        "src/sim7600e.c"
        "src/sim7600e_gsm.c"
        "src/sim7600e_gnss.c"
        "src/sim7600e_tcp.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        "driver"
        "esp_timer"
        "freertos"
        "esp_common"
        "log"
    PRIV_REQUIRES
        "esp_driver_uart"
        "esp_driver_gpio"
)