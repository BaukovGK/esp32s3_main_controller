/**
 * @file telemetry.h
 * @brief Расчётные параметры установки обратного осмоса
 *
 * Вычисляет: перепад давления фильтра, подачу 1-й ступени,
 * степень извлечения, селективность мембран.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float filter_dp;            /* P1 - P2, бар (перепад на фильтре) */
    float stage1_feed_m3h;      /* Q1 + Q2, м³/ч (подача 1-й ступени) */
    float stage2_recovery_pct;  /* Q3/(Q3+Q4)*100, % (извлечение 2-й ст.) */
    float system_recovery_pct;  /* Q3/Q1*100, % (общее извлечение) */
    float stage1_selectivity;   /* (σ1-σ2)/σ1*100, % (селективность 1-й ст.) */
    float stage2_selectivity;   /* (σ2-σ3)/σ2*100, % (селективность 2-й ст.) */
    bool  valid;                /* Данные достоверны */
} telemetry_data_t;

void telemetry_init(void);
void telemetry_update(void);
const telemetry_data_t *telemetry_get(void);

#ifdef __cplusplus
}
#endif
