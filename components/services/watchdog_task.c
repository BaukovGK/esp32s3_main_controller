/**
 * @file watchdog_task.c
 * @brief Программный сторожевой таймер для нескольких задач.
 *
 * См. watchdog_task.h. При зависании клиента:
 *   stale_off_s    — отключение всех DO (safe state)
 *   stale_reboot_s — esp_restart() (если задано >0)
 *
 * Регистрация thread-safe (mutex), feed — atomic add без mutex'а.
 *
 * Публикация записей в s_clients[] для watchdog_task (без mutex'а):
 * сначала полностью заполняем поля локально, потом atomic-store'ом
 * с release-семантикой публикуем счётчик s_count. Watchdog_task
 * читает s_count с acquire-семантикой и итерирует только по гарантированно
 * заполненным записям. Это лечит C-8 (race в register_internal).
 */
#include "watchdog_task.h"
#include "hal_gpio.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <stdatomic.h>
#include <string.h>

static const char *TAG = "watchdog";

#define WDT_CHECK_INTERVAL_MS  1000  /* интервал проверки */

typedef struct {
    const char *name;
    atomic_uint_fast32_t counter;   /* инкрементируется feed'ом */
    uint32_t   last_seen;           /* последнее значение counter */
    uint32_t   stale_off_s;
    uint32_t   stale_reboot_s;
    int        stale_count;         /* секунд stale подряд */
    bool       in_use;
} wdt_client_t;

static wdt_client_t          s_clients[WDT_MAX_CLIENTS];
/* s_count — atomic: пишется под s_reg_lock с release-семантикой
 * (после того как поля slot'а полностью записаны), читается watchdog_task
 * с acquire-семантикой. */
static atomic_uint_fast32_t  s_count = 0;
static SemaphoreHandle_t     s_reg_lock = NULL;

static int register_internal(const char *name, uint32_t off_s, uint32_t reboot_s)
{
    /* relaxed: вся синхронизация уже под s_reg_lock, watchdog_task
     * читает s_count отдельно с acquire-семантикой ниже. */
    uint32_t cur = atomic_load_explicit(&s_count, memory_order_relaxed);
    if (cur >= WDT_MAX_CLIENTS) {
        ESP_LOGE(TAG, "WDT_MAX_CLIENTS exceeded, ignore '%s'", name);
        return WDT_INVALID_HANDLE;
    }
    int h = (int)cur;

    /* Сначала полностью готовим slot — watchdog_task его ещё не видит,
     * т.к. s_count пока не инкрементирован. */
    s_clients[h].name = name;
    atomic_store_explicit(&s_clients[h].counter, 0u, memory_order_relaxed);
    s_clients[h].last_seen = 0;
    s_clients[h].stale_off_s = off_s;
    s_clients[h].stale_reboot_s = reboot_s;
    s_clients[h].stale_count = 0;
    s_clients[h].in_use = true;

    /* Release-store публикует все предыдущие записи. */
    atomic_store_explicit(&s_count, cur + 1, memory_order_release);

    ESP_LOGI(TAG, "Зарегистрирован '%s' h=%d off=%lus reboot=%lus",
             name, h, (unsigned long)off_s, (unsigned long)reboot_s);
    return h;
}

int watchdog_register(const char *name, uint32_t stale_off_s, uint32_t stale_reboot_s)
{
    if (s_reg_lock == NULL) {
        s_reg_lock = xSemaphoreCreateMutex();
        if (s_reg_lock == NULL) return WDT_INVALID_HANDLE;
    }
    if (xSemaphoreTake(s_reg_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return WDT_INVALID_HANDLE;
    }
    int h = register_internal(name, stale_off_s, stale_reboot_s);
    xSemaphoreGive(s_reg_lock);
    return h;
}

void watchdog_feed_h(int handle)
{
    if (handle < 0 || handle >= WDT_MAX_CLIENTS) return;
    /* Acquire: гарантирует, что после release-store в register_internal
     * мы увидим in_use=true и валидные поля slot'а. */
    uint32_t cnt = atomic_load_explicit(&s_count, memory_order_acquire);
    if ((uint32_t)handle >= cnt) return;
    if (!s_clients[handle].in_use) return;
    atomic_fetch_add(&s_clients[handle].counter, 1u);
}

void watchdog_feed(void)
{
    /* Совместимость: handle 0 = "process". Если ещё не зарегистрирован —
     * регистрируем сейчас. */
    if (atomic_load_explicit(&s_count, memory_order_acquire) == 0) {
        (void)watchdog_register("process", 3, 10);
    }
    watchdog_feed_h(0);
}

void watchdog_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Watchdog task запущена (период %d мс)", WDT_CHECK_INTERVAL_MS);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(WDT_CHECK_INTERVAL_MS));

        /* Acquire-load: видим только те slot'ы, что были полностью
         * заполнены до release-store в register_internal. */
        uint32_t cnt = atomic_load_explicit(&s_count, memory_order_acquire);
        for (uint32_t i = 0; i < cnt; i++) {
            wdt_client_t *c = &s_clients[i];
            if (!c->in_use) continue;

            uint32_t cur = atomic_load(&c->counter);
            if (cur == c->last_seen) {
                c->stale_count++;

                if (c->stale_off_s > 0 && c->stale_count == (int)c->stale_off_s) {
                    ESP_LOGE(TAG, "'%s' stale %ds → DO=0", c->name, c->stale_count);
                    hal_gpio_write_do(0x00);
                }
                if (c->stale_reboot_s > 0 && c->stale_count >= (int)c->stale_reboot_s) {
                    ESP_LOGE(TAG, "'%s' stale %ds → ESP_RESTART", c->name, c->stale_count);
                    esp_restart();
                }
            } else {
                if (c->stale_count >= (int)c->stale_off_s && c->stale_off_s > 0) {
                    ESP_LOGW(TAG, "'%s' восстановлен", c->name);
                }
                c->stale_count = 0;
                c->last_seen = cur;
            }
        }
    }
}
