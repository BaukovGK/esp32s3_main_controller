/**
 * @file power_meter.c
 * @brief Драйвер счётчиков электроэнергии KWS-306L (НД и ВД насосы).
 *
 * Опрашиваемый блок 14 регистров 0x000E..0x001B читается одним запросом
 * FC 0x03 через modbus_poller (CID_KWS_PUMP_LP / CID_KWS_PUMP_HP).
 * Драйвер хранит снимок последних значений в s_data[], защищённый
 * portMUX_TYPE спинлоком (по образцу conductivity.c, Phase-4 M-4).
 *
 * Известные регистры в блоке (см. power_meter.h, TODO datasheet):
 *   offset 0  (0x000E) voltage     × 0.1   → В
 *   offset 2  (0x0010) current     × 0.001 → А
 *   offset 4  (0x0012) power       × 0.1   → Вт
 *   offset 12 (0x001A) energy      × 0.01  → кВт·ч  (TODO uint32?)
 *   offset 13 (0x001B) temperature × 1     → °C
 *   offset 1, 3, 5..11             — неизвестно (см. datasheet TODO)
 *
 * При offline / ESP_ERR_INVALID_STATE / TIMEOUT → valid=false, геттеры NaN.
 */
#include "power_meter.h"
#include "modbus_poller.h"
#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <string.h>
#include <math.h>
#include <stddef.h>

static const char *TAG = "kws_drv";

/* --- Смещения регистров от mb_reg_start = 0x000E --- */
#define KWS_REG_OFFSET_VOLTAGE   0   /* 0x000E - 0x000E */
#define KWS_REG_OFFSET_CURRENT   2   /* 0x0010 - 0x000E */
#define KWS_REG_OFFSET_POWER     4   /* 0x0012 - 0x000E */
#define KWS_REG_OFFSET_ENERGY   12   /* 0x001A - 0x000E */
#define KWS_REG_OFFSET_TEMP     13   /* 0x001B - 0x000E */

/* --- Множители raw → инженерные единицы --- */
#define KWS_VOLTAGE_SCALE   0.1f       /* В   */
#define KWS_CURRENT_SCALE   0.001f     /* А   */
#define KWS_POWER_SCALE     0.1f       /* Вт  */
#define KWS_ENERGY_SCALE    0.01f      /* кВт·ч */
#define KWS_TEMP_SCALE      1.0f       /* °C  */

/* --- Соответствие pump_id_t → MB slave addr --- */
static uint8_t pump_slave_addr(pump_id_t pump)
{
    switch (pump) {
        case PUMP_LP: return MB_ADDR_KWS_PUMP_LP;
        case PUMP_HP: return MB_ADDR_KWS_PUMP_HP;
        default:      return 0;
    }
}

/* --- Снимок состояния, защищён спинлоком --- */
static power_meter_data_t s_data[PUMP_COUNT];
static portMUX_TYPE s_data_mux = portMUX_INITIALIZER_UNLOCKED;

void power_meter_init(void)
{
    portENTER_CRITICAL(&s_data_mux);
    memset(s_data, 0, sizeof(s_data));
    portEXIT_CRITICAL(&s_data_mux);
    ESP_LOGI(TAG, "Драйвер KWS-306L инициализирован (LP=%d, HP=%d)",
             MB_ADDR_KWS_PUMP_LP, MB_ADDR_KWS_PUMP_HP);
}

/* Чтение блока через соответствующий getter. Один блок = 14 регистров. */
static esp_err_t read_block(pump_id_t pump, uint16_t *out)
{
    if (pump == PUMP_LP) return modbus_poller_get_kws_lp_raw(out, CID_KWS_REG_COUNT);
    if (pump == PUMP_HP) return modbus_poller_get_kws_hp_raw(out, CID_KWS_REG_COUNT);
    return ESP_ERR_NOT_FOUND;
}

/* Конвертация raw-блока в power_meter_data_t (без локов).
 * Если valid=false — все поля кроме online остаются нулями (без NaN —
 * NaN выдаётся только на стороне геттера). */
static void convert_block(const uint16_t *regs, bool online, bool ok,
                          power_meter_data_t *out)
{
    memset(out, 0, sizeof(*out));
    out->online = online;
    out->valid  = online && ok;
    if (!out->valid) {
        return;
    }
    out->voltage_V     = (float)regs[KWS_REG_OFFSET_VOLTAGE] * KWS_VOLTAGE_SCALE;
    out->current_A     = (float)regs[KWS_REG_OFFSET_CURRENT] * KWS_CURRENT_SCALE;
    out->power_W       = (float)regs[KWS_REG_OFFSET_POWER]   * KWS_POWER_SCALE;
    out->energy_kWh    = (float)regs[KWS_REG_OFFSET_ENERGY]  * KWS_ENERGY_SCALE;
    out->temperature_C = (float)regs[KWS_REG_OFFSET_TEMP]    * KWS_TEMP_SCALE;
}

void power_meter_update(void)
{
    power_meter_data_t snapshots[PUMP_COUNT];

    for (int i = 0; i < PUMP_COUNT; i++) {
        uint16_t regs[CID_KWS_REG_COUNT];
        esp_err_t err = read_block((pump_id_t)i, regs);
        bool dev_online = modbus_poller_is_device_online(pump_slave_addr((pump_id_t)i));
        bool ok = (err == ESP_OK);
        convert_block(regs, dev_online, ok, &snapshots[i]);
    }

    portENTER_CRITICAL(&s_data_mux);
    for (int i = 0; i < PUMP_COUNT; i++) {
        s_data[i] = snapshots[i];
    }
    portEXIT_CRITICAL(&s_data_mux);
}

void power_meter_get_data(pump_id_t pump, power_meter_data_t *out)
{
    if (out == NULL || pump >= PUMP_COUNT) return;
    portENTER_CRITICAL(&s_data_mux);
    *out = s_data[pump];
    portEXIT_CRITICAL(&s_data_mux);
}

/* --- Helper: атомарно скопировать только нужное поле и флаг valid --- */
static float read_field_locked(pump_id_t pump, size_t offset)
{
    if (pump >= PUMP_COUNT) return NAN;
    portENTER_CRITICAL(&s_data_mux);
    bool valid = s_data[pump].valid;
    float value = NAN;
    if (valid) {
        const char *base = (const char *)&s_data[pump];
        value = *(const float *)(base + offset);
    }
    portEXIT_CRITICAL(&s_data_mux);
    return value;
}

float power_meter_get_voltage(pump_id_t pump)
{
    return read_field_locked(pump, offsetof(power_meter_data_t, voltage_V));
}

float power_meter_get_current(pump_id_t pump)
{
    return read_field_locked(pump, offsetof(power_meter_data_t, current_A));
}

float power_meter_get_power(pump_id_t pump)
{
    return read_field_locked(pump, offsetof(power_meter_data_t, power_W));
}

float power_meter_get_energy(pump_id_t pump)
{
    return read_field_locked(pump, offsetof(power_meter_data_t, energy_kWh));
}

float power_meter_get_temperature(pump_id_t pump)
{
    return read_field_locked(pump, offsetof(power_meter_data_t, temperature_C));
}

bool power_meter_is_online(pump_id_t pump)
{
    if (pump >= PUMP_COUNT) return false;
    portENTER_CRITICAL(&s_data_mux);
    bool on = s_data[pump].online;
    portEXIT_CRITICAL(&s_data_mux);
    return on;
}
