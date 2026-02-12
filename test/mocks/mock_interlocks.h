/**
 * @file mock_interlocks.h
 * @brief Мок: управляемый результат interlocks_check()
 *
 * Используется ТОЛЬКО в test_state_machine (чтобы не линковать real interlocks.c)
 */
#pragma once

#include "interlocks.h"

/* Установить результат, который будет возвращён из interlocks_check() */
void mock_interlocks_set(const interlock_result_t *r);

/* Сбросить к "все разрешено" */
void mock_interlocks_reset(void);
