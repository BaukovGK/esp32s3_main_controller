idf_component_register(
    SRCS "modbus_poller.c"
    INCLUDE_DIRS "include"
    REQUIRES board_hal
    PRIV_REQUIRES esp-modbus esp_timer services
)
