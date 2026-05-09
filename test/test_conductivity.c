/**
 * @file test_conductivity.c
 * @brief Тесты драйвера кондуктометров СЛ21.
 *
 * Формат СЛ21:
 *   Электропроводность: uint32(hi:lo) / 100 → µS/cm
 *   Температура: int16 / 10 → °C
 *
 * Addr 10 (6 регистров): [σ1_hi, σ1_lo, T1, σ2_hi, σ2_lo, T2]
 * Addr 11 (3 регистра):   [σ3_hi, σ3_lo, T3]
 */
#include "unity.h"
#include "conductivity.h"

#include "mock_modbus_poller.h"
#include "board_config.h"

#include <math.h>

void setUp(void)
{
    mock_mb_reset();
    conductivity_init();
}

void tearDown(void) {}

/* 1. Конкретный пример: σ = 12345 µS/cm, T = 25.3 °C
 *   raw_cond = 12345 * 100 = 1234500 = 0x0012D644 → hi=0x0012, lo=0xD644
 *   raw_temp = 253 = 0x00FD */
void test_cond_addr10_ch1_basic(void)
{
    uint16_t c10[6] = {
        /* σ1 */ 0x0012, 0xD644, /* T1 */ 0x00FD,
        /* σ2 */ 0,      0,      /* T2 */ 0,
    };
    mock_mb_set_cond10(c10, 6);

    conductivity_update();

    conductivity_data_t data;
    conductivity_get_data(&data);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12345.0f, data.conductivity_uS[COND_CH_FEED]);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 25.3f, data.temperature_C[COND_CH_FEED]);
    TEST_ASSERT_TRUE(data.channel_ok[COND_CH_FEED]);
}

/* 2. Канал 2 (после первой ступени) addr 10 */
void test_cond_addr10_ch2_uses_offset_3(void)
{
    /* σ2 = 100.0 µS/cm = 10000 raw = 0x00002710 → hi=0x0000, lo=0x2710
     * T2 = 24.7 = 247 = 0x00F7 */
    uint16_t c10[6] = {
        /* σ1 */ 0xFFFF, 0xFFFF, /* T1 */ 0x0000,  /* мусор */
        /* σ2 */ 0x0000, 0x2710, /* T2 */ 0x00F7,
    };
    mock_mb_set_cond10(c10, 6);

    conductivity_update();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, conductivity_get_value(COND_CH_PERM1));
    conductivity_data_t data;
    conductivity_get_data(&data);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 24.7f, data.temperature_C[COND_CH_PERM1]);
}

/* 3. Канал addr 11 (пермеат 2-й ступени) */
void test_cond_addr11_perm2(void)
{
    /* σ3 = 5.0 µS/cm = 500 raw = 0x000001F4 → hi=0x0000, lo=0x01F4
     * T3 = 23.5 = 235 = 0x00EB */
    uint16_t c11[3] = { 0x0000, 0x01F4, 0x00EB };
    mock_mb_set_cond11(c11, 3);

    conductivity_update();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, conductivity_get_value(COND_CH_PERM2));
}

/* 4. Отрицательная температура (lake water зимой и т.п.) */
void test_cond_negative_temperature(void)
{
    /* T = -5.0°C = -50 raw = 0xFFCE (int16) */
    uint16_t c10[6] = { 0, 0, 0xFFCE, 0, 0, 0 };
    mock_mb_set_cond10(c10, 6);

    conductivity_update();

    conductivity_data_t data;
    conductivity_get_data(&data);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -5.0f, data.temperature_C[COND_CH_FEED]);
}

/* 5. Offline addr 10 → каналы FEED + PERM1 → channel_ok = false (NaN геттер) */
void test_cond_offline_addr10_blocks_feed_and_perm1(void)
{
    uint16_t c10[6] = { 0x0012, 0xD684, 0x00FD, 0, 0, 0 };
    mock_mb_set_cond10(c10, 6);
    mock_mb_set_online(MB_ADDR_SL21_201, false);

    conductivity_update();

    TEST_ASSERT_TRUE(isnan(conductivity_get_value(COND_CH_FEED)));
    TEST_ASSERT_TRUE(isnan(conductivity_get_value(COND_CH_PERM1)));
}

/* 6. Offline addr 11 → только PERM2 NaN, FEED/PERM1 валидны */
void test_cond_offline_addr11_blocks_only_perm2(void)
{
    uint16_t c10[6] = { 0, 0x2710, 0, 0, 0x2710, 0 };  /* σ1=σ2=100 */
    uint16_t c11[3] = { 0, 0x01F4, 0 };
    mock_mb_set_cond10(c10, 6);
    mock_mb_set_cond11(c11, 3);
    mock_mb_set_online(MB_ADDR_SL21_101, false);

    conductivity_update();

    TEST_ASSERT_FALSE(isnan(conductivity_get_value(COND_CH_FEED)));
    TEST_ASSERT_FALSE(isnan(conductivity_get_value(COND_CH_PERM1)));
    TEST_ASSERT_TRUE(isnan(conductivity_get_value(COND_CH_PERM2)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cond_addr10_ch1_basic);
    RUN_TEST(test_cond_addr10_ch2_uses_offset_3);
    RUN_TEST(test_cond_addr11_perm2);
    RUN_TEST(test_cond_negative_temperature);
    RUN_TEST(test_cond_offline_addr10_blocks_feed_and_perm1);
    RUN_TEST(test_cond_offline_addr11_blocks_only_perm2);
    return UNITY_END();
}
