/**
 * @file hal_gpio.c
 * @brief DI (прямые GPIO с debounce) + DO (через I2C TCA9554)
 */
#include "hal_gpio.h"
#include "hal_i2c.h"
#include "board_config.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "hal_gpio";

/* --- TCA9554 регистры --- */
#define TCA9554_REG_INPUT   0x00
#define TCA9554_REG_OUTPUT  0x01
#define TCA9554_REG_POLARITY 0x02
#define TCA9554_REG_CONFIG  0x03

/* --- Таблица GPIO для DI --- */
static const int s_di_gpios[BOARD_DI_COUNT] = {
    BOARD_DI1_GPIO, BOARD_DI2_GPIO, BOARD_DI3_GPIO, BOARD_DI4_GPIO,
    BOARD_DI5_GPIO, BOARD_DI6_GPIO, BOARD_DI7_GPIO, BOARD_DI8_GPIO,
};

/* --- Debounce --- */
#define DEBOUNCE_THRESHOLD  5  /* 5 × 10мс = 50мс */

static uint8_t s_debounce_cnt[BOARD_DI_COUNT];
static uint8_t s_di_stable;  /* стабилизированное состояние DI (битовая маска) */

/* --- DO кэш --- */
static uint8_t s_do_state;

esp_err_t hal_gpio_init(void)
{
    /* Инициализация DI как входов с подтяжкой */
    for (int i = 0; i < BOARD_DI_COUNT; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << s_di_gpios[i]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Ошибка конфигурации DI%d (GPIO%d): %s",
                     i + 1, s_di_gpios[i], esp_err_to_name(ret));
            return ret;
        }
    }

    /* Инициализация DO: TCA9554 — все пины = выход, все выключены */
    esp_err_t ret = hal_i2c_write_reg(BOARD_TCA9554_ADDR, TCA9554_REG_OUTPUT, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 (0x%02X) не отвечает — невозможно управлять DO!",
                 BOARD_TCA9554_ADDR);
        return ret;
    }
    ret = hal_i2c_write_reg(BOARD_TCA9554_ADDR, TCA9554_REG_CONFIG, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 ошибка записи регистра конфигурации");
        return ret;
    }
    s_do_state = 0x00;

    /* Начальное чтение DI */
    s_di_stable = 0;
    for (int i = 0; i < BOARD_DI_COUNT; i++) {
        s_debounce_cnt[i] = 0;
    }

    ESP_LOGI(TAG, "GPIO инициализированы: %d DI (GPIO), %d DO (TCA9554 I2C 0x%02X)",
             BOARD_DI_COUNT, BOARD_DO_COUNT, BOARD_TCA9554_ADDR);
    return ESP_OK;
}

void hal_gpio_debounce_process(void)
{
    for (int i = 0; i < BOARD_DI_COUNT; i++) {
        /* Чтение raw GPIO. Логика инвертирована: LOW = активен (1) */
        int raw = gpio_get_level(s_di_gpios[i]);
        bool active = (raw == 0);  /* инверсия */

        bool currently_stable = (s_di_stable >> i) & 1;

        if (active != currently_stable) {
            s_debounce_cnt[i]++;
            if (s_debounce_cnt[i] >= DEBOUNCE_THRESHOLD) {
                /* Состояние стабилизировалось */
                if (active) {
                    s_di_stable |= (1 << i);
                } else {
                    s_di_stable &= ~(1 << i);
                }
                s_debounce_cnt[i] = 0;
            }
        } else {
            s_debounce_cnt[i] = 0;
        }
    }
}

uint8_t hal_gpio_read_di(void)
{
    return s_di_stable;
}

bool hal_gpio_read_di_pin(uint8_t pin)
{
    if (pin < 1 || pin > BOARD_DI_COUNT) {
        return false;
    }
    return (s_di_stable >> (pin - 1)) & 1;
}

esp_err_t hal_gpio_write_do(uint8_t mask)
{
    esp_err_t ret = hal_i2c_write_reg(BOARD_TCA9554_ADDR, TCA9554_REG_OUTPUT, mask);
    if (ret == ESP_OK) {
        s_do_state = mask;
    } else {
        /* Повтор 1 раз */
        ESP_LOGW(TAG, "DO запись повтор...");
        ret = hal_i2c_write_reg(BOARD_TCA9554_ADDR, TCA9554_REG_OUTPUT, mask);
        if (ret == ESP_OK) {
            s_do_state = mask;
        } else {
            ESP_LOGE(TAG, "DO запись ошибка: %s", esp_err_to_name(ret));
        }
    }
    return ret;
}

esp_err_t hal_gpio_write_do_pin(uint8_t pin, bool state)
{
    if (pin < 1 || pin > BOARD_DO_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t new_state = s_do_state;
    if (state) {
        new_state |= (1 << (pin - 1));
    } else {
        new_state &= ~(1 << (pin - 1));
    }
    return hal_gpio_write_do(new_state);
}

uint8_t hal_gpio_read_do_state(void)
{
    return s_do_state;
}

bool hal_gpio_is_estop_raw(void)
{
    /* DI5 = s_di_gpios[4], инверсия: LOW = активен */
    return (gpio_get_level(s_di_gpios[4]) == 0);
}
