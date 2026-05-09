/**
 * @file hal_rgb.h
 * @brief Драйвер сигнальной RGB-LED (WS2812 на BOARD_RGB_LED_GPIO).
 *
 * Phase-4 (L-4 расширение): визуальная индикация состояния установки.
 * Используется led_strip managed компонент ESP-IDF — если на плате стоит
 * другая модель LED, нужно адаптировать реализацию.
 *
 * Цветовая схема (по приоритету: критичные аварии перекрывают состояние SM):
 *   КРАСНЫЙ мигающий  — CRITICAL alarm (E-STOP, перегрев, DO mismatch)
 *   КРАСНЫЙ           — FAULT
 *   ЖЁЛТЫЙ            — ALARM (превышение давления, sensor fault)
 *   ОРАНЖЕВЫЙ мигающий — ожидание оператора (WASH_WAIT_*)
 *   СИНИЙ             — WASHING (активные подфазы)
 *   ГОЛУБОЙ           — AUTO transitions (RAMP, FILLING)
 *   ЗЕЛЁНЫЙ           — AUTO_RUNNING
 *   БЕЛЫЙ тусклый     — IDLE
 *   ФИОЛЕТОВЫЙ        — MANUAL
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hal_rgb_init(void);

/**
 * @brief Установить цвет напрямую.
 * @param r,g,b 0..255
 * @param blink true — мигание примерно 1 Гц (вкл 500мс / выкл 500мс)
 */
void hal_rgb_set(uint8_t r, uint8_t g, uint8_t b, bool blink);

/**
 * @brief Тик автомата мигания. Вызывать раз в 100мс из process_task.
 */
void hal_rgb_tick(void);

/** @brief Полное выключение. */
void hal_rgb_off(void);

#ifdef __cplusplus
}
#endif
