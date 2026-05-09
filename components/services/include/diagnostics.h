/**
 * @file diagnostics.h
 * @brief Сбор системной диагностики — heap, стеки задач, Modbus, uptime
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIAG_MAX_TASKS 6

typedef struct {
    /* Система */
    uint32_t free_heap;
    uint32_t min_free_heap;
    int64_t  uptime_us;

    /* Задачи — свободный стек (байты) */
    struct {
        const char *name;
        uint32_t    stack_free;
    } tasks[DIAG_MAX_TASKS];
    int task_count;

    /* Modbus. Phase-4 (M-6): размер берётся из modbus_poller_get_slave_addrs.
     * Сохраняем максимум 8 устройств — этого хватит до конца расширения. */
    #define DIAG_MAX_MB_DEVICES  8
    uint8_t  mb_addrs[DIAG_MAX_MB_DEVICES];
    uint32_t mb_errors[DIAG_MAX_MB_DEVICES];
    bool     mb_online[DIAG_MAX_MB_DEVICES];
    size_t   mb_count;
} diagnostics_data_t;

/**
 * @brief Зарегистрировать задачу для мониторинга стека
 * @param name   Имя задачи (строковый литерал)
 * @param handle Хэндл задачи из xTaskCreate()
 */
void diagnostics_register_task(const char *name, TaskHandle_t handle);

/**
 * @brief Собрать снимок диагностики
 */
void diagnostics_collect(diagnostics_data_t *out);

#ifdef __cplusplus
}
#endif
