/**
 * @file mqtt_app.h
 * @brief MQTT клиент для публикации телеметрии и приёма команд
 *
 * Подключение к брокеру, Home Assistant Discovery,
 * периодическая публикация статуса, обработка команд.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Запуск MQTT клиента (создаёт MqttTask)
 * Читает конфигурацию из config_manager.
 * Регистрирует alarm_notify callback.
 */
esp_err_t mqtt_app_start(void);

/**
 * @brief Остановка MQTT клиента
 */
void mqtt_app_stop(void);

/**
 * @brief Проверка подключения к MQTT
 */
bool mqtt_app_is_connected(void);

/**
 * @brief Перезапуск клиента с новыми настройками
 */
esp_err_t mqtt_app_reconnect(void);

#ifdef __cplusplus
}
#endif
