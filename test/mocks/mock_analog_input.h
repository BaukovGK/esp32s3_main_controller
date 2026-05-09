/**
 * @file mock_analog_input.h
 * @brief Мок: inject аналоговых значений (давление, температура)
 */
#pragma once

#include <stdint.h>

/* Установить значение канала */
void mock_analog_set(uint8_t ch, float val);

/* Сбросить все каналы в NAN */
void mock_analog_reset(void);
