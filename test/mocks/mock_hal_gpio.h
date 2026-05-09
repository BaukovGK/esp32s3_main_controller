/**
 * @file mock_hal_gpio.h
 * @brief Мок: DI inject + DO spy
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Управление DI из тестов */
void mock_hal_gpio_set_di(uint8_t val);

/* Чтение DO из тестов */
uint8_t mock_hal_gpio_get_do(void);
bool mock_hal_gpio_get_do_pin(uint8_t pin);

/* Полный сброс */
void mock_hal_gpio_reset(void);
