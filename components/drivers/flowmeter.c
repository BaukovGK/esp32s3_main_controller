/**
 * @file flowmeter.c
 * @brief Драйвер расходомера УРЖ2КМ — нестандартный float
 *
 * УРЖ2КМ передаёт float в 2 Modbus-регистрах (word-swapped):
 *   reg[0] = low word  (байты B,C по IEEE754)
 *   reg[1] = high word (байты S,E,A по IEEE754)
 * Восстановление: (reg_hi << 16) | reg_lo → memcpy → float
 *
 * Phase-4 (M-4): доступ к s_data защищён spinlock'ом — readers (mqtt/httpd)
 * получают атомарный снимок без разрывов.
 */
#include "flowmeter.h"
#include "modbus_poller.h"
#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <string.h>
#include <math.h>

static const char *TAG = "flow_drv";

static flowmeter_data_t s_data;
static portMUX_TYPE s_data_mux = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief Конвертация word-swapped регистров в IEEE754 float
 */
static float urzh_regs_to_float(uint16_t reg_lo, uint16_t reg_hi)
{
    uint32_t ieee = ((uint32_t)reg_hi << 16) | (uint32_t)reg_lo;
    float result;
    memcpy(&result, &ieee, sizeof(float));
    return result;
}

void flowmeter_init(void)
{
    portENTER_CRITICAL(&s_data_mux);
    memset(&s_data, 0, sizeof(s_data));
    portEXIT_CRITICAL(&s_data_mux);
    ESP_LOGI(TAG, "Драйвер расходомера УРЖ2КМ инициализирован");
}

void flowmeter_update(void)
{
    uint16_t flow_raw[CID_FLOW_RATE_REG_COUNT];
    uint16_t vol_raw[CID_FLOW_VOL_REG_COUNT];

    /* Phase-5 (C-9/C-10/H-modbus-initial-state): raw-getters сами зануляют
     * буфер и возвращают ESP_ERR_INVALID_STATE до первого опроса. Любой
     * не-OK здесь трактуется как «нет валидных данных» → выставляем
     * online=false локально, чтобы channel_ok оказался false для всех
     * каналов (даже если is_device_online между вызовами вернул true). */
    esp_err_t err_flow = modbus_poller_get_flow_raw(flow_raw, CID_FLOW_RATE_REG_COUNT);
    esp_err_t err_vol  = modbus_poller_get_volume_raw(vol_raw, CID_FLOW_VOL_REG_COUNT);
    bool online = modbus_poller_is_device_online(MB_ADDR_URZH2KM) &&
                  err_flow == ESP_OK && err_vol == ESP_OK;

    /* Локальный snapshot — публикуем атомарно */
    flowmeter_data_t snapshot;
    snapshot.device_online = online;

    for (int ch = 0; ch < FLOW_CHANNEL_COUNT; ch++) {
        float q = urzh_regs_to_float(flow_raw[ch * 2], flow_raw[ch * 2 + 1]);
        float v = urzh_regs_to_float(vol_raw[ch * 2], vol_raw[ch * 2 + 1]);
        snapshot.flow_m3h[ch]   = q;
        snapshot.volume_m3[ch]  = v;
        snapshot.channel_ok[ch] = online &&
                                  isfinite(q) && q >= 0.0f &&
                                  isfinite(v) && v >= 0.0f;
    }

    portENTER_CRITICAL(&s_data_mux);
    s_data = snapshot;
    portEXIT_CRITICAL(&s_data_mux);
}

void flowmeter_get_data(flowmeter_data_t *out)
{
    portENTER_CRITICAL(&s_data_mux);
    *out = s_data;
    portEXIT_CRITICAL(&s_data_mux);
}

float flowmeter_get_flow(uint8_t ch)
{
    if (ch >= FLOW_CHANNEL_COUNT) return NAN;
    portENTER_CRITICAL(&s_data_mux);
    bool ok = s_data.channel_ok[ch];
    float v = s_data.flow_m3h[ch];
    portEXIT_CRITICAL(&s_data_mux);
    return ok ? v : NAN;
}
