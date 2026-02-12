/**
 * @file hal_gpio.h
 * @brief Дискретные входы (DI) и релейные выходы (DO)
 *
 * DI1-DI8: прямые GPIO с программным debounce (50мс).
 * RO1-RO8: через I2C-расширитель TCA9554PWR.
 * Логика DI инвертирована (оптоизоляция): замкнут на GND → 1.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализация DI (GPIO) и DO (TCA9554)
 * @note Требует предварительного вызова hal_i2c_init()
 * @return ESP_OK при успехе
 */
esp_err_t hal_gpio_init(void);

/**
 * @brief Чтение всех DI как битовой маски
 * @return Битовая маска: бит 0 = DI1, бит 7 = DI8. 1 = активен
 */
uint8_t hal_gpio_read_di(void);

/**
 * @brief Чтение отдельного DI
 * @param pin Номер входа 1..8
 * @return true если вход активен
 */
bool hal_gpio_read_di_pin(uint8_t pin);

/**
 * @brief Установить все DO по битовой маске
 * @param mask Бит 0 = RO1, бит 7 = RO8. 1 = включён
 * @return ESP_OK при успехе
 */
esp_err_t hal_gpio_write_do(uint8_t mask);

/**
 * @brief Установить отдельный DO
 * @param pin   Номер выхода 1..8
 * @param state true = включить, false = выключить
 * @return ESP_OK при успехе
 */
esp_err_t hal_gpio_write_do_pin(uint8_t pin, bool state);

/**
 * @brief Получить текущее состояние DO
 * @return Битовая маска текущего состояния выходов
 */
uint8_t hal_gpio_read_do_state(void);

/**
 * @brief Обработка debounce для DI — вызывать каждые 10мс из IoTask
 */
void hal_gpio_debounce_process(void);

/**
 * @brief Чтение E-STOP напрямую без debounce (для реакции < 10мс)
 * @return true если E-STOP активен (DI5 замкнут на GND)
 */
bool hal_gpio_is_estop_raw(void);

#ifdef __cplusplus
}
#endif
