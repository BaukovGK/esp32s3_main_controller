/**
 * @file hal_nvs.h
 * @brief Обёртка NVS для хранения конфигурации
 *
 * Namespace: "ro_plant". Атомарные операции чтения/записи.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализация NVS Flash
 * @return ESP_OK при успехе
 */
esp_err_t hal_nvs_init(void);

/**
 * @brief Записать int32 в NVS
 */
esp_err_t hal_nvs_set_i32(const char *key, int32_t value);

/**
 * @brief Прочитать int32 из NVS
 */
esp_err_t hal_nvs_get_i32(const char *key, int32_t *value);

/**
 * @brief Записать float в NVS (хранится как blob)
 */
esp_err_t hal_nvs_set_float(const char *key, float value);

/**
 * @brief Прочитать float из NVS
 */
esp_err_t hal_nvs_get_float(const char *key, float *value);

/**
 * @brief Записать строку в NVS
 */
esp_err_t hal_nvs_set_str(const char *key, const char *value);

/**
 * @brief Прочитать строку из NVS
 */
esp_err_t hal_nvs_get_str(const char *key, char *value, size_t max_len);

#ifdef __cplusplus
}
#endif
