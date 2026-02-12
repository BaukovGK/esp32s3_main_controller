/**
 * @file test_alarm_manager.c
 * @brief Тесты менеджера аварий
 *
 * Тестирует: raise/clear, дедупликацию, INFO без дедупликации,
 * кольцевой буфер истории, callback-уведомления.
 */
#include "unity.h"
#include "alarm_manager.h"

#include "mock_esp_timer.h"
#include "mock_hal_nvs.h"

#include <string.h>

/* Переменные для callback-теста */
static int s_cb_count = 0;
static alarm_entry_t s_cb_last_entry;

static void test_alarm_callback(const alarm_entry_t *entry)
{
    s_cb_count++;
    s_cb_last_entry = *entry;
}

void setUp(void)
{
    mock_esp_timer_reset();
    mock_nvs_reset();
    s_cb_count = 0;
    memset(&s_cb_last_entry, 0, sizeof(s_cb_last_entry));

    /* Инициализация: сбрасывает все static, поднимает ALARM_SYSTEM_START */
    alarm_manager_init();
}

void tearDown(void) {}

/* 1. alarm_raise → запись в active + history */
void test_alarm_raise_and_active(void)
{
    alarm_raise(ALARM_P1_HIGH, ALARM_CAT_ALARM, 6.5f);

    alarm_entry_t buf[10];
    int cnt = alarm_get_active(buf, 10);
    TEST_ASSERT_EQUAL(1, cnt);
    TEST_ASSERT_EQUAL(ALARM_P1_HIGH, buf[0].code);
    TEST_ASSERT_EQUAL(ALARM_CAT_ALARM, buf[0].category);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.5f, buf[0].value);
    TEST_ASSERT_TRUE(buf[0].active);
}

/* 2. Дедупликация: повторный raise того же кода игнорируется */
void test_alarm_dedup(void)
{
    alarm_raise(ALARM_P1_HIGH, ALARM_CAT_ALARM, 6.0f);
    alarm_raise(ALARM_P1_HIGH, ALARM_CAT_ALARM, 7.0f); /* Дубликат */

    alarm_entry_t buf[10];
    int cnt = alarm_get_active(buf, 10);
    TEST_ASSERT_EQUAL(1, cnt);  /* Только 1, не 2 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f, buf[0].value);  /* Первое значение */
}

/* 3. INFO не дедуплицируется и не попадает в active */
void test_alarm_info_no_dedup_no_active(void)
{
    alarm_raise(ALARM_STATE_CHANGE, ALARM_CAT_INFO, 1.0f);
    alarm_raise(ALARM_STATE_CHANGE, ALARM_CAT_INFO, 2.0f);

    /* В active не попадают INFO */
    alarm_entry_t buf[10];
    int active_cnt = alarm_get_active(buf, 10);
    TEST_ASSERT_EQUAL(0, active_cnt);

    /* В history попадают обе + SYSTEM_START от init */
    int hist_cnt = alarm_get_history(buf, 10);
    TEST_ASSERT_GREATER_OR_EQUAL(3, hist_cnt);  /* SYSTEM_START + 2 INFO */
}

/* 4. alarm_clear → удаление из active, запись clear в history */
void test_alarm_clear(void)
{
    alarm_raise(ALARM_P3_HIGH, ALARM_CAT_ALARM, 40.0f);

    alarm_entry_t buf[10];
    int cnt = alarm_get_active(buf, 10);
    TEST_ASSERT_EQUAL(1, cnt);

    /* Снять аварию */
    alarm_clear(ALARM_P3_HIGH);

    cnt = alarm_get_active(buf, 10);
    TEST_ASSERT_EQUAL(0, cnt);

    /* В history есть и raise и clear */
    cnt = alarm_get_history(buf, 10);
    /* Последние 2: raise(active=true) + clear(active=false) */
    bool found_raise = false, found_clear = false;
    for (int i = 0; i < cnt; i++) {
        if (buf[i].code == ALARM_P3_HIGH) {
            if (buf[i].active) found_raise = true;
            else found_clear = true;
        }
    }
    TEST_ASSERT_TRUE(found_raise);
    TEST_ASSERT_TRUE(found_clear);
}

/* 5. Callback вызывается при raise и clear */
void test_alarm_callback_invoked(void)
{
    alarm_manager_register_notify(test_alarm_callback);

    alarm_raise(ALARM_T_HIGH, ALARM_CAT_CRITICAL, 50.0f);
    TEST_ASSERT_EQUAL(1, s_cb_count);
    TEST_ASSERT_EQUAL(ALARM_T_HIGH, s_cb_last_entry.code);
    TEST_ASSERT_TRUE(s_cb_last_entry.active);

    alarm_clear(ALARM_T_HIGH);
    TEST_ASSERT_EQUAL(2, s_cb_count);
    TEST_ASSERT_EQUAL(ALARM_T_HIGH, s_cb_last_entry.code);
    TEST_ASSERT_FALSE(s_cb_last_entry.active);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_alarm_raise_and_active);
    RUN_TEST(test_alarm_dedup);
    RUN_TEST(test_alarm_info_no_dedup_no_active);
    RUN_TEST(test_alarm_clear);
    RUN_TEST(test_alarm_callback_invoked);
    return UNITY_END();
}
