/**
 * @file watchdog_task.h
 * @brief Сторожевой таймер процесса
 *
 * Проверяет активность ProcessTask. Если счётчик циклов
 * не обновляется 3 секунды — аварийное отключение всех выходов.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief FreeRTOS задача сторожевого таймера (prio 7, 2048 стек)
 */
void watchdog_task(void *arg);

/**
 * @brief Сброс сторожевого таймера — вызывается из ProcessTask каждый цикл
 */
void watchdog_feed(void);

#ifdef __cplusplus
}
#endif
