/**
 * @file mock_analog_input.c
 * @brief Мок: inject аналоговых значений
 */
#include "mock_analog_input.h"
#include "analog_input.h"
#include <math.h>
#include <string.h>

static float s_values[AI_CHANNEL_COUNT];

/* === Реализация production API === */

void analog_input_init(void)
{
    mock_analog_reset();
}

void analog_input_update(void)
{
    /* no-op */
}

float analog_input_get_value(uint8_t ch)
{
    if (ch >= AI_CHANNEL_COUNT) return NAN;
    return s_values[ch];
}

bool analog_input_is_fault(uint8_t ch)
{
    if (ch >= AI_CHANNEL_COUNT) return true;
    return isnan(s_values[ch]);
}

void analog_input_get_data(ai_data_t *out)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < AI_CHANNEL_COUNT; i++) {
        out->channels[i].value = s_values[i];
        out->channels[i].valid = !isnan(s_values[i]);
        out->channels[i].fault = isnan(s_values[i]);
    }
    out->device_online = true;
}

/* === Управление из тестов === */

void mock_analog_set(uint8_t ch, float val)
{
    if (ch < AI_CHANNEL_COUNT) {
        s_values[ch] = val;
    }
}

void mock_analog_reset(void)
{
    for (int i = 0; i < AI_CHANNEL_COUNT; i++) {
        s_values[i] = NAN;
    }
}
