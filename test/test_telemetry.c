/**
 * @file test_telemetry.c
 * @brief Тесты расчётных параметров телеметрии
 *
 * Тестирует: filter_dp, recovery, selectivity, NaN propagation.
 */
#include "unity.h"
#include "telemetry.h"
#include "analog_input.h"
#include "flowmeter.h"
#include "conductivity.h"

#include "mock_analog_input.h"
#include "mock_flowmeter.h"
#include "mock_conductivity.h"

#include <math.h>

void setUp(void)
{
    mock_analog_reset();
    mock_flowmeter_reset();
    mock_conductivity_reset();
    telemetry_init();
}

void tearDown(void) {}

/* 1. filter_dp = P1 - P2 */
void test_telemetry_filter_dp(void)
{
    mock_analog_set(AI_CH_P1, 4.0f);
    mock_analog_set(AI_CH_P2, 3.5f);

    telemetry_update();
    const telemetry_data_t *d = telemetry_get();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, d->filter_dp);
    TEST_ASSERT_TRUE(d->valid);
}

/* 2. recovery: system = Q3/Q1*100, stage2 = Q3/(Q3+Q4)*100 */
void test_telemetry_recovery(void)
{
    mock_analog_set(AI_CH_P1, 3.0f);
    mock_analog_set(AI_CH_P2, 2.8f);

    mock_flowmeter_set(FLOW_CH_INLET, 1.0f);   /* Q1 */
    mock_flowmeter_set(FLOW_CH_CONC1, 0.2f);   /* Q2 */
    mock_flowmeter_set(FLOW_CH_PERM2, 0.3f);   /* Q3 */
    mock_flowmeter_set(FLOW_CH_CONC2, 0.5f);   /* Q4 */

    telemetry_update();
    const telemetry_data_t *d = telemetry_get();

    /* stage1_feed = Q1 + Q2 = 1.2 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.2f, d->stage1_feed_m3h);

    /* stage2_recovery = 0.3 / (0.3+0.5) * 100 = 37.5% */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 37.5f, d->stage2_recovery_pct);

    /* system_recovery = 0.3 / 1.0 * 100 = 30.0% */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 30.0f, d->system_recovery_pct);
}

/* 3. selectivity: (σ1-σ2)/σ1*100 */
void test_telemetry_selectivity(void)
{
    mock_analog_set(AI_CH_P1, 3.0f);
    mock_analog_set(AI_CH_P2, 2.8f);

    mock_conductivity_set(COND_CH_FEED,  500.0f);  /* σ1 */
    mock_conductivity_set(COND_CH_PERM1, 10.0f);   /* σ2 */
    mock_conductivity_set(COND_CH_PERM2, 0.5f);    /* σ3 */

    telemetry_update();
    const telemetry_data_t *d = telemetry_get();

    /* stage1 = (500-10)/500*100 = 98.0% */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 98.0f, d->stage1_selectivity);

    /* stage2 = (10-0.5)/10*100 = 95.0% */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 95.0f, d->stage2_selectivity);
}

/* 4. NaN propagation: NAN → NAN */
void test_telemetry_nan_propagation(void)
{
    /* Все входы = NAN (по умолчанию из reset) */
    telemetry_update();
    const telemetry_data_t *d = telemetry_get();

    TEST_ASSERT_TRUE(isnan(d->filter_dp));
    TEST_ASSERT_TRUE(isnan(d->stage1_feed_m3h));
    TEST_ASSERT_TRUE(isnan(d->stage2_recovery_pct));
    TEST_ASSERT_TRUE(isnan(d->system_recovery_pct));
    TEST_ASSERT_TRUE(isnan(d->stage1_selectivity));
    TEST_ASSERT_TRUE(isnan(d->stage2_selectivity));
    TEST_ASSERT_FALSE(d->valid);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_telemetry_filter_dp);
    RUN_TEST(test_telemetry_recovery);
    RUN_TEST(test_telemetry_selectivity);
    RUN_TEST(test_telemetry_nan_propagation);
    return UNITY_END();
}
