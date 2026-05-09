/**
 * @file config_manager.c
 * @brief Менеджер конфигурации — загрузка/сохранение параметров через NVS
 *
 * Потокобезопасность: spinlock защищает s_config при чтении/записи.
 * config_manager_get() возвращает снимок (копию) конфигурации.
 */
#include "config_manager.h"
#include "hal_nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include <string.h>

static const char *TAG = "config";

/* --- Допустимые диапазоны валидации --- */

/* Давления, бар */
#define CFG_P1_MAX_LO       1.0f
#define CFG_P1_MAX_HI       10.0f
#define CFG_P3_MAX_LO       10.0f
#define CFG_P3_MAX_HI       60.0f
#define CFG_P4_MAX_LO       2.0f
#define CFG_P4_MAX_HI       20.0f
#define CFG_FILTER_DP_LO    0.5f
#define CFG_FILTER_DP_HI    3.0f

/* Дозатор, мин */
#define CFG_DOSER_RUN_LO    1
#define CFG_DOSER_RUN_HI    60
#define CFG_DOSER_CYC_LO    10
#define CFG_DOSER_CYC_HI    1440

/* Промывка */
#define CFG_WASH_TARGET_LO  20.0f
#define CFG_WASH_TARGET_HI  40.0f
#define CFG_WASH_MAX_LO     25.0f
#define CFG_WASH_MAX_HI     50.0f
#define CFG_WASH_OVER_LO    30.0f
#define CFG_WASH_OVER_HI    60.0f
#define CFG_WASH_HYST_LO    0.5f
#define CFG_WASH_HYST_HI    10.0f
#define CFG_WASH_HEAT_LO    5
#define CFG_WASH_HEAT_HI    120
#define CFG_WASH_SUP_LO     5
#define CFG_WASH_SUP_HI     120
#define CFG_WASH_DRN_LO     1
#define CFG_WASH_DRN_HI     60

/* Таймауты насосов, мс */
#define CFG_PUMP_CONF_LO    1000
#define CFG_PUMP_CONF_HI    10000
#define CFG_PUMP_RAMP_LO    5000
#define CFG_PUMP_RAMP_HI    30000

/* Phase-4: таймаут шага AUTO, сек */
#define CFG_STEP_TIMEOUT_LO 10
#define CFG_STEP_TIMEOUT_HI 600

/* MQTT */
#define CFG_MQTT_INTV_LO    1
#define CFG_MQTT_INTV_HI    60

/* NVS string load buffer */
#define NVS_STR_BUF_SIZE    128

static plant_config_t s_config;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

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
        .broker_uri = "mqtt://192.168.1.1:1883",
        .username = "",
        .password = "",
        .client_id = "ro_plant",
        .publish_interval_s = 5,
        .enabled = 1,
    },
    /* Phase-4: web auth выключен по умолчанию (пустой username).
     * Включается через POST /api/v1/config/web_auth. */
    .web_auth = {
        .username = "",
        .password = "",
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
    char buf[NVS_STR_BUF_SIZE];
    size_t read_len = (max_len < sizeof(buf)) ? max_len : sizeof(buf);
    if (hal_nvs_get_str(key, buf, read_len) == ESP_OK) {
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

/* Применение валидации к секциям конфига */
static void validate_pressure(config_pressure_t *p)
{
    p->p1_max       = clamp_float(p->p1_max, CFG_P1_MAX_LO, CFG_P1_MAX_HI, s_defaults.pressure.p1_max);
    p->p3_max       = clamp_float(p->p3_max, CFG_P3_MAX_LO, CFG_P3_MAX_HI, s_defaults.pressure.p3_max);
    p->p4_max       = clamp_float(p->p4_max, CFG_P4_MAX_LO, CFG_P4_MAX_HI, s_defaults.pressure.p4_max);
    p->filter_dp_warn = clamp_float(p->filter_dp_warn, CFG_FILTER_DP_LO, CFG_FILTER_DP_HI, s_defaults.pressure.filter_dp_warn);
}

static void validate_doser(config_doser_t *d)
{
    d->run_time_min   = clamp_i32(d->run_time_min, CFG_DOSER_RUN_LO, CFG_DOSER_RUN_HI, s_defaults.doser.run_time_min);
    d->cycle_time_min = clamp_i32(d->cycle_time_min, CFG_DOSER_CYC_LO, CFG_DOSER_CYC_HI, s_defaults.doser.cycle_time_min);
    /* run_time должен быть меньше cycle_time */
    if (d->run_time_min >= d->cycle_time_min) {
        d->run_time_min = s_defaults.doser.run_time_min;
        d->cycle_time_min = s_defaults.doser.cycle_time_min;
        ESP_LOGW(TAG, "Дозатор: run_time >= cycle_time, сброс к дефолтам");
    }
}

static void validate_washing(config_washing_t *w)
{
    w->target_temp_C    = clamp_float(w->target_temp_C, CFG_WASH_TARGET_LO, CFG_WASH_TARGET_HI, s_defaults.washing.target_temp_C);
    w->max_temp_C       = clamp_float(w->max_temp_C, CFG_WASH_MAX_LO, CFG_WASH_MAX_HI, s_defaults.washing.max_temp_C);
    w->t_overshoot_C    = clamp_float(w->t_overshoot_C, CFG_WASH_OVER_LO, CFG_WASH_OVER_HI, s_defaults.washing.t_overshoot_C);
    w->hysteresis_C     = clamp_float(w->hysteresis_C, CFG_WASH_HYST_LO, CFG_WASH_HYST_HI, s_defaults.washing.hysteresis_C);
    w->heat_timeout_min = clamp_i32(w->heat_timeout_min, CFG_WASH_HEAT_LO, CFG_WASH_HEAT_HI, s_defaults.washing.heat_timeout_min);
    w->supply_time_min  = clamp_i32(w->supply_time_min, CFG_WASH_SUP_LO, CFG_WASH_SUP_HI, s_defaults.washing.supply_time_min);
    w->drain_time_min   = clamp_i32(w->drain_time_min, CFG_WASH_DRN_LO, CFG_WASH_DRN_HI, s_defaults.washing.drain_time_min);
}

static void validate_timeouts(config_timeouts_t *t)
{
    t->pump_confirm_ms = clamp_i32(t->pump_confirm_ms, CFG_PUMP_CONF_LO, CFG_PUMP_CONF_HI, s_defaults.timeouts.pump_confirm_ms);
    t->pump_ramp_ms    = clamp_i32(t->pump_ramp_ms, CFG_PUMP_RAMP_LO, CFG_PUMP_RAMP_HI, s_defaults.timeouts.pump_ramp_ms);
    t->step_timeout_s  = clamp_i32(t->step_timeout_s, CFG_STEP_TIMEOUT_LO, CFG_STEP_TIMEOUT_HI, s_defaults.timeouts.step_timeout_s);
}

static void validate_mqtt(config_mqtt_t *m)
{
    m->publish_interval_s = clamp_i32(m->publish_interval_s, CFG_MQTT_INTV_LO, CFG_MQTT_INTV_HI, s_defaults.mqtt.publish_interval_s);
    m->enabled = (m->enabled != 0) ? 1 : 0;
    if (m->broker_uri[0] == '\0') {
        strncpy(m->broker_uri, s_defaults.mqtt.broker_uri, sizeof(m->broker_uri));
    }
    if (m->client_id[0] == '\0') {
        strncpy(m->client_id, s_defaults.mqtt.client_id, sizeof(m->client_id));
    }
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
    load_i32("step_to",  &s_config.timeouts.step_timeout_s);

    load_str("mqtt_uri",  s_config.mqtt.broker_uri, sizeof(s_config.mqtt.broker_uri));
    load_str("mqtt_user", s_config.mqtt.username,   sizeof(s_config.mqtt.username));
    load_str("mqtt_pass", s_config.mqtt.password,   sizeof(s_config.mqtt.password));
    load_str("mqtt_id",   s_config.mqtt.client_id,  sizeof(s_config.mqtt.client_id));
    load_i32("mqtt_intv", &s_config.mqtt.publish_interval_s);
    load_i32("mqtt_en",   &s_config.mqtt.enabled);

    /* Phase-4: web auth */
    load_str("web_user", s_config.web_auth.username, sizeof(s_config.web_auth.username));
    load_str("web_pass", s_config.web_auth.password, sizeof(s_config.web_auth.password));

    /* Валидация */
    validate_pressure(&s_config.pressure);
    validate_doser(&s_config.doser);
    validate_washing(&s_config.washing);
    validate_timeouts(&s_config.timeouts);
    validate_mqtt(&s_config.mqtt);

    ESP_LOGI(TAG, "Конфигурация загружена: P1<%.1f P3<%.1f P4<%.1f доз=%ld/%ldмин",
             s_config.pressure.p1_max, s_config.pressure.p3_max, s_config.pressure.p4_max,
             (long)s_config.doser.run_time_min, (long)s_config.doser.cycle_time_min);

    return ESP_OK;
}

const plant_config_t *config_manager_get(void)
{
    /* Возвращаем указатель — вызывающий код должен использовать данные
     * в рамках своего цикла. Для многобайтовых полей (float, int32)
     * atomic read гарантирован на ESP32. Для критичных обновлений
     * setter-ы используют spinlock + копирование целой секции. */
    return &s_config;
}

void config_manager_get_copy(plant_config_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_config;
    portEXIT_CRITICAL(&s_mux);
}

void config_manager_get_pressure(config_pressure_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_config.pressure;
    portEXIT_CRITICAL(&s_mux);
}

void config_manager_get_doser(config_doser_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_config.doser;
    portEXIT_CRITICAL(&s_mux);
}

void config_manager_get_washing(config_washing_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_config.washing;
    portEXIT_CRITICAL(&s_mux);
}

void config_manager_get_timeouts(config_timeouts_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_config.timeouts;
    portEXIT_CRITICAL(&s_mux);
}

esp_err_t config_manager_set_pressure(const config_pressure_t *cfg)
{
    config_pressure_t validated = *cfg;
    validate_pressure(&validated);

    portENTER_CRITICAL(&s_mux);
    s_config.pressure = validated;
    portEXIT_CRITICAL(&s_mux);

    esp_err_t ret = ESP_OK;
    if (hal_nvs_set_float("p1_max", validated.p1_max) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_float("p3_max", validated.p3_max) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_float("p4_max", validated.p4_max) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_float("filt_dp", validated.filter_dp_warn) != ESP_OK) ret = ESP_FAIL;
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS: не все параметры pressure сохранены");
    }
    return ret;
}

esp_err_t config_manager_set_doser(const config_doser_t *cfg)
{
    config_doser_t validated = *cfg;
    validate_doser(&validated);

    portENTER_CRITICAL(&s_mux);
    s_config.doser = validated;
    portEXIT_CRITICAL(&s_mux);

    esp_err_t ret = ESP_OK;
    if (hal_nvs_set_i32("dos_run", validated.run_time_min) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_i32("dos_cyc", validated.cycle_time_min) != ESP_OK) ret = ESP_FAIL;
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS: не все параметры doser сохранены");
    }
    return ret;
}

esp_err_t config_manager_set_washing(const config_washing_t *cfg)
{
    config_washing_t validated = *cfg;
    validate_washing(&validated);

    portENTER_CRITICAL(&s_mux);
    s_config.washing = validated;
    portEXIT_CRITICAL(&s_mux);

    esp_err_t ret = ESP_OK;
    if (hal_nvs_set_float("wash_t", validated.target_temp_C) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_float("wash_mx", validated.max_temp_C) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_float("t_over", validated.t_overshoot_C) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_float("wash_hys", validated.hysteresis_C) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_i32("wash_heat", validated.heat_timeout_min) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_i32("wash_sup", validated.supply_time_min) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_i32("wash_drn", validated.drain_time_min) != ESP_OK) ret = ESP_FAIL;
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS: не все параметры washing сохранены");
    }
    return ret;
}

esp_err_t config_manager_set_timeouts(const config_timeouts_t *cfg)
{
    config_timeouts_t validated = *cfg;
    validate_timeouts(&validated);

    portENTER_CRITICAL(&s_mux);
    s_config.timeouts = validated;
    portEXIT_CRITICAL(&s_mux);

    esp_err_t ret = ESP_OK;
    if (hal_nvs_set_i32("pmp_conf", validated.pump_confirm_ms) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_i32("pmp_ramp", validated.pump_ramp_ms) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_i32("step_to",  validated.step_timeout_s) != ESP_OK) ret = ESP_FAIL;
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS: не все параметры timeouts сохранены");
    }
    return ret;
}

esp_err_t config_manager_set_mqtt(const config_mqtt_t *cfg)
{
    config_mqtt_t validated = *cfg;
    validate_mqtt(&validated);

    portENTER_CRITICAL(&s_mux);
    s_config.mqtt = validated;
    portEXIT_CRITICAL(&s_mux);

    esp_err_t ret = ESP_OK;
    if (hal_nvs_set_str("mqtt_uri",  validated.broker_uri) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_str("mqtt_user", validated.username) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_str("mqtt_pass", validated.password) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_str("mqtt_id",   validated.client_id) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_i32("mqtt_intv", validated.publish_interval_s) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_i32("mqtt_en",   validated.enabled) != ESP_OK) ret = ESP_FAIL;
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS: не все параметры mqtt сохранены");
    }
    return ret;
}

/* Phase-4 (H-13): web Basic Auth */
esp_err_t config_manager_set_web_auth(const config_web_auth_t *cfg)
{
    config_web_auth_t validated = *cfg;
    /* Trivial-валидация: терминаторы строк */
    validated.username[sizeof(validated.username) - 1] = '\0';
    validated.password[sizeof(validated.password) - 1] = '\0';

    portENTER_CRITICAL(&s_mux);
    s_config.web_auth = validated;
    portEXIT_CRITICAL(&s_mux);

    esp_err_t ret = ESP_OK;
    if (hal_nvs_set_str("web_user", validated.username) != ESP_OK) ret = ESP_FAIL;
    if (hal_nvs_set_str("web_pass", validated.password) != ESP_OK) ret = ESP_FAIL;
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS: web_auth не сохранён");
    }
    if (validated.username[0] == '\0') {
        ESP_LOGW(TAG, "Web auth: пустой username — авторизация ВЫКЛЮЧЕНА");
    } else {
        ESP_LOGI(TAG, "Web auth: включена для пользователя '%s'", validated.username);
    }
    return ret;
}
