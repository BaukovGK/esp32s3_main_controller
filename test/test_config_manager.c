/**
 * @file test_config_manager.c
 * @brief Тесты менеджера конфигурации
 *
 * Тестирует: defaults, загрузку из NVS, clamping, set+persist.
 */
#include "unity.h"
#include "config_manager.h"
#include "hal_nvs.h"

#include "mock_hal_nvs.h"

void setUp(void)
{
    mock_nvs_reset();
}

void tearDown(void) {}

/* 1. Пустой NVS → загружаются defaults */
void test_config_defaults_on_empty_nvs(void)
{
    config_manager_init();
    const plant_config_t *cfg = config_manager_get();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.5f, cfg->pressure.p1_max);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 35.0f, cfg->pressure.p3_max);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.0f, cfg->pressure.p4_max);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, cfg->pressure.filter_dp_warn);
    TEST_ASSERT_EQUAL_INT32(5, cfg->doser.run_time_min);
    TEST_ASSERT_EQUAL_INT32(60, cfg->doser.cycle_time_min);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 35.0f, cfg->washing.target_temp_C);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, cfg->washing.hysteresis_C);
    TEST_ASSERT_EQUAL_INT32(30, cfg->washing.heat_timeout_min);
    TEST_ASSERT_EQUAL_INT32(20, cfg->washing.supply_time_min);
    TEST_ASSERT_EQUAL_INT32(5, cfg->washing.drain_time_min);
    TEST_ASSERT_EQUAL_INT32(3000, cfg->timeouts.pump_confirm_ms);
    TEST_ASSERT_EQUAL_INT32(15000, cfg->timeouts.pump_ramp_ms);
}

/* 2. Значения из NVS загружаются корректно */
void test_config_load_from_nvs(void)
{
    /* Предзаполнить NVS */
    hal_nvs_set_float("p1_max", 4.0f);
    hal_nvs_set_float("p3_max", 25.0f);
    hal_nvs_set_i32("dos_run", 10);

    config_manager_init();
    const plant_config_t *cfg = config_manager_get();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.0f, cfg->pressure.p1_max);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, cfg->pressure.p3_max);
    TEST_ASSERT_EQUAL_INT32(10, cfg->doser.run_time_min);
    /* Остальные — defaults */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.0f, cfg->pressure.p4_max);
}

/* 3. Значения вне диапазона → clamp к defaults */
void test_config_clamp_out_of_range(void)
{
    /* p1_max: допустимый диапазон 1.0-10.0, default 5.5 */
    hal_nvs_set_float("p1_max", 99.0f);  /* Вне диапазона */
    /* dos_run: допустимый диапазон 1-60, default 5 */
    hal_nvs_set_i32("dos_run", 100);  /* Вне диапазона */

    config_manager_init();
    const plant_config_t *cfg = config_manager_get();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.5f, cfg->pressure.p1_max);  /* Вернулся к default */
    TEST_ASSERT_EQUAL_INT32(5, cfg->doser.run_time_min);  /* Вернулся к default */
}

/* 4. set_pressure() обновляет конфиг и записывает в NVS */
void test_config_set_writes_nvs(void)
{
    config_manager_init();

    config_pressure_t new_p = {
        .p1_max = 4.5f,
        .p3_max = 30.0f,
        .p4_max = 7.0f,
        .filter_dp_warn = 0.8f,
    };
    config_manager_set_pressure(&new_p);

    /* Проверяем in-memory */
    const plant_config_t *cfg = config_manager_get();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.5f, cfg->pressure.p1_max);

    /* Проверяем NVS */
    float nvs_val = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hal_nvs_get_float("p1_max", &nvs_val));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.5f, nvs_val);

    TEST_ASSERT_EQUAL(ESP_OK, hal_nvs_get_float("p3_max", &nvs_val));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, nvs_val);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_config_defaults_on_empty_nvs);
    RUN_TEST(test_config_load_from_nvs);
    RUN_TEST(test_config_clamp_out_of_range);
    RUN_TEST(test_config_set_writes_nvs);
    return UNITY_END();
}
