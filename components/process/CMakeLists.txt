idf_component_register(
    SRCS "interlocks.c" "state_machine.c" "doser.c" "process_task.c"
    INCLUDE_DIRS "include"
    REQUIRES drivers services board_hal
    PRIV_REQUIRES esp_timer
)
