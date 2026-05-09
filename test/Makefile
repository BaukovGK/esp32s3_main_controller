# Makefile для host-based тестов RO Plant Controller
# Использование: make (из WSL или MinGW)
#   make            — собрать и запустить все тесты
#   make test       — только запуск
#   make clean      — очистка
#   make VERBOSE=1  — с логами ESP_LOG

PROJ_ROOT := ..
COMP_DIR  := $(PROJ_ROOT)/components

CC      ?= gcc
CFLAGS  := -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
           -std=c11 -g -O0
LDFLAGS := -lm

# Include paths: stubs ПЕРВЫМИ (подменяют ESP-IDF)
INCLUDES := -Istubs \
            -I$(COMP_DIR)/process/include \
            -I$(COMP_DIR)/services/include \
            -I$(COMP_DIR)/board_hal/include \
            -I$(COMP_DIR)/drivers/include \
            -I$(COMP_DIR)/modbus_poller/include \
            -Imocks \
            -Iunity

ifdef VERBOSE
CFLAGS += -DTEST_VERBOSE_LOG
endif

BUILDDIR := build

# Unity
UNITY_OBJ := $(BUILDDIR)/unity.o

# Общие моки (без конфликтов)
MOCK_CORE_SRCS := mocks/mock_esp_timer.c \
                  mocks/mock_hal_gpio.c \
                  mocks/mock_hal_nvs.c \
                  mocks/mock_analog_input.c \
                  mocks/mock_flowmeter.c \
                  mocks/mock_conductivity.c

MOCK_CORE_OBJS := $(patsubst mocks/%.c,$(BUILDDIR)/%.o,$(MOCK_CORE_SRCS))

# Минимальный набор моков для тестирования настоящих драйверов
# (без мок-замен driver-уровня; вместо этого мокируется modbus_poller)
MOCK_DRV_SRCS := mocks/mock_esp_timer.c \
                 mocks/mock_hal_nvs.c \
                 mocks/mock_modbus_poller.c
MOCK_DRV_OBJS := $(patsubst mocks/%.c,$(BUILDDIR)/%.o,$(MOCK_DRV_SRCS))

# Отдельные моки
MOCK_CONFIG_OBJ     := $(BUILDDIR)/mock_config_manager.o
MOCK_INTERLOCKS_OBJ := $(BUILDDIR)/mock_interlocks.o

# Тестовые программы
TESTS := test_interlocks test_doser test_state_machine \
         test_alarm_manager test_telemetry test_config_manager \
         test_flowmeter test_conductivity test_analog_input

.PHONY: all test clean

all: $(addprefix $(BUILDDIR)/,$(TESTS))
	@echo ""
	@echo "=== Running all tests ==="
	@echo ""
	@PASS=0; FAIL=0; \
	for t in $(TESTS); do \
		echo "--- $$t ---"; \
		$(BUILDDIR)/$$t; \
		if [ $$? -eq 0 ]; then PASS=$$((PASS+1)); else FAIL=$$((FAIL+1)); fi; \
		echo ""; \
	done; \
	echo "=== Results: $$PASS passed, $$FAIL failed ===";\
	if [ $$FAIL -ne 0 ]; then exit 1; fi

test: all

clean:
	rm -rf $(BUILDDIR)

# --- Правила компиляции ---

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/unity.o: unity/unity.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iunity -c $< -o $@

$(BUILDDIR)/%.o: mocks/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# --- Тесты ---

# test_interlocks: real interlocks.c + mocks_core + mock_config
$(BUILDDIR)/test_interlocks: test_interlocks.c $(COMP_DIR)/process/interlocks.c \
    $(UNITY_OBJ) $(MOCK_CORE_OBJS) $(MOCK_CONFIG_OBJ) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

# test_doser: real doser.c + mocks_core + mock_config
$(BUILDDIR)/test_doser: test_doser.c $(COMP_DIR)/process/doser.c \
    $(UNITY_OBJ) $(MOCK_CORE_OBJS) $(MOCK_CONFIG_OBJ) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

# test_state_machine: real state_machine.c + alarm_manager.c + mocks_core + mock_config + mock_interlocks
$(BUILDDIR)/test_state_machine: test_state_machine.c $(COMP_DIR)/process/state_machine.c \
    $(COMP_DIR)/services/alarm_manager.c \
    $(UNITY_OBJ) $(MOCK_CORE_OBJS) $(MOCK_CONFIG_OBJ) $(MOCK_INTERLOCKS_OBJ) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

# test_alarm_manager: real alarm_manager.c + mocks_core (БЕЗ mock_config)
$(BUILDDIR)/test_alarm_manager: test_alarm_manager.c $(COMP_DIR)/services/alarm_manager.c \
    $(UNITY_OBJ) $(MOCK_CORE_OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

# test_telemetry: real telemetry.c + mocks_core (БЕЗ mock_config)
$(BUILDDIR)/test_telemetry: test_telemetry.c $(COMP_DIR)/services/telemetry.c \
    $(UNITY_OBJ) $(MOCK_CORE_OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

# test_config_manager: real config_manager.c + mocks_core (БЕЗ mock_config!)
$(BUILDDIR)/test_config_manager: test_config_manager.c $(COMP_DIR)/services/config_manager.c \
    $(UNITY_OBJ) $(MOCK_CORE_OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

# Phase-3 (L-5): тесты драйверов с моком modbus_poller'а

# test_flowmeter: real flowmeter.c + mock_modbus_poller (НЕ mocks_core!)
$(BUILDDIR)/test_flowmeter: test_flowmeter.c $(COMP_DIR)/drivers/flowmeter.c \
    $(UNITY_OBJ) $(MOCK_DRV_OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

# test_conductivity: real conductivity.c
$(BUILDDIR)/test_conductivity: test_conductivity.c $(COMP_DIR)/drivers/conductivity.c \
    $(UNITY_OBJ) $(MOCK_DRV_OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

# test_analog_input: real analog_input.c
$(BUILDDIR)/test_analog_input: test_analog_input.c $(COMP_DIR)/drivers/analog_input.c \
    $(UNITY_OBJ) $(MOCK_DRV_OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)
