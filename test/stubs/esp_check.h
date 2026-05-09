/**
 * @file esp_check.h
 * @brief Stub для host-тестов: ESP_RETURN_ON_ERROR и др.
 */
#pragma once

#include "esp_err.h"

#define ESP_RETURN_ON_ERROR(x, tag, msg, ...) do { \
    esp_err_t __err = (x); \
    if (__err != ESP_OK) return __err; \
} while(0)

#define ESP_RETURN_ON_FALSE(cond, err, tag, msg, ...) do { \
    if (!(cond)) return (err); \
} while(0)
