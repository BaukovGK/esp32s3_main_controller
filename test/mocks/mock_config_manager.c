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
        .step_timeout_s = 60,
    },
    .mqtt = {
        .broker_uri = "mqtt://test:1883",
        .username = "",
        .password = "",
        .client_id = "test",
        .publish_interval_s = 5,
        .enabled = 1,
    },
    .web_auth = {
        .username = "",
        .password = "",
    },
    .kws = {
        .current_min_A = 0.1f,
        .current_check_delay_ms = 5000,
        .temp_max_C = 80.0f,
        .voltage_lp_min_V = 200.0f,
        .voltage_lp_max_V = 250.0f,
        .voltage_hp_min_V = 198.0f,
        .voltage_hp_max_V = 242.0f,
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

void config_manager_get_copy(plant_config_t *out)            { *out = s_test_config; }
void config_manager_get_pressure(config_pressure_t *out)     { *out = s_test_config.pressure; }
void config_manager_get_doser(config_doser_t *out)           { *out = s_test_config.doser; }
void config_manager_get_washing(config_washing_t *out)       { *out = s_test_config.washing; }
void config_manager_get_timeouts(config_timeouts_t *out)     { *out = s_test_config.timeouts; }
void config_manager_get_kws(config_kws_t *out)               { *out = s_test_config.kws; }

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

esp_err_t config_manager_set_web_auth(const config_web_auth_t *cfg)
{
    s_test_config.web_auth = *cfg;
    return ESP_OK;
}

esp_err_t config_manager_set_kws(const config_kws_t *cfg)
{
    s_test_config.kws = *cfg;
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
