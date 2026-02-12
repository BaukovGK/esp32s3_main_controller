/**
 * @file modbus_poller.h
 * @brief Modbus RTU Master — менеджер циклического опроса
 *
 * Использует esp-modbus компонент для RTU Master.
 * Опрашивает устройства по расписанию, хранит сырые данные регистров.
 * Потокобезопасный доступ к данным через getter-функции.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Идентификаторы наборов регистров (CID) --- */
typedef enum {
    CID_AI_CHANNELS = 0,    /* Waveshare AI: slave 1, input regs 0x0000-0x0007 */
    CID_FLOW_RATES,         /* УРЖ2КМ расход: slave 2, holding regs 0x0000-0x0007 */
    CID_FLOW_VOLUMES,       /* УРЖ2КМ объём: slave 2, holding regs 0x0036-0x0045 */
    CID_COND_ADDR10,        /* СЛ21 addr 10: holding regs 0x0001-0x0006 */
    CID_COND_ADDR11,        /* СЛ21 addr 11: holding regs 0x0001-0x0003 */
    CID_COUNT
} modbus_cid_t;

/* --- Размеры буферов (в uint16_t регистрах) --- */
#define CID_AI_REG_COUNT        8
#define CID_FLOW_RATE_REG_COUNT 8
#define CID_FLOW_VOL_REG_COUNT  16
#define CID_COND10_REG_COUNT    6
#define CID_COND11_REG_COUNT    3

/**
 * @brief Инициализация esp-modbus master и регистрация параметров
 * @return ESP_OK при успехе
 */
esp_err_t modbus_poller_init(void);

/**
 * @brief FreeRTOS задача циклического опроса
 * @param arg не используется
 */
void modbus_poller_task(void *arg);

/**
 * @brief Получить сырые данные аналоговых входов (Waveshare AI, slave 1)
 * @param out буфер для CID_AI_REG_COUNT регистров
 * @param count размер буфера в uint16_t (должен быть >= CID_AI_REG_COUNT)
 * @return ESP_OK при успехе, ESP_ERR_INVALID_SIZE если буфер мал
 */
esp_err_t modbus_poller_get_ai_raw(uint16_t *out, size_t count);

/**
 * @brief Получить сырые регистры расхода (УРЖ2КМ, slave 2)
 */
esp_err_t modbus_poller_get_flow_raw(uint16_t *out, size_t count);

/**
 * @brief Получить сырые регистры объёма (УРЖ2КМ, slave 2)
 */
esp_err_t modbus_poller_get_volume_raw(uint16_t *out, size_t count);

/**
 * @brief Получить сырые регистры кондуктометра (СЛ21, addr 10)
 */
esp_err_t modbus_poller_get_cond10_raw(uint16_t *out, size_t count);

/**
 * @brief Получить сырые регистры кондуктометра (СЛ21, addr 11)
 */
esp_err_t modbus_poller_get_cond11_raw(uint16_t *out, size_t count);

/**
 * @brief Проверить доступность Modbus-устройства
 * @param slave_addr адрес устройства
 * @return true если устройство отвечает
 */
bool modbus_poller_is_device_online(uint8_t slave_addr);

/**
 * @brief Получить счётчик ошибок для устройства
 * @param slave_addr адрес устройства
 * @return количество ошибок подряд (0 = нет ошибок)
 */
uint32_t modbus_poller_get_error_count(uint8_t slave_addr);

#ifdef __cplusplus
}
#endif
