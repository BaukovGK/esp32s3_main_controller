idf_component_register(
    SRCS "hal_i2c.c" "hal_gpio.c" "hal_uart.c" "hal_nvs.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver nvs_flash
)
