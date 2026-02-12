/**
 * @file task.h
 * @brief Stub для host-тестов: минимальные типы FreeRTOS tasks
 */
#pragma once

#include "FreeRTOS.h"

typedef void *TaskHandle_t;

static inline void vTaskDelay(TickType_t ticks) { (void)ticks; }
static inline uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask)
{
    (void)xTask;
    return 256; /* Фиктивное значение для тестов */
}
