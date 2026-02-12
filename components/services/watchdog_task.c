/**
 * @file watchdog_task.c
 * @brief Сторожевой таймер процесса
 *
 * При зависании ProcessTask:
 *   3с — аварийное отключение DO
 *  10с — перезагрузка ESP32
 */
#include "watchdog_task.h"
#include "hal_gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "watchdog";

#define WDT_DO_OFF_THRESHOLD   3   /* секунд до отключения DO */
#define WDT_REBOOT_THRESHOLD  10   /* секунд до перезагрузки */

/* Счётчик циклов ProcessTask (volatile: один писатель, один читатель) */
static volatile uint32_t s_cycle_counter = 0;

void watchdog_feed(void)
{
    s_cycle_counter++;
}

void watchdog_task(void *arg)
{
    ESP_LOGI(TAG, "Watchdog task запущена");
    uint32_t last_counter = 0;
    int stale_count = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t current = s_cycle_counter;
        if (current == last_counter) {
            stale_count++;
            if (stale_count >= WDT_DO_OFF_THRESHOLD) {
                ESP_LOGE(TAG, "ProcessTask не отвечает %d с — аварийное отключение!", stale_count);
                hal_gpio_write_do(0x00);
            }
            if (stale_count >= WDT_REBOOT_THRESHOLD) {
                ESP_LOGE(TAG, "ProcessTask зависла %d с — ПЕРЕЗАГРУЗКА!", stale_count);
                esp_restart();
            }
        } else {
            if (stale_count >= WDT_DO_OFF_THRESHOLD) {
                ESP_LOGW(TAG, "ProcessTask восстановлен");
            }
            stale_count = 0;
            last_counter = current;
        }
    }
}
