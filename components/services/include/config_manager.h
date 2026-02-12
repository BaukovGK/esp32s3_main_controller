/**
 * @file config_manager.h
 * @brief Менеджер конфигурации — хранение/загрузка параметров из NVS
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float p1_max;           /* Макс. P1, бар (по умолч. 5.5) */
    float p3_max;           /* Макс. P3, бар (по умолч. 35.0) */
    float p4_max;           /* Макс. P4, бар (по умолч. 8.0) */
    float filter_dp_warn;   /* Порог засорения фильтра P1-P2, бар (по умолч. 1.0) */
} config_pressure_t;

typedef struct {
    int32_t run_time_min;   /* Время работы дозатора, мин (по умолч. 5) */
    int32_t cycle_time_min; /* Период цикла дозатора, мин (по умолч. 60) */
} config_doser_t;

typedef struct {
    float target_temp_C;    /* Целевая температура нагрева, °C (по умолч. 35.0) */
    float max_temp_C;       /* Макс. температура, °C (по умолч. 40.0) */
    float t_overshoot_C;    /* Аварийная температура, °C (по умолч. 45.0) */
    float hysteresis_C;     /* Гистерезис ТЭНа, °C (по умолч. 2.0) */
    int32_t heat_timeout_min;  /* Таймаут фазы нагрева, мин (по умолч. 30) */
    int32_t supply_time_min;   /* Длительность фазы подачи, мин (по умолч. 20) */
    int32_t drain_time_min;    /* Длительность фазы дренажа, мин (по умолч. 5) */
} config_washing_t;

typedef struct {
    int32_t pump_confirm_ms;  /* Таймаут подтверждения насоса, мс (по умолч. 3000) */
    int32_t pump_ramp_ms;     /* Время разгона УПП, мс (по умолч. 15000) */
} config_timeouts_t;

typedef struct {
    char    broker_uri[64];       /* URI брокера, напр. "mqtt://192.168.1.1:1883" */
    char    username[32];         /* Логин (пусто = без авторизации) */
    char    password[32];
    char    client_id[32];        /* ID клиента (по умолч. "ro_plant") */
    int32_t publish_interval_s;   /* Интервал публикации, сек (по умолч. 5) */
    int32_t enabled;              /* 0 = отключён, 1 = включён */
} config_mqtt_t;

typedef struct {
    config_pressure_t pressure;
    config_doser_t    doser;
    config_washing_t  washing;
    config_timeouts_t timeouts;
    config_mqtt_t     mqtt;
} plant_config_t;

/**
 * @brief Загрузить конфигурацию из NVS (или значения по умолчанию)
 */
esp_err_t config_manager_init(void);

/**
 * @brief Получить указатель на текущую конфигурацию (read-only).
 *        Безопасно для чтения отдельных полей (float/int32 атомарны на ESP32).
 */
const plant_config_t *config_manager_get(void);

/**
 * @brief Потокобезопасная копия всей конфигурации
 */
void config_manager_get_copy(plant_config_t *out);

esp_err_t config_manager_set_pressure(const config_pressure_t *cfg);
esp_err_t config_manager_set_doser(const config_doser_t *cfg);
esp_err_t config_manager_set_washing(const config_washing_t *cfg);
esp_err_t config_manager_set_timeouts(const config_timeouts_t *cfg);
esp_err_t config_manager_set_mqtt(const config_mqtt_t *cfg);

#ifdef __cplusplus
}
#endif
