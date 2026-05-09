idf_component_register(
    SRCS "analog_input.c" "flowmeter.c" "conductivity.c" "power_meter.c"
    INCLUDE_DIRS "include"
    REQUIRES modbus_poller board_hal
)
