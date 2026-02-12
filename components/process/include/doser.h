/**
 * @file doser.h
 * @brief Таймер дозатора антискаланта
 *
 * Состояния: OFF → RUNNING → PAUSE → (цикл).
 * Активен только в AUTO при doser_enabled.
 * Управляет RO5 (BOARD_DO_DOSER).
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOSER_OFF,
    DOSER_RUNNING,
    DOSER_PAUSE
} doser_state_t;

void doser_init(void);

/**
 * @brief Обновить состояние дозатора (вызывать каждые 100мс)
 * @param auto_running  true = установка в режиме AUTO_RUNNING
 */
void doser_update(bool auto_running);

doser_state_t doser_get_state(void);
void doser_enable(bool en);
bool doser_is_enabled(void);

#ifdef __cplusplus
}
#endif
