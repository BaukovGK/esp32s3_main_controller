/**
 * @file process_task.h
 * @brief Главная задача процесса (100мс, prio 5, 8192 стек)
 *
 * Цикл:
 *  1. Обновление драйверов (AI, расходомер, кондуктометр)
 *  2. Обновление КА (интерлоки внутри)
 *  3. Обновление дозатора
 *  4. Обновление телеметрии
 *  5. Сброс watchdog
 *  6. Периодический лог (5с)
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief FreeRTOS задача главного процесса
 */
void process_task(void *arg);

#ifdef __cplusplus
}
#endif
