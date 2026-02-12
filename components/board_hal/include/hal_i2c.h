/**
 * @file hal_i2c.h
 * @brief I2C master шина — потокобезопасная обёртка
 *
 * Используется для TCA9554 (управление DO) и RTC PCF85063.
 * Внутренний mutex гарантирует безопасный доступ из разных задач.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализация I2C master шины
 * @return ESP_OK при успехе
 */
esp_err_t hal_i2c_init(void);

/**
 * @brief Запись байта в регистр I2C-устройства
 * @param dev_addr 7-битный адрес устройства
 * @param reg      адрес регистра
 * @param data     байт данных
 * @return ESP_OK при успехе
 */
esp_err_t hal_i2c_write_reg(uint8_t dev_addr, uint8_t reg, uint8_t data);

/**
 * @brief Чтение байта из регистра I2C-устройства
 * @param dev_addr 7-битный адрес устройства
 * @param reg      адрес регистра
 * @param data     [out] прочитанный байт
 * @return ESP_OK при успехе
 */
esp_err_t hal_i2c_read_reg(uint8_t dev_addr, uint8_t reg, uint8_t *data);

#ifdef __cplusplus
}
#endif
