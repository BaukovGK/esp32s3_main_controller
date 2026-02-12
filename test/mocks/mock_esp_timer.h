/**
 * @file mock_esp_timer.h
 * @brief Мок: управляемые фейковые часы
 */
#pragma once

#include <stdint.h>

/* Управление из тестов */
void mock_esp_timer_set(int64_t us);
void mock_esp_timer_advance(int64_t us);
void mock_esp_timer_reset(void);
