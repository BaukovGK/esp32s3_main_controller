/**
 * @file conductivity.h
 * @brief Драйвер кондуктометров СЛ21 (4 канала на двух блоках)
 *
 * Каждый блок СЛ21 имеет 2 ячейки проводимости (X1, X2) и 2 термодатчика
 * (t1, t2). Два блока на шине = 4 канала. Раскладка (по текущей конфигурации):
 *
 *   slave 10 (MB_ADDR_SL21_201) X1, t1 → COND_CH_FEED   — питательная вода
 *   slave 10                    X2, t2 → COND_CH_PERM1  — пермеат 1-й ступени
 *   slave 11 (MB_ADDR_SL21_101) X1, t1 → COND_CH_PERM2  — пермеат 2-й ступени (товарный)
 *   slave 11                    X2, t2 → COND_CH_CONC   — концентрат
 *
 * Если физическое подключение ячеек к точкам отбора отличается — поправить
 * либо физический монтаж, либо мап в conductivity_update().
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COND_CHANNEL_COUNT  4

#define COND_CH_FEED    0   /* slave 10 X1: исходная вода */
#define COND_CH_PERM1   1   /* slave 10 X2: пермеат 1-й ступени */
#define COND_CH_PERM2   2   /* slave 11 X1: пермеат 2-й ступени */
#define COND_CH_CONC    3   /* slave 11 X2: концентрат */

typedef struct {
    float conductivity_uS[COND_CHANNEL_COUNT];  /* µS/cm */
    float temperature_C[COND_CHANNEL_COUNT];     /* °C */
    bool  channel_ok[COND_CHANNEL_COUNT];
    bool  device10_online;
    bool  device11_online;
} conductivity_data_t;

void conductivity_init(void);
void conductivity_update(void);
void conductivity_get_data(conductivity_data_t *out);
float conductivity_get_value(uint8_t ch);

#ifdef __cplusplus
}
#endif
