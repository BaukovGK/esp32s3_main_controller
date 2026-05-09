/**
 * @file test_interlocks.c
 * @brief Тесты блокировок безопасности
 *
 * Тестирует interlocks_check(): 11 проверок безопасности,
 * включая E-STOP, давления, температуру, сухой ход, фильтр DP.
 */
#include "unity.h"
#include "interlocks.h"
#include "board_config.h"
#include "analog_input.h"

#include "mock_hal_gpio.h"
#include "mock_config_manager.h"
#include "mock_analog_input.h"

#include <math.h>

void setUp(void)
{
    mock_hal_gpio_reset();
    mock_config_set_defaults();
    mock_analog_reset();
    interlocks_init();

    /* Установить нормальные значения датчиков */
    mock_analog_set(AI_CH_P1, 3.0f);
    mock_analog_set(AI_CH_P2, 2.8f);
    mock_analog_set(AI_CH_P3, 20.0f);
    mock_analog_set(AI_CH_P4, 5.0f);
    mock_analog_set(AI_CH_T, 25.0f);
}

void tearDown(void) {}

/* 1. Все в норме → все разрешено */
void test_interlocks_all_normal(void)
{
    mock_hal_gpio_set_di(0xFF);  /* Все DI = 1 (NC замкнуты) */

    interlock_result_t r;
    interlocks_check(false, &r);

    TEST_ASSERT_FALSE(r.estop_active);
    TEST_ASSERT_TRUE(r.allow_pump_feed);
    TEST_ASSERT_TRUE(r.allow_pump_stage1);
    TEST_ASSERT_TRUE(r.allow_pump_stage2);
    TEST_ASSERT_TRUE(r.allow_heater);
    TEST_ASSERT_TRUE(r.allow_doser);
    TEST_ASSERT_FALSE(r.filter_warn);
    TEST_ASSERT_EQUAL_UINT32(0, r.active_flags);
}

/* 2. E-STOP активен → всё заблокировано */
void test_interlocks_estop_blocks_all(void)
{
    /* DI5 (E-STOP) = 0: NC разомкнут → авария */
    uint8_t di = 0xFF & ~(1 << (BOARD_DI_ESTOP - 1));
    mock_hal_gpio_set_di(di);

    interlock_result_t r;
    interlocks_check(false, &r);

    TEST_ASSERT_TRUE(r.estop_active);
    TEST_ASSERT_FALSE(r.allow_pump_feed);
    TEST_ASSERT_FALSE(r.allow_pump_stage1);
    TEST_ASSERT_FALSE(r.allow_pump_stage2);
    TEST_ASSERT_FALSE(r.allow_heater);
    TEST_ASSERT_FALSE(r.allow_doser);
    TEST_ASSERT_EQUAL_UINT32(INTERLOCK_ESTOP, r.active_flags & INTERLOCK_ESTOP);
}

/* 3. Источник пуст → pump_feed + pump_stage1 блокированы */
void test_interlocks_source_empty(void)
{
    /* DI1 = 0: NC разомкнут → источник пуст */
    uint8_t di = 0xFF & ~(1 << (BOARD_DI_SOURCE_EMPTY - 1));
    mock_hal_gpio_set_di(di);

    interlock_result_t r;
    interlocks_check(false, &r);

    TEST_ASSERT_FALSE(r.estop_active);
    TEST_ASSERT_FALSE(r.allow_pump_feed);
    TEST_ASSERT_FALSE(r.allow_pump_stage1);
    TEST_ASSERT_TRUE(r.allow_pump_stage2);
    TEST_ASSERT_EQUAL_UINT32(INTERLOCK_SOURCE_EMPTY, r.active_flags & INTERLOCK_SOURCE_EMPTY);
}

/* 4. P3 высокое → pump_stage1 блокирован */
void test_interlocks_p3_high(void)
{
    mock_hal_gpio_set_di(0xFF);
    mock_analog_set(AI_CH_P3, 40.0f);  /* Выше p3_max = 35.0 */

    interlock_result_t r;
    interlocks_check(false, &r);

    TEST_ASSERT_FALSE(r.allow_pump_stage1);
    TEST_ASSERT_EQUAL_UINT32(INTERLOCK_P3_HIGH, r.active_flags & INTERLOCK_P3_HIGH);
    /* Остальные насосы не затронуты */
    TEST_ASSERT_TRUE(r.allow_pump_feed);
    TEST_ASSERT_TRUE(r.allow_pump_stage2);
}

/* 5. Температура > overshoot → heater блокирован */
void test_interlocks_temperature_overshoot(void)
{
    mock_hal_gpio_set_di(0xFF);
    mock_analog_set(AI_CH_T, 50.0f);  /* Выше t_overshoot = 45.0 */

    interlock_result_t r;
    interlocks_check(false, &r);

    TEST_ASSERT_FALSE(r.allow_heater);
    TEST_ASSERT_EQUAL_UINT32(INTERLOCK_T_HIGH, r.active_flags & INTERLOCK_T_HIGH);
    /* Насосы не затронуты */
    TEST_ASSERT_TRUE(r.allow_pump_feed);
}

/* 6. Phase-1 (K-1): отказ датчика P1 (NaN) → блокировка pump_feed.
 * До фикса: NaN молча пропускал проверку → насос работал без защиты. */
void test_interlocks_p1_sensor_fault_blocks_pump_feed(void)
{
    mock_hal_gpio_set_di(0xFF);
    mock_analog_set(AI_CH_P1, NAN);  /* обрыв датчика P1 */

    interlock_result_t r;
    interlocks_check(false, &r);

    TEST_ASSERT_FALSE(r.allow_pump_feed);
    TEST_ASSERT_EQUAL_UINT32(INTERLOCK_SENSOR_FAULT_P1,
                             r.active_flags & INTERLOCK_SENSOR_FAULT_P1);
    /* Остальные насосы не затронуты */
    TEST_ASSERT_TRUE(r.allow_pump_stage1);
    TEST_ASSERT_TRUE(r.allow_pump_stage2);
    TEST_ASSERT_TRUE(r.allow_heater);
}

/* 7. Phase-1 (K-1): отказ P3 → блокировка pump_stage1 */
void test_interlocks_p3_sensor_fault_blocks_stage1(void)
{
    mock_hal_gpio_set_di(0xFF);
    mock_analog_set(AI_CH_P3, NAN);

    interlock_result_t r;
    interlocks_check(false, &r);

    TEST_ASSERT_FALSE(r.allow_pump_stage1);
    TEST_ASSERT_EQUAL_UINT32(INTERLOCK_SENSOR_FAULT_P3,
                             r.active_flags & INTERLOCK_SENSOR_FAULT_P3);
    TEST_ASSERT_TRUE(r.allow_pump_feed);
    TEST_ASSERT_TRUE(r.allow_pump_stage2);
}

/* 8. Phase-1 (K-1): отказ T → блокировка heater */
void test_interlocks_t_sensor_fault_blocks_heater(void)
{
    mock_hal_gpio_set_di(0xFF);
    mock_analog_set(AI_CH_T, NAN);

    interlock_result_t r;
    interlocks_check(false, &r);

    TEST_ASSERT_FALSE(r.allow_heater);
    TEST_ASSERT_EQUAL_UINT32(INTERLOCK_SENSOR_FAULT_T,
                             r.active_flags & INTERLOCK_SENSOR_FAULT_T);
}

/* 9. Manual mode → только E-STOP проверяется */
void test_interlocks_manual_mode_skips_checks(void)
{
    /* Источник пуст + P1 высокое — но manual mode */
    uint8_t di = 0xFF & ~(1 << (BOARD_DI_SOURCE_EMPTY - 1));
    mock_hal_gpio_set_di(di);
    mock_analog_set(AI_CH_P1, 10.0f);  /* Выше p1_max = 5.5 */

    interlock_result_t r;
    interlocks_check(true, &r);  /* manual_mode = true */

    /* E-STOP не активен → все разрешено в manual */
    TEST_ASSERT_FALSE(r.estop_active);
    TEST_ASSERT_TRUE(r.allow_pump_feed);
    TEST_ASSERT_TRUE(r.allow_pump_stage1);
    TEST_ASSERT_TRUE(r.allow_pump_stage2);
    TEST_ASSERT_TRUE(r.allow_heater);
    TEST_ASSERT_EQUAL_UINT32(0, r.active_flags);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_interlocks_all_normal);
    RUN_TEST(test_interlocks_estop_blocks_all);
    RUN_TEST(test_interlocks_source_empty);
    RUN_TEST(test_interlocks_p3_high);
    RUN_TEST(test_interlocks_temperature_overshoot);
    RUN_TEST(test_interlocks_p1_sensor_fault_blocks_pump_feed);
    RUN_TEST(test_interlocks_p3_sensor_fault_blocks_stage1);
    RUN_TEST(test_interlocks_t_sensor_fault_blocks_heater);
    RUN_TEST(test_interlocks_manual_mode_skips_checks);
    return UNITY_END();
}
