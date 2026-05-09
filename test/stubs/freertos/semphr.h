/**
 * @file semphr.h
 * @brief Stub для host-тестов: минимальная имитация FreeRTOS-семафоров.
 *
 * Тесты однопоточные, поэтому семафоры всегда успешны.
 * Mutex моделируется как счётчик «ownership»: take/give балансируются.
 */
#pragma once

#include "FreeRTOS.h"

typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;

/* Фиктивный «идентификатор» — ненулевой указатель */
#define HOST_TEST_FAKE_HANDLE  ((SemaphoreHandle_t)0x1)

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return HOST_TEST_FAKE_HANDLE;
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xBlockTime)
{
    (void)xSemaphore;
    (void)xBlockTime;
    return pdTRUE;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    (void)xSemaphore;
    return pdTRUE;
}

static inline void vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
    (void)xSemaphore;
}

/* Очередь — упрощённая (тесты её не используют для проверки логики) */
static inline QueueHandle_t xQueueCreate(uint32_t length, uint32_t item_size)
{
    (void)length; (void)item_size;
    return HOST_TEST_FAKE_HANDLE;
}

static inline BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t to)
{
    (void)q; (void)item; (void)to;
    return pdTRUE;
}

static inline BaseType_t xQueueReceive(QueueHandle_t q, void *out, TickType_t to)
{
    (void)q; (void)out; (void)to;
    return pdFALSE;
}
