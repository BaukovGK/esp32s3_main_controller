/**
 * @file esp_timer.h
 * @brief Stub для host-тестов: декларация esp_timer_get_time()
 *
 * Реализация — в mock_esp_timer.c (фейковые часы).
 */
#pragma once

#include <stdint.h>

int64_t esp_timer_get_time(void);
