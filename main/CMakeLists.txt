idf_component_register(
    SRCS 
        "main.c"
        "driver/gsm/gsm.c"
        "system/gsm_system.c"
        "system/tcp_system.c"
        "system/gnss_system.c"
    INCLUDE_DIRS 
        "."
        "driver"
        "system"

)
