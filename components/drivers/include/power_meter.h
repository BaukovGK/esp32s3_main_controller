/**
 * @file power_meter.h
 * @brief Драйвер счётчика электроэнергии KWS-306L (трёхфазный, RS-485 Modbus RTU)
 *
 * Используется для контроля потребления НД-насоса (slave 20) и ВД-насоса
 * (slave 21). Через FC 0x03 одним блоком читаются регистры 0x000E..0x001B
 * (14 регистров). Конвертация → инженерные единицы.
 *
 * Карта регистров (что подтверждено заказчиком):
 *   0x000E voltage     uint16  × 0.1   → В
 *   0x0010 current     uint16  × 0.001 → А
 *   0x0012 power       uint16  × 0.1   → Вт
 *   0x001A energy      uint16  × 0.01  → кВт·ч
 *   0x001B temperature uint16  × 1     → °C
 *
 * ⚠️ TODO datasheet KWS-306L:
 *   - KWS-306L 3-фазный, но в карте только один канал U/I/P. Уточнить
 *     адреса фазных значений (Ua/Ub/Uc, Ia/Ib/Ic).
 *   - Energy uint16 × 0.01 даёт max 655.35 кВт·ч → переполнится за дни.
 *     Реально, скорее всего, 0x001A+0x001B = uint32 energy, а
 *     temperature живёт в другом регистре. Опросить полный блок
 *     0x0000..0x0040 при первом подключении.
 *   - Регистр управления реле 0x003F (FC 0x06) — реализуется отдельно.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Идентификаторы насосов на стороне power_meter */
typedef enum {
    PUMP_LP = 0,    /* НД-насос (slave 20, MB_ADDR_KWS_PUMP_LP) */
    PUMP_HP = 1,    /* ВД-насос (slave 21, MB_ADDR_KWS_PUMP_HP) */
    PUMP_COUNT
} pump_id_t;

typedef struct {
    float voltage_V;        /* Напряжение, В                    (raw 0x000E × 0.1)   */
    float current_A;        /* Ток, А                            (raw 0x0010 × 0.001) */
    float power_W;          /* Активная мощность, Вт             (raw 0x0012 × 0.1)   */
    float energy_kWh;       /* Накопленная энергия, кВт·ч        (raw 0x001A × 0.01)  */
    float temperature_C;    /* Температура корпуса, °C           (raw 0x001B × 1)     */
    bool  online;           /* Устройство отвечает на шине       */
    bool  valid;            /* online && first_poll_done && OK   */
} power_meter_data_t;

/**
 * @brief Инициализация драйвера. Должна быть вызвана один раз при старте.
 *        Все поля state'а зануляются, online/valid = false.
 */
void power_meter_init(void);

/**
 * @brief Цикл обновления (вызывать периодически из process_task / отдельной задачи).
 *        Запрашивает свежие сырые регистры через modbus_poller_get_kws_*_raw,
 *        конвертирует в физические единицы, обновляет внутренний state под локом.
 */
void power_meter_update(void);

/**
 * @brief Скопировать текущий снимок данных конкретного насоса.
 *
 * @param pump  идентификатор насоса (PUMP_LP / PUMP_HP)
 * @param out   буфер вызывающего; не должен быть NULL.
 *              При недопустимом pump или NULL out — функция ничего не делает.
 */
void power_meter_get_data(pump_id_t pump, power_meter_data_t *out);

/**
 * @brief Геттеры одиночных значений. Возвращают NaN, если data.valid == false
 *        либо pump >= PUMP_COUNT.
 */
float power_meter_get_voltage(pump_id_t pump);
float power_meter_get_current(pump_id_t pump);
float power_meter_get_power(pump_id_t pump);
float power_meter_get_energy(pump_id_t pump);
float power_meter_get_temperature(pump_id_t pump);

/**
 * @brief Online-статус устройства. False до первого успешного опроса
 *        и при потере связи (на основе modbus_poller_is_device_online).
 */
bool power_meter_is_online(pump_id_t pump);

#ifdef __cplusplus
}
#endif
