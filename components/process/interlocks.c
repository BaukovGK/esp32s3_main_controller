/**
 * @file interlocks.c
 * @brief Блокировки безопасности установки обратного осмоса
 */
#include "interlocks.h"
#include "analog_input.h"
#include "hal_gpio.h"
#include "config_manager.h"
#include "board_config.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "intlk";

void interlocks_init(void)
{
    ESP_LOGI(TAG, "Блокировки безопасности инициализированы");
}

void interlocks_check(bool manual_mode, interlock_result_t *result)
{
    /* Phase-2 (H-1): атомарные снимки секций конфига вместо нескольких полевых
     * чтений из общего указателя. Без этого setter мог обновить структуру
     * между чтениями p1_max и p3_max → reader видел несогласованные пороги. */
    config_pressure_t pcfg;
    config_washing_t  wcfg;
    config_manager_get_pressure(&pcfg);
    config_manager_get_washing(&wcfg);

    uint8_t di = hal_gpio_read_di();

    result->active_flags = 0;
    result->allow_pump_feed = true;
    result->allow_pump_stage1 = true;
    result->allow_pump_stage2 = true;
    result->allow_heater = true;
    result->allow_doser = true;
    result->estop_active = false;
    result->filter_warn = false;

    /* ===== E-STOP: NC — активен когда DI5 = 0 (разомкнут) ===== */
    bool estop = !(di & (1 << (BOARD_DI_ESTOP - 1)));
    if (estop) {
        result->active_flags |= INTERLOCK_ESTOP;
        result->estop_active = true;
        result->allow_pump_feed = false;
        result->allow_pump_stage1 = false;
        result->allow_pump_stage2 = false;
        result->allow_heater = false;
        result->allow_doser = false;
        return;  /* E-STOP блокирует всё */
    }

    /* В MANUAL — только E-STOP, остальные проверки пропускаем */
    if (manual_mode) return;

    /* ===== Дискретные входы ===== */

    /* Источник пуст (DI1, NC: 0 = пуст) → стоп pump1 + pump2 */
    bool source_empty = !(di & (1 << (BOARD_DI_SOURCE_EMPTY - 1)));
    if (source_empty) {
        result->active_flags |= INTERLOCK_SOURCE_EMPTY;
        result->allow_pump_feed = false;
        result->allow_pump_stage1 = false;
    }

    /* Промбак пуст (DI2, NC: 0 = пуст) → стоп pump3 */
    bool interm_empty = !(di & (1 << (BOARD_DI_INTERM_EMPTY - 1)));
    if (interm_empty) {
        result->active_flags |= INTERLOCK_INTERM_EMPTY;
        result->allow_pump_stage2 = false;
    }

    /* ===== Давления =====
     *
     * Phase-1: NaN от датчика трактуется как отказ — блокируем агрегат,
     * который зависит от этого датчика. Без этого при обрыве датчика
     * проверка `p > limit` пропускалась → насосы работали без защиты.
     */

    float p1 = analog_input_get_value(AI_CH_P1);
    float p2 = analog_input_get_value(AI_CH_P2);
    float p3 = analog_input_get_value(AI_CH_P3);
    float p4 = analog_input_get_value(AI_CH_P4);

    if (isnan(p1)) {
        result->active_flags |= INTERLOCK_SENSOR_FAULT_P1;
        result->allow_pump_feed = false;
    } else if (p1 > pcfg.p1_max) {
        result->active_flags |= INTERLOCK_P1_HIGH;
        result->allow_pump_feed = false;
    }

    if (isnan(p3)) {
        result->active_flags |= INTERLOCK_SENSOR_FAULT_P3;
        result->allow_pump_stage1 = false;
    } else if (p3 > pcfg.p3_max) {
        result->active_flags |= INTERLOCK_P3_HIGH;
        result->allow_pump_stage1 = false;
    }

    if (isnan(p4)) {
        result->active_flags |= INTERLOCK_SENSOR_FAULT_P4;
        result->allow_pump_stage2 = false;
    } else if (p4 > pcfg.p4_max) {
        result->active_flags |= INTERLOCK_P4_HIGH;
        result->allow_pump_stage2 = false;
    }

    /* ===== Температура ===== */

    float t = analog_input_get_value(AI_CH_T);
    if (isnan(t)) {
        result->active_flags |= INTERLOCK_SENSOR_FAULT_T;
        result->allow_heater = false;
    } else if (t > wcfg.t_overshoot_C) {
        result->active_flags |= INTERLOCK_T_HIGH;
        result->allow_heater = false;
    }

    /* ===== Перепад давления на фильтре ===== */

    if (!isnan(p1) && !isnan(p2) && (p1 - p2) > pcfg.filter_dp_warn) {
        result->active_flags |= INTERLOCK_FILTER_DP;
        result->filter_warn = true;
        /* Только предупреждение, не блокируем */
    }
}
