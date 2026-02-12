/**
 * @file mock_config_manager.c
 * @brief Мок: управляемая конфигурация
 */
#include "mock_config_manager.h"
#include <string.h>

static plant_config_t s_test_config;

static const plant_config_t s_defaults = {
    .pressure = {
        .p1_max = 5.5f,
        .p3_max = 35.0f,
        .p4_max = 8.0f,
        .filter_dp_warn = 1.0f,
    },
    .doser = {
        .run_time_min = 5,
        .cycle_time_min = 60,
    },
    .washing = {
        .target_temp_C = 35.0f,
        .max_temp_C = 40.0f,
        .t_overshoot_C = 45.0f,
        .hysteresis_C = 2.0f,
        .heat_timeout_min = 30,
        .supply_time_min = 20,
        .drain_time_min = 5,
    },
    .timeouts = {
        .pump_confirm_ms = 3000,
        .pump_ramp_ms = 15000,
    },
    .mqtt = {
        .broker_uri = "mqtt://test:1883",
        .username = "",
        .password = "",
        .client_id = "test",
        .publish_interval_s = 5,
        .enabled = 1,
    },
};

/* === Реализация production API === */

esp_err_t config_manager_init(void)
{
    mock_config_set_defaults();
    return ESP_OK;
}

const plant_config_t *config_manager_get(void)
{
    return &s_test_config;
}

esp_err_t config_manager_set_pressure(const config_pressure_t *cfg)
{
    s_test_config.pressure = *cfg;
    return ESP_OK;
}

esp_err_t config_manager_set_doser(const config_doser_t *cfg)
{
    s_test_config.doser = *cfg;
    return ESP_OK;
}

esp_err_t config_manager_set_washing(const config_washing_t *cfg)
{
    s_test_config.washing = *cfg;
    return ESP_OK;
}

esp_err_t config_manager_set_timeouts(const config_timeouts_t *cfg)
{
    s_test_config.timeouts = *cfg;
    return ESP_OK;
}

esp_err_t config_manager_set_mqtt(const config_mqtt_t *cfg)
{
    s_test_config.mqtt = *cfg;
    return ESP_OK;
}

/* === Управление из тестов === */

plant_config_t *mock_config_get_mutable(void)
{
    return &s_test_config;
}

void mock_config_set_defaults(void)
{
    s_test_config = s_defaults;
}
