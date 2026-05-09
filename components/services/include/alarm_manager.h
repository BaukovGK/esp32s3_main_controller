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
    ALARM_MODBUS_OFFLINE    = 0x0070,   /* value = slave addr */
    ALARM_STATE_CHANGE      = 0x0080,
    ALARM_FAULT_RESET       = 0x0081,
    ALARM_MANUAL_DEP_WARN   = 0x0082,   /* MANUAL: нарушение зависимости агрегатов */
    ALARM_SYSTEM_START      = 0x0090,
    /* Введены в Phase-1 (отказоустойчивость) */
    ALARM_DO_READBACK_FAIL  = 0x00A0,   /* TCA9554: реальное состояние != ожидаемое */
    ALARM_I2C_BUS_HUNG      = 0x00A1,   /* Таймаут I2C-семафора (>100мс) */
    ALARM_MB_DATA_LOCK_HUNG = 0x00A2,   /* Таймаут mutex Modbus poller */
    /* Введены в Phase-2 (надёжность) */
    ALARM_UNEXPECTED_RESTART = 0x00B0,  /* Перезагрузка не от POWERON/SW (panic, WDT, brownout); value = esp_reset_reason_t.
                                         * Также используется при невалидном восстановлении SM-state из NVS
                                         * (повреждённое значение state или маски fault_flags). */
    ALARM_RESTART_DURING_OP  = 0x00B1,  /* Перезагрузка во время AUTO/WASHING — оператор должен сбросить fault */
    ALARM_STEP_TIMEOUT       = 0x00B2,  /* AUTO: подсостояние не завершилось за timeouts.step_timeout_s (зависание процесса);
                                         * value = (float)auto_substate_t. */
    /* Введены в Phase-3 (зрелость) */
    ALARM_LOW_HEAP           = 0x00C0,  /* free heap < threshold; value = свободные байты */
    /* Введены в Phase-5 (KWS-306L integration) */
    ALARM_PUMP_LP_NO_CURRENT = 0x00C1,  /* НД: отсутствует ток при RUNNING; value = текущий ток (А) */
    ALARM_PUMP_HP_NO_CURRENT = 0x00C2,  /* ВД: отсутствует ток при RUNNING; value = текущий ток (А) */
    ALARM_PUMP_LP_OVERTEMP   = 0x00C3,  /* НД: перегрев двигателя; value = температура (°C) */
    ALARM_PUMP_HP_OVERTEMP   = 0x00C4,  /* ВД: перегрев двигателя; value = температура (°C) */
    ALARM_KWS_VOLTAGE_OOR    = 0x00C5,  /* KWS V вне допуска (1ф НД 200..250 В, 3ф ВД фазное 198..242 В);
                                         * value = измеренное напряжение */
    ALARM_KWS_OFFLINE        = 0x00C6,  /* нет связи с KWS slave 20 или 21;
                                         * value = адрес slave (20 или 21) */
    /* Введены в Phase-5 (mb_device_check) — health check Modbus-устройств при старте.
     * Это диагностические алармы (категория ALARM/INFO), не CRITICAL — установка
     * продолжает работать, оператор должен проверить настройку прибора. */
    ALARM_DEV_CHECK_FAILED   = 0x00D0,  /* health-check провалился (value = число неудачных устройств) */
    ALARM_AI_BAD_MODE        = 0x00D1,  /* Waveshare AI: канал не в режиме 4–20мА после автокоррекции;
                                         * value = номер канала (1..8) */
    ALARM_AI_WRONG_ADDR      = 0x00D2,  /* Waveshare AI: device addr != expected; value = читаемый адрес */
    ALARM_AI_VERSION_MISMATCH = 0x00D3, /* Waveshare AI: неожиданная FW version; value = raw register (V*100+v) */
    ALARM_AI_RANGE_OOR       = 0x00D4,  /* Waveshare AI: канал вне sanity-диапазона (зарезервировано) */
    ALARM_SL21_RANGE_OOR     = 0x00D5,  /* СЛ21: проводимость или температура вне ожидаемого диапазона */
    ALARM_URZH_RANGE_OOR     = 0x00D6,  /* УРЖ2КМ: NaN/inf/огромный расход на старте */
    ALARM_KWS_RANGE_OOR      = 0x00D7,  /* KWS: voltage/temperature вне sanity-диапазона */
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
