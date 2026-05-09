/**
 * @file test_mb_device_check.c
 * @brief Тесты health-check Modbus-устройств (mb_device_check.c).
 *
 * Использует mock_modbus_poller — задаёт «отвечающие» окна holding-регистров
 * через mock_mb_set_holding(), считает write-вызовы и проверяет, что
 * автокоррекция (FC 0x10) действительно отправлена с корректными данными.
 *
 * Sanity-проверки KWS/SL21/УРЖ используют реальные драйверы (linked) —
 * данные подаются через mock_mb_set_kws/cond/flow.
 */
#include "unity.h"

#include "mb_device_check.h"
#include "modbus_poller.h"
#include "alarm_manager.h"
#include "board_config.h"

#include "mock_modbus_poller.h"
#include "mock_esp_timer.h"
#include "mock_hal_nvs.h"

#include "flowmeter.h"
#include "conductivity.h"
#include "power_meter.h"

#include <string.h>

/* Helper: задать корректный отклик Waveshare AI (правильная FW, правильный
 * адрес, modes = 3) через mock-окна. */
static void seed_ai_correct(void)
{
    /* FW version V1.00 = 100 (BCD-стиль) */
    uint16_t ver = 100;
    mock_mb_set_holding(MB_ADDR_WAVESHARE_AI, 0x8000, &ver, 1);

    /* Device addr = 1 */
    uint16_t addr = MB_ADDR_WAVESHARE_AI;
    mock_mb_set_holding(MB_ADDR_WAVESHARE_AI, 0x4000, &addr, 1);

    /* Modes: все 8 = 0x0003 (4-20mA) */
    uint16_t modes[8] = { 3, 3, 3, 3, 3, 3, 3, 3 };
    mock_mb_set_holding(MB_ADDR_WAVESHARE_AI, 0x1000, modes, 8);
}

/* Helper: KWS LP/HP корректные (typical 230 V, 5 A, 1100 W, 12.5 kWh, 45°C) */
static void seed_kws_correct(void)
{
    uint16_t regs[14] = {0};
    regs[0]  = 2300;   /* voltage 230.0 V */
    regs[2]  = 5000;   /* current 5.000 A */
    regs[4]  = 11000;  /* power 1100 W    */
    regs[12] = 1250;   /* energy 12.5 kWh */
    regs[13] = 45;     /* temp 45 °C      */
    mock_mb_set_kws_lp(regs, 14);
    mock_mb_set_kws_hp(regs, 14);
    /* Драйвер обновляет внутренний snapshot из mock-данных */
    power_meter_update();
}

/* Helper: УРЖ корректный (нулевые потоки/объёмы — sanity passes) */
static void seed_urzh_correct(void)
{
    uint16_t flow[8] = {0};
    uint16_t vol[8]  = {0};
    mock_mb_set_flow(flow, 8);
    mock_mb_set_volume(vol, 8);
    flowmeter_update();
}

/* Helper: СЛ21 корректные (0 µS/cm, 25.0 °C — внутри sanity-диапазона) */
static void seed_sl21_correct(void)
{
    uint16_t c10[6] = { 0, 0, 250,  0, 0, 250 };  /* T = 250/10 = 25.0 °C */
    uint16_t c11[6] = { 0, 0, 250,  0, 0, 250 };
    mock_mb_set_cond10(c10, 6);
    mock_mb_set_cond11(c11, 6);
    conductivity_update();
}

void setUp(void)
{
    mock_esp_timer_reset();
    mock_nvs_reset();
    mock_mb_reset();

    /* Драйверы — должны быть инициализированы для health-check'а */
    flowmeter_init();
    conductivity_init();
    power_meter_init();

    alarm_manager_init();
}

void tearDown(void) {}

/* 1. Все устройства в норме → ESP_OK, ни одного активного ALARM_*_RANGE_OOR. */
void test_check_passes_with_correct_state(void)
{
    seed_ai_correct();
    seed_kws_correct();
    seed_urzh_correct();
    seed_sl21_correct();

    esp_err_t err = mb_device_check_run();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Никакая авария RANGE_OOR не должна быть активной */
    alarm_entry_t buf[16];
    int n = alarm_get_active(buf, 16);
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_NOT_EQUAL(ALARM_DEV_CHECK_FAILED,    buf[i].code);
        TEST_ASSERT_NOT_EQUAL(ALARM_AI_BAD_MODE,         buf[i].code);
        TEST_ASSERT_NOT_EQUAL(ALARM_AI_VERSION_MISMATCH, buf[i].code);
        TEST_ASSERT_NOT_EQUAL(ALARM_AI_WRONG_ADDR,       buf[i].code);
        TEST_ASSERT_NOT_EQUAL(ALARM_KWS_RANGE_OOR,       buf[i].code);
        TEST_ASSERT_NOT_EQUAL(ALARM_SL21_RANGE_OOR,      buf[i].code);
        TEST_ASSERT_NOT_EQUAL(ALARM_URZH_RANGE_OOR,      buf[i].code);
    }

    /* Автокоррекция не нужна — write_holding не вызывался */
    TEST_ASSERT_EQUAL(0, mock_mb_get_write_count());
}

/* 2. Один из режимов AI != 3 → автокоррекция: FC 0x10 на 0x1000 с
 *    payload = 8× 0x0003. После ре-чтения снова все 3 → нет alarm. */
void test_check_corrects_wrong_ai_mode(void)
{
    /* FW и device addr — корректные */
    uint16_t ver = 100;
    mock_mb_set_holding(MB_ADDR_WAVESHARE_AI, 0x8000, &ver, 1);
    uint16_t addr = MB_ADDR_WAVESHARE_AI;
    mock_mb_set_holding(MB_ADDR_WAVESHARE_AI, 0x4000, &addr, 1);

    /* Modes: ch3 = 0 (вольтаж 0..5V), остальные 3. */
    uint16_t modes[8] = { 3, 3, 0, 3, 3, 3, 3, 3 };
    mock_mb_set_holding(MB_ADDR_WAVESHARE_AI, 0x1000, modes, 8);

    /* Остальные устройства — корректны (чтобы не повлияли на failures) */
    seed_kws_correct();
    seed_urzh_correct();
    seed_sl21_correct();

    esp_err_t err = mb_device_check_run();
    /* Автокоррекция успешна, остальные devices OK → ESP_OK */
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Был один write_holding на 0x1000 */
    TEST_ASSERT_EQUAL(1, mock_mb_get_write_count());

    uint8_t  ws;
    uint16_t wa;
    uint16_t wd[16];
    size_t   wc;
    mock_mb_get_last_write(&ws, &wa, wd, &wc, 16);
    TEST_ASSERT_EQUAL(MB_ADDR_WAVESHARE_AI, ws);
    TEST_ASSERT_EQUAL(0x1000, wa);
    TEST_ASSERT_EQUAL(8, wc);
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL(0x0003, wd[i]);
    }
}

/* 3. KWS voltage out-of-range → ALARM_KWS_RANGE_OOR активен. */
void test_check_kws_voltage_oor_raises_alarm(void)
{
    seed_ai_correct();
    seed_urzh_correct();
    seed_sl21_correct();

    /* HP-насос: voltage = 300 В (raw 3000) — вне [50..280] */
    uint16_t lp[14] = {0};
    lp[0] = 2300;     /* 230 V — норма */
    lp[2] = 5000;
    lp[13] = 45;
    mock_mb_set_kws_lp(lp, 14);

    uint16_t hp[14] = {0};
    hp[0] = 3000;     /* 300 V — out of range */
    hp[2] = 5000;
    hp[13] = 45;
    mock_mb_set_kws_hp(hp, 14);
    power_meter_update();

    esp_err_t err = mb_device_check_run();
    TEST_ASSERT_EQUAL(ESP_FAIL, err);

    alarm_entry_t buf[16];
    int n = alarm_get_active(buf, 16);
    bool found = false;
    for (int i = 0; i < n; i++) {
        if (buf[i].code == ALARM_KWS_RANGE_OOR) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "ALARM_KWS_RANGE_OOR должен быть активен");

    /* DEV_CHECK_FAILED тоже должен быть поднят (>0 failures) */
    bool fail_alarm = false;
    for (int i = 0; i < n; i++) {
        if (buf[i].code == ALARM_DEV_CHECK_FAILED) { fail_alarm = true; break; }
    }
    TEST_ASSERT_TRUE(fail_alarm);
}

/* 4. AI неответ (нет mock-окон для 0x8000) → check_waveshare_ai возвращает
 *    ESP_FAIL. mb_device_check_run возвращает ESP_FAIL и поднимает
 *    ALARM_DEV_CHECK_FAILED. */
void test_check_ai_unreachable_yields_fail(void)
{
    /* НЕ добавляем mock_mb_set_holding для AI — вернётся ESP_ERR_TIMEOUT */
    seed_kws_correct();
    seed_urzh_correct();
    seed_sl21_correct();

    esp_err_t err = mb_device_check_run();
    TEST_ASSERT_EQUAL(ESP_FAIL, err);

    alarm_entry_t buf[16];
    int n = alarm_get_active(buf, 16);
    bool fail_alarm = false;
    for (int i = 0; i < n; i++) {
        if (buf[i].code == ALARM_DEV_CHECK_FAILED) {
            fail_alarm = true;
            /* value содержит количество fail'ов */
            TEST_ASSERT_GREATER_OR_EQUAL(1.0f, buf[i].value);
            break;
        }
    }
    TEST_ASSERT_TRUE(fail_alarm);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_passes_with_correct_state);
    RUN_TEST(test_check_corrects_wrong_ai_mode);
    RUN_TEST(test_check_kws_voltage_oor_raises_alarm);
    RUN_TEST(test_check_ai_unreachable_yields_fail);
    return UNITY_END();
}
