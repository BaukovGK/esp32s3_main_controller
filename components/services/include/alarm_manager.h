/**
 * @file alarm_manager.h
 * @brief Менеджер аварий и предупреждений
 *
 * Категории: CRITICAL, ALARM, WARNING, INFO.
 * Дедупликация по коду, кольцевой буфер истории, NVS-персистентность.
 * Уведомление через callback (MQTT, web и т.д.).
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Категории аварий */
typedef enum {
    ALARM_CAT_CRITICAL,     /* Немедленная остановка */
    ALARM_CAT_ALARM,        /* Авария, требует внимания */
    ALARM_CAT_WARNING,      /* Предупреждение */
    ALARM_CAT_INFO          /* Информационное */
} alarm_category_t;

/* Коды аварий */
typedef enum {
    ALARM_ESTOP             = 0x0001,
    ALARM_SOURCE_EMPTY      = 0x0002,
    ALARM_INTERM_EMPTY      = 0x0003,
    ALARM_P1_HIGH           = 0x0010,
    ALARM_P3_HIGH           = 0x0011,
    ALARM_P4_HIGH           = 0x0012,
    ALARM_T_HIGH            = 0x0020,
    ALARM_FILTER_DP         = 0x0030,
    ALARM_PUMP1_TIMEOUT     = 0x0040,
    ALARM_PUMP2_TIMEOUT     = 0x0041,
    ALARM_PUMP3_TIMEOUT     = 0x0042,
    ALARM_SENSOR_FAULT_P1   = 0x0050,
    ALARM_SENSOR_FAULT_P2   = 0x0051,
    ALARM_SENSOR_FAULT_P3   = 0x0052,
    ALARM_SENSOR_FAULT_P4   = 0x0053,
    ALARM_SENSOR_FAULT_T    = 0x0054,
    ALARM_MQTT_DISCONNECT   = 0x0060,
    ALARM_MODBUS_OFFLINE    = 0x0070,
    ALARM_STATE_CHANGE      = 0x0080,
    ALARM_FAULT_RESET       = 0x0081,
    ALARM_MANUAL_DEP_WARN   = 0x0082,   /* MANUAL: нарушение зависимости агрегатов */
    ALARM_SYSTEM_START      = 0x0090,
} alarm_code_t;

/* Запись аварии */
typedef struct {
    uint32_t          id;           /* Порядковый номер */
    int64_t           timestamp_us; /* esp_timer_get_time() */
    alarm_category_t  category;
    alarm_code_t      code;
    float             value;        /* Опциональное числовое значение */
    bool              active;       /* true = активна, false = снята */
} alarm_entry_t;

/* Callback для уведомления (MQTT и т.д.) */
typedef void (*alarm_notify_cb_t)(const alarm_entry_t *entry);

/**
 * @brief Инициализация менеджера аварий, загрузка счётчика из NVS
 */
esp_err_t alarm_manager_init(void);

/**
 * @brief Зарегистрировать callback уведомления (макс. 4)
 */
esp_err_t alarm_manager_register_notify(alarm_notify_cb_t cb);

/**
 * @brief Поднять аварию (дедупликация: повторный вызов с тем же кодом игнорируется)
 */
void alarm_raise(alarm_code_t code, alarm_category_t cat, float value);

/**
 * @brief Снять аварию (если была активна)
 */
void alarm_clear(alarm_code_t code);

/**
 * @brief Получить текущие активные аварии
 * @param out     Буфер для записи
 * @param max_cnt Макс. количество записей
 * @return Фактическое количество активных аварий
 */
int alarm_get_active(alarm_entry_t *out, int max_cnt);

/**
 * @brief Получить историю аварий (кольцевой буфер, последние N)
 * @param out     Буфер для записи
 * @param max_cnt Макс. количество записей
 * @return Фактическое количество записей
 */
int alarm_get_history(alarm_entry_t *out, int max_cnt);

/**
 * @brief Название категории
 */
const char *alarm_category_str(alarm_category_t cat);

/**
 * @brief Краткое название кода аварии
 */
const char *alarm_code_str(alarm_code_t code);

#ifdef __cplusplus
}
#endif
