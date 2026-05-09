idf_component_register(
    SRCS "hal_i2c.c" "hal_gpio.c" "hal_uart.c" "hal_nvs.c"
         "hal_buzzer.c" "hal_rgb.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver nvs_flash espressif__led_strip
)
