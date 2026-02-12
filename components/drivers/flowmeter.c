/**
 * @file flowmeter.c
 * @brief Драйвер расходомера УРЖ2КМ — нестандартный float
 *
 * УРЖ2КМ передаёт float в 2 Modbus-регистрах (word-swapped):
 *   reg[0] = low word  (байты B,C по IEEE754)
 *   reg[1] = high word (байты S,E,A по IEEE754)
 * Восстановление: (reg_hi << 16) | reg_lo → memcpy → float
 */
#include "flowmeter.h"
#include "modbus_poller.h"
#include "board_config.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "flow_drv";

static flowmeter_data_t s_data;

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
    memset(&s_data, 0, sizeof(s_data));
    ESP_LOGI(TAG, "Драйвер расходомера УРЖ2КМ инициализирован");
}

void flowmeter_update(void)
{
    uint16_t flow_raw[CID_FLOW_RATE_REG_COUNT];
    uint16_t vol_raw[CID_FLOW_VOL_REG_COUNT];

    modbus_poller_get_flow_raw(flow_raw, CID_FLOW_RATE_REG_COUNT);
    modbus_poller_get_volume_raw(vol_raw, CID_FLOW_VOL_REG_COUNT);
    s_data.device_online = modbus_poller_is_device_online(MB_ADDR_URZH2KM);

    for (int ch = 0; ch < FLOW_CHANNEL_COUNT; ch++) {
        /* Расход: 2 регистра на float */
        float q = urzh_regs_to_float(flow_raw[ch * 2], flow_raw[ch * 2 + 1]);
        s_data.flow_m3h[ch] = q;

        /* Объём: 2 регистра на float */
        float v = urzh_regs_to_float(vol_raw[ch * 2], vol_raw[ch * 2 + 1]);
        s_data.volume_m3[ch] = v;

        /* Валидность: finite, неотрицательный */
        s_data.channel_ok[ch] = s_data.device_online &&
                                isfinite(q) && q >= 0.0f &&
                                isfinite(v) && v >= 0.0f;
    }
}

void flowmeter_get_data(flowmeter_data_t *out)
{
    *out = s_data;
}

float flowmeter_get_flow(uint8_t ch)
{
    if (ch >= FLOW_CHANNEL_COUNT || !s_data.channel_ok[ch]) {
        return NAN;
    }
    return s_data.flow_m3h[ch];
}
