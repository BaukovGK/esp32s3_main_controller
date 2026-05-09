/**
 * @file analog_input.c
 * @brief Драйвер аналоговых входов Waveshare AI 8CH
 */
#include "analog_input.h"
#include "modbus_poller.h"
#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
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

/* Параметры 4-20мА токовой петли — Waveshare Modbus RTU Analog Input 8CH (A).
 *
 * В режиме 3 (4–20 мА) raw из input-регистра возвращается в МИКРОАМПЕРАХ
 * напрямую (4 мА = 4000, 20 мА = 20000). Это подтверждено wiki производителя
 * (раздел Software Test → Modbus Poll: «displays the current by default,
 * and the unit is uA») и Arduino-демо doc/waveshare_ai_ref/.
 *
 * ⚠️ Этот код предполагает что модуль настроен в mode=3. По умолчанию
 * jumper'ы замкнуты и mode=3, но если кто-то перевёл модуль в mode 4
 * (scale code 0..4095) или mode 0/1 (вольтаж), формула даст неверный
 * результат. См. analog_input_setup() в этом файле — TODO добавить
 * one-time-setup через FC 0x10 на регистры 4x1000..4x1007. */
#define UA_AT_4MA               4000    /* 4.0 мА = 4000 мкА */
#define UA_AT_20MA              20000   /* 20.0 мА */
#define UA_SPAN                 16000   /* (20 − 4) мА = 16000 мкА */

/* Sensor-fault детекция:
 *   raw < FAULT_BREAK_UA (3.5 мА)  → обрыв токовой петли (нет 4 мА «живого нуля»)
 *   raw > FAULT_SHORT_UA (20.5 мА) → короткое замыкание / переходный процесс
 * Зона 3.5..4.0 мА и 20.0..20.5 мА — нормальный gracefulный диапазон датчика
 * (производственный допуск ±0.1 мА × 5 = 0.5 мА), не считаем fault. */
#define FAULT_BREAK_UA          3500
#define FAULT_SHORT_UA          20500

/* Количество активных каналов (P1..T) */
#define AI_ENABLED_CHANNELS     5

static ai_data_t s_data;
/* Phase-4 (M-4): защита s_data от гонок между process_task (writer) и
 * mqtt_task / httpd (readers). Spinlock — короткие критические секции
 * (memcpy / чтение поля), не блокирует. */
static portMUX_TYPE s_data_mux = portMUX_INITIALIZER_UNLOCKED;

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
    /* Phase-5 (C-9/C-10/H-modbus-initial-state): raw-getter зануляет
     * буфер и возвращает ESP_ERR_INVALID_STATE до первого опроса.
     * Не-OK эквивалентен offline. */
    esp_err_t err_ai = modbus_poller_get_ai_raw(raw, CID_AI_REG_COUNT);
    bool online = modbus_poller_is_device_online(MB_ADDR_WAVESHARE_AI) &&
                  err_ai == ESP_OK;

    /* MA-буфер — приватен для process_task (только update пишет/читает),
     * lock не нужен. */
    for (int ch = 0; ch < AI_CHANNEL_COUNT; ch++) {
        s_ma_buf[ch][s_ma_idx] = raw[ch];
    }
    s_ma_idx = (s_ma_idx + 1) % MA_WINDOW;
    if (s_ma_idx == 0) s_ma_filled = true;

    int count = s_ma_filled ? MA_WINDOW : (s_ma_idx > 0 ? s_ma_idx : 1);

    /* Считаем новый снимок локально, потом атомарно публикуем в s_data
     * под spinlock'ом — readers (mqtt/httpd) увидят либо старое целое,
     * либо новое целое, но не разорванный снимок. */
    ai_data_t snapshot;
    snapshot.device_online = online;

    for (int ch = 0; ch < AI_CHANNEL_COUNT; ch++) {
        if (!s_config[ch].enabled) {
            snapshot.channels[ch].valid = false;
            snapshot.channels[ch].fault = false;
            snapshot.channels[ch].value = 0.0f;
            snapshot.channels[ch].raw_ma = 0.0f;
            continue;
        }

        /* Среднее арифметическое (raw в мкА) */
        uint32_t sum = 0;
        for (int j = 0; j < count; j++) {
            sum += s_ma_buf[ch][j];
        }
        uint32_t avg_uA = sum / count;

        /* Sensor fault: обрыв линии или КЗ */
        bool fault_break = (avg_uA < FAULT_BREAK_UA);
        bool fault_short = (avg_uA > FAULT_SHORT_UA);
        snapshot.channels[ch].fault = fault_break || fault_short;

        /* raw_mA для диагностики — всегда, даже при fault */
        snapshot.channels[ch].raw_ma = (float)avg_uA / 1000.0f;

        if (snapshot.channels[ch].fault) {
            /* При fault не публикуем «реальное» давление — только NaN.
             * Caller (state_machine, mqtt) должен трактовать это как
             * sensor fault и не принимать решений по value. */
            snapshot.channels[ch].value = NAN;
            snapshot.channels[ch].valid = false;
        } else {
            /* (raw_uA − 4000) / 16000 → ratio в 0..1 для нормальной токовой петли.
             * Клипуем на ±допуск (3.5..20.5 мА → ratio чуть меньше 0 или больше 1
             * не считаем fault, но прижимаем к границам диапазона датчика). */
            float ratio = ((float)avg_uA - (float)UA_AT_4MA) / (float)UA_SPAN;
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;

            float span = s_config[ch].range_max - s_config[ch].range_min;
            snapshot.channels[ch].value = s_config[ch].range_min + ratio * span;
            snapshot.channels[ch].valid = online;
        }
    }

    portENTER_CRITICAL(&s_data_mux);
    s_data = snapshot;
    portEXIT_CRITICAL(&s_data_mux);
}

void analog_input_get_data(ai_data_t *out)
{
    portENTER_CRITICAL(&s_data_mux);
    *out = s_data;
    portEXIT_CRITICAL(&s_data_mux);
}

float analog_input_get_value(uint8_t ch)
{
    if (ch >= AI_CHANNEL_COUNT) return NAN;
    portENTER_CRITICAL(&s_data_mux);
    bool valid = s_data.channels[ch].valid;
    float val  = s_data.channels[ch].value;
    portEXIT_CRITICAL(&s_data_mux);
    return valid ? val : NAN;
}

bool analog_input_is_fault(uint8_t ch)
{
    if (ch >= AI_CHANNEL_COUNT) return true;
    portENTER_CRITICAL(&s_data_mux);
    bool fault = s_data.channels[ch].fault;
    portEXIT_CRITICAL(&s_data_mux);
    return fault;
}
