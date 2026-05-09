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
#include "freertos/queue.h"

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

/* Phase-2 (H-6): сериализация stop/start/reconnect.
 * httpd может одновременно получить два POST /api/v1/config/mqtt — без mutex'а
 * это вело к двойному esp_mqtt_client_destroy и краху. */
static SemaphoreHandle_t s_lifecycle_lock = NULL;

/* Потокобезопасная очередь аварий (FreeRTOS queue) */
#define ALARM_QUEUE_SIZE        16

/* Параметры задачи и публикации */
#define MQTT_TASK_STACK_SIZE    8192
#define MQTT_TASK_PRIORITY      4
#define MQTT_DIAG_CYCLE         6       /* публикация диагностики каждый N-й интервал */
#define MQTT_RECONNECT_MS       5000
#define MQTT_LWT_MSG            "offline"

/* C-4: при переподключении надо перепубликовать активные аварии. Размер
 * совпадает с MAX_ACTIVE в alarm_manager.c (32). На стеке ~768 байт — ок,
 * стек event-обработчика ESP-IDF MQTT клиента обычно ≥ 6 кБ. */
#define MQTT_REPUBLISH_MAX_ALARMS 32

static QueueHandle_t s_alarm_queue = NULL;

/* --- Callback аварий для alarm_manager --- */

static void alarm_notify_cb(const alarm_entry_t *entry)
{
    if (s_alarm_queue) {
        /* xQueueSendFromISR не нужен — callback вызывается из обычной задачи */
        xQueueSend(s_alarm_queue, entry, 0);
    }
    if (s_task_handle) {
        xTaskNotifyGive(s_task_handle);
    }
}

/* C-4: повторная публикация всех активных аварий после (пере)подключения.
 *
 * После MQTT_EVENT_DISCONNECTED брокер выкидывает не-retained сообщения,
 * а ro_plant/alarms публикуется БЕЗ retain (см. mqtt_publish_alarm). Если
 * мы только переоткрыли сессию, новые подписчики (HMI/диспетчер) не увидят
 * текущее состояние, пока что-нибудь не поднимется/снимется заново.
 *
 * Решение: снимаем снапшот активных через alarm_get_active() (он сам берёт
 * lock alarm_manager, копирует и отпускает), затем уже вне любого lock'а
 * публикуем каждую запись тем же путём, что и raise — mqtt_publish_alarm,
 * тот же топик, тот же JSON. Дедлоков с s_lock alarm_manager не будет:
 * publish ничего обратно в alarm_manager не зовёт.
 */
static void republish_active_alarms(void)
{
    alarm_entry_t snapshot[MQTT_REPUBLISH_MAX_ALARMS];
    int cnt = alarm_get_active(snapshot, MQTT_REPUBLISH_MAX_ALARMS);
    if (cnt <= 0) {
        return;
    }
    ESP_LOGI(TAG, "Re-publish %d активных аварий после reconnect", cnt);
    for (int i = 0; i < cnt; i++) {
        mqtt_publish_alarm(s_client, &snapshot[i]);
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

        /* C-4: после reconnect перепубликовать активные аварии — иначе
         * подписчики (HMI/диспетчер) видят устаревшее состояние, т.к.
         * ro_plant/alarms не retained. Должно идти ПОСЛЕ alarm_clear, чтобы
         * только что снятая ALARM_MQTT_DISCONNECT не попала в снапшот. */
        republish_active_alarms();

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
        alarm_entry_t alarm;
        while (xQueueReceive(s_alarm_queue, &alarm, 0) == pdTRUE) {
            mqtt_publish_alarm(s_client, &alarm);
        }

        /* Периодическая публикация полного статуса */
        mqtt_publish_full_status(s_client);

        /* Диагностика — каждый N-й цикл (~30с при интервале 5с) */
        if (++diag_counter >= MQTT_DIAG_CYCLE) {
            mqtt_publish_diagnostics(s_client);
            diag_counter = 0;
        }
    }
}

/* --- Публичные функции --- */

/* Внутренняя реализация без захвата s_lifecycle_lock — caller должен держать его */
static esp_err_t mqtt_app_start_locked(void);
static void      mqtt_app_stop_locked(void);

esp_err_t mqtt_app_start(void)
{
    if (s_lifecycle_lock == NULL) {
        s_lifecycle_lock = xSemaphoreCreateMutex();
        if (s_lifecycle_lock == NULL) return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_lifecycle_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "mqtt_app_start: lifecycle lock timeout");
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t r = mqtt_app_start_locked();
    xSemaphoreGive(s_lifecycle_lock);
    return r;
}

static esp_err_t mqtt_app_start_locked(void)
{
    if (s_client) {
        ESP_LOGW(TAG, "MQTT уже запущен");
        return ESP_OK;
    }

    /* Создать очередь аварий (если ещё не создана) */
    if (!s_alarm_queue) {
        s_alarm_queue = xQueueCreate(ALARM_QUEUE_SIZE, sizeof(alarm_entry_t));
        if (!s_alarm_queue) {
            ESP_LOGE(TAG, "Не удалось создать очередь аварий");
            return ESP_ERR_NO_MEM;
        }
    }

    const config_mqtt_t *cfg = &config_manager_get()->mqtt;

    ESP_LOGI(TAG, "Запуск MQTT: broker=%s client_id=%s interval=%lds",
             cfg->broker_uri, cfg->client_id, (long)cfg->publish_interval_s);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = cfg->broker_uri,
        .credentials.client_id = cfg->client_id,
        .session.last_will = {
            .topic = "ro_plant/availability",
            .msg = MQTT_LWT_MSG,
            .msg_len = sizeof(MQTT_LWT_MSG) - 1,
            .qos = 1,
            .retain = true,
        },
        .network.reconnect_timeout_ms = MQTT_RECONNECT_MS,
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
    BaseType_t ok = xTaskCreate(mqtt_task, "mqtt", MQTT_TASK_STACK_SIZE, NULL, MQTT_TASK_PRIORITY, &s_task_handle);
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
    if (s_lifecycle_lock == NULL) {
        /* stop без предыдущего start — ничего не делаем */
        return;
    }
    if (xSemaphoreTake(s_lifecycle_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "mqtt_app_stop: lifecycle lock timeout");
        return;
    }
    mqtt_app_stop_locked();
    xSemaphoreGive(s_lifecycle_lock);
}

static void mqtt_app_stop_locked(void)
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
    /* Phase-2 (H-6): захватываем lock на ВЕСЬ stop+start, чтобы два параллельных
     * вызова из httpd не привели к двойному destroy. */
    if (s_lifecycle_lock == NULL) {
        s_lifecycle_lock = xSemaphoreCreateMutex();
        if (s_lifecycle_lock == NULL) return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_lifecycle_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "mqtt_app_reconnect: lifecycle lock timeout");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Переподключение MQTT с новыми настройками...");
    mqtt_app_stop_locked();
    esp_err_t r = mqtt_app_start_locked();

    xSemaphoreGive(s_lifecycle_lock);
    return r;
}
