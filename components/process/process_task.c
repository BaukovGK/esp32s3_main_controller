/**
 * @file process_task.c
 * @brief Главная задача процесса — 100мс цикл
 *
 * Phase-1 (отказоустойчивость):
 *  - Раз в секунду: hal_gpio_verify_do() — readback TCA9554 (K-2)
 *  - Раз в 3 секунды: проверка online-статуса Modbus устройств,
 *    raise/clear ALARM_MODBUS_OFFLINE (K-8)
 */
#include "process_task.h"
#include "analog_input.h"
#include "flowmeter.h"
#include "conductivity.h"
#include "power_meter.h"
#include "state_machine.h"
#include "doser.h"
#include "telemetry.h"
#include "watchdog_task.h"
#include "hal_gpio.h"
#include "hal_buzzer.h"
#include "hal_rgb.h"
#include "modbus_poller.h"
#include "alarm_manager.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "process";

/* Период основного цикла, мс */
#define PROCESS_CYCLE_MS    100

/* Период логирования: 5с / 100мс = 50 циклов */
#define LOG_PERIOD_CYCLES   50

/* Phase-1: периоды вспомогательных проверок (в циклах по 100мс) */
#define VERIFY_DO_PERIOD     10   /* 1 секунда */
#define MB_CHECK_PERIOD      30   /* 3 секунды */

/* Phase-3 (M-3): мониторинг heap */
#define HEAP_CHECK_PERIOD    50   /* 5 секунд */
#define HEAP_LOW_THRESHOLD   16384  /* 16 КБ — поднимаем аларм; ниже 8 КБ обычно уже SOS */
#define HEAP_OK_HYSTERESIS   (HEAP_LOW_THRESHOLD + 4096)  /* снимаем при > 20 КБ */

/* Phase-3 (L-4): период обновления buzzer'а */
#define BUZZER_TICK_PERIOD   1    /* каждый цикл (100мс) */

static const uint8_t s_mb_addrs[] = {
    MB_ADDR_WAVESHARE_AI, MB_ADDR_URZH2KM, MB_ADDR_SL21_201, MB_ADDR_SL21_101
};
#define MB_ADDR_COUNT (sizeof(s_mb_addrs) / sizeof(s_mb_addrs[0]))

static bool s_mb_online_prev[MB_ADDR_COUNT] = { true, true, true, true };

/* Phase-3 (L-4): по активным авариям возвращаем самую опасную категорию.
 * Phase-4 (RGB): возвращаем сразу обе характеристики через struct, чтобы
 * не дёргать alarm_get_active() дважды. */
typedef struct {
    bool has_critical;
    bool has_alarm;
    bool has_warning;
} active_alarm_summary_t;

static active_alarm_summary_t scan_active_alarms(void)
{
    active_alarm_summary_t r = { false, false, false };
    alarm_entry_t active[16];
    int n = alarm_get_active(active, 16);
    for (int i = 0; i < n; i++) {
        switch (active[i].category) {
        case ALARM_CAT_CRITICAL: r.has_critical = true; break;
        case ALARM_CAT_ALARM:    r.has_alarm = true; break;
        case ALARM_CAT_WARNING:  r.has_warning = true; break;
        default: break;
        }
    }
    return r;
}

static buzzer_pattern_t buzzer_pattern_for(const active_alarm_summary_t *a)
{
    if (a->has_critical) return BUZZER_PATTERN_CONTINUOUS;
    if (a->has_alarm)    return BUZZER_PATTERN_SLOW;
    if (a->has_warning)  return BUZZER_PATTERN_SHORT;
    return BUZZER_PATTERN_OFF;
}

/* Phase-4 (RGB): цвет LED по приоритету: alarm > SM-state.
 * blink включается для CRITICAL и для WAIT_* фаз. */
static void rgb_set_for_state(const active_alarm_summary_t *a, sm_status_t st)
{
    /* Алармы перекрывают состояние */
    if (a->has_critical) { hal_rgb_set(255,   0,   0, true);  return; }  /* мигающий красный */

    switch (st.state) {
    case SM_FAULT:   hal_rgb_set(255,   0,   0, false); return;          /* красный */
    case SM_MANUAL:  hal_rgb_set(128,   0, 128, false); return;          /* фиолетовый */
    case SM_WASHING:
        /* Подфазы ожидания оператора → мигающий оранжевый */
        if (st.wash_sub == WASH_WAIT_HEAT ||
            st.wash_sub == WASH_WAIT_SUPPLY ||
            st.wash_sub == WASH_WAIT_DRAIN) {
            hal_rgb_set(255, 128,   0, true);  return;
        }
        if (a->has_alarm)   { hal_rgb_set(255, 200,   0, false); return; }
        hal_rgb_set(  0,   0, 255, false); return;                       /* синий */
    case SM_AUTO:
        if (a->has_alarm)   { hal_rgb_set(255, 200,   0, false); return; }
        if (st.auto_sub == AUTO_RUNNING) {
            hal_rgb_set(0, 200, 0, false); return;                       /* зелёный */
        }
        hal_rgb_set(  0, 200, 200, false); return;                       /* голубой — переходные */
    case SM_IDLE:
    default:
        if (a->has_alarm)   { hal_rgb_set(255, 200,   0, false); return; }
        if (a->has_warning) { hal_rgb_set( 64,  48,   0, false); return; }
        hal_rgb_set( 16,  16,  16, false); return;                       /* тусклый белый */
    }
}

void process_task(void *arg)
{
    /* Watchdog handle (передан через xTaskCreate arg, либо регистрируемся сами).
     * Encoding `+1` (см. WDT_ARG_TO_HANDLE) — иначе handle == 0 потерялся бы
     * на проверке `arg > 0`. NULL → wdt_h = -1, fallback на legacy watchdog_feed(). */
    int wdt_h = WDT_ARG_TO_HANDLE(arg);
    ESP_LOGI(TAG, "ProcessTask запущена (100мс, wdt=%d)", wdt_h);

    int log_counter = 0;
    int verify_counter = 0;
    int mb_check_counter = 0;
    int heap_check_counter = 0;
    bool heap_low_active = false;

    while (1) {
        /* 1. Обновление драйверов */
        analog_input_update();
        flowmeter_update();
        conductivity_update();
        power_meter_update();  /* Phase-5: KWS-306L (НД/ВД) */

        /* 2. Обновление конечного автомата (вызывает interlocks_check внутри) */
        state_machine_update();

        /* 3. Обновление дозатора.
         * TODO: дозировать промывочный реагент в WASHING — см. README.md.
         * Сейчас дозатор работает только в AUTO_RUNNING. */
        sm_status_t st = state_machine_get_status();
        bool auto_running = (st.state == SM_AUTO && st.auto_sub == AUTO_RUNNING);
        doser_update(auto_running);

        /* 4. Обновление телеметрии */
        telemetry_update();

        /* 5. Сброс watchdog (Phase-1: handle-based, fallback на legacy) */
        if (wdt_h >= 0) {
            watchdog_feed_h(wdt_h);
        } else {
            watchdog_feed();
        }

        /* 6. Phase-1 (K-2): readback TCA9554 раз в секунду */
        if (++verify_counter >= VERIFY_DO_PERIOD) {
            verify_counter = 0;
            esp_err_t v = hal_gpio_verify_do();
            if (v == ESP_ERR_INVALID_STATE) {
                /* Реальное состояние реле != ожидаемое (TCA9554 завис?) */
                alarm_raise(ALARM_DO_READBACK_FAIL, ALARM_CAT_CRITICAL, 0);
            } else if (v != ESP_OK) {
                /* Ошибка чтения I2C — шина повисла или TCA9554 не отвечает */
                alarm_raise(ALARM_I2C_BUS_HUNG, ALARM_CAT_CRITICAL, 0);
            }
        }

        /* 7. Phase-1 (K-8): проверка Modbus online раз в 3 секунды */
        if (++mb_check_counter >= MB_CHECK_PERIOD) {
            mb_check_counter = 0;
            for (size_t i = 0; i < MB_ADDR_COUNT; i++) {
                bool online = modbus_poller_is_device_online(s_mb_addrs[i]);
                if (!online && s_mb_online_prev[i]) {
                    alarm_raise(ALARM_MODBUS_OFFLINE, ALARM_CAT_ALARM,
                                (float)s_mb_addrs[i]);
                } else if (online && !s_mb_online_prev[i]) {
                    alarm_clear(ALARM_MODBUS_OFFLINE);
                }
                s_mb_online_prev[i] = online;
            }

            /* Phase-5: отдельная защита потери связи с KWS-306L.
             * Любой из двух offline → потеря защит насосов (NO_CURRENT/OVERTEMP/V_OOR
             * не сработают, т.к. геттеры дают NaN при offline).
             * value = адрес проблемного slave (20 / 21); если оба — раздельные алармы
             * не дедуплицируются (общий код), value сохранит первый. */
            bool lp_on = power_meter_is_online(PUMP_LP);
            bool hp_on = power_meter_is_online(PUMP_HP);
            if (!lp_on || !hp_on) {
                float v = !lp_on ? (float)MB_ADDR_KWS_PUMP_LP
                                 : (float)MB_ADDR_KWS_PUMP_HP;
                alarm_raise(ALARM_KWS_OFFLINE, ALARM_CAT_WARNING, v);
            } else {
                alarm_clear(ALARM_KWS_OFFLINE);
            }
        }

        /* 7.4. Phase-3 (L-4) / Phase-4 (RGB): tick индикаторов.
         * Паттерн пересчитываем раз в 5 секунд (вместе с heap-check),
         * tick'и (моргание) — каждый цикл. */
        hal_buzzer_tick();
        hal_rgb_tick();

        /* 7.5. Phase-3 (M-3): мониторинг heap раз в 5 секунд.
         * При уходе ниже HEAP_LOW_THRESHOLD поднимаем аларм; снимаем
         * только когда уровень восстановится с гистерезисом, чтобы
         * не флапать у границы. */
        if (++heap_check_counter >= HEAP_CHECK_PERIOD) {
            heap_check_counter = 0;
            uint32_t free_heap = esp_get_free_heap_size();
            if (!heap_low_active && free_heap < HEAP_LOW_THRESHOLD) {
                alarm_raise(ALARM_LOW_HEAP, ALARM_CAT_WARNING, (float)free_heap);
                heap_low_active = true;
            } else if (heap_low_active && free_heap > HEAP_OK_HYSTERESIS) {
                alarm_clear(ALARM_LOW_HEAP);
                heap_low_active = false;
            }

            /* Phase-3 (L-4) / Phase-4 (RGB): пересчёт паттернов индикации
             * раз в 5 сек — общий проход по active alarms и одно решение
             * для buzzer + RGB. */
            active_alarm_summary_t alarms = scan_active_alarms();
            hal_buzzer_set_pattern(buzzer_pattern_for(&alarms));
            rgb_set_for_state(&alarms, st);
        }

        /* 8. Периодический лог */
        if (++log_counter >= LOG_PERIOD_CYCLES) {
            log_counter = 0;

            float p1 = analog_input_get_value(AI_CH_P1);
            float p2 = analog_input_get_value(AI_CH_P2);
            float p3 = analog_input_get_value(AI_CH_P3);
            float p4 = analog_input_get_value(AI_CH_P4);
            float t  = analog_input_get_value(AI_CH_T);

            ESP_LOGI(TAG, "[%s] P1=%.2f P2=%.2f P3=%.1f P4=%.2f T=%.1f°C",
                     sm_state_name(st.state),
                     isnan(p1) ? 0.0f : p1,
                     isnan(p2) ? 0.0f : p2,
                     isnan(p3) ? 0.0f : p3,
                     isnan(p4) ? 0.0f : p4,
                     isnan(t)  ? 0.0f : t);

            float q1 = flowmeter_get_flow(FLOW_CH_INLET);
            float q3 = flowmeter_get_flow(FLOW_CH_PERM2);
            float s1 = conductivity_get_value(COND_CH_FEED);
            float s2 = conductivity_get_value(COND_CH_PERM1);
            float s3 = conductivity_get_value(COND_CH_PERM2);

            ESP_LOGI(TAG, "  Q1=%.3f Q3=%.3f σ1=%.0f σ2=%.0f σ3=%.0f µS/cm",
                     isnan(q1) ? 0.0f : q1,
                     isnan(q3) ? 0.0f : q3,
                     isnan(s1) ? 0.0f : s1,
                     isnan(s2) ? 0.0f : s2,
                     isnan(s3) ? 0.0f : s3);

            const telemetry_data_t *tel = telemetry_get();
            ESP_LOGI(TAG, "  dP=%.2f rec=%.1f%% sel1=%.1f%% sel2=%.1f%%",
                     isnan(tel->filter_dp) ? 0.0f : tel->filter_dp,
                     isnan(tel->system_recovery_pct) ? 0.0f : tel->system_recovery_pct,
                     isnan(tel->stage1_selectivity) ? 0.0f : tel->stage1_selectivity,
                     isnan(tel->stage2_selectivity) ? 0.0f : tel->stage2_selectivity);
        }

        vTaskDelay(pdMS_TO_TICKS(PROCESS_CYCLE_MS));
    }
}
