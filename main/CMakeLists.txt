idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES board_hal modbus_poller ethernet_init drivers services process web_server mqtt_app
    PRIV_REQUIRES esp_netif esp_eth esp_event
)
