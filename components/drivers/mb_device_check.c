/**
 * @file mb_device_check.c
 * @brief Реализация старт-ап health-check Modbus-устройств.
 *
 * Подробнее см. mb_device_check.h. Алгоритм:
 *
 *   1. Waveshare AI (slave 1):
 *      - читаем 0x8000 → FW version (BCD V*100 + v); версия < 1.00 → alarm.
 *      - читаем 0x4000 → device address; != MB_ADDR_WAVESHARE_AI → alarm.
 *      - читаем 0x1000..0x1007 → 8 channel modes; если хоть один != 3 (4–20mA)
 *        → пишем 0x0003 ×8 через FC 0x10 и перепроверяем. На неудачу → alarm.
 *
 *   2. УРЖ2КМ (slave 2): берём snapshot из flowmeter, проверяем что расход
 *      finite и |F| < 10000 м³/ч (физически невозможно для этой установки).
 *
 *   3. СЛ21 #10 / #11: snapshot из conductivity, проверка conductivity
 *      0..10000 µS/cm и t° -20..+80 °C.
 *
 *   4. KWS LP / HP: snapshot из power_meter, проверка voltage 50..280 В
 *      и t° < 100 °C. Voltage > 280 при работе — звонок ремонтнику.
 *
 * При offline / no-data проверка пропускается с warn-логом — это не
 * считается провалом health-check'а (offline уже сигналит ALARM_KWS_OFFLINE
 * и т.п. отдельно). Реальный fail только при заведомо-некорректных данных.
 */
#include "mb_device_check.h"
#include "modbus_poller.h"
#include "alarm_manager.h"
#include "board_config.h"
#include "power_meter.h"
#include "conductivity.h"
#include "flowmeter.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <stdbool.h>

static const char *TAG = "mb_check";

/* --- Ожидаемые значения Waveshare AI --- */
#define AI_EXPECTED_DEV_ADDR  MB_ADDR_WAVESHARE_AI   /* slave 1 */
#define AI_EXPECTED_MODE      0x0003                  /* 4–20 mA */
#define AI_VERSION_MIN        0x0064                  /* V1.00 = 100 в BCD */
#define AI_RECHECK_DELAY_MS   200

/* --- Sanity-диапазоны (диагностика, не interlock) --- */
#define SL21_COND_MIN_US      0.0f
#define SL21_COND_MAX_US      10000.0f
#define SL21_TEMP_MIN_C       -20.0f
#define SL21_TEMP_MAX_C       80.0f

#define URZH_FLOW_MAX_M3H     10000.0f

#define KWS_VOLTAGE_MIN_V     50.0f
#define KWS_VOLTAGE_MAX_V     280.0f
#define KWS_TEMP_MAX_C        100.0f

/* --- 1. Waveshare AI --- */
static esp_err_t check_waveshare_ai(void)
{
    /* 1.1 FW version (regs 0x8000) */
    uint16_t version = 0;
    esp_err_t err = modbus_poller_read_holding(MB_ADDR_WAVESHARE_AI,
                                               0x8000, &version, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Waveshare AI: версия FW недоступна (%s) — пропуск",
                 esp_err_to_name(err));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Waveshare AI: FW version = V%d.%02d (raw 0x%04X)",
             (int)(version / 100), (int)(version % 100), (unsigned)version);
    if (version < AI_VERSION_MIN) {
        ESP_LOGW(TAG, "Waveshare AI: подозрительно низкая FW version 0x%04X",
                 (unsigned)version);
        alarm_raise(ALARM_AI_VERSION_MISMATCH, ALARM_CAT_ALARM, (float)version);
    }

    /* 1.2 Device address (reg 0x4000) — sanity */
    uint16_t dev_addr = 0;
    err = modbus_poller_read_holding(MB_ADDR_WAVESHARE_AI, 0x4000,
                                     &dev_addr, 1);
    if (err == ESP_OK && dev_addr != AI_EXPECTED_DEV_ADDR) {
        ESP_LOGW(TAG, "Waveshare AI: device addr %u, ожидался %u",
                 (unsigned)dev_addr, (unsigned)AI_EXPECTED_DEV_ADDR);
        alarm_raise(ALARM_AI_WRONG_ADDR, ALARM_CAT_ALARM, (float)dev_addr);
    }

    /* 1.3 Channel modes (regs 0x1000..0x1007). 3 = 4–20mA. */
    uint16_t modes[CID_AI_MODES_COUNT] = {0};
    err = modbus_poller_read_holding(MB_ADDR_WAVESHARE_AI, 0x1000,
                                     modes, CID_AI_MODES_COUNT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Waveshare AI: чтение режимов 0x1000 неудача: %s",
                 esp_err_to_name(err));
        return err;
    }

    bool need_setup = false;
    for (int ch = 0; ch < CID_AI_MODES_COUNT; ch++) {
        if (modes[ch] != AI_EXPECTED_MODE) {
            ESP_LOGW(TAG, "Waveshare AI ch%d: mode=0x%04X (ожидался 0x%04X)",
                     ch + 1, (unsigned)modes[ch], (unsigned)AI_EXPECTED_MODE);
            need_setup = true;
        }
    }

    if (!need_setup) {
        ESP_LOGI(TAG, "Waveshare AI: все 8 каналов уже в режиме 4-20mA");
        return ESP_OK;
    }

    /* Автокоррекция: пишем 0x0003 во все 8 каналов через FC 0x10 (one-time
     * setup, см. doc/modbus_signal_map.md §4.4). Не interlock — это плановая
     * операция первого запуска. */
    ESP_LOGI(TAG, "Waveshare AI: автокоррекция режимов → 4-20mA для всех 8 каналов");
    uint16_t target[CID_AI_MODES_COUNT];
    for (int i = 0; i < CID_AI_MODES_COUNT; i++) target[i] = AI_EXPECTED_MODE;

    err = modbus_poller_write_holding(MB_ADDR_WAVESHARE_AI, 0x1000,
                                      target, CID_AI_MODES_COUNT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Waveshare AI: запись режимов 4-20mA не удалась: %s",
                 esp_err_to_name(err));
        alarm_raise(ALARM_AI_BAD_MODE, ALARM_CAT_ALARM, 0.0f);
        return err;
    }

    /* Ре-проверка после небольшой паузы — устройству нужно
     * закоммитить настройки во внутренний EEPROM. */
    vTaskDelay(pdMS_TO_TICKS(AI_RECHECK_DELAY_MS));
    err = modbus_poller_read_holding(MB_ADDR_WAVESHARE_AI, 0x1000,
                                     modes, CID_AI_MODES_COUNT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Waveshare AI: повторное чтение режимов неудача: %s",
                 esp_err_to_name(err));
        return err;
    }
    bool ok = true;
    for (int ch = 0; ch < CID_AI_MODES_COUNT; ch++) {
        if (modes[ch] != AI_EXPECTED_MODE) {
            ESP_LOGE(TAG, "Waveshare AI ch%d: повторная проверка mode=0x%04X",
                     ch + 1, (unsigned)modes[ch]);
            alarm_raise(ALARM_AI_BAD_MODE, ALARM_CAT_ALARM, (float)(ch + 1));
            ok = false;
        }
    }
    if (ok) {
        ESP_LOGI(TAG, "Waveshare AI: все 8 каналов переведены в 4-20mA");
    }
    return ok ? ESP_OK : ESP_FAIL;
}

/* --- 2. УРЖ2КМ --- */
static esp_err_t check_urzh(void)
{
    if (!modbus_poller_is_device_online(MB_ADDR_URZH2KM)) {
        ESP_LOGW(TAG, "УРЖ2КМ (slave %d): offline на момент health-check",
                 MB_ADDR_URZH2KM);
        return ESP_FAIL;
    }
    flowmeter_data_t fm;
    flowmeter_get_data(&fm);
    bool ok = true;
    for (int i = 0; i < FLOW_CHANNEL_COUNT; i++) {
        if (!fm.channel_ok[i]) continue;  /* нет данных — не считаем фейлом */
        float f = fm.flow_m3h[i];
        if (!isfinite(f) || fabsf(f) > URZH_FLOW_MAX_M3H) {
            ESP_LOGW(TAG, "УРЖ2КМ ch%d: подозрительный расход %.2f м³/ч",
                     i, (double)f);
            alarm_raise(ALARM_URZH_RANGE_OOR, ALARM_CAT_ALARM, f);
            ok = false;
        }
    }
    if (ok) ESP_LOGI(TAG, "УРЖ2КМ (slave %d): sanity OK", MB_ADDR_URZH2KM);
    return ok ? ESP_OK : ESP_FAIL;
}

/* --- 3. СЛ21 (две штуки на slaves 10/11) --- */
static esp_err_t check_sl21(uint8_t slave_addr, const char *label)
{
    if (!modbus_poller_is_device_online(slave_addr)) {
        ESP_LOGW(TAG, "%s (slave %d): offline на момент health-check",
                 label, slave_addr);
        return ESP_FAIL;
    }

    conductivity_data_t cd;
    conductivity_get_data(&cd);

    /* Mapping slave → пара логических каналов (см. conductivity.h):
     *   slave 10 → COND_CH_FEED, COND_CH_PERM1
     *   slave 11 → COND_CH_PERM2, COND_CH_CONC
     */
    int ch_a, ch_b;
    if (slave_addr == MB_ADDR_SL21_201) {
        ch_a = COND_CH_FEED;  ch_b = COND_CH_PERM1;
    } else if (slave_addr == MB_ADDR_SL21_101) {
        ch_a = COND_CH_PERM2; ch_b = COND_CH_CONC;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    bool ok = true;
    int channels[2] = { ch_a, ch_b };
    for (int i = 0; i < 2; i++) {
        int ch = channels[i];
        if (!cd.channel_ok[ch]) continue;
        float c = cd.conductivity_uS[ch];
        float t = cd.temperature_C[ch];
        if (!isfinite(c) || c < SL21_COND_MIN_US || c > SL21_COND_MAX_US) {
            ESP_LOGW(TAG, "%s ch%d: conductivity=%.2f мкСм вне [%.0f..%.0f]",
                     label, ch, (double)c,
                     (double)SL21_COND_MIN_US, (double)SL21_COND_MAX_US);
            alarm_raise(ALARM_SL21_RANGE_OOR, ALARM_CAT_ALARM, c);
            ok = false;
        }
        if (!isfinite(t) || t < SL21_TEMP_MIN_C || t > SL21_TEMP_MAX_C) {
            ESP_LOGW(TAG, "%s ch%d: t=%.1f°C вне [%.0f..%.0f]",
                     label, ch, (double)t,
                     (double)SL21_TEMP_MIN_C, (double)SL21_TEMP_MAX_C);
            alarm_raise(ALARM_SL21_RANGE_OOR, ALARM_CAT_ALARM, t);
            ok = false;
        }
    }
    if (ok) ESP_LOGI(TAG, "%s (slave %d): sanity OK", label, slave_addr);
    return ok ? ESP_OK : ESP_FAIL;
}

/* --- 4. KWS-306L --- */
static esp_err_t check_kws(pump_id_t pump, const char *label, uint8_t slave_addr)
{
    if (!power_meter_is_online(pump)) {
        ESP_LOGW(TAG, "%s (slave %d): offline на момент health-check",
                 label, slave_addr);
        return ESP_FAIL;
    }
    power_meter_data_t pm;
    power_meter_get_data(pump, &pm);
    if (!pm.valid) {
        ESP_LOGW(TAG, "%s (slave %d): данные ещё не валидны (нет первого опроса?)",
                 label, slave_addr);
        return ESP_FAIL;
    }
    bool ok = true;
    if (!isfinite(pm.voltage_V) ||
        pm.voltage_V < KWS_VOLTAGE_MIN_V || pm.voltage_V > KWS_VOLTAGE_MAX_V) {
        ESP_LOGW(TAG, "%s: voltage=%.1f В вне [%.0f..%.0f]",
                 label, (double)pm.voltage_V,
                 (double)KWS_VOLTAGE_MIN_V, (double)KWS_VOLTAGE_MAX_V);
        alarm_raise(ALARM_KWS_RANGE_OOR, ALARM_CAT_ALARM, pm.voltage_V);
        ok = false;
    }
    if (isfinite(pm.temperature_C) && pm.temperature_C > KWS_TEMP_MAX_C) {
        ESP_LOGW(TAG, "%s: t=%.1f°C > %.0f", label,
                 (double)pm.temperature_C, (double)KWS_TEMP_MAX_C);
        alarm_raise(ALARM_KWS_RANGE_OOR, ALARM_CAT_ALARM, pm.temperature_C);
        ok = false;
    }
    if (ok) {
        ESP_LOGI(TAG, "%s (slave %d): sanity OK (V=%.1f I=%.3f W=%.1f T=%.1f)",
                 label, slave_addr,
                 (double)pm.voltage_V, (double)pm.current_A,
                 (double)pm.power_W, (double)pm.temperature_C);
    }
    return ok ? ESP_OK : ESP_FAIL;
}

/* --- Top-level --- */
esp_err_t mb_device_check_run(void)
{
    ESP_LOGI(TAG, "=== Modbus device health check ===");
    int failures = 0;

    if (check_waveshare_ai()                                    != ESP_OK) failures++;
    if (check_sl21(MB_ADDR_SL21_201, "СЛ21 #10")                != ESP_OK) failures++;
    if (check_sl21(MB_ADDR_SL21_101, "СЛ21 #11")                != ESP_OK) failures++;
    if (check_urzh()                                            != ESP_OK) failures++;
    if (check_kws(PUMP_LP, "KWS НД", MB_ADDR_KWS_PUMP_LP)       != ESP_OK) failures++;
    if (check_kws(PUMP_HP, "KWS ВД", MB_ADDR_KWS_PUMP_HP)       != ESP_OK) failures++;

    if (failures == 0) {
        ESP_LOGI(TAG, "=== Все 6 устройств прошли health check ===");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "=== Health check: %d из 6 устройств с проблемами ===",
                 failures);
        alarm_raise(ALARM_DEV_CHECK_FAILED, ALARM_CAT_ALARM, (float)failures);
        return ESP_FAIL;
    }
}
