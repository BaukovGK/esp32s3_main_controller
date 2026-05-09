/**
 * @file hal_gpio.c
 * @brief DI (прямые GPIO с debounce) + DO (через I2C TCA9554)
 *
 * Phase-1 (отказоустойчивость):
 *  - Доступ к разделяемому s_do_state защищён FreeRTOS-mutex'ом.
 *    Ранее использовался portMUX_TYPE (spinlock), что вызывало DEADLOCK:
 *    внутри critical section вызывался hal_i2c_write_reg(), который сам
 *    берёт SemaphoreHandle_t — блокирующий вызов из critical section
 *    запрещён ESP-IDF.
 *  - hal_gpio_verify_do() читает OUTPUT TCA9554 и сравнивает с кэшем,
 *    при расхождении поднимает ALARM_DO_READBACK_FAIL и пытается
 *    восстановить состояние.
 */
#include "hal_gpio.h"
#include "hal_i2c.h"
#include "board_config.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "hal_gpio";

/* --- TCA9554 регистры --- */
#define TCA9554_REG_INPUT    0x00
#define TCA9554_REG_OUTPUT   0x01
#define TCA9554_REG_POLARITY 0x02
#define TCA9554_REG_CONFIG   0x03

/* --- Таблица GPIO для DI --- */
static const int s_di_gpios[BOARD_DI_COUNT] = {
    BOARD_DI1_GPIO, BOARD_DI2_GPIO, BOARD_DI3_GPIO, BOARD_DI4_GPIO,
    BOARD_DI5_GPIO, BOARD_DI6_GPIO, BOARD_DI7_GPIO, BOARD_DI8_GPIO,
};

/* --- Debounce --- */
#define DEBOUNCE_THRESHOLD  5  /* 5 x 10мс = 50мс */

static uint8_t s_debounce_cnt[BOARD_DI_COUNT];
static uint8_t s_di_stable;  /* стабилизированное состояние DI (битовая маска) */

/* --- DO кэш + mutex для read-modify-write --- */
static uint8_t           s_do_state;
static SemaphoreHandle_t s_do_lock = NULL;

#define DO_LOCK_TIMEOUT_MS  100

static inline bool do_lock_take(void)
{
    if (s_do_lock == NULL) return true;  /* до init() */
    return xSemaphoreTake(s_do_lock, pdMS_TO_TICKS(DO_LOCK_TIMEOUT_MS)) == pdTRUE;
}

static inline void do_lock_give(void)
{
    if (s_do_lock) xSemaphoreGive(s_do_lock);
}

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

    /* Mutex для защиты s_do_state */
    if (s_do_lock == NULL) {
        s_do_lock = xSemaphoreCreateMutex();
        if (s_do_lock == NULL) {
            ESP_LOGE(TAG, "Не удалось создать mutex для DO");
            return ESP_ERR_NO_MEM;
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

    /* Verify init: читаем OUTPUT обратно */
    uint8_t actual = 0xFF;
    ret = hal_i2c_read_reg(BOARD_TCA9554_ADDR, TCA9554_REG_OUTPUT, &actual);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 verify-read не удалось: %s", esp_err_to_name(ret));
        return ret;
    }
    if (actual != 0x00) {
        ESP_LOGE(TAG, "TCA9554 init verify mismatch: expected 0x00, got 0x%02X", actual);
        return ESP_ERR_INVALID_STATE;
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
    if (!do_lock_take()) {
        ESP_LOGE(TAG, "DO lock timeout (write_do) — возможно зависла шина I2C");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = hal_i2c_write_reg(BOARD_TCA9554_ADDR, TCA9554_REG_OUTPUT, mask);
    if (ret != ESP_OK) {
        /* Повтор 1 раз */
        ret = hal_i2c_write_reg(BOARD_TCA9554_ADDR, TCA9554_REG_OUTPUT, mask);
    }
    if (ret == ESP_OK) {
        s_do_state = mask;
    } else {
        ESP_LOGE(TAG, "DO запись (mask=0x%02X) ошибка: %s", mask, esp_err_to_name(ret));
    }

    do_lock_give();
    return ret;
}

esp_err_t hal_gpio_write_do_pin(uint8_t pin, bool state)
{
    if (pin < 1 || pin > BOARD_DO_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!do_lock_take()) {
        ESP_LOGE(TAG, "DO lock timeout (write_do_pin %d)", pin);
        return ESP_ERR_TIMEOUT;
    }

    uint8_t new_state = s_do_state;
    if (state) new_state |= (1 << (pin - 1));
    else       new_state &= ~(1 << (pin - 1));

    esp_err_t ret = hal_i2c_write_reg(BOARD_TCA9554_ADDR, TCA9554_REG_OUTPUT, new_state);
    if (ret != ESP_OK) {
        ret = hal_i2c_write_reg(BOARD_TCA9554_ADDR, TCA9554_REG_OUTPUT, new_state);
    }
    if (ret == ESP_OK) {
        s_do_state = new_state;
    } else {
        ESP_LOGE(TAG, "DO pin %d запись ошибка: %s", pin, esp_err_to_name(ret));
    }

    do_lock_give();
    return ret;
}

uint8_t hal_gpio_read_do_state(void)
{
    /* Atomic read uint8 — lock не нужен */
    return s_do_state;
}

bool hal_gpio_is_estop_raw(void)
{
    /* DI5 = s_di_gpios[4], инверсия: LOW = активен */
    return (gpio_get_level(s_di_gpios[4]) == 0);
}

esp_err_t hal_gpio_verify_do(void)
{
    uint8_t actual = 0;
    esp_err_t ret = hal_i2c_read_reg(BOARD_TCA9554_ADDR, TCA9554_REG_OUTPUT, &actual);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "verify_do: чтение TCA9554 не удалось: %s", esp_err_to_name(ret));
        return ret;  /* caller (process_task) поднимает аларм */
    }

    /* Считываем ожидаемое состояние под lock'ом */
    if (!do_lock_take()) {
        ESP_LOGE(TAG, "verify_do: DO lock timeout");
        return ESP_ERR_TIMEOUT;
    }
    uint8_t expected = s_do_state;
    do_lock_give();

    if (actual != expected) {
        ESP_LOGE(TAG, "DO readback mismatch! expected=0x%02X actual=0x%02X",
                 expected, actual);
        /* Попытка восстановить: записать ожидаемое */
        (void)hal_gpio_write_do(expected);
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}
