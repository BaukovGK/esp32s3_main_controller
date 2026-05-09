/**
 * @file mock_interlocks.c
 * @brief Мок: управляемый результат interlocks_check()
 */
#include "mock_interlocks.h"
#include <string.h>

static interlock_result_t s_mock_result;

/* === Реализация production API === */

void interlocks_init(void)
{
    mock_interlocks_reset();
}

void interlocks_check(bool manual_mode, interlock_result_t *result)
{
    (void)manual_mode;
    *result = s_mock_result;
}

/* === Управление из тестов === */

void mock_interlocks_set(const interlock_result_t *r)
{
    s_mock_result = *r;
}

void mock_interlocks_reset(void)
{
    memset(&s_mock_result, 0, sizeof(s_mock_result));
    s_mock_result.allow_pump_feed = true;
    s_mock_result.allow_pump_stage1 = true;
    s_mock_result.allow_pump_stage2 = true;
    s_mock_result.allow_heater = true;
    s_mock_result.allow_doser = true;
    s_mock_result.estop_active = false;
    s_mock_result.filter_warn = false;
}
