/**
 * @file conductivity.c
 * @brief Драйвер кондуктометров СЛ21 — 4 канала на двух блоках
 *
 * Addr 10 (6 рег): [σ1_hi, σ1_lo, T1, σ2_hi, σ2_lo, T2] → FEED + PERM1
 * Addr 11 (6 рег): [σ3_hi, σ3_lo, T3, σ4_hi, σ4_lo, T4] → PERM2 + CONC
 *
 * Электропроводность: uint32(hi,lo) / 100 → µS/cm  (СЛ21-100Т, делитель 100)
 * Температура: int16 / 10 → °C
 *
 * ⚠️ Word-order СЛ21: HI в МЛАДШЕМ адресе регистра (40001 = HI, 40002 = LO).
 * В реализации нашего esp-modbus master регистры читаются в порядке адресации,
 * поэтому в массиве c10/c11 [0]=HI и [1]=LO. См. doc/sl21.pdf п.5.16.3.
 *
 * Phase-4 (M-4): spinlock защищает s_data от гонок process_task ↔ mqtt/httpd.
 * 2026-05-09: расширено с 3 до 4 каналов (добавлен COND_CH_CONC через slave 11 X2/t2).
 */
#include "conductivity.h"
#include "modbus_poller.h"
#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <string.h>
#include <math.h>

static const char *TAG = "cond_drv";

/* Коэффициенты преобразования СЛ21 */
#define SL21_COND_DIVISOR   100.0f   /* raw → µS/cm */
#define SL21_TEMP_DIVISOR   10.0f    /* raw → °C */

/* Смещения регистров в блоке канала (3 рег на канал) */
#define SL21_REG_COND_HI   0
#define SL21_REG_COND_LO   1
#define SL21_REG_TEMP       2
#define SL21_CH_STRIDE      3   /* регистров на канал */

static conductivity_data_t s_data;
static portMUX_TYPE s_data_mux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t regs_to_uint32(uint16_t hi, uint16_t lo)
{
    return ((uint32_t)hi << 16) | (uint32_t)lo;
}

void conductivity_init(void)
{
    portENTER_CRITICAL(&s_data_mux);
    memset(&s_data, 0, sizeof(s_data));
    portEXIT_CRITICAL(&s_data_mux);
    ESP_LOGI(TAG, "Драйвер кондуктометров СЛ21 инициализирован");
}

void conductivity_update(void)
{
    uint16_t c10[CID_COND10_REG_COUNT];
    uint16_t c11[CID_COND11_REG_COUNT];

    /* Phase-5 (C-9/C-10/H-modbus-initial-state): raw-getters зануляют
     * буферы и возвращают ESP_ERR_INVALID_STATE до первого опроса.
     * Не-OK от конкретного устройства → его dev*_on = false → каналы
     * этого устройства уйдут в channel_ok=false (NaN на выдаче). */
    esp_err_t err10 = modbus_poller_get_cond10_raw(c10, CID_COND10_REG_COUNT);
    esp_err_t err11 = modbus_poller_get_cond11_raw(c11, CID_COND11_REG_COUNT);

    bool dev10_on = modbus_poller_is_device_online(MB_ADDR_SL21_201) && err10 == ESP_OK;
    bool dev11_on = modbus_poller_is_device_online(MB_ADDR_SL21_101) && err11 == ESP_OK;

    /* Локальный snapshot */
    conductivity_data_t snapshot;
    snapshot.device10_online = dev10_on;
    snapshot.device11_online = dev11_on;

    /* Addr 10, канал 1: σ1 */
    uint32_t raw1 = regs_to_uint32(c10[SL21_REG_COND_HI], c10[SL21_REG_COND_LO]);
    snapshot.conductivity_uS[COND_CH_FEED] = (float)raw1 / SL21_COND_DIVISOR;
    snapshot.temperature_C[COND_CH_FEED]   = (int16_t)c10[SL21_REG_TEMP] / SL21_TEMP_DIVISOR;
    snapshot.channel_ok[COND_CH_FEED]      = dev10_on;

    /* Addr 10, канал 2: σ2 */
    uint32_t raw2 = regs_to_uint32(c10[SL21_CH_STRIDE + SL21_REG_COND_HI],
                                   c10[SL21_CH_STRIDE + SL21_REG_COND_LO]);
    snapshot.conductivity_uS[COND_CH_PERM1] = (float)raw2 / SL21_COND_DIVISOR;
    snapshot.temperature_C[COND_CH_PERM1]   = (int16_t)c10[SL21_CH_STRIDE + SL21_REG_TEMP] / SL21_TEMP_DIVISOR;
    snapshot.channel_ok[COND_CH_PERM1]      = dev10_on;

    /* Addr 11, канал 1: σ3 (пермеат 2-й ступени) */
    uint32_t raw3 = regs_to_uint32(c11[SL21_REG_COND_HI], c11[SL21_REG_COND_LO]);
    snapshot.conductivity_uS[COND_CH_PERM2] = (float)raw3 / SL21_COND_DIVISOR;
    snapshot.temperature_C[COND_CH_PERM2]   = (int16_t)c11[SL21_REG_TEMP] / SL21_TEMP_DIVISOR;
    snapshot.channel_ok[COND_CH_PERM2]      = dev11_on;

    /* Addr 11, канал 2: σ4 (концентрат) — добавлено 2026-05-09 */
    uint32_t raw4 = regs_to_uint32(c11[SL21_CH_STRIDE + SL21_REG_COND_HI],
                                   c11[SL21_CH_STRIDE + SL21_REG_COND_LO]);
    snapshot.conductivity_uS[COND_CH_CONC] = (float)raw4 / SL21_COND_DIVISOR;
    snapshot.temperature_C[COND_CH_CONC]   = (int16_t)c11[SL21_CH_STRIDE + SL21_REG_TEMP] / SL21_TEMP_DIVISOR;
    snapshot.channel_ok[COND_CH_CONC]      = dev11_on;

    portENTER_CRITICAL(&s_data_mux);
    s_data = snapshot;
    portEXIT_CRITICAL(&s_data_mux);
}

void conductivity_get_data(conductivity_data_t *out)
{
    portENTER_CRITICAL(&s_data_mux);
    *out = s_data;
    portEXIT_CRITICAL(&s_data_mux);
}

float conductivity_get_value(uint8_t ch)
{
    if (ch >= COND_CHANNEL_COUNT) return NAN;
    portENTER_CRITICAL(&s_data_mux);
    bool ok = s_data.channel_ok[ch];
    float v = s_data.conductivity_uS[ch];
    portEXIT_CRITICAL(&s_data_mux);
    return ok ? v : NAN;
}
