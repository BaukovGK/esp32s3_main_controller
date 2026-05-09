/**
 * @file web_server.h
 * @brief HTTP REST API + веб-панель контроллера обратного осмоса
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Запуск HTTP-сервера и регистрация всех REST API endpoints
 * @return ESP_OK при успехе
 */
esp_err_t web_server_start(void);

/**
 * @brief Остановка HTTP-сервера
 */
void web_server_stop(void);

#ifdef __cplusplus
}
#endif
