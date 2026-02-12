/**
 * @file config_manager.c
 * @brief Менеджер конфигурации — загрузка/сохранение параметров через NVS
 */
#include "config_manager.h"
#include "hal_nvs.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "config";

static plant_config_t s_config;

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
        .broker_uri = "mqtt://192.168.1.1:1883",
        .username = "",
        .password = "",
        .client_id = "ro_plant",
        .publish_interval_s = 5,
        .enabled = 1,
    },
};

/* Загрузка float из NVS (игнорируем ошибку ESP_ERR_NVS_NOT_FOUND) */
static void load_float(const char *key, float *dst)
{
    float val;
    if (hal_nvs_get_float(key, &val) == ESP_OK) {
        *dst = val;
    }
}

static void load_i32(const char *key, int32_t *dst)
{
    int32_t val;
    if (hal_nvs_get_i32(key, &val) == ESP_OK) {
        *dst = val;
    }
}

static void load_str(const char *key, char *dst, size_t max_len)
{
    char buf[64];
    if (max_len > sizeof(buf)) max_len = sizeof(buf);
    if (hal_nvs_get_str(key, buf, max_len) == ESP_OK) {
        strncpy(dst, buf, max_len - 1);
        dst[max_len - 1] = '\0';
    }
}

/* Валидация: если параметр вне допустимого диапазона — вернуть default */
static float clamp_float(float val, float min, float max, float def)
{
    if (val < min || val > max) return def;
    return val;
}

static int32_t clamp_i32(int32_t val, int32_t min, int32_t max, int32_t def)
{
    if (val < min || val > max) return def;
    return val;
}

esp_err_t config_manager_init(void)
{
    /* Начинаем со значений по умолчанию */
    s_config = s_defaults;

    /* Пытаемся загрузить из NVS (отсутствующие ключи = default) */
    load_float("p1_max",  &s_config.pressure.p1_max);
    load_float("p3_max",  &s_config.pressure.p3_max);
    load_float("p4_max",  &s_config.pressure.p4_max);
    load_float("filt_dp", &s_config.pressure.filter_dp_warn);

    load_i32("dos_run", &s_config.doser.run_time_min);
    load_i32("dos_cyc", &s_config.doser.cycle_time_min);

    load_float("wash_t",  &s_config.washing.target_temp_C);
    load_float("wash_mx", &s_config.washing.max_temp_C);
    load_float("t_over",  &s_config.washing.t_overshoot_C);
    load_float("wash_hys", &s_config.washing.hysteresis_C);
    load_i32("wash_heat", &s_config.washing.heat_timeout_min);
    load_i32("wash_sup",  &s_config.washing.supply_time_min);
    load_i32("wash_drn",  &s_config.washing.drain_time_min);

    load_i32("pmp_conf", &s_config.timeouts.pump_confirm_ms);
    load_i32("pmp_ramp", &s_config.timeouts.pump_ramp_ms);

    load_str("mqtt_uri",  s_config.mqtt.broker_uri, sizeof(s_config.mqtt.broker_uri));
    load_str("mqtt_user", s_config.mqtt.username,   sizeof(s_config.mqtt.username));
    load_str("mqtt_pass", s_config.mqtt.password,   sizeof(s_config.mqtt.password));
    load_str("mqtt_id",   s_config.mqtt.client_id,  sizeof(s_config.mqtt.client_id));
    load_i32("mqtt_intv", &s_config.mqtt.publish_interval_s);
    load_i32("mqtt_en",   &s_config.mqtt.enabled);

    /* Валидация */
    s_config.pressure.p1_max       = clamp_float(s_config.pressure.p1_max, 1.0f, 10.0f, s_defaults.pressure.p1_max);
    s_config.pressure.p3_max       = clamp_float(s_config.pressure.p3_max, 10.0f, 60.0f, s_defaults.pressure.p3_max);
    s_config.pressure.p4_max       = clamp_float(s_config.pressure.p4_max, 2.0f, 20.0f, s_defaults.pressure.p4_max);
    s_config.pressure.filter_dp_warn = clamp_float(s_config.pressure.filter_dp_warn, 0.5f, 3.0f, s_defaults.pressure.filter_dp_warn);

    s_config.doser.run_time_min    = clamp_i32(s_config.doser.run_time_min, 1, 60, s_defaults.doser.run_time_min);
    s_config.doser.cycle_time_min  = clamp_i32(s_config.doser.cycle_time_min, 10, 1440, s_defaults.doser.cycle_time_min);

    s_config.washing.target_temp_C = clamp_float(s_config.washing.target_temp_C, 20.0f, 40.0f, s_defaults.washing.target_temp_C);
    s_config.washing.max_temp_C    = clamp_float(s_config.washing.max_temp_C, 25.0f, 50.0f, s_defaults.washing.max_temp_C);
    s_config.washing.t_overshoot_C = clamp_float(s_config.washing.t_overshoot_C, 30.0f, 60.0f, s_defaults.washing.t_overshoot_C);
    s_config.washing.hysteresis_C  = clamp_float(s_config.washing.hysteresis_C, 0.5f, 10.0f, s_defaults.washing.hysteresis_C);
    s_config.washing.heat_timeout_min = clamp_i32(s_config.washing.heat_timeout_min, 5, 120, s_defaults.washing.heat_timeout_min);
    s_config.washing.supply_time_min  = clamp_i32(s_config.washing.supply_time_min, 5, 120, s_defaults.washing.supply_time_min);
    s_config.washing.drain_time_min   = clamp_i32(s_config.washing.drain_time_min, 1, 60, s_defaults.washing.drain_time_min);

    s_config.timeouts.pump_confirm_ms = clamp_i32(s_config.timeouts.pump_confirm_ms, 1000, 10000, s_defaults.timeouts.pump_confirm_ms);
    s_config.timeouts.pump_ramp_ms    = clamp_i32(s_config.timeouts.pump_ramp_ms, 5000, 30000, s_defaults.timeouts.pump_ramp_ms);

    s_config.mqtt.publish_interval_s = clamp_i32(s_config.mqtt.publish_interval_s, 1, 60, s_defaults.mqtt.publish_interval_s);
    s_config.mqtt.enabled = (s_config.mqtt.enabled != 0) ? 1 : 0;
    if (s_config.mqtt.broker_uri[0] == '\0') {
        strncpy(s_config.mqtt.broker_uri, s_defaults.mqtt.broker_uri, sizeof(s_config.mqtt.broker_uri));
    }
    if (s_config.mqtt.client_id[0] == '\0') {
        strncpy(s_config.mqtt.client_id, s_defaults.mqtt.client_id, sizeof(s_config.mqtt.client_id));
    }

    ESP_LOGI(TAG, "Конфигурация загружена: P1<%.1f P3<%.1f P4<%.1f доз=%ld/%ldмин",
             s_config.pressure.p1_max, s_config.pressure.p3_max, s_config.pressure.p4_max,
             (long)s_config.doser.run_time_min, (long)s_config.doser.cycle_time_min);

    return ESP_OK;
}

const plant_config_t *config_manager_get(void)
{
    return &s_config;
}

esp_err_t config_manager_set_pressure(const config_pressure_t *cfg)
{
    s_config.pressure = *cfg;
    hal_nvs_set_float("p1_max", cfg->p1_max);
    hal_nvs_set_float("p3_max", cfg->p3_max);
    hal_nvs_set_float("p4_max", cfg->p4_max);
    hal_nvs_set_float("filt_dp", cfg->filter_dp_warn);
    return ESP_OK;
}

esp_err_t config_manager_set_doser(const config_doser_t *cfg)
{
    s_config.doser = *cfg;
    hal_nvs_set_i32("dos_run", cfg->run_time_min);
    hal_nvs_set_i32("dos_cyc", cfg->cycle_time_min);
    return ESP_OK;
}

esp_err_t config_manager_set_washing(const config_washing_t *cfg)
{
    s_config.washing = *cfg;
    hal_nvs_set_float("wash_t", cfg->target_temp_C);
    hal_nvs_set_float("wash_mx", cfg->max_temp_C);
    hal_nvs_set_float("t_over", cfg->t_overshoot_C);
    hal_nvs_set_float("wash_hys", cfg->hysteresis_C);
    hal_nvs_set_i32("wash_heat", cfg->heat_timeout_min);
    hal_nvs_set_i32("wash_sup", cfg->supply_time_min);
    hal_nvs_set_i32("wash_drn", cfg->drain_time_min);
    return ESP_OK;
}

esp_err_t config_manager_set_timeouts(const config_timeouts_t *cfg)
{
    s_config.timeouts = *cfg;
    hal_nvs_set_i32("pmp_conf", cfg->pump_confirm_ms);
    hal_nvs_set_i32("pmp_ramp", cfg->pump_ramp_ms);
    return ESP_OK;
}

esp_err_t config_manager_set_mqtt(const config_mqtt_t *cfg)
{
    s_config.mqtt = *cfg;
    hal_nvs_set_str("mqtt_uri",  cfg->broker_uri);
    hal_nvs_set_str("mqtt_user", cfg->username);
    hal_nvs_set_str("mqtt_pass", cfg->password);
    hal_nvs_set_str("mqtt_id",   cfg->client_id);
    hal_nvs_set_i32("mqtt_intv", cfg->publish_interval_s);
    hal_nvs_set_i32("mqtt_en",   cfg->enabled);
    return ESP_OK;
}
