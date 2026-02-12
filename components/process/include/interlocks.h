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
