/**
 * @file test_flowmeter.c
 * @brief Тесты драйвера расходомера УРЖ2КМ.
 *
 * Главная цель — проверить word-swapped IEEE-754 на конкретных битовых
 * паттернах. Этот формат легко перепутать (low/high слова), и ошибка
 * приведёт к катастрофически неверным значениям расхода.
 *
 * Layout УРЖ2КМ для float значения V:
 *   reg[0] = low word  (биты [15:0]  бинарного представления float)
 *   reg[1] = high word (биты [31:16] бинарного представления float)
 * Восстановление: ((reg_hi << 16) | reg_lo) → memcpy → float.
 */
#include "unity.h"
#include "flowmeter.h"

#include "mock_modbus_poller.h"
#include "board_config.h"

#include <math.h>
#include <string.h>

/* Хелпер: «упаковать» float в 2 word-swapped регистра, как делает УРЖ2КМ.
 * Используем реальные битовые паттерны IEEE-754, проверенные на этом же
 * алгоритме декодирования (циклическая проверка). */
static void pack_float_word_swap(float val, uint16_t *reg_lo, uint16_t *reg_hi)
{
    uint32_t bits;
    memcpy(&bits, &val, sizeof(bits));
    *reg_lo = (uint16_t)(bits & 0xFFFF);
    *reg_hi = (uint16_t)((bits >> 16) & 0xFFFF);
}

void setUp(void)
{
    mock_mb_reset();
    flowmeter_init();
}

void tearDown(void) {}

/* 1. Идентичность 0.0 → нули в регистрах → 0.0 */
void test_flow_zero(void)
{
    uint16_t flow[8] = {0};
    uint16_t vol[16] = {0};
    mock_mb_set_flow(flow, 8);
    mock_mb_set_volume(vol, 16);

    flowmeter_update();

    TEST_ASSERT_EQUAL_FLOAT(0.0f, flowmeter_get_flow(FLOW_CH_INLET));
}

/* 2. Конкретный пример: 1.5 м³/ч.
 *    IEEE-754 1.5f = 0x3FC00000.
 *    word-swapped: reg_lo=0x0000, reg_hi=0x3FC0. */
void test_flow_word_swap_1_5(void)
{
    uint16_t flow[8] = {
        /* Q1 */ 0x0000, 0x3FC0,
        /* Q2 */ 0x0000, 0x0000,
        /* Q3 */ 0x0000, 0x0000,
        /* Q4 */ 0x0000, 0x0000,
    };
    uint16_t vol[16] = {0};
    mock_mb_set_flow(flow, 8);
    mock_mb_set_volume(vol, 16);

    flowmeter_update();

    TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.5f, flowmeter_get_flow(FLOW_CH_INLET));
}

/* 3. Round-trip для всех 4 каналов: упаковываем разные float, читаем обратно. */
void test_flow_word_swap_round_trip_all_channels(void)
{
    const float expected[4] = { 0.123f, 12.34f, 0.5f, 100.0f };
    uint16_t flow[8] = {0};

    for (int ch = 0; ch < 4; ch++) {
        pack_float_word_swap(expected[ch], &flow[ch * 2], &flow[ch * 2 + 1]);
    }
    /* volume: тоже задаём, иначе channel_ok = false из-за NaN/inf проверки */
    uint16_t vol[16] = {0};
    for (int ch = 0; ch < 4; ch++) {
        pack_float_word_swap(0.0f, &vol[ch * 2], &vol[ch * 2 + 1]);
    }
    mock_mb_set_flow(flow, 8);
    mock_mb_set_volume(vol, 16);

    flowmeter_update();

    for (int ch = 0; ch < 4; ch++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "channel %d", ch);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4, expected[ch], flowmeter_get_flow(ch), msg);
    }
}

/* 4. Регрессия: НЕправильный порядок слов даёт совершенно другое число.
 *    Гарантирует, что при случайной перестановке байтов тест упадёт. */
void test_flow_word_swap_wrong_order_detection(void)
{
    /* "Неправильно" упакованное 1.5: reg_lo=0x3FC0, reg_hi=0x0000.
     * Если алгоритм декодирования перепутан — он бы выдал 1.5,
     * но правильный — выдаст 0x3FC00000 → почти ноль (denormal/0). */
    uint16_t flow[8] = {
        /* Q1 (НЕВЕРНЫЙ порядок) */ 0x3FC0, 0x0000,
        0,0, 0,0, 0,0,
    };
    uint16_t vol[16] = {0};
    mock_mb_set_flow(flow, 8);
    mock_mb_set_volume(vol, 16);

    flowmeter_update();

    /* Должно быть НЕ 1.5, поскольку байты переставлены */
    float v = flowmeter_get_flow(FLOW_CH_INLET);
    if (!isnan(v)) {
        TEST_ASSERT_FALSE(fabsf(v - 1.5f) < 0.01f);
    }
}

/* 5. Отрицательные значения отвергаются (channel_ok = false → NaN геттер) */
void test_flow_negative_rejected(void)
{
    uint16_t flow[8] = {0};
    uint16_t vol[16] = {0};
    pack_float_word_swap(-1.0f, &flow[0], &flow[1]);
    /* Остальные нули → ok */
    mock_mb_set_flow(flow, 8);
    mock_mb_set_volume(vol, 16);

    flowmeter_update();

    /* Q1 отрицательный → channel_ok=false → get_flow возвращает NaN */
    TEST_ASSERT_TRUE(isnan(flowmeter_get_flow(FLOW_CH_INLET)));
    /* Q2 — нули, валидный */
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.0f, flowmeter_get_flow(FLOW_CH_CONC1));
}

/* 6. Offline устройства Modbus → все каналы NaN */
void test_flow_offline_device(void)
{
    uint16_t flow[8] = {0};
    pack_float_word_swap(5.0f, &flow[0], &flow[1]);
    uint16_t vol[16] = {0};
    mock_mb_set_flow(flow, 8);
    mock_mb_set_volume(vol, 16);
    mock_mb_set_online(MB_ADDR_URZH2KM, false);

    flowmeter_update();

    TEST_ASSERT_TRUE(isnan(flowmeter_get_flow(FLOW_CH_INLET)));
}

/* 7. NaN/Inf в данных отвергаются */
void test_flow_nan_rejected(void)
{
    uint16_t flow[8] = {
        /* Q1: NaN */ 0xFFFF, 0x7FC0,
        0,0, 0,0, 0,0,
    };
    uint16_t vol[16] = {0};
    mock_mb_set_flow(flow, 8);
    mock_mb_set_volume(vol, 16);

    flowmeter_update();

    TEST_ASSERT_TRUE(isnan(flowmeter_get_flow(FLOW_CH_INLET)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_flow_zero);
    RUN_TEST(test_flow_word_swap_1_5);
    RUN_TEST(test_flow_word_swap_round_trip_all_channels);
    RUN_TEST(test_flow_word_swap_wrong_order_detection);
    RUN_TEST(test_flow_negative_rejected);
    RUN_TEST(test_flow_offline_device);
    RUN_TEST(test_flow_nan_rejected);
    return UNITY_END();
}
