/**
 * @file web_auth.h
 * @brief HTTP Basic Auth helper для web-сервера (Phase-4, H-13).
 */
#pragma once

#include "esp_http_server.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализация (читает текущий config_web_auth_t).
 *        Вызывать в web_server_start() ПОСЛЕ config_manager_init().
 */
void web_auth_init(void);

/**
 * @brief Перечитать конфигурацию из NVS/RAM (после POST /api/v1/config/web_auth).
 */
void web_auth_refresh(void);

/**
 * @brief Проверить заголовок Authorization запроса.
 *
 * Если auth выключена (пустой username) — возвращает true без проверки.
 * Если auth включена и заголовок не совпал — отправляет 401 с
 * WWW-Authenticate, возвращает false. Caller должен сразу вернуть ESP_OK.
 *
 * @return true — авторизован (продолжать handler), false — отказ.
 */
bool web_auth_check(httpd_req_t *req);

#ifdef __cplusplus
}
#endif
