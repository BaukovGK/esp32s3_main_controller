idf_component_register(
    SRCS "config_manager.c" "telemetry.c" "watchdog_task.c" "alarm_manager.c" "diagnostics.c"
    INCLUDE_DIRS "include"
    REQUIRES board_hal drivers
    PRIV_REQUIRES esp_timer
)
