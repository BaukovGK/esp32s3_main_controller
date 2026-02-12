/**
 * @file conductivity.c
 * @brief Драйвер кондуктометров СЛ21
 *
 * Addr 10 (6 рег): [σ1_hi, σ1_lo, T1, σ2_hi, σ2_lo, T2]
 * Addr 11 (3 рег): [σ3_hi, σ3_lo, T3]
 * Электропроводность: uint32(hi,lo) / 100 → µS/cm
 * Температура: int16 / 10 → °C
 */
#include "conductivity.h"
#include "modbus_poller.h"
#include "board_config.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "cond_drv";

static conductivity_data_t s_data;

static uint32_t regs_to_uint32(uint16_t hi, uint16_t lo)
{
    return ((uint32_t)hi << 16) | (uint32_t)lo;
}

void conductivity_init(void)
{
    memset(&s_data, 0, sizeof(s_data));
    ESP_LOGI(TAG, "Драйвер кондуктометров СЛ21 инициализирован");
}

void conductivity_update(void)
{
    uint16_t c10[CID_COND10_REG_COUNT];
    uint16_t c11[CID_COND11_REG_COUNT];

    modbus_poller_get_cond10_raw(c10, CID_COND10_REG_COUNT);
    modbus_poller_get_cond11_raw(c11, CID_COND11_REG_COUNT);

    s_data.device10_online = modbus_poller_is_device_online(MB_ADDR_SL21_201);
    s_data.device11_online = modbus_poller_is_device_online(MB_ADDR_SL21_101);

    /* Addr 10, канал 1: σ1 */
    uint32_t raw1 = regs_to_uint32(c10[0], c10[1]);
    s_data.conductivity_uS[COND_CH_FEED] = (float)raw1 / 100.0f;
    s_data.temperature_C[COND_CH_FEED]   = (int16_t)c10[2] / 10.0f;
    s_data.channel_ok[COND_CH_FEED]      = s_data.device10_online;

    /* Addr 10, канал 2: σ2 */
    uint32_t raw2 = regs_to_uint32(c10[3], c10[4]);
    s_data.conductivity_uS[COND_CH_PERM1] = (float)raw2 / 100.0f;
    s_data.temperature_C[COND_CH_PERM1]   = (int16_t)c10[5] / 10.0f;
    s_data.channel_ok[COND_CH_PERM1]      = s_data.device10_online;

    /* Addr 11, канал 1: σ3 */
    uint32_t raw3 = regs_to_uint32(c11[0], c11[1]);
    s_data.conductivity_uS[COND_CH_PERM2] = (float)raw3 / 100.0f;
    s_data.temperature_C[COND_CH_PERM2]   = (int16_t)c11[2] / 10.0f;
    s_data.channel_ok[COND_CH_PERM2]      = s_data.device11_online;
}

void conductivity_get_data(conductivity_data_t *out)
{
    *out = s_data;
}

float conductivity_get_value(uint8_t ch)
{
    if (ch >= COND_CHANNEL_COUNT || !s_data.channel_ok[ch]) {
        return NAN;
    }
    return s_data.conductivity_uS[ch];
}
