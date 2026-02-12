/**
 * @file analog_input.c
 * @brief Драйвер аналоговых входов Waveshare AI 8CH
 */
#include "analog_input.h"
#include "modbus_poller.h"
#include "board_config.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "ai_drv";

/* Конфигурация каналов */
typedef struct {
    float range_min;
    float range_max;
    bool  enabled;
} ai_ch_cfg_t;

static ai_ch_cfg_t s_config[AI_CHANNEL_COUNT];

/* Скользящее среднее */
#define MA_WINDOW   8
static uint16_t s_ma_buf[AI_CHANNEL_COUNT][MA_WINDOW];
static uint8_t  s_ma_idx;
static bool     s_ma_filled;

/* Порог обрыва: ~0.5% от 65535 → ток ниже ~4.08мА (обрыв провода) */
#define FAULT_RAW_THRESHOLD  328

/* Параметры 4-20мА токовой петли */
#define ADC_RAW_MAX             65535.0f
#define CURRENT_LOOP_MIN_MA     4.0f
#define CURRENT_LOOP_SPAN_MA    16.0f   /* 20 - 4 мА */

/* Количество активных каналов (P1..T) */
#define AI_ENABLED_CHANNELS     5

static ai_data_t s_data;

void analog_input_init(void)
{
    s_config[AI_CH_P1] = (ai_ch_cfg_t){ .range_min = 0.0f, .range_max = 6.0f,   .enabled = true };
    s_config[AI_CH_P2] = (ai_ch_cfg_t){ .range_min = 0.0f, .range_max = 6.0f,   .enabled = true };
    s_config[AI_CH_P3] = (ai_ch_cfg_t){ .range_min = 0.0f, .range_max = 40.0f,  .enabled = true };
    s_config[AI_CH_P4] = (ai_ch_cfg_t){ .range_min = 0.0f, .range_max = 10.0f,  .enabled = true };
    s_config[AI_CH_T]  = (ai_ch_cfg_t){ .range_min = 0.0f, .range_max = 100.0f, .enabled = true };
    /* AI6-AI8: резерв */
    for (int i = AI_ENABLED_CHANNELS; i < AI_CHANNEL_COUNT; i++) {
        s_config[i] = (ai_ch_cfg_t){ .enabled = false };
    }

    memset(s_ma_buf, 0, sizeof(s_ma_buf));
    s_ma_idx = 0;
    s_ma_filled = false;
    memset(&s_data, 0, sizeof(s_data));

    ESP_LOGI(TAG, "Драйвер AI инициализирован (5 каналов)");
}

void analog_input_update(void)
{
    uint16_t raw[CID_AI_REG_COUNT];
    modbus_poller_get_ai_raw(raw, CID_AI_REG_COUNT);
    s_data.device_online = modbus_poller_is_device_online(MB_ADDR_WAVESHARE_AI);

    /* Записать в кольцевой буфер */
    for (int ch = 0; ch < AI_CHANNEL_COUNT; ch++) {
        s_ma_buf[ch][s_ma_idx] = raw[ch];
    }
    s_ma_idx = (s_ma_idx + 1) % MA_WINDOW;
    if (s_ma_idx == 0) s_ma_filled = true;

    int count = s_ma_filled ? MA_WINDOW : (s_ma_idx > 0 ? s_ma_idx : 1);

    for (int ch = 0; ch < AI_CHANNEL_COUNT; ch++) {
        if (!s_config[ch].enabled) {
            s_data.channels[ch].valid = false;
            continue;
        }

        /* Среднее арифметическое */
        uint32_t sum = 0;
        for (int j = 0; j < count; j++) {
            sum += s_ma_buf[ch][j];
        }
        uint16_t avg = (uint16_t)(sum / count);

        /* Обрыв датчика */
        s_data.channels[ch].fault = (avg < FAULT_RAW_THRESHOLD);

        /* raw 0..ADC_RAW_MAX → range_min..range_max */
        float ratio = (float)avg / ADC_RAW_MAX;
        s_data.channels[ch].value = s_config[ch].range_min +
                                    ratio * (s_config[ch].range_max - s_config[ch].range_min);
        s_data.channels[ch].raw_ma = CURRENT_LOOP_MIN_MA + ratio * CURRENT_LOOP_SPAN_MA;
        s_data.channels[ch].valid = s_data.device_online && !s_data.channels[ch].fault;
    }
}

void analog_input_get_data(ai_data_t *out)
{
    *out = s_data;
}

float analog_input_get_value(uint8_t ch)
{
    if (ch >= AI_CHANNEL_COUNT || !s_data.channels[ch].valid) {
        return NAN;
    }
    return s_data.channels[ch].value;
}

bool analog_input_is_fault(uint8_t ch)
{
    if (ch >= AI_CHANNEL_COUNT) return true;
    return s_data.channels[ch].fault;
}
