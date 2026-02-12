/**
 * @file mock_conductivity.h
 * @brief Мок: inject значений проводимости
 */
#pragma once

#include <stdint.h>

void mock_conductivity_set(uint8_t ch, float val);
void mock_conductivity_reset(void);
