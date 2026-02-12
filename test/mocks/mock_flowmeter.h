/**
 * @file mock_flowmeter.h
 * @brief Мок: inject значений расхода
 */
#pragma once

#include <stdint.h>

void mock_flowmeter_set(uint8_t ch, float val);
void mock_flowmeter_reset(void);
