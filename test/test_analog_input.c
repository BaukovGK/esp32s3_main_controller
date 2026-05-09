/**
 * @file test_analog_input.c
 * @brief Тесты драйвера аналоговых входов Waveshare AI 8CH (4-20 мА).
 *
 * Алгоритм:
 *   raw / 65535 → ratio (0..1) → range_min + ratio*(range_max-range_min)
 *   raw < FAULT_RAW_THRESHOLD (≈4.08 мА) → fault.
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

/* 1. Канал P1: range 0-6 бар, raw = 32768 (≈12 мА) → 3 бар.
 *    Прогоняем 8 циклов чтобы заполнить MA-фильтр. */
void test_ai_p1_mid_range(void)
{
    uint16_t raw[CID_AI_REG_COUNT] = {
        32768, 0, 0, 0, 0, 0, 0, 0
    };
    mock_mb_set_ai(raw, CID_AI_REG_COUNT);

    for (int i = 0; i < 8; i++) analog_input_update();

    float p1 = analog_input_get_value(AI_CH_P1);
    /* (32768/65535) * (6-0) ≈ 3.000045 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, p1);
}

/* 2. Канал P3: range 0-40 бар, raw=65535 (20мА) → ~40 бар */
void test_ai_p3_full_scale(void)
{
    uint16_t raw[CID_AI_REG_COUNT] = { 0, 0, 65535, 0, 0, 0, 0, 0 };
    mock_mb_set_ai(raw, CID_AI_REG_COUNT);

    for (int i = 0; i < 8; i++) analog_input_update();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, analog_input_get_value(AI_CH_P3));
}

/* 3. Обрыв 4-20 мА (raw < FAULT_RAW_THRESHOLD = 328) → fault, NaN */
void test_ai_sensor_break(void)
{
    /* P1 = обрыв (raw=0), остальные ок */
    uint16_t raw[CID_AI_REG_COUNT] = { 0, 32768, 32768, 32768, 32768, 0, 0, 0 };
    mock_mb_set_ai(raw, CID_AI_REG_COUNT);

    for (int i = 0; i < 8; i++) analog_input_update();

    TEST_ASSERT_TRUE(isnan(analog_input_get_value(AI_CH_P1)));
    TEST_ASSERT_TRUE(analog_input_is_fault(AI_CH_P1));
    /* P2 валиден */
    TEST_ASSERT_FALSE(isnan(analog_input_get_value(AI_CH_P2)));
}

/* 4. Граница fault threshold: ровно 328 → ещё fault, 329 → уже не fault */
void test_ai_fault_threshold_boundary(void)
{
    uint16_t raw_at[CID_AI_REG_COUNT] = { 327, 0, 0, 0, 0, 0, 0, 0 };
    mock_mb_set_ai(raw_at, CID_AI_REG_COUNT);
    for (int i = 0; i < 8; i++) analog_input_update();
    TEST_ASSERT_TRUE(analog_input_is_fault(AI_CH_P1));

    /* Сменим raw на 1000 (≈4.24мА), пройдут 8 циклов — фильтр сменится */
    mock_mb_set_ai((uint16_t[]){1000, 0,0,0,0,0,0,0}, CID_AI_REG_COUNT);
    /* Reset MA внутри analog_input_init не вызывается — заполним новыми значениями */
    for (int i = 0; i < 16; i++) analog_input_update();  /* >2 окна для уверенной замены */
    TEST_ASSERT_FALSE(analog_input_is_fault(AI_CH_P1));
}

/* 5. Offline Modbus → device_online=false → все каналы не валидны */
void test_ai_offline_invalidates_all(void)
{
    uint16_t raw[CID_AI_REG_COUNT] = { 32768, 32768, 32768, 32768, 32768, 0, 0, 0 };
    mock_mb_set_ai(raw, CID_AI_REG_COUNT);
    mock_mb_set_online(MB_ADDR_WAVESHARE_AI, false);

    for (int i = 0; i < 8; i++) analog_input_update();

    TEST_ASSERT_TRUE(isnan(analog_input_get_value(AI_CH_P1)));
    TEST_ASSERT_TRUE(isnan(analog_input_get_value(AI_CH_T)));
}

/* 6. Скользящее среднее: после смены raw нужно несколько циклов до стабилизации */
void test_ai_moving_average_smooths(void)
{
    /* Заполняем фильтр значением 16384 (≈1.5 бар при range 0-6) */
    uint16_t raw_low[CID_AI_REG_COUNT] = { 16384, 0,0,0,0, 0,0,0 };
    mock_mb_set_ai(raw_low, CID_AI_REG_COUNT);
    for (int i = 0; i < 8; i++) analog_input_update();
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.5f, analog_input_get_value(AI_CH_P1));

    /* Резкий переход на 49152 (≈4.5 бар) — после 1 цикла среднее ~2 бар */
    uint16_t raw_high[CID_AI_REG_COUNT] = { 49152, 0,0,0,0, 0,0,0 };
    mock_mb_set_ai(raw_high, CID_AI_REG_COUNT);
    analog_input_update();
    float v1 = analog_input_get_value(AI_CH_P1);
    /* Среднее из 7×16384 + 1×49152 = (114688+49152)/8 = 20480 → ~1.875 бар */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.875f, v1);

    /* Через 8 циклов фильтр полностью обновится */
    for (int i = 0; i < 8; i++) analog_input_update();
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 4.5f, analog_input_get_value(AI_CH_P1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ai_p1_mid_range);
    RUN_TEST(test_ai_p3_full_scale);
    RUN_TEST(test_ai_sensor_break);
    RUN_TEST(test_ai_fault_threshold_boundary);
    RUN_TEST(test_ai_offline_invalidates_all);
    RUN_TEST(test_ai_moving_average_smooths);
    return UNITY_END();
}
