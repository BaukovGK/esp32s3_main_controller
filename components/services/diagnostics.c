/**
 * @file diagnostics.c
 * @brief Сбор системной диагностики
 *
 * Phase-4 (M-6): список Modbus-устройств берётся из modbus_poller'а
 * (раньше дублировался жёстко прописанным массивом, при добавлении
 * устройств diagnostics не обновлялся).
 */
#include "diagnostics.h"
#include "modbus_poller.h"

#include "esp_system.h"
#include "esp_timer.h"

#include <string.h>

/* Зарегистрированные задачи */
static struct {
    const char   *name;
    TaskHandle_t  handle;
} s_tasks[DIAG_MAX_TASKS];
static int s_task_count = 0;

void diagnostics_register_task(const char *name, TaskHandle_t handle)
{
    if (s_task_count < DIAG_MAX_TASKS && handle != NULL) {
        s_tasks[s_task_count].name = name;
        s_tasks[s_task_count].handle = handle;
        s_task_count++;
    }
}

void diagnostics_collect(diagnostics_data_t *out)
{
    memset(out, 0, sizeof(*out));

    /* Система */
    out->free_heap = esp_get_free_heap_size();
    out->min_free_heap = esp_get_minimum_free_heap_size();
    out->uptime_us = esp_timer_get_time();

    /* Стеки задач */
    out->task_count = s_task_count;
    for (int i = 0; i < s_task_count; i++) {
        out->tasks[i].name = s_tasks[i].name;
        out->tasks[i].stack_free =
            uxTaskGetStackHighWaterMark(s_tasks[i].handle) * sizeof(StackType_t);
    }

    /* Modbus — динамический список из poller'а */
    out->mb_count = modbus_poller_get_slave_addrs(out->mb_addrs, DIAG_MAX_MB_DEVICES);
    for (size_t i = 0; i < out->mb_count; i++) {
        out->mb_errors[i] = modbus_poller_get_error_count(out->mb_addrs[i]);
        out->mb_online[i] = modbus_poller_is_device_online(out->mb_addrs[i]);
    }
}
