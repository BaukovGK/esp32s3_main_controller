/**
 * @file flowmeter.h
 * @brief Драйвер расходомера УРЖ2КМ
 *
 * Нестандартный формат float: word-swapped.
 * Q1-Q4 расход (м³/ч), V1-V4 накопленный объём (м³).
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLOW_CHANNEL_COUNT  4

#define FLOW_CH_INLET   0   /* Q1: вход (после насоса подачи) */
#define FLOW_CH_CONC1   1   /* Q2: рециркуляция концентрата 1-й ступени */
#define FLOW_CH_PERM2   2   /* Q3: пермеат 2-й ступени */
#define FLOW_CH_CONC2   3   /* Q4: концентрат 2-й ступени */

typedef struct {
    float flow_m3h[FLOW_CHANNEL_COUNT];     /* Расход Q1-Q4, м³/ч */
    float volume_m3[FLOW_CHANNEL_COUNT];    /* Накопленный объём, м³ */
    bool  channel_ok[FLOW_CHANNEL_COUNT];
    bool  device_online;
} flowmeter_data_t;

void flowmeter_init(void);
void flowmeter_update(void);
void flowmeter_get_data(flowmeter_data_t *out);
float flowmeter_get_flow(uint8_t ch);

#ifdef __cplusplus
}
#endif
