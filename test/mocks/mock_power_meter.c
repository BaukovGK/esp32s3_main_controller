/**
 * @file mock_power_meter.c
 * @brief Мок-реализация power_meter API. Используется в test_state_machine.
 */
#include "mock_power_meter.h"
#include <math.h>
#include <string.h>

static power_meter_data_t s_data[PUMP_COUNT];

/* === Управление из тестов === */

void mock_pm_reset(void)
{
    memset(s_data, 0, sizeof(s_data));
    /* offline / valid=false → геттеры NaN */
}

void mock_pm_set(pump_id_t pump, float V, float A, float W,
                 float kWh, float T, bool online)
{
    if (pump >= PUMP_COUNT) return;
    s_data[pump].voltage_V     = V;
    s_data[pump].current_A     = A;
    s_data[pump].power_W       = W;
    s_data[pump].energy_kWh    = kWh;
    s_data[pump].temperature_C = T;
    s_data[pump].online        = online;
    s_data[pump].valid         = online;
}

/* === Реализация production API === */

void power_meter_init(void)
{
    mock_pm_reset();
}

void power_meter_update(void)
{
    /* No-op в моке: тесты задают данные напрямую через mock_pm_set(). */
}

void power_meter_get_data(pump_id_t pump, power_meter_data_t *out)
{
    if (out == NULL || pump >= PUMP_COUNT) return;
    *out = s_data[pump];
}

float power_meter_get_voltage(pump_id_t pump)
{
    if (pump >= PUMP_COUNT || !s_data[pump].valid) return NAN;
    return s_data[pump].voltage_V;
}

float power_meter_get_current(pump_id_t pump)
{
    if (pump >= PUMP_COUNT || !s_data[pump].valid) return NAN;
    return s_data[pump].current_A;
}

float power_meter_get_power(pump_id_t pump)
{
    if (pump >= PUMP_COUNT || !s_data[pump].valid) return NAN;
    return s_data[pump].power_W;
}

float power_meter_get_energy(pump_id_t pump)
{
    if (pump >= PUMP_COUNT || !s_data[pump].valid) return NAN;
    return s_data[pump].energy_kWh;
}

float power_meter_get_temperature(pump_id_t pump)
{
    if (pump >= PUMP_COUNT || !s_data[pump].valid) return NAN;
    return s_data[pump].temperature_C;
}

bool power_meter_is_online(pump_id_t pump)
{
    if (pump >= PUMP_COUNT) return false;
    return s_data[pump].online;
}
