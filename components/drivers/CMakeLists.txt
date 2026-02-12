idf_component_register(
    SRCS "analog_input.c" "flowmeter.c" "conductivity.c"
    INCLUDE_DIRS "include"
    REQUIRES modbus_poller board_hal
)
