/**
 * @file web_server.c
 * @brief Инициализация HTTP-сервера и регистрация URI
 */
#include "web_server.h"
#include "web_auth.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "httpd";

#define WEB_MAX_URI_HANDLERS  20    /* 15 используем + запас */
#define WEB_STACK_SIZE        6144  /* увеличен для cJSON */

static httpd_handle_t s_server = NULL;

/* Внутренние функции регистрации из других .c файлов */
void web_static_register(httpd_handle_t server);
void web_api_register_status(httpd_handle_t server);
void web_api_register_commands(httpd_handle_t server);

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = WEB_MAX_URI_HANDLERS;
    config.stack_size = WEB_STACK_SIZE;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Запуск HTTP-сервера на порту %d...", config.server_port);

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка запуска HTTP-сервера: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Phase-4: подготовить auth-кэш ДО регистрации handler'ов */
    web_auth_init();

    /* Регистрация обработчиков */
    web_static_register(s_server);
    web_api_register_status(s_server);
    web_api_register_commands(s_server);

    ESP_LOGI(TAG, "HTTP-сервер запущен");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "HTTP-сервер остановлен");
    }
}
