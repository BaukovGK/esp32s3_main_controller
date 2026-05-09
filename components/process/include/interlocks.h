/**
 * @file interlocks.h
 * @brief Блокировки безопасности установки обратного осмоса
 *
 * Проверки каждый цикл (100мс):
 *  - E-STOP (всегда, даже в MANUAL)
 *  - Сухой ход (DI1, DI2)
 *  - Превышение давления (P1, P3, P4)
 *  - Перегрев (T > overshoot)
 *  - Засорение фильтра (P1-P2 > порога)
 *  - KWS-306L (Phase-5, биты 17..21):
 *      * NO_CURRENT  — насос команд "ON", но KWS Я ниже current_min_A
 *                     (катушка не тянет, обрыв обмотки, стартер не сработал)
 *      * OVERTEMP    — KWS T > config_kws_t.temp_max_C (перегрев двигателя)
 *      * VOLTAGE_OOR — KWS V вне допустимого диапазона (НД 1ф / ВД 3ф фазное)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Битовые флаги активных блокировок */
#define INTERLOCK_ESTOP             (1 << 0)
#define INTERLOCK_SOURCE_EMPTY      (1 << 1)
#define INTERLOCK_INTERM_EMPTY      (1 << 2)
#define INTERLOCK_P1_HIGH           (1 << 3)
#define INTERLOCK_P3_HIGH           (1 << 4)
#define INTERLOCK_P4_HIGH           (1 << 5)
#define INTERLOCK_T_HIGH            (1 << 6)
#define INTERLOCK_FILTER_DP         (1 << 7)
#define INTERLOCK_PUMP1_TIMEOUT     (1 << 8)    /* Используется state_machine */
#define INTERLOCK_PUMP2_TIMEOUT     (1 << 9)
#define INTERLOCK_PUMP3_TIMEOUT     (1 << 10)
/* Введены в Phase-1: отказ датчика → блокировка агрегата, требующего этот датчик.
 * NaN от analog_input трактуется как отказ (обрыв или offline Modbus). */
#define INTERLOCK_SENSOR_FAULT_P1   (1 << 11)   /* Блокирует pump_feed */
#define INTERLOCK_SENSOR_FAULT_P3   (1 << 12)   /* Блокирует pump_stage1 */
#define INTERLOCK_SENSOR_FAULT_P4   (1 << 13)   /* Блокирует pump_stage2 */
#define INTERLOCK_SENSOR_FAULT_T    (1 << 14)   /* Блокирует heater */
/* Phase-2: после неожиданной перезагрузки в активном режиме (AUTO/WASHING)
 * система входит в FAULT и требует ручного сброса оператором — нельзя слепо
 * продолжать работу без знания состояния агрегатов. */
#define INTERLOCK_UNEXPECTED_RESTART (1 << 15)
/* Phase-4 (H-step-timeout): подсостояние AUTO (STARTING_PUMP1/2/3 или
 * FILLING_INTERM) не завершилось за config.timeouts.step_timeout_s — ожидаемое
 * событие (давление, поток, уровень) не пришло из-за неисправности. Отдельный
 * alarm ALARM_STEP_TIMEOUT. */
#define INTERLOCK_STEP_TIMEOUT      (1 << 16)

/* Phase-5 (KWS-306L integration): биты на основе данных от счётчиков
 * KWS-306L (slaves 20/21). Поднимаются из state_machine.update_auto, когда
 * драйвер power_meter сообщает аномалию: НД-насос не тянет ток, перегрев
 * корпуса, питающее напряжение вне допуска. */
#define INTERLOCK_PUMP_LP_NO_CURRENT  (1U << 17)  /* НД-насос: SM в RUNNING, но KWS Я=0 → обмотка не включилась */
#define INTERLOCK_PUMP_HP_NO_CURRENT  (1U << 18)  /* ВД-насос: то же */
#define INTERLOCK_PUMP_LP_OVERTEMP    (1U << 19)  /* НД-насос: KWS T > порога (default 80°C) */
#define INTERLOCK_PUMP_HP_OVERTEMP    (1U << 20)  /* ВД-насос: то же */
#define INTERLOCK_KWS_VOLTAGE_OOR     (1U << 21)  /* KWS V вне допуска для соответствующей фазы */

/* Phase-4: маска всех известных бит fault_flags. Используется при восстановлении
 * из NVS для отбрасывания «мусорных» бит, если NVS повреждена. ОБНОВЛЯТЬ при
 * добавлении новых INTERLOCK_* выше. */
#define INTERLOCK_KNOWN_MASK        (\
    INTERLOCK_ESTOP             | \
    INTERLOCK_SOURCE_EMPTY      | \
    INTERLOCK_INTERM_EMPTY      | \
    INTERLOCK_P1_HIGH           | \
    INTERLOCK_P3_HIGH           | \
    INTERLOCK_P4_HIGH           | \
    INTERLOCK_T_HIGH            | \
    INTERLOCK_FILTER_DP         | \
    INTERLOCK_PUMP1_TIMEOUT     | \
    INTERLOCK_PUMP2_TIMEOUT     | \
    INTERLOCK_PUMP3_TIMEOUT     | \
    INTERLOCK_SENSOR_FAULT_P1   | \
    INTERLOCK_SENSOR_FAULT_P3   | \
    INTERLOCK_SENSOR_FAULT_P4   | \
    INTERLOCK_SENSOR_FAULT_T    | \
    INTERLOCK_UNEXPECTED_RESTART| \
    INTERLOCK_STEP_TIMEOUT      | \
    INTERLOCK_PUMP_LP_NO_CURRENT| \
    INTERLOCK_PUMP_HP_NO_CURRENT| \
    INTERLOCK_PUMP_LP_OVERTEMP  | \
    INTERLOCK_PUMP_HP_OVERTEMP  | \
    INTERLOCK_KWS_VOLTAGE_OOR)

typedef struct {
    uint32_t active_flags;      /* Битовая маска активных блокировок */
    bool allow_pump_feed;       /* Разрешение на насос подачи (pump1) */
    bool allow_pump_stage1;     /* Разрешение на насос 1-й ступени (pump2) */
    bool allow_pump_stage2;     /* Разрешение на насос 2-й ступени (pump3) */
    bool allow_heater;          /* Разрешение на ТЭН */
    bool allow_doser;           /* Разрешение на дозатор */
    bool estop_active;          /* E-STOP активен */
    bool filter_warn;           /* Предупреждение засорения фильтра */
} interlock_result_t;

void interlocks_init(void);

/**
 * @brief Проверить все блокировки
 * @param manual_mode  true = только E-STOP активен
 * @param result       [out] результат проверки
 */
void interlocks_check(bool manual_mode, interlock_result_t *result);

#ifdef __cplusplus
}
#endif
