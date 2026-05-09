/**
 * @file mock_esp_timer.c
 * @brief Мок: управляемые фейковые часы
 */
#include "mock_esp_timer.h"

static int64_t s_fake_time_us = 0;

int64_t esp_timer_get_time(void)
{
    return s_fake_time_us;
}

void mock_esp_timer_set(int64_t us)
{
    s_fake_time_us = us;
}

void mock_esp_timer_advance(int64_t us)
{
    s_fake_time_us += us;
}

void mock_esp_timer_reset(void)
{
    s_fake_time_us = 0;
}
