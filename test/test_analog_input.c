/**
 * @file test_analog_input.c
 * @brief Тесты драйвера аналоговых входов Waveshare Modbus RTU Analog Input 8CH (4-20 мА).
 *
 * Алгоритм (после правки 2026-05-09):
 *   raw_uA → mA = raw / 1000
 *   ratio = (mA − 4) / 16   (клипуется в 0..1)
 *   value = range_min + ratio × (range_max − range_min)
 *
 *   raw < 3500 (3.5 мА) → fault break (обрыв)
 *   raw > 20500 (20.5 мА) → fault short (КЗ)
 *
 * Скользящее среднее N=8 для подавления шума.
 */
#include "unity.h"
#include "analog_input.h"

#include "mock_modbus_poller.h"
#include "board_config.h"

#include <math.h>

void setUp(void)
{
    mock_mb_reset();
    analog_input_init();
}

void tearDown(void) {}

/* 1. Канал P1 (range 0..6 бар), raw=12000 мкА (12 мА) → ratio=0.5 → 3.0 бар */
void test_ai_p1_mid_range(void)
{
    uint16_t raw[CID_AI_REG_COUNT] = { 12000, 0, 0, 0, 0, 0, 0, 0 };
    mock_mb_set_ai(raw, CID_AI_REG_COUNT);

    for (int i = 0; i < 8; i++) analog_input_update();

    float p1 = analog_input_get_value(AI_CH_P1);
    /* (12000-4000)/16000 = 0.5 → 0.5*6 = 3.0 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, p1);
}

/* 2. Канал P3 (range 0..40 бар), raw=20000 мкА (20 мА) → 40 бар */
void test_ai_p3_full_scale(void)
{
    uint16_t raw[CID_AI_REG_COUNT] = { 0, 0, 20000, 0, 0, 0, 0, 0 };
    /* P1=0 даст fault, не трогаем для этого теста */
    mock_mb_set_ai(raw, CID_AI_REG_COUNT);

    for (int i = 0; i < 8; i++) analog_input_update();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, analog_input_get_value(AI_CH_P3));
}

/* 3. Нижний предел шкалы: raw=4000 мкА (4 мА) → 0 бар. */
void test_ai_p1_zero_scale(void)
{
    uint16_t raw[CID_AI_REG_COUNT] = { 4000, 0, 12000, 12000, 12000, 0, 0, 0 };
    mock_mb_set_ai(raw, CID_AI_REG_COUNT);

    for (int i = 0; i < 8; i++) analog_input_update();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, analog_input_get_value(AI_CH_P1));
    TEST_ASSERT_FALSE(analog_input_is_fault(AI_CH_P1));
}

/* 4. Обрыв линии: raw < 3500 мкА → fault, NaN. */
void test_ai_sensor_break(void)
{
    /* P1 = обрыв (raw=0), остальные ок (12 мА = 12000 мкА) */
    uint16_t raw[CID_AI_REG_COUNT] = { 0, 12000, 12000, 12000, 12000, 0, 0, 0 };
    mock_mb_set_ai(raw, CID_AI_REG_COUNT);

    for (int i = 0; i < 8; i++) analog_input_update();

    TEST_ASSERT_TRUE(isnan(analog_input_get_value(AI_CH_P1)));
    TEST_ASSERT_TRUE(analog_input_is_fault(AI_CH_P1));
    /* P2 валиден */
    TEST_ASSERT_FALSE(isnan(analog_input_get_value(AI_CH_P2)));
}

/* 5. Короткое замыкание: raw > 20500 мкА → fault, NaN. */
void test_ai_sensor_short(void)
{
    /* P1 = КЗ (raw=21000), остальные ок */
    uint16_t raw[CID_AI_REG_COUNT] = { 21000, 12000, 12000, 12000, 12000, 0, 0, 0 };
    mock_mb_set_ai(raw, CID_AI_REG_COUNT);

    for (int i = 0; i < 8; i++) analog_input_update();

    TEST_ASSERT_TRUE(isnan(analog_input_get_value(AI_CH_P1)));
    TEST_ASSERT_TRUE(analog_input_is_fault(AI_CH_P1));
}

/* 6. Граница fault threshold (обрыв):
 *    raw=3499 → fault, raw=3501 → not fault. */
void test_ai_fault_break_threshold_boundary(void)
{
    uint16_t raw_break[CID_AI_REG_COUNT] = { 3499, 12000, 12000, 12000, 12000, 0, 0, 0 };
    mock_mb_set_ai(raw_break, CID_AI_REG_COUNT);
    for (int i = 0; i < 8; i++) analog_input_update();
    TEST_ASSERT_TRUE(analog_input_is_fault(AI_CH_P1));

    /* Заполним фильтр новым значением > 3500 */
    uint16_t raw_ok[CID_AI_REG_COUNT] = { 3501, 12000, 12000, 12000, 12000, 0, 0, 0 };
    mock_mb_set_ai(raw_ok, CID_AI_REG_COUNT);
    for (int i = 0; i < 16; i++) analog_input_update();  /* >2 окна для замены */
    TEST_ASSERT_FALSE(analog_input_is_fault(AI_CH_P1));
}

/* 7. Граница fault threshold (КЗ):
 *    raw=20501 → fault, raw=20499 → not fault. */
void test_ai_fault_short_threshold_boundary(void)
{
    uint16_t raw_short[CID_AI_REG_COUNT] = { 20501, 12000, 12000, 12000, 12000, 0, 0, 0 };
    mock_mb_set_ai(raw_short, CID_AI_REG_COUNT);
    for (int i = 0; i < 8; i++) analog_input_update();
    TEST_ASSERT_TRUE(analog_input_is_fault(AI_CH_P1));

    uint16_t raw_ok[CID_AI_REG_COUNT] = { 20499, 12000, 12000, 12000, 12000, 0, 0, 0 };
    mock_mb_set_ai(raw_ok, CID_AI_REG_COUNT);
    for (int i = 0; i < 16; i++) analog_input_update();
    TEST_ASSERT_FALSE(analog_input_is_fault(AI_CH_P1));
}

/* 8. Offline Modbus → device_online=false → каналы valid=false (NaN). */
void test_ai_offline_invalidates_all(void)
{
    uint16_t raw[CID_AI_REG_COUNT] = { 12000, 12000, 12000, 12000, 12000, 0, 0, 0 };
    mock_mb_set_ai(raw, CID_AI_REG_COUNT);
    mock_mb_set_online(MB_ADDR_WAVESHARE_AI, false);

    for (int i = 0; i < 8; i++) analog_input_update();

    TEST_ASSERT_TRUE(isnan(analog_input_get_value(AI_CH_P1)));
    TEST_ASSERT_TRUE(isnan(analog_input_get_value(AI_CH_T)));
}

/* 9. Скользящее среднее: после смены raw нужно несколько циклов до стабилизации.
 *    Нижнее значение 8000 мкА (8 мА) → ratio=0.25 → P1 (0..6 бар) = 1.5 бар.
 *    Верхнее значение 16000 мкА (16 мА) → ratio=0.75 → P1 = 4.5 бар. */
void test_ai_moving_average_smooths(void)
{
    uint16_t raw_low[CID_AI_REG_COUNT] = { 8000, 12000, 12000, 12000, 12000, 0, 0, 0 };
    mock_mb_set_ai(raw_low, CID_AI_REG_COUNT);
    for (int i = 0; i < 8; i++) analog_input_update();
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.5f, analog_input_get_value(AI_CH_P1));

    /* Резкий переход на 16000 мкА — после 1 цикла среднее = (7×8000 + 1×16000)/8 = 9000 мкА
     * → ratio = (9000-4000)/16000 = 0.3125 → P1 = 1.875 бар */
    uint16_t raw_high[CID_AI_REG_COUNT] = { 16000, 12000, 12000, 12000, 12000, 0, 0, 0 };
    mock_mb_set_ai(raw_high, CID_AI_REG_COUNT);
    analog_input_update();
    float v1 = analog_input_get_value(AI_CH_P1);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.875f, v1);

    /* Через 8 циклов фильтр полностью обновится */
    for (int i = 0; i < 8; i++) analog_input_update();
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 4.5f, analog_input_get_value(AI_CH_P1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ai_p1_mid_range);
    RUN_TEST(test_ai_p3_full_scale);
    RUN_TEST(test_ai_p1_zero_scale);
    RUN_TEST(test_ai_sensor_break);
    RUN_TEST(test_ai_sensor_short);
    RUN_TEST(test_ai_fault_break_threshold_boundary);
    RUN_TEST(test_ai_fault_short_threshold_boundary);
    RUN_TEST(test_ai_offline_invalidates_all);
    RUN_TEST(test_ai_moving_average_smooths);
    return UNITY_END();
}
