/**
 * @file hal_uart.h
 * @brief UART для RS-485 Modbus
 *
 * Минимальная обёртка. esp-modbus сам управляет UART,
 * но hal_uart_init() настраивает пины GPIO для UART1.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализация UART1 для RS-485 (115200 8N1, GPIO17/18)
 * @note esp-modbus переконфигурирует UART при mbc_master_setup().
 *       Этот вызов нужен для ранней проверки аппаратуры.
 * @return ESP_OK при успехе
 */
esp_err_t hal_uart_init(void);

#ifdef __cplusplus
}
#endif
