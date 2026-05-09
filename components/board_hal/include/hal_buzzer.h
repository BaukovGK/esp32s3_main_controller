/**
 * @file hal_buzzer.h
 * @brief Простой драйвер пьезо-зуммера на GPIO (Phase-3, L-4).
 *
 * Buzzer на BOARD_BUZZER_GPIO (GPIO 46). Пассивная пищалка управляется
 * прямо через GPIO push-pull. Активные паттерны (BEEP/PULSE) реализуются
 * программным таймером — buzzer_tick() вызывается раз в 100 мс из
 * process_task'а.
 *
 * Интеграция с alarm_manager:
 *   buzzer_set_alarm_state(category) → выставляет паттерн в зависимости
 *   от категории самой опасной активной аварии:
 *     CRITICAL → CONTINUOUS (постоянный сигнал)
 *     ALARM    → SLOW (вкл 0.5 с, выкл 0.5 с)
 *     WARNING  → SHORT (короткий писк раз в 5 сек)
 *     INFO/нет → OFF
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUZZER_PATTERN_OFF = 0,
    BUZZER_PATTERN_SHORT,        /* 100мс ВКЛ, 5 с тишины */
    BUZZER_PATTERN_SLOW,         /* 500мс ВКЛ / 500мс ВЫКЛ */
    BUZZER_PATTERN_CONTINUOUS,   /* постоянный сигнал */
} buzzer_pattern_t;

esp_err_t hal_buzzer_init(void);

/**
 * @brief Установить паттерн.
 * @note Не вызывает GPIO напрямую — это делает hal_buzzer_tick().
 */
void hal_buzzer_set_pattern(buzzer_pattern_t p);

/**
 * @brief Один шаг автомата паттернов. Вызывать раз в 100 мс.
 */
void hal_buzzer_tick(void);

/** @brief Принудительная остановка (например, по кнопке "silence"). */
void hal_buzzer_silence(void);

#ifdef __cplusplus
}
#endif
