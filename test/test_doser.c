/**
 * @file test_doser.c
 * @brief Тесты таймера дозатора антискаланта
 *
 * Тестирует doser_update(): переходы OFF → RUNNING → PAUSE → RUNNING,
 * управление DO5, enable/disable.
 *
 * TODO: тесты для дозатора в WASHING — см. README.md.
 */
#include "unity.h"
#include "doser.h"
#include "board_config.h"

#include "mock_esp_timer.h"
#include "mock_hal_gpio.h"
#include "mock_config_manager.h"

void setUp(void)
{
    mock_esp_timer_reset();
    mock_hal_gpio_reset();
    mock_config_set_defaults();
    doser_init();
}

void tearDown(void) {}

/* 1. DOSER_OFF → DOSER_RUNNING при auto_running=true */
void test_doser_starts_on_auto_running(void)
{
    TEST_ASSERT_EQUAL(DOSER_OFF, doser_get_state());

    doser_update(true);

    TEST_ASSERT_EQUAL(DOSER_RUNNING, doser_get_state());
    TEST_ASSERT_TRUE(mock_hal_gpio_get_do_pin(BOARD_DO_DOSER));
}

/* 2. Полный цикл: RUNNING(5мин) → PAUSE(55мин) → RUNNING */
void test_doser_full_cycle(void)
{
    doser_update(true);
    TEST_ASSERT_EQUAL(DOSER_RUNNING, doser_get_state());

    /* Через 5 минут → PAUSE */
    int64_t run_us = 5LL * 60 * 1000000;
    mock_esp_timer_set(run_us);
    doser_update(true);
    TEST_ASSERT_EQUAL(DOSER_PAUSE, doser_get_state());
    TEST_ASSERT_FALSE(mock_hal_gpio_get_do_pin(BOARD_DO_DOSER));

    /* Через 55 минут паузы → RUNNING */
    int64_t pause_us = 55LL * 60 * 1000000;
    mock_esp_timer_set(run_us + pause_us);
    doser_update(true);
    TEST_ASSERT_EQUAL(DOSER_RUNNING, doser_get_state());
    TEST_ASSERT_TRUE(mock_hal_gpio_get_do_pin(BOARD_DO_DOSER));
}

/* 3. STOP при auto_running=false → DOSER_OFF */
void test_doser_stops_when_not_auto(void)
{
    doser_update(true);
    TEST_ASSERT_EQUAL(DOSER_RUNNING, doser_get_state());

    doser_update(false);
    TEST_ASSERT_EQUAL(DOSER_OFF, doser_get_state());
    TEST_ASSERT_FALSE(mock_hal_gpio_get_do_pin(BOARD_DO_DOSER));
}

/* 4. Disabled → не запускается */
void test_doser_disabled(void)
{
    doser_enable(false);
    doser_update(true);
    TEST_ASSERT_EQUAL(DOSER_OFF, doser_get_state());
    TEST_ASSERT_FALSE(mock_hal_gpio_get_do_pin(BOARD_DO_DOSER));
}

/* 5. Изменение конфигурации влияет на длительность */
void test_doser_config_change(void)
{
    plant_config_t *cfg = mock_config_get_mutable();
    cfg->doser.run_time_min = 1;
    cfg->doser.cycle_time_min = 2;

    doser_update(true);
    TEST_ASSERT_EQUAL(DOSER_RUNNING, doser_get_state());

    /* Через 1 минуту → PAUSE */
    mock_esp_timer_set(1LL * 60 * 1000000);
    doser_update(true);
    TEST_ASSERT_EQUAL(DOSER_PAUSE, doser_get_state());

    /* Через ещё 1 минуту → RUNNING */
    mock_esp_timer_set(2LL * 60 * 1000000);
    doser_update(true);
    TEST_ASSERT_EQUAL(DOSER_RUNNING, doser_get_state());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_doser_starts_on_auto_running);
    RUN_TEST(test_doser_full_cycle);
    RUN_TEST(test_doser_stops_when_not_auto);
    RUN_TEST(test_doser_disabled);
    RUN_TEST(test_doser_config_change);
    return UNITY_END();
}
