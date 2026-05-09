idf_component_register(
    SRCS "analog_input.c" "flowmeter.c" "conductivity.c" "power_meter.c"
         "mb_device_check.c"
    INCLUDE_DIRS "include"
    REQUIRES modbus_poller board_hal services
)
