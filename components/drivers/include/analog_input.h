/**
 * @file analog_input.h
 * @brief Драйвер аналоговых входов Waveshare AI 8CH
 *
 * raw 0-65535 (4-20мА) → инженерные единицы (бар, °C).
 * Скользящее среднее N=8, детекция обрыва датчика.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AI_CHANNEL_COUNT    8

/* Индексы каналов (0-based) */
#define AI_CH_P1    0   /* P1: давление после насоса подачи, 0-6 бар */
#define AI_CH_P2    1   /* P2: давление после фильтра, 0-6 бар */
#define AI_CH_P3    2   /* P3: давление после насоса 1-й ступени, 0-40 бар */
#define AI_CH_P4    3   /* P4: давление после насоса 2-й ступени, 0-10 бар */
#define AI_CH_T     4   /* T: температура, 0-100°C */

typedef struct {
    float value;    /* Значение в инженерных единицах */
    float raw_ma;   /* Ток в мА (для диагностики) */
    bool  fault;    /* Обрыв датчика (< 4мА) */
    bool  valid;    /* Данные актуальны */
} ai_channel_data_t;

typedef struct {
    ai_channel_data_t channels[AI_CHANNEL_COUNT];
    bool device_online;
} ai_data_t;

void analog_input_init(void);
void analog_input_update(void);
void analog_input_get_data(ai_data_t *out);
float analog_input_get_value(uint8_t ch);
bool analog_input_is_fault(uint8_t ch);

#ifdef __cplusplus
}
#endif
