/**
 * @file process_task.c
 * @brief Главная задача процесса — 100мс цикл
 */
#include "process_task.h"
#include "analog_input.h"
#include "flowmeter.h"
#include "conductivity.h"
#include "state_machine.h"
#include "doser.h"
#include "telemetry.h"
#include "watchdog_task.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "process";

/* Период логирования: 5с / 100мс = 50 циклов */
#define LOG_PERIOD_CYCLES  50

void process_task(void *arg)
{
    ESP_LOGI(TAG, "ProcessTask запущена (100мс)");
    int log_counter = 0;

    while (1) {
        /* 1. Обновление драйверов */
        analog_input_update();
        flowmeter_update();
        conductivity_update();

        /* 2. Обновление конечного автомата (вызывает interlocks_check внутри) */
        state_machine_update();

        /* 3. Обновление дозатора */
        sm_status_t st = state_machine_get_status();
        bool auto_running = (st.state == SM_AUTO && st.auto_sub == AUTO_RUNNING);
        doser_update(auto_running);

        /* 4. Обновление телеметрии */
        telemetry_update();

        /* 5. Сброс watchdog */
        watchdog_feed();

        /* 6. Периодический лог */
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

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
