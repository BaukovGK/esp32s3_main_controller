/**
 * @file mock_hal_nvs.h
 * @brief Мок: key-value store в RAM (замена NVS)
 */
#pragma once

#include <stdint.h>

/* Сбросить всё хранилище */
void mock_nvs_reset(void);
