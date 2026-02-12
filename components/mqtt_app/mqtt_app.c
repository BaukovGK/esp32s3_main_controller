/**
 * @file mqtt_app.c
 * @brief Ядро MQTT клиента — MqttTask, обработка событий, управление подключением
 */
#include "mqtt_app.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config_manager.h"
#include "alarm_manager.h"
#include "diagnostics.h"

#include <string.h>

static const char *TAG = "mqtt_app";

/* Прототипы из mqtt_publish.c / mqtt_subscribe.c */
extern void mqtt_publish_ha_discovery(esp_mqtt_client_handle_t client);
extern void mqtt_publish_full_status(esp_mqtt_client_handle_t client);
extern void mqtt_publish_alarm(esp_mqtt_client_handle_t client, const alarm_entry_t *alarm);
extern void mqtt_publish_online(esp_mqtt_client_handle_t client);
extern void mqtt_publish_diagnostics(esp_mqtt_client_handle_t client);
extern void mqtt_subscribe_all(esp_mqtt_client_handle_t client);
extern void mqtt_subscribe_handle_message(const char *topic, int topic_len,
                                           const char *data, int data_len);

/* Состояние модуля */
static esp_mqtt_client_handle_t s_client = NULL;
static TaskHandle_t s_task_handle = NULL;
static volatile bool s_connected = false;

/* Очередь аварий для отложенной публикации */
#define ALARM_QUEUE_SIZE 16
static alarm_entry_t s_alarm_queue[ALARM_QUEUE_SIZE];
static volatile int s_alarm_queue_head = 0;
static volatile int s_alarm_queue_tail = 0;

/* --- Callback аварий для alarm_manager --- */

static void alarm_notify_cb(const alarm_entry_t *entry)
{
    /* Помещаем в кольцевой буфер, будим задачу */
    int next = (s_alarm_queue_head + 1) % ALARM_QUEUE_SIZE;
    if (next != s_alarm_queue_tail) {
        s_alarm_queue[s_alarm_queue_head] = *entry;
        s_alarm_queue_head = next;
    }
    if (s_task_handle) {
        xTaskNotifyGive(s_task_handle);
    }
}

/* --- MQTT event handler --- */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT подключён к брокеру");
        s_connected = true;

        /* Публикуем availability */
        mqtt_publish_online(s_client);

        /* Home Assistant Discovery */
        mqtt_publish_ha_discovery(s_client);

        /* Подписки на команды */
        mqtt_subscribe_all(s_client);

        /* Снимаем аварию отключения (если была) */
        alarm_clear(ALARM_MQTT_DISCONNECT);

        /* Будим задачу для немедленной публикации */
        if (s_task_handle) {
            xTaskNotifyGive(s_task_handle);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT отключён от брокера");
        s_connected = false;
        alarm_raise(ALARM_MQTT_DISCONNECT, ALARM_CAT_INFO, 0);
        break;

    case MQTT_EVENT_DATA:
        mqtt_subscribe_handle_message(event->topic, event->topic_len,
                                       event->data, event->data_len);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT ошибка: type=%d", event->error_handle->error_type);
        break;

    default:
        break;
    }
}

/* --- MqttTask --- */

static void mqtt_task(void *arg)
{
    ESP_LOGI(TAG, "MqttTask запущена");

    const config_mqtt_t *cfg = &config_manager_get()->mqtt;
    TickType_t interval = pdMS_TO_TICKS(cfg->publish_interval_s * 1000);
    int diag_counter = 0;

    while (1) {
        /* Ждём уведомления или таймаута (интервал публикации) */
        ulTaskNotifyTake(pdTRUE, interval);

        /* Обновляем интервал (может измениться через конфиг) */
        cfg = &config_manager_get()->mqtt;
        interval = pdMS_TO_TICKS(cfg->publish_interval_s * 1000);

        if (!s_connected || !s_client) {
            continue;
        }

        /* Публикуем аварии из очереди */
        while (s_alarm_queue_tail != s_alarm_queue_head) {
            mqtt_publish_alarm(s_client, &s_alarm_queue[s_alarm_queue_tail]);
            s_alarm_queue_tail = (s_alarm_queue_tail + 1) % ALARM_QUEUE_SIZE;
        }

        /* Периодическая публикация полного статуса */
        mqtt_publish_full_status(s_client);

        /* Диагностика — каждый 6-й цикл (~30с при интервале 5с) */
        if (++diag_counter >= 6) {
            mqtt_publish_diagnostics(s_client);
            diag_counter = 0;
        }
    }
}

/* --- Публичные функции --- */

esp_err_t mqtt_app_start(void)
{
    if (s_client) {
        ESP_LOGW(TAG, "MQTT уже запущен");
        return ESP_OK;
    }

    const config_mqtt_t *cfg = &config_manager_get()->mqtt;

    ESP_LOGI(TAG, "Запуск MQTT: broker=%s client_id=%s interval=%lds",
             cfg->broker_uri, cfg->client_id, (long)cfg->publish_interval_s);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = cfg->broker_uri,
        .credentials.client_id = cfg->client_id,
        .session.last_will = {
            .topic = "ro_plant/availability",
            .msg = "offline",
            .msg_len = 7,
            .qos = 1,
            .retain = true,
        },
        .network.reconnect_timeout_ms = 5000,
    };

    /* Авторизация (если задана) */
    if (cfg->username[0] != '\0') {
        mqtt_cfg.credentials.username = cfg->username;
        if (cfg->password[0] != '\0') {
            mqtt_cfg.credentials.authentication.password = cfg->password;
        }
    }

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                    mqtt_event_handler, NULL);

    /* Регистрируем callback аварий */
    alarm_manager_register_notify(alarm_notify_cb);

    /* Запуск клиента (неблокирующий — создаёт внутреннюю задачу) */
    esp_err_t ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return ret;
    }

    /* Создаём задачу публикации */
    BaseType_t ok = xTaskCreate(mqtt_task, "mqtt", 8192, NULL, 4, &s_task_handle);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Не удалось создать MqttTask");
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return ESP_FAIL;
    }

    diagnostics_register_task("mqtt", s_task_handle);

    return ESP_OK;
}

void mqtt_app_stop(void)
{
    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected = false;
    ESP_LOGI(TAG, "MQTT остановлен");
}

bool mqtt_app_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_app_reconnect(void)
{
    ESP_LOGI(TAG, "Переподключение MQTT с новыми настройками...");
    mqtt_app_stop();
    return mqtt_app_start();
}
