/**
 * @file telemetry.c
 * @brief Расчётные параметры установки обратного осмоса
 */
#include "telemetry.h"
#include "analog_input.h"
#include "flowmeter.h"
#include "conductivity.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "telemetry";

static telemetry_data_t s_data;

void telemetry_init(void)
{
    memset(&s_data, 0, sizeof(s_data));
    ESP_LOGI(TAG, "Телеметрия инициализирована");
}

void telemetry_update(void)
{
    float p1 = analog_input_get_value(AI_CH_P1);
    float p2 = analog_input_get_value(AI_CH_P2);

    /* Перепад давления на фильтре */
    s_data.filter_dp = (!isnan(p1) && !isnan(p2)) ? (p1 - p2) : NAN;

    float q1 = flowmeter_get_flow(FLOW_CH_INLET);
    float q2 = flowmeter_get_flow(FLOW_CH_CONC1);
    float q3 = flowmeter_get_flow(FLOW_CH_PERM2);
    float q4 = flowmeter_get_flow(FLOW_CH_CONC2);

    /* Подача 1-й ступени */
    s_data.stage1_feed_m3h = (!isnan(q1) && !isnan(q2)) ? (q1 + q2) : NAN;

    /* Степень извлечения 2-й ступени */
    if (!isnan(q3) && !isnan(q4) && (q3 + q4) > 0.001f) {
        s_data.stage2_recovery_pct = q3 / (q3 + q4) * 100.0f;
    } else {
        s_data.stage2_recovery_pct = NAN;
    }

    /* Общая степень извлечения */
    if (!isnan(q3) && !isnan(q1) && q1 > 0.001f) {
        s_data.system_recovery_pct = q3 / q1 * 100.0f;
    } else {
        s_data.system_recovery_pct = NAN;
    }

    float s1 = conductivity_get_value(COND_CH_FEED);
    float s2 = conductivity_get_value(COND_CH_PERM1);
    float s3 = conductivity_get_value(COND_CH_PERM2);

    /* Селективность 1-й ступени */
    if (!isnan(s1) && !isnan(s2) && s1 > 0.01f) {
        s_data.stage1_selectivity = (s1 - s2) / s1 * 100.0f;
    } else {
        s_data.stage1_selectivity = NAN;
    }

    /* Селективность 2-й ступени */
    if (!isnan(s2) && !isnan(s3) && s2 > 0.01f) {
        s_data.stage2_selectivity = (s2 - s3) / s2 * 100.0f;
    } else {
        s_data.stage2_selectivity = NAN;
    }

    s_data.valid = !isnan(s_data.filter_dp);
}

const telemetry_data_t *telemetry_get(void)
{
    return &s_data;
}
