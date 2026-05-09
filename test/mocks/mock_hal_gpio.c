/**
 * @file mock_hal_gpio.c
 * @brief Мок: DI inject + DO spy
 */
#include "mock_hal_gpio.h"
#include "esp_err.h"
#include "board_config.h"

static uint8_t s_di_value = 0xFF;  /* Все DI=1 (NC замкнуты = норма) */
static uint8_t s_do_state = 0x00;

/* === Реализация production API === */

esp_err_t hal_gpio_init(void)
{
    return ESP_OK;
}

uint8_t hal_gpio_read_di(void)
{
    return s_di_value;
}

bool hal_gpio_read_di_pin(uint8_t pin)
{
    if (pin < 1 || pin > 8) return false;
    return (s_di_value >> (pin - 1)) & 1;
}

esp_err_t hal_gpio_write_do(uint8_t mask)
{
    s_do_state = mask;
    return ESP_OK;
}

esp_err_t hal_gpio_write_do_pin(uint8_t pin, bool state)
{
    if (pin < 1 || pin > 8) return ESP_ERR_INVALID_ARG;
    if (state)
        s_do_state |= (1 << (pin - 1));
    else
        s_do_state &= ~(1 << (pin - 1));
    return ESP_OK;
}

uint8_t hal_gpio_read_do_state(void)
{
    return s_do_state;
}

void hal_gpio_debounce_process(void)
{
    /* no-op */
}

bool hal_gpio_is_estop_raw(void)
{
    /* DI5 = E-STOP, NC: 0 = авария */
    return !(s_di_value & (1 << (BOARD_DI_ESTOP - 1)));
}

/* === Управление из тестов === */

void mock_hal_gpio_set_di(uint8_t val)
{
    s_di_value = val;
}

uint8_t mock_hal_gpio_get_do(void)
{
    return s_do_state;
}

bool mock_hal_gpio_get_do_pin(uint8_t pin)
{
    if (pin < 1 || pin > 8) return false;
    return (s_do_state >> (pin - 1)) & 1;
}

void mock_hal_gpio_reset(void)
{
    s_di_value = 0xFF;
    s_do_state = 0x00;
}
