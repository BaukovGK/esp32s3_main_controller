/**
 * @file watchdog_task.h
 * @brief Программный сторожевой таймер для нескольких FreeRTOS-задач.
 *
 * Phase-1 (K-5): ранее watchdog отслеживал только process_task. Теперь
 * любая задача может зарегистрироваться, получить handle и кормить его
 * watchdog_feed_h(handle). Отдельная задача watchdog_task раз в секунду
 * проверяет каждый клиент, при stale > stale_off_s выключает все DO,
 * при stale > stale_reboot_s — esp_restart().
 *
 * Совместимость: watchdog_feed() (без аргументов) кормит «process»
 * (handle 0), который регистрируется неявно при первом feed'e.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Невалидный handle для проверок ошибок */
#define WDT_INVALID_HANDLE   (-1)

/** Максимум клиентов watchdog'а */
#define WDT_MAX_CLIENTS      6

/**
 * @brief Helpers для передачи watchdog handle через arg в xTaskCreate.
 *
 * Поскольку валидный handle == 0 (первый зарегистрированный клиент),
 * нельзя кодировать «нет watchdog'а» как arg == NULL и одновременно
 * проверять `if (arg) ...` — handle 0 теряется. Поэтому на отправке
 * прибавляем 1, на приёме отнимаем 1; arg == NULL → handle = -1.
 *
 * Использование:
 * @code
 *   xTaskCreate(my_task, "name", STACK, WDT_HANDLE_TO_ARG(my_h), PRIO, NULL);
 *
 *   void my_task(void *arg) {
 *       int wdt_h = WDT_ARG_TO_HANDLE(arg);   // -1 если arg == NULL
 *       ...
 *       if (wdt_h >= 0) watchdog_feed_h(wdt_h);
 *   }
 * @endcode
 */
#define WDT_HANDLE_TO_ARG(h)  ((void *)(intptr_t)((h) + 1))
#define WDT_ARG_TO_HANDLE(a)  ((a) ? (int)((intptr_t)(a) - 1) : WDT_INVALID_HANDLE)

/**
 * @brief Зарегистрировать клиента сторожевого таймера.
 *
 * @param name             Имя для логов (статический литерал).
 * @param stale_off_s      Через сколько секунд stale → отключить DO.
 *                         Для критичных задач (process, io) ставьте 3.
 * @param stale_reboot_s   Через сколько секунд stale → esp_restart().
 *                         0 = не перезагружать (для не-критичных задач).
 *                         Для process — 10. Для io — 5.
 * @return handle (>= 0) или WDT_INVALID_HANDLE.
 */
int watchdog_register(const char *name, uint32_t stale_off_s, uint32_t stale_reboot_s);

/**
 * @brief Сброс watchdog'а конкретного клиента (atomic-инкремент счётчика).
 */
void watchdog_feed_h(int handle);

/**
 * @brief Совместимость: кормит клиента «process» (handle 0).
 *        Если ещё не зарегистрирован — регистрируется автоматически
 *        с порогами 3/10 секунд.
 */
void watchdog_feed(void);

/**
 * @brief FreeRTOS задача сторожевого таймера (prio 7, 2048 стек).
 *        Период проверки — 1 секунда.
 */
void watchdog_task(void *arg);

#ifdef __cplusplus
}
#endif
