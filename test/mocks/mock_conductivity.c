/**
 * @file mock_conductivity.c
 * @brief Мок: inject значений проводимости
 */
#include "mock_conductivity.h"
#include "conductivity.h"
#include <math.h>
#include <string.h>

static float s_cond[COND_CHANNEL_COUNT];

/* === Реализация production API === */

void conductivity_init(void)
{
    mock_conductivity_reset();
}

void conductivity_update(void)
{
    /* no-op */
}

float conductivity_get_value(uint8_t ch)
{
    if (ch >= COND_CHANNEL_COUNT) return NAN;
    return s_cond[ch];
}

void conductivity_get_data(conductivity_data_t *out)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < COND_CHANNEL_COUNT; i++) {
        out->conductivity_uS[i] = s_cond[i];
        out->channel_ok[i] = !isnan(s_cond[i]);
    }
    out->device10_online = true;
    out->device11_online = true;
}

/* === Управление из тестов === */

void mock_conductivity_set(uint8_t ch, float val)
{
    if (ch < COND_CHANNEL_COUNT) {
        s_cond[ch] = val;
    }
}

void mock_conductivity_reset(void)
{
    for (int i = 0; i < COND_CHANNEL_COUNT; i++) {
        s_cond[i] = NAN;
    }
}
