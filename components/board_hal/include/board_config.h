/**
 * @file board_config.h
 * @brief Распиновка и константы платы Waveshare ESP32-S3-ETH-8DI-8RO
 */
#pragma once

/* --- Дискретные входы: прямые GPIO, инвертированная логика (оптоизоляция) --- */
#define BOARD_DI1_GPIO      4
#define BOARD_DI2_GPIO      5
#define BOARD_DI3_GPIO      6
#define BOARD_DI4_GPIO      7
#define BOARD_DI5_GPIO      8
#define BOARD_DI6_GPIO      9
#define BOARD_DI7_GPIO      10
#define BOARD_DI8_GPIO      11
#define BOARD_DI_COUNT      8

/* --- Релейные выходы: через I2C-расширитель TCA9554PWR --- */
#define BOARD_TCA9554_ADDR  0x20
#define BOARD_DO_COUNT      8

/* --- I2C шина (TCA9554 + RTC PCF85063) --- */
#define BOARD_I2C_SDA_GPIO  42
#define BOARD_I2C_SCL_GPIO  41
#define BOARD_I2C_FREQ_HZ   100000
#define BOARD_I2C_PORT      0   /* I2C_NUM_0 */

/* --- RS-485 UART --- */
#define BOARD_RS485_TX_GPIO     17
#define BOARD_RS485_RX_GPIO     18
#define BOARD_RS485_UART_PORT   1   /* UART_NUM_1 */
#define BOARD_RS485_BAUDRATE    115200
#define BOARD_RS485_RTS_GPIO    (-1)  /* UART_PIN_NO_CHANGE, авто-направление */

/* --- Modbus slave адреса --- */
#define MB_ADDR_WAVESHARE_AI    1
#define MB_ADDR_URZH2KM        2
#define MB_ADDR_SL21_201       10
#define MB_ADDR_SL21_101       11

/* --- Назначение дискретных выходов (RO1-RO8) --- */
#define BOARD_DO_PUMP_FEED    1   /* RO1: насос подачи */
#define BOARD_DO_PUMP_STAGE1  2   /* RO2: насос 1-й ступени */
#define BOARD_DO_PUMP_STAGE2  3   /* RO3: насос 2-й ступени */
#define BOARD_DO_HEATER       4   /* RO4: ТЭН */
#define BOARD_DO_DOSER        5   /* RO5: дозатор */

/* --- Назначение дискретных входов (DI1-DI8) --- */
#define BOARD_DI_SOURCE_EMPTY   1   /* DI1: источник пуст (NC, 0=пуст) */
#define BOARD_DI_INTERM_EMPTY   2   /* DI2: промбак пуст (NC, 0=пуст) */
#define BOARD_DI_INTERM_FULL    3   /* DI3: промбак полон (NO, 1=полон) */
#define BOARD_DI_PERMEATE_FULL  4   /* DI4: бак пермеата полон (NO, 1=полон) */
#define BOARD_DI_ESTOP          5   /* DI5: аварийный стоп (NC, 0=авария) */
#define BOARD_DI_PUMP1_CONF     6   /* DI6: подтверждение насоса подачи */
#define BOARD_DI_PUMP2_CONF     7   /* DI7: подтверждение насоса 1-й ст. */
#define BOARD_DI_PUMP3_CONF     8   /* DI8: подтверждение насоса 2-й ст. */

/* --- W5500 Ethernet SPI --- */
#define BOARD_ETH_SPI_MOSI_GPIO     13
#define BOARD_ETH_SPI_MISO_GPIO     14
#define BOARD_ETH_SPI_SCLK_GPIO     15
#define BOARD_ETH_SPI_CS_GPIO       16
#define BOARD_ETH_SPI_INT_GPIO      12
#define BOARD_ETH_SPI_HOST          1   /* SPI2_HOST */
#define BOARD_ETH_SPI_CLOCK_MHZ     16

/* --- Прочая периферия --- */
#define BOARD_BUZZER_GPIO       46
#define BOARD_RGB_LED_GPIO      38
