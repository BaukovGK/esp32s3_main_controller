/**
 * @file hal_uart.c
 * @brief UART для RS-485 Modbus
 *
 * Минимальная инициализация. esp-modbus переконфигурирует UART
 * при вызове mbc_master_setup(), но мы настраиваем пины заранее.
 */
#include "hal_uart.h"
#include "board_config.h"

#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "hal_uart";

esp_err_t hal_uart_init(void)
{
    /* esp-modbus сам инициализирует UART драйвер.
     * Здесь только логирование конфигурации для диагностики. */
    ESP_LOGI(TAG, "RS-485 UART: порт=%d, TX=GPIO%d, RX=GPIO%d, %d бод 8N1",
             BOARD_RS485_UART_PORT,
             BOARD_RS485_TX_GPIO,
             BOARD_RS485_RX_GPIO,
             BOARD_RS485_BAUDRATE);
    return ESP_OK;
}
