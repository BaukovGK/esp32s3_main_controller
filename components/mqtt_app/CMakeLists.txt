idf_component_register(
    SRCS "mqtt_app.c" "mqtt_publish.c" "mqtt_subscribe.c"
    INCLUDE_DIRS "include"
    REQUIRES services process drivers board_hal
    PRIV_REQUIRES mqtt json esp_timer esp_event
)
