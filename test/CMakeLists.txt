cmake_minimum_required(VERSION 3.16)
project(ro_plant_tests C)

# ===========================================
# Пути к исходникам проекта
# ===========================================
set(PROJ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(COMP_DIR  "${PROJ_ROOT}/components")

# ===========================================
# Include: stubs ПЕРВЫМИ (подменяют ESP-IDF), затем component includes
# ===========================================
set(STUB_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
)

set(COMPONENT_INCLUDES
    "${COMP_DIR}/process/include"
    "${COMP_DIR}/services/include"
    "${COMP_DIR}/board_hal/include"
    "${COMP_DIR}/drivers/include"
    "${COMP_DIR}/modbus_poller/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/mocks"
)

# ===========================================
# Unity test framework
# ===========================================
add_library(unity STATIC unity/unity.c)
target_include_directories(unity PUBLIC unity)

# ===========================================
# Библиотеки моков (разделены для предотвращения конфликтов символов)
# ===========================================

# mocks_core: безконфликтные моки (HAL, sensors, timer)
add_library(mocks_core STATIC
    mocks/mock_esp_timer.c
    mocks/mock_hal_gpio.c
    mocks/mock_hal_nvs.c
    mocks/mock_analog_input.c
    mocks/mock_flowmeter.c
    mocks/mock_conductivity.c
)
target_include_directories(mocks_core PUBLIC
    ${STUB_INCLUDES}
    ${COMPONENT_INCLUDES}
)

# mock_config: отдельно, т.к. конфликтует с реальным config_manager.c
add_library(mock_config STATIC mocks/mock_config_manager.c)
target_include_directories(mock_config PUBLIC
    ${STUB_INCLUDES}
    ${COMPONENT_INCLUDES}
)

# mock_interlocks: отдельно, т.к. конфликтует с реальным interlocks.c
add_library(mock_interlocks_lib STATIC mocks/mock_interlocks.c)
target_include_directories(mock_interlocks_lib PUBLIC
    ${STUB_INCLUDES}
    ${COMPONENT_INCLUDES}
)

# Phase-5: mock_power_meter — отдельно, т.к. конфликтует с реальным power_meter.c
# (test_power_meter линкует настоящий драйвер через mocks_drivers).
add_library(mock_power_meter_lib STATIC mocks/mock_power_meter.c)
target_include_directories(mock_power_meter_lib PUBLIC
    ${STUB_INCLUDES}
    ${COMPONENT_INCLUDES}
)

# Phase-3 (L-5): минимальный набор моков для тестирования настоящих драйверов
# (без мок-замен driver-уровня; вместо этого мокируется modbus_poller).
add_library(mocks_drivers STATIC
    mocks/mock_esp_timer.c
    mocks/mock_hal_nvs.c
    mocks/mock_modbus_poller.c
)
target_include_directories(mocks_drivers PUBLIC
    ${STUB_INCLUDES}
    ${COMPONENT_INCLUDES}
)

# ===========================================
# Опции компиляции
# ===========================================
if(MSVC)
    add_compile_options(/W3 /wd4244 /wd4267 /wd4996)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# Стандарт C11 (для isnan, NAN и т.д.)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ===========================================
# CTest
# ===========================================
enable_testing()

# ===========================================
# Макрос: add_host_test(NAME test_src source_under_test... LIBS libs...)
# ===========================================
function(add_host_test TEST_NAME)
    cmake_parse_arguments(ARG "" "" "SRCS;LIBS" ${ARGN})

    add_executable(${TEST_NAME} ${ARG_SRCS})
    target_include_directories(${TEST_NAME} PRIVATE
        ${STUB_INCLUDES}
        ${COMPONENT_INCLUDES}
    )
    target_link_libraries(${TEST_NAME} PRIVATE unity ${ARG_LIBS})

    if(NOT MSVC)
        target_link_libraries(${TEST_NAME} PRIVATE m)
    endif()

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# ===========================================
# Тесты
# ===========================================

# 1. test_interlocks: real interlocks.c + mocks_core + mock_config
add_host_test(test_interlocks
    SRCS test_interlocks.c "${COMP_DIR}/process/interlocks.c"
    LIBS mocks_core mock_config
)

# 2. test_doser: real doser.c + mocks_core + mock_config
add_host_test(test_doser
    SRCS test_doser.c "${COMP_DIR}/process/doser.c"
    LIBS mocks_core mock_config
)

# 3. test_state_machine: real state_machine.c + alarm_manager.c + mocks_core + mock_config + mock_interlocks + mock_power_meter (Phase-5)
add_host_test(test_state_machine
    SRCS test_state_machine.c "${COMP_DIR}/process/state_machine.c" "${COMP_DIR}/services/alarm_manager.c"
    LIBS mocks_core mock_config mock_interlocks_lib mock_power_meter_lib
)

# 4. test_alarm_manager: real alarm_manager.c + mocks_core
add_host_test(test_alarm_manager
    SRCS test_alarm_manager.c "${COMP_DIR}/services/alarm_manager.c"
    LIBS mocks_core
)

# 5. test_telemetry: real telemetry.c + mocks_core
add_host_test(test_telemetry
    SRCS test_telemetry.c "${COMP_DIR}/services/telemetry.c"
    LIBS mocks_core
)

# 6. test_config_manager: real config_manager.c + mocks_core (НЕ mock_config!)
add_host_test(test_config_manager
    SRCS test_config_manager.c "${COMP_DIR}/services/config_manager.c"
    LIBS mocks_core
)

# Phase-3 (L-5): тесты драйверов с моком modbus_poller'а

# 7. test_flowmeter: real flowmeter.c + mocks_drivers (с mock_modbus_poller)
add_host_test(test_flowmeter
    SRCS test_flowmeter.c "${COMP_DIR}/drivers/flowmeter.c"
    LIBS mocks_drivers
)

# 8. test_conductivity: real conductivity.c
add_host_test(test_conductivity
    SRCS test_conductivity.c "${COMP_DIR}/drivers/conductivity.c"
    LIBS mocks_drivers
)

# 9. test_analog_input: real analog_input.c
add_host_test(test_analog_input
    SRCS test_analog_input.c "${COMP_DIR}/drivers/analog_input.c"
    LIBS mocks_drivers
)

# 10. test_power_meter: real power_meter.c (KWS-306L) + mocks_drivers
add_host_test(test_power_meter
    SRCS test_power_meter.c "${COMP_DIR}/drivers/power_meter.c"
    LIBS mocks_drivers
)

# 11. test_mb_device_check: real mb_device_check.c + все драйверы + alarm_manager
# (проверяет автокоррекцию AI mode и sanity-проверки sensors).
add_host_test(test_mb_device_check
    SRCS test_mb_device_check.c
         "${COMP_DIR}/drivers/mb_device_check.c"
         "${COMP_DIR}/drivers/flowmeter.c"
         "${COMP_DIR}/drivers/conductivity.c"
         "${COMP_DIR}/drivers/power_meter.c"
         "${COMP_DIR}/services/alarm_manager.c"
    LIBS mocks_drivers
)
