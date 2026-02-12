/**
 * @file test_state_machine.c
 * @brief Тесты конечного автомата установки обратного осмоса
 *
 * Использует mock_interlocks для управления результатами interlocks_check().
 * Тестирует переходы состояний, E-STOP, FAULT, MANUAL, pump timeout.
 */
#include "unity.h"
#include "state_machine.h"
#include "interlocks.h"
#include "board_config.h"

#include "mock_esp_timer.h"
#include "mock_hal_gpio.h"
#include "mock_config_manager.h"
#include "mock_analog_input.h"
#include "mock_interlocks.h"
#include "mock_hal_nvs.h"
#include "analog_input.h"
#include "alarm_manager.h"

void setUp(void)
{
    mock_esp_timer_reset();
    mock_hal_gpio_reset();
    mock_nvs_reset();
    mock_config_set_defaults();
    mock_analog_reset();
    mock_interlocks_reset();

    /* alarm_manager нужен для state_machine (MANUAL dep warnings) */
    alarm_manager_init();

    /* Нормальные значения датчиков для washing */
    mock_analog_set(AI_CH_T, 25.0f);

    state_machine_init();
}

/* Хелпер: войти в WASHING и подтвердить фазу нагрева */
static void enter_washing_heating(void)
{
    state_machine_send_command(CMD_START_WASHING);
    state_machine_update();
    TEST_ASSERT_EQUAL(WASH_WAIT_HEAT, state_machine_get_status().wash_sub);

    /* Подтверждение оператора → WASH_HEATING */
    state_machine_send_command(CMD_CONFIRM_WASH_PHASE);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(WASH_HEATING, state_machine_get_status().wash_sub);
}

/* Хелпер: пройти HEATING + подтвердить SUPPLY */
static void enter_washing_supply(void)
{
    mock_analog_set(AI_CH_T, 35.0f);  /* >= target → сразу нагрет */
    enter_washing_heating();

    /* Цикл: T>=target → переход в WASH_WAIT_SUPPLY */
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(WASH_WAIT_SUPPLY, state_machine_get_status().wash_sub);

    /* Подтверждение → WASH_SUPPLY */
    state_machine_send_command(CMD_CONFIRM_WASH_PHASE);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(WASH_SUPPLY, state_machine_get_status().wash_sub);
}

void tearDown(void) {}

/* 1. IDLE → AUTO: CMD_START_AUTO */
void test_sm_idle_to_auto(void)
{
    TEST_ASSERT_EQUAL(SM_IDLE, state_machine_get_state());

    state_machine_send_command(CMD_START_AUTO);
    state_machine_update();

    sm_status_t st = state_machine_get_status();
    TEST_ASSERT_EQUAL(SM_AUTO, st.state);
    TEST_ASSERT_EQUAL(AUTO_STARTING_PUMP1, st.auto_sub);
}

/* 2. AUTO полная последовательность: PUMP1→RAMP→PUMP2→FILLING→PUMP3→RUNNING */
void test_sm_auto_full_sequence(void)
{
    state_machine_send_command(CMD_START_AUTO);
    state_machine_update();
    TEST_ASSERT_EQUAL(SM_AUTO, state_machine_get_state());

    /* AUTO_STARTING_PUMP1: ждём подтверждения DI6 */
    TEST_ASSERT_EQUAL(AUTO_STARTING_PUMP1, state_machine_get_status().auto_sub);

    /* Подтверждение pump1 (DI6) */
    uint8_t di = 0xFF | (1 << (BOARD_DI_PUMP1_CONF - 1));
    mock_hal_gpio_set_di(di);
    mock_esp_timer_advance(100000);  /* +100мс */
    state_machine_update();
    TEST_ASSERT_EQUAL(AUTO_RAMP, state_machine_get_status().auto_sub);

    /* Разгон УПП: ждём pump_ramp_ms (15000мс) */
    mock_esp_timer_advance(15000LL * 1000);
    state_machine_update();
    TEST_ASSERT_EQUAL(AUTO_STARTING_PUMP2, state_machine_get_status().auto_sub);

    /* Подтверждение pump2 (DI7) */
    di |= (1 << (BOARD_DI_PUMP2_CONF - 1));
    mock_hal_gpio_set_di(di);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(AUTO_FILLING_INTERM, state_machine_get_status().auto_sub);

    /* FILLING_INTERM → STARTING_PUMP3 (мгновенный переход) */
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(AUTO_STARTING_PUMP3, state_machine_get_status().auto_sub);

    /* Подтверждение pump3 (DI8) */
    di |= (1 << (BOARD_DI_PUMP3_CONF - 1));
    mock_hal_gpio_set_di(di);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(AUTO_RUNNING, state_machine_get_status().auto_sub);
}

/* 3. E-STOP → SM_FAULT, все DO=0 */
void test_sm_estop_enters_fault(void)
{
    /* Запускаем AUTO */
    state_machine_send_command(CMD_START_AUTO);
    state_machine_update();
    TEST_ASSERT_EQUAL(SM_AUTO, state_machine_get_state());

    /* Активируем E-STOP через mock_interlocks */
    interlock_result_t ilk = {
        .estop_active = true,
        .active_flags = INTERLOCK_ESTOP,
        .allow_pump_feed = false,
        .allow_pump_stage1 = false,
        .allow_pump_stage2 = false,
        .allow_heater = false,
        .allow_doser = false,
    };
    mock_interlocks_set(&ilk);
    mock_esp_timer_advance(100000);
    state_machine_update();

    TEST_ASSERT_EQUAL(SM_FAULT, state_machine_get_state());
    TEST_ASSERT_EQUAL_UINT8(0x00, mock_hal_gpio_get_do());
    TEST_ASSERT_EQUAL_UINT32(INTERLOCK_ESTOP, state_machine_get_status().fault_flags & INTERLOCK_ESTOP);
}

/* 4. FAULT + ESTOP: CMD_RESET_FAULT отклоняется; убрать ESTOP → сброс OK */
void test_sm_fault_reset_with_estop(void)
{
    /* Вызываем FAULT через ESTOP */
    state_machine_send_command(CMD_START_AUTO);
    state_machine_update();

    interlock_result_t ilk = {
        .estop_active = true,
        .active_flags = INTERLOCK_ESTOP,
    };
    mock_interlocks_set(&ilk);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(SM_FAULT, state_machine_get_state());

    /* Попытка сброса при активном ESTOP — отклоняется */
    state_machine_send_command(CMD_RESET_FAULT);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(SM_FAULT, state_machine_get_state());

    /* Убираем ESTOP */
    mock_interlocks_reset();
    state_machine_send_command(CMD_RESET_FAULT);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(SM_IDLE, state_machine_get_state());
}

/* 5. MANUAL: CMD_SET_MANUAL → SM_MANUAL, manual_set_do() управляет DO */
void test_sm_manual_mode(void)
{
    state_machine_send_command(CMD_SET_MANUAL);
    state_machine_update();
    TEST_ASSERT_EQUAL(SM_MANUAL, state_machine_get_state());

    /* Установить DO через manual */
    state_machine_manual_set_do(0x05);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL_UINT8(0x05, mock_hal_gpio_get_do());

    /* CMD_STOP → IDLE, DO=0 */
    state_machine_send_command(CMD_STOP);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(SM_IDLE, state_machine_get_state());
    TEST_ASSERT_EQUAL_UINT8(0x00, mock_hal_gpio_get_do());
}

/* 6. Pump timeout → SM_FAULT */
void test_sm_pump_timeout_fault(void)
{
    state_machine_send_command(CMD_START_AUTO);
    state_machine_update();
    TEST_ASSERT_EQUAL(SM_AUTO, state_machine_get_state());

    /* Pump1 включён, но нет подтверждения DI6 — таймаут через 3с */
    /* DI6 бит = 0 (нет подтверждения), остальные нормальные */
    uint8_t di = 0xFF & ~(1 << (BOARD_DI_PUMP1_CONF - 1));
    mock_hal_gpio_set_di(di);

    /* Прокручиваем время до таймаута (3000мс + запас) */
    for (int i = 0; i < 35; i++) {
        mock_esp_timer_advance(100000);  /* +100мс */
        state_machine_update();
        if (state_machine_get_state() == SM_FAULT) break;
    }

    TEST_ASSERT_EQUAL(SM_FAULT, state_machine_get_state());
    TEST_ASSERT_EQUAL_UINT32(INTERLOCK_PUMP1_TIMEOUT,
                              state_machine_get_status().fault_flags & INTERLOCK_PUMP1_TIMEOUT);
}

/* 7. WASHING: ожидание подтверждения оператора перед каждой фазой */
void test_sm_washing_hmi_confirm(void)
{
    mock_analog_set(AI_CH_T, 20.0f);

    state_machine_send_command(CMD_START_WASHING);
    state_machine_update();
    TEST_ASSERT_EQUAL(SM_WASHING, state_machine_get_state());
    /* Начинаем с WASH_WAIT_HEAT */
    TEST_ASSERT_EQUAL(WASH_WAIT_HEAT, state_machine_get_status().wash_sub);

    /* Без подтверждения — остаёмся в WAIT_HEAT */
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(WASH_WAIT_HEAT, state_machine_get_status().wash_sub);

    /* Подтверждение → WASH_HEATING */
    state_machine_send_command(CMD_CONFIRM_WASH_PHASE);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(WASH_HEATING, state_machine_get_status().wash_sub);
}

/* 8. WASHING: гистерезис ТЭНа (конфигурируемый) */
void test_sm_washing_heater_hysteresis(void)
{
    /* target=35°C, max=40°C, hysteresis=2°C: ON при T<33, OFF при T>=35 */
    mock_analog_set(AI_CH_T, 20.0f);
    enter_washing_heating();

    /* T=20°C < 33°C → ТЭН ВКЛ */
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_TRUE(mock_hal_gpio_get_do_pin(BOARD_DO_HEATER));

    /* T=34°C — в зоне гистерезиса (33..35) → ТЭН сохраняет ON */
    mock_analog_set(AI_CH_T, 34.0f);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_TRUE(mock_hal_gpio_get_do_pin(BOARD_DO_HEATER));

    /* T=35°C → target → ТЭН ВЫКЛ, переход в WASH_WAIT_SUPPLY */
    mock_analog_set(AI_CH_T, 35.0f);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(WASH_WAIT_SUPPLY, state_machine_get_status().wash_sub);

    /* Подтвердить SUPPLY */
    state_machine_send_command(CMD_CONFIRM_WASH_PHASE);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(WASH_SUPPLY, state_machine_get_status().wash_sub);

    /* В SUPPLY: T=35 >= target → ТЭН ВЫКЛ */
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_FALSE(mock_hal_gpio_get_do_pin(BOARD_DO_HEATER));

    /* T=32°C < 33 → ТЭН ВКЛ (гистерезис) */
    mock_analog_set(AI_CH_T, 32.0f);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_TRUE(mock_hal_gpio_get_do_pin(BOARD_DO_HEATER));
}

/* 9. WASHING: перегрев → FAULT */
void test_sm_washing_overheat_fault(void)
{
    mock_analog_set(AI_CH_T, 25.0f);
    enter_washing_heating();

    /* T=41°C > max_temp_C(40°C) → FAULT */
    mock_analog_set(AI_CH_T, 41.0f);
    mock_esp_timer_advance(100000);
    state_machine_update();

    TEST_ASSERT_EQUAL(SM_FAULT, state_machine_get_state());
    TEST_ASSERT_EQUAL_UINT32(INTERLOCK_T_HIGH,
                              state_machine_get_status().fault_flags & INTERLOCK_T_HIGH);
}

/* 10. WASHING: supply длится 20 мин (настраиваемо) */
void test_sm_washing_supply_duration(void)
{
    enter_washing_supply();

    /* Через 19 мин — ещё SUPPLY */
    mock_esp_timer_advance(19LL * 60 * 1000000LL);
    state_machine_update();
    TEST_ASSERT_EQUAL(WASH_SUPPLY, state_machine_get_status().wash_sub);

    /* Через ещё 1.5 мин (суммарно 20.5 мин) → WASH_WAIT_DRAIN */
    mock_esp_timer_advance(90LL * 1000000LL);
    state_machine_update();
    TEST_ASSERT_EQUAL(WASH_WAIT_DRAIN, state_machine_get_status().wash_sub);

    /* Подтверждение → DRAIN */
    state_machine_send_command(CMD_CONFIRM_WASH_PHASE);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(WASH_DRAIN, state_machine_get_status().wash_sub);
}

/* 11. WASHING: полный цикл до DONE */
void test_sm_washing_full_cycle(void)
{
    enter_washing_supply();

    /* Supply 20 мин → WAIT_DRAIN */
    mock_esp_timer_advance(20LL * 60 * 1000000LL + 100000LL);
    state_machine_update();
    TEST_ASSERT_EQUAL(WASH_WAIT_DRAIN, state_machine_get_status().wash_sub);

    /* Подтвердить DRAIN */
    state_machine_send_command(CMD_CONFIRM_WASH_PHASE);
    mock_esp_timer_advance(100000);
    state_machine_update();
    TEST_ASSERT_EQUAL(WASH_DRAIN, state_machine_get_status().wash_sub);

    /* Drain 5 мин → DONE → IDLE */
    mock_esp_timer_advance(5LL * 60 * 1000000LL + 100000LL);
    state_machine_update();  /* WASH_DONE */
    mock_esp_timer_advance(100000);
    state_machine_update();  /* → IDLE */
    TEST_ASSERT_EQUAL(SM_IDLE, state_machine_get_state());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sm_idle_to_auto);
    RUN_TEST(test_sm_auto_full_sequence);
    RUN_TEST(test_sm_estop_enters_fault);
    RUN_TEST(test_sm_fault_reset_with_estop);
    RUN_TEST(test_sm_manual_mode);
    RUN_TEST(test_sm_pump_timeout_fault);
    RUN_TEST(test_sm_washing_hmi_confirm);
    RUN_TEST(test_sm_washing_heater_hysteresis);
    RUN_TEST(test_sm_washing_overheat_fault);
    RUN_TEST(test_sm_washing_supply_duration);
    RUN_TEST(test_sm_washing_full_cycle);
    return UNITY_END();
}
