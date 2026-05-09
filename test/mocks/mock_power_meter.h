/**
 * @file mock_power_meter.h
 * @brief Мок драйвера KWS-306L для unit-тестов state_machine.
 *
 * Phase-5: state_machine.c теперь зависит от power_meter (KWS-защиты).
 * Этот мок позволяет тестам управлять «текущими» значениями V/I/T и
 * online-статусом без подключения mock_modbus_poller или реального
 * power_meter.c.
 *
 * Поведение по умолчанию (после mock_pm_reset / setUp):
 *   - оба насоса offline, valid=false
 *   - геттеры возвращают NaN
 *   - state_machine не поднимает KWS-алармы
 *
 * Использование:
 *   mock_pm_set(PUMP_LP, 230.0f, 5.0f, 1100.0f, 0.0f, 45.0f, true);
 */
#pragma once

#include "power_meter.h"

/** Сброс мока: оба насоса offline, поля = 0. */
void mock_pm_reset(void);

/**
 * @brief Установить «измерение» для одного KWS.
 * @param pump      PUMP_LP / PUMP_HP
 * @param V         напряжение (В)
 * @param A         ток (А)
 * @param W         мощность (Вт)
 * @param kWh       энергия (кВт·ч)
 * @param T         температура (°C)
 * @param online    true = устройство отвечает (valid=true)
 */
void mock_pm_set(pump_id_t pump, float V, float A, float W,
                 float kWh, float T, bool online);
