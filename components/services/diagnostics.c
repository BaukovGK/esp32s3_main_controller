/**
 * @file diagnostics.c
 * @brief Сбор системной диагностики
 */
#include "diagnostics.h"
#include "modbus_poller.h"
#include "board_config.h"

#include "esp_system.h"
#include "esp_timer.h"

#include <string.h>

/* Зарегистрированные задачи */
static struct {
    const char   *name;
    TaskHandle_t  handle;
} s_tasks[DIAG_MAX_TASKS];
static int s_task_count = 0;

/* Modbus slave-адреса для мониторинга */
static const uint8_t s_mb_addrs[] = {
    MB_ADDR_WAVESHARE_AI, MB_ADDR_URZH2KM, MB_ADDR_SL21_201, MB_ADDR_SL21_101
};

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

    /* Modbus ошибки и онлайн-статус */
    for (int i = 0; i < (int)(sizeof(s_mb_addrs) / sizeof(s_mb_addrs[0])); i++) {
        out->mb_errors[i] = modbus_poller_get_error_count(s_mb_addrs[i]);
        out->mb_online[i] = modbus_poller_is_device_online(s_mb_addrs[i]);
    }
}
