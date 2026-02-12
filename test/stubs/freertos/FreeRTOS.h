/**
 * @file FreeRTOS.h
 * @brief Stub для host-тестов: минимальные определения FreeRTOS
 */
#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;
typedef int      BaseType_t;
typedef uint32_t StackType_t;

#define pdTRUE   1
#define pdFALSE  0
#define pdPASS   1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY     0xFFFFFFFFUL
#define configTICK_RATE_HZ 1000

/* portMUX stubs (нет реальной многозадачности в тестах) */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux)  ((void)(mux))
