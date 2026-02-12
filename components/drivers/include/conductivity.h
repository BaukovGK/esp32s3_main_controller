/**
 * @file conductivity.h
 * @brief Драйвер кондуктометров СЛ21
 *
 * σ1 (addr 10 ch1): исходная вода
 * σ2 (addr 10 ch2): пермеат 1-й ступени
 * σ3 (addr 11 ch1): пермеат 2-й ступени
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COND_CHANNEL_COUNT  3

#define COND_CH_FEED    0   /* σ1: исходная вода */
#define COND_CH_PERM1   1   /* σ2: пермеат 1-й ступени */
#define COND_CH_PERM2   2   /* σ3: пермеат 2-й ступени */

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
