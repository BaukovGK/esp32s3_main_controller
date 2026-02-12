/**
 * @file mock_flowmeter.c
 * @brief Мок: inject значений расхода
 */
#include "mock_flowmeter.h"
#include "flowmeter.h"
#include <math.h>
#include <string.h>

static float s_flow[FLOW_CHANNEL_COUNT];

/* === Реализация production API === */

void flowmeter_init(void)
{
    mock_flowmeter_reset();
}

void flowmeter_update(void)
{
    /* no-op */
}

float flowmeter_get_flow(uint8_t ch)
{
    if (ch >= FLOW_CHANNEL_COUNT) return NAN;
    return s_flow[ch];
}

void flowmeter_get_data(flowmeter_data_t *out)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < FLOW_CHANNEL_COUNT; i++) {
        out->flow_m3h[i] = s_flow[i];
        out->channel_ok[i] = !isnan(s_flow[i]);
    }
    out->device_online = true;
}

/* === Управление из тестов === */

void mock_flowmeter_set(uint8_t ch, float val)
{
    if (ch < FLOW_CHANNEL_COUNT) {
        s_flow[ch] = val;
    }
}

void mock_flowmeter_reset(void)
{
    for (int i = 0; i < FLOW_CHANNEL_COUNT; i++) {
        s_flow[i] = NAN;
    }
}
