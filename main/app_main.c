/**
 * @file app_main.c
 * @brief Точка входа — контроллер установки обратного осмоса
 *
 * Инициализация HAL, Ethernet, Modbus Master.
 * Запуск FreeRTOS задач: modbus_poller_task, io_task.
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"

#include "board_config.h"
#include "hal_nvs.h"
#include "hal_i2c.h"
#include "hal_gpio.h"
#include "hal_uart.h"
#include "hal_buzzer.h"
#include "hal_rgb.h"
#include "modbus_poller.h"
#include "mb_device_check.h"
#include "ethernet_init.h"

#include "config_manager.h"
#include "analog_input.h"
#include "flowmeter.h"
#include "conductivity.h"
#include "power_meter.h"
#include "interlocks.h"
#include "state_machine.h"
#include "doser.h"
#include "telemetry.h"
#include "process_task.h"
#include "watchdog_task.h"
#include "web_server.h"
#include "alarm_manager.h"
#include "diagnostics.h"
#include "mqtt_app.h"
#include "watchdog_task.h"

static const char *TAG = "app_main";

/* --- Параметры задач: стек (байт) и приоритет --- */
#define TASK_MODBUS_STACK   4096
#define TASK_MODBUS_PRIO    6
#define TASK_IO_STACK       2048
#define TASK_IO_PRIO        6
#define TASK_PROCESS_STACK  8192
#define TASK_PROCESS_PRIO   5
#define TASK_WDT_STACK      2048
#define TASK_WDT_PRIO       7

/* Период задачи IO (debounce + E-STOP), мс */
#define IO_TASK_CYCLE_MS    10

/* Health-check: задержка перед запуском проверки (даём 2-3 цикла опроса
 * шине стабилизироваться, чтобы snapshot'ы драйверов были заполнены). */
#define HEALTH_CHECK_DELAY_MS  5000
#define HEALTH_CHECK_STACK     4096
#define HEALTH_CHECK_PRIO      (tskIDLE_PRIORITY + 1)

/* --- Ethernet event handlers --- */

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet подключён, MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Ethernet отключён");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet запущен");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet остановлен");
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;
    ESP_LOGI(TAG, "Получен IP: " IPSTR ", маска: " IPSTR ", шлюз: " IPSTR,
             IP2STR(&ip_info->ip), IP2STR(&ip_info->netmask), IP2STR(&ip_info->gw));
}

/* --- Health-check task: одноразовый старт-ап аудит Modbus-устройств --- */
static void health_check_task(void *arg)
{
    (void)arg;
    /* Дать modbus_poller'у пройти 2-3 цикла опроса (минимальный период
     * 100мс для AI, максимальный 3000мс для Cond) — за 5с все устройства
     * должны успеть ответить хотя бы раз. До этого snapshot'ы драйверов
     * NaN/zero, и КЖС-проверки бесполезны. */
    vTaskDelay(pdMS_TO_TICKS(HEALTH_CHECK_DELAY_MS));
    mb_device_check_run();
    /* Самозавершение — это разовая операция при старте системы. */
    vTaskDelete(NULL);
}

/* --- IoTask: debounce DI, логирование, watchdog feed --- */

static void io_task(void *arg)
{
    /* Phase-1 (K-5): handle watchdog'а передаётся через arg.
     * Используем encoding `+1` (см. WDT_HANDLE_TO_ARG/ARG_TO_HANDLE),
     * иначе валидный handle == 0 теряется на проверке `arg > 0`. */
    int wdt_h = WDT_ARG_TO_HANDLE(arg);
    ESP_LOGI(TAG, "IoTask запущена (wdt=%d)", wdt_h);

    uint8_t prev_di = 0;

    while (1) {
        /* E-STOP: raw check без debounce (реакция < 10мс) */
        if (hal_gpio_is_estop_raw()) {
            hal_gpio_write_do(0x00);
        }

        hal_gpio_debounce_process();

        /* Логируем изменения DI */
        uint8_t di = hal_gpio_read_di();
        if (di != prev_di) {
            ESP_LOGI(TAG, "DI изменились: 0x%02X -> 0x%02X", prev_di, di);
            prev_di = di;
        }

        if (wdt_h >= 0) watchdog_feed_h(wdt_h);

        vTaskDelay(pdMS_TO_TICKS(IO_TASK_CYCLE_MS));
    }
}

/* --- Инициализация Ethernet --- */

static esp_err_t init_ethernet(void)
{
    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles;

    ESP_RETURN_ON_ERROR(example_eth_init(&eth_handles, &eth_port_cnt),
                        TAG, "Ethernet init failed");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");

    /* Создание netif для первого Ethernet-порта */
    if (eth_port_cnt > 0) {
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        esp_netif_t *eth_netif = esp_netif_new(&cfg);
        esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth_handles[0]);
        ESP_RETURN_ON_ERROR(esp_netif_attach(eth_netif, glue), TAG, "netif attach");
    }

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL),
        TAG, "register eth handler");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL),
        TAG, "register ip handler");

    for (int i = 0; i < eth_port_cnt; i++) {
        ESP_RETURN_ON_ERROR(esp_eth_start(eth_handles[i]), TAG, "eth start");
    }

    return ESP_OK;
}

/* --- Точка входа --- */

/* Phase-2 (M-2): записываем причину последней перезагрузки в alarm_manager,
 * чтобы оператор/MQTT увидел при необходимости. Вызывается после init NVS
 * и alarm_manager. */
static void log_reset_reason(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    const char *name;
    bool unexpected = false;
    switch (reason) {
    case ESP_RST_POWERON:  name = "POWERON";  break;
    case ESP_RST_SW:       name = "SW";       break;
    case ESP_RST_DEEPSLEEP:name = "DEEPSLEEP";break;
    case ESP_RST_BROWNOUT: name = "BROWNOUT"; unexpected = true; break;
    case ESP_RST_PANIC:    name = "PANIC";    unexpected = true; break;
    case ESP_RST_INT_WDT:  name = "INT_WDT";  unexpected = true; break;
    case ESP_RST_TASK_WDT: name = "TASK_WDT"; unexpected = true; break;
    case ESP_RST_WDT:      name = "WDT";      unexpected = true; break;
    case ESP_RST_USB:      name = "USB";      break;
    default:               name = "UNKNOWN";  unexpected = true; break;
    }
    ESP_LOGI(TAG, "Причина последней перезагрузки: %s (%d)", name, (int)reason);
    if (unexpected) {
        alarm_raise(ALARM_UNEXPECTED_RESTART, ALARM_CAT_WARNING, (float)reason);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Контроллер обратного осмоса ===");
    ESP_LOGI(TAG, "Инициализация...");

    /* 1. NVS */
    ESP_ERROR_CHECK(hal_nvs_init());

    /* 2. I2C шина (для TCA9554 DO + RTC) */
    ESP_ERROR_CHECK(hal_i2c_init());

    /* 3. GPIO: DI (прямые GPIO) + DO (через TCA9554) */
    ESP_ERROR_CHECK(hal_gpio_init());

    /* 4. UART RS-485 (информационный) */
    ESP_ERROR_CHECK(hal_uart_init());

    /* 4.5. Phase-3 (L-4): пьезо-buzzer для звуковой индикации аварий.
     * Soft-fail: ошибка инициализации buzzer'а не должна валить систему. */
    if (hal_buzzer_init() != ESP_OK) {
        ESP_LOGW(TAG, "hal_buzzer_init failed — продолжаем без звуковой индикации");
    }

    /* 4.6. Phase-4: WS2812 RGB-LED для визуальной индикации.
     * Soft-fail: если led_strip не инициализировался (нет компонента, конфликт RMT) —
     * система всё равно управляет установкой. */
    if (hal_rgb_init() != ESP_OK) {
        ESP_LOGW(TAG, "hal_rgb_init failed — продолжаем без RGB-индикации");
    }

    /* 5. Ethernet W5500 */
    ESP_ERROR_CHECK(init_ethernet());

    /* 6. Modbus Master */
    ESP_ERROR_CHECK(modbus_poller_init());

    /* 7. Конфигурация */
    ESP_ERROR_CHECK(config_manager_init());

    /* 8. Драйверы */
    analog_input_init();
    flowmeter_init();
    conductivity_init();
    power_meter_init();  /* Phase-5: KWS-306L (НД/ВД) */

    /* 9. Менеджер аварий — должен быть до state_machine_init,
     *    т.к. SM может поднять ALARM_RESTART_DURING_OP при восстановлении. */
    ESP_ERROR_CHECK(alarm_manager_init());

    /* 9.1. Phase-2 (M-2): запись причины перезагрузки в журнал аварий. */
    log_reset_reason();

    /* 9.2. Логика процесса (state_machine_init читает SM-state из NVS). */
    interlocks_init();
    state_machine_init();
    doser_init();
    telemetry_init();

    /* 9.6. Phase-1: регистрация клиентов watchdog'а ДО запуска задач.
     *      process — критичная, перезагружаем при stale 10с.
     *      io — критичная (E-STOP), перезагружаем при stale 5с.
     *      modbus — некритичная, только safe-state без рестарта. */
    int wdt_process = watchdog_register("process", 3, 10);
    int wdt_io      = watchdog_register("io",      3, 5);
    int wdt_modbus  = watchdog_register("modbus", 15,  0);

    /* 10. Запуск задач (handle watchdog'а передаётся через arg задачи).
     * Используем WDT_HANDLE_TO_ARG (encoding +1), чтобы валидный handle == 0
     * не интерпретировался как «нет watchdog'а» (NULL). */
    TaskHandle_t h;
    xTaskCreate(modbus_poller_task, "modbus", TASK_MODBUS_STACK,
                WDT_HANDLE_TO_ARG(wdt_modbus), TASK_MODBUS_PRIO, &h);
    diagnostics_register_task("modbus", h);
    xTaskCreate(io_task, "io", TASK_IO_STACK,
                WDT_HANDLE_TO_ARG(wdt_io), TASK_IO_PRIO, &h);
    diagnostics_register_task("io", h);
    xTaskCreate(process_task, "process", TASK_PROCESS_STACK,
                WDT_HANDLE_TO_ARG(wdt_process), TASK_PROCESS_PRIO, &h);
    diagnostics_register_task("process", h);
    xTaskCreate(watchdog_task, "watchdog", TASK_WDT_STACK, NULL, TASK_WDT_PRIO, &h);
    diagnostics_register_task("watchdog", h);

    /* 10.5. Health-check Modbus-устройств — одноразовая задача с задержкой
     *       (см. mb_device_check.h). Запускается в фоне, чтобы не блокировать
     *       app_main на 5 секунд. После выполнения сама себя удаляет. */
    xTaskCreate(health_check_task, "hc", HEALTH_CHECK_STACK, NULL,
                HEALTH_CHECK_PRIO, &h);
    diagnostics_register_task("hc", h);

    /* 11. HTTP-сервер + REST API.
     * Phase-3 (M-7): soft-fail. Web — не-критичный компонент: при ошибке
     * запуска контроллер должен продолжить управление установкой.
     * Раньше ESP_ERROR_CHECK абортил всю систему при любой ошибке httpd
     * (например, недостаток heap при старте). */
    {
        esp_err_t r = web_server_start();
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "web_server_start failed: %s — продолжаем без web",
                     esp_err_to_name(r));
            /* Алармы инициализирован → можем подсказать оператору */
            alarm_raise(ALARM_SYSTEM_START, ALARM_CAT_WARNING, (float)r);
        }
    }

    /* 12. MQTT клиент.
     * Phase-3 (M-7): soft-fail. Аналогично web — отказ MQTT не должен
     * парализовать установку. */
    if (config_manager_get()->mqtt.enabled) {
        esp_err_t r = mqtt_app_start();
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "mqtt_app_start failed: %s — продолжаем без MQTT",
                     esp_err_to_name(r));
            alarm_raise(ALARM_MQTT_DISCONNECT, ALARM_CAT_WARNING, (float)r);
        }
    }

    ESP_LOGI(TAG, "Инициализация завершена, все задачи запущены");
}
