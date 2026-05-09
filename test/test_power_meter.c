/**
 * @file test_power_meter.c
 * @brief Тесты драйвера KWS-306L (счётчик электроэнергии для НД- и ВД-насосов).
 *
 * Карта известных регистров блока 0x000E..0x001B (14 регистров):
 *   offset 0  voltage     × 0.1   → В     (raw = V * 10)
 *   offset 2  current     × 0.001 → А     (raw = A * 1000)
 *   offset 4  power       × 0.1   → Вт    (raw = W * 10)
 *   offset 12 energy      × 0.01  → кВт·ч (raw = kWh * 100)
 *   offset 13 temperature × 1     → °C
 *   offset 1, 3, 5..11 — неизвестно (см. TODO datasheet в power_meter.h)
 */
#include "unity.h"
#include "power_meter.h"

#include "mock_modbus_poller.h"
#include "board_config.h"

#include <math.h>
#include <string.h>

/* Заполнить блок KWS типовыми значениями: 230 В, 5 А, 1100 Вт, 12.5 кВт·ч, 45 °C.
 *   2300 = 0x08FC, 5000 = 0x1388, 11000 = 0x2AF8, 1250 = 0x04E2, 45 = 0x002D */
static void fill_typical(uint16_t *regs)
{
    memset(regs, 0, sizeof(uint16_t) * 14);
    regs[0]  = 2300;   /* voltage  */
    regs[2]  = 5000;   /* current  */
    regs[4]  = 11000;  /* power    */
    regs[12] = 1250;   /* energy   */
    regs[13] = 45;     /* temp     */
}

void setUp(void)
{
    mock_mb_reset();
    power_meter_init();
}

void tearDown(void) {}

/* 1. Базовое чтение всех полей KWS — типовые значения для НД-насоса. */
void test_pm_basic_all_fields_read(void)
{
    uint16_t regs[14];
    fill_typical(regs);
    mock_mb_set_kws_lp(regs, 14);

    power_meter_update();

    power_meter_data_t d;
    power_meter_get_data(PUMP_LP, &d);
    TEST_ASSERT_TRUE(d.online);
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 230.0f,  d.voltage_V);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.000f, d.current_A);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1100.0f, d.power_W);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f,  d.energy_kWh);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 45.0f,    d.temperature_C);

    /* Геттеры должны возвращать те же значения */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 230.0f,  power_meter_get_voltage(PUMP_LP));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f,   power_meter_get_current(PUMP_LP));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1100.0f, power_meter_get_power(PUMP_LP));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f,  power_meter_get_energy(PUMP_LP));
    TEST_ASSERT_FLOAT_WITHIN(0.5f,  45.0f,   power_meter_get_temperature(PUMP_LP));
    TEST_ASSERT_TRUE(power_meter_is_online(PUMP_LP));
}

/* 2. Round-trip через hex: 230.0 V = 2300 raw = 0x08FC.
 *    Плюс проверка масштабов остальных полей через сырые hex-значения. */
void test_pm_voltage_hex_round_trip(void)
{
    /* hex-проверка: 0x08FC = 2300 → 230.0 V */
    uint16_t regs[14] = {0};
    regs[0]  = 0x08FC;  /* voltage = 230.0 V       */
    regs[2]  = 0x1388;  /* current = 5.000 A       */
    regs[4]  = 0x2AF8;  /* power   = 1100.0 W      */
    regs[12] = 0x04E2;  /* energy  = 12.50 kWh     */
    regs[13] = 0x002D;  /* temp    = 45 °C         */
    mock_mb_set_kws_hp(regs, 14);

    power_meter_update();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 230.0f,  power_meter_get_voltage(PUMP_HP));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.000f, power_meter_get_current(PUMP_HP));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1100.0f, power_meter_get_power(PUMP_HP));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.50f, power_meter_get_energy(PUMP_HP));
    TEST_ASSERT_FLOAT_WITHIN(0.5f,  45.0f,   power_meter_get_temperature(PUMP_HP));
}

/* 3. Offline → valid=false, геттеры NaN. */
void test_pm_offline_yields_nan(void)
{
    uint16_t regs[14];
    fill_typical(regs);
    mock_mb_set_kws_lp(regs, 14);
    /* Ставим offline ПОСЛЕ set — first_poll_done остаётся true,
     * но is_device_online() возвращает false. */
    mock_mb_set_online(MB_ADDR_KWS_PUMP_LP, false);

    power_meter_update();

    power_meter_data_t d;
    power_meter_get_data(PUMP_LP, &d);
    TEST_ASSERT_FALSE(d.online);
    TEST_ASSERT_FALSE(d.valid);

    TEST_ASSERT_TRUE(isnan(power_meter_get_voltage(PUMP_LP)));
    TEST_ASSERT_TRUE(isnan(power_meter_get_current(PUMP_LP)));
    TEST_ASSERT_TRUE(isnan(power_meter_get_power(PUMP_LP)));
    TEST_ASSERT_TRUE(isnan(power_meter_get_energy(PUMP_LP)));
    TEST_ASSERT_TRUE(isnan(power_meter_get_temperature(PUMP_LP)));
    TEST_ASSERT_FALSE(power_meter_is_online(PUMP_LP));
}

/* 4. Каждый из двух насосов опрашивается независимо. */
void test_pm_two_pumps_independent(void)
{
    /* НД: 220 В, 3.0 А */
    uint16_t lp[14] = {0};
    lp[0] = 2200;
    lp[2] = 3000;
    lp[4] = 6600;     /* 660 W */
    lp[12] = 100;     /* 1.0 kWh */
    lp[13] = 40;
    mock_mb_set_kws_lp(lp, 14);

    /* ВД: 380 В, 8.5 А */
    uint16_t hp[14] = {0};
    hp[0] = 3800;
    hp[2] = 8500;
    hp[4] = 32300;    /* 3230 W */
    hp[12] = 5000;    /* 50.0 kWh */
    hp[13] = 55;
    mock_mb_set_kws_hp(hp, 14);

    power_meter_update();

    /* НД */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 220.0f, power_meter_get_voltage(PUMP_LP));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f,  power_meter_get_current(PUMP_LP));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 660.0f, power_meter_get_power(PUMP_LP));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f,  power_meter_get_energy(PUMP_LP));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 40.0f,   power_meter_get_temperature(PUMP_LP));

    /* ВД */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 380.0f,  power_meter_get_voltage(PUMP_HP));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 8.5f,   power_meter_get_current(PUMP_HP));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 3230.0f, power_meter_get_power(PUMP_HP));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f,  power_meter_get_energy(PUMP_HP));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 55.0f,    power_meter_get_temperature(PUMP_HP));

    TEST_ASSERT_TRUE(power_meter_is_online(PUMP_LP));
    TEST_ASSERT_TRUE(power_meter_is_online(PUMP_HP));
}

/* 5. До первого опроса (mock_mb_clear_first_poll) → valid=false, геттеры NaN.
 *    Моделирует production-путь H-modbus-initial-state. */
void test_pm_no_first_poll_yields_nan(void)
{
    uint16_t regs[14];
    fill_typical(regs);
    mock_mb_set_kws_lp(regs, 14);
    /* Моделируем: данных нет (first_poll сброшен) */
    mock_mb_clear_first_poll(MB_ADDR_KWS_PUMP_LP);

    power_meter_update();

    power_meter_data_t d;
    power_meter_get_data(PUMP_LP, &d);
    TEST_ASSERT_FALSE(d.valid);
    TEST_ASSERT_FALSE(d.online);

    TEST_ASSERT_TRUE(isnan(power_meter_get_voltage(PUMP_LP)));
    TEST_ASSERT_TRUE(isnan(power_meter_get_current(PUMP_LP)));
    TEST_ASSERT_TRUE(isnan(power_meter_get_power(PUMP_LP)));
    TEST_ASSERT_TRUE(isnan(power_meter_get_energy(PUMP_LP)));
    TEST_ASSERT_TRUE(isnan(power_meter_get_temperature(PUMP_LP)));
}

/* 6. Sanity-check: один насос offline, другой работает — изоляция отказа. */
void test_pm_one_pump_offline_other_ok(void)
{
    uint16_t lp[14];
    fill_typical(lp);
    uint16_t hp[14];
    fill_typical(hp);
    mock_mb_set_kws_lp(lp, 14);
    mock_mb_set_kws_hp(hp, 14);
    mock_mb_set_online(MB_ADDR_KWS_PUMP_LP, false);

    power_meter_update();

    /* НД упал — NaN */
    TEST_ASSERT_TRUE(isnan(power_meter_get_voltage(PUMP_LP)));
    TEST_ASSERT_FALSE(power_meter_is_online(PUMP_LP));

    /* ВД работает — типовые значения */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 230.0f, power_meter_get_voltage(PUMP_HP));
    TEST_ASSERT_TRUE(power_meter_is_online(PUMP_HP));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pm_basic_all_fields_read);
    RUN_TEST(test_pm_voltage_hex_round_trip);
    RUN_TEST(test_pm_offline_yields_nan);
    RUN_TEST(test_pm_two_pumps_independent);
    RUN_TEST(test_pm_no_first_poll_yields_nan);
    RUN_TEST(test_pm_one_pump_offline_other_ok);
    return UNITY_END();
}
