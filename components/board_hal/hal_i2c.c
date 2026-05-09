/**
 * @file hal_i2c.c
 * @brief I2C master шина — потокобезопасная обёртка
 */
#include "hal_i2c.h"
#include "board_config.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "hal_i2c";

#define I2C_GLITCH_IGNORE_CNT     7
#define I2C_TIMEOUT_MS            100   /* timeout одной транзакции */
#define I2C_BUS_LOCK_TIMEOUT_MS   200   /* timeout захвата мьютекса шины */

static i2c_master_bus_handle_t s_bus_handle;
static SemaphoreHandle_t s_mutex;

esp_err_t hal_i2c_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Не удалось создать mutex");
        return ESP_ERR_NO_MEM;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = I2C_GLITCH_IGNORE_CNT,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации I2C шины: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C шина инициализирована (SDA=%d, SCL=%d, %d Hz)",
             BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO, BOARD_I2C_FREQ_HZ);
    return ESP_OK;
}

/* Захват мьютекса шины с конечным таймаутом.
 * При таймауте — ESP_LOGE; вызывающий слой (process_task / state_machine)
 * получит ESP_ERR_TIMEOUT и поднимет аларм. HAL не зависит от alarm_manager
 * (избегаем циклической зависимости services ↔ board_hal). */
static bool i2c_bus_lock(const char *who)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(I2C_BUS_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "I2C bus lock timeout (%s) — возможно зависла шина", who ? who : "?");
        return false;
    }
    return true;
}

esp_err_t hal_i2c_write_reg(uint8_t dev_addr, uint8_t reg, uint8_t data)
{
    /* Добавляем устройство на шину (кэширование в будущем, сейчас каждый раз) */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };

    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret;

    if (!i2c_bus_lock("write_reg")) return ESP_ERR_TIMEOUT;

    ret = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка добавления I2C устройства 0x%02X: %s", dev_addr, esp_err_to_name(ret));
        xSemaphoreGive(s_mutex);
        return ret;
    }

    uint8_t buf[2] = { reg, data };
    ret = i2c_master_transmit(dev_handle, buf, sizeof(buf), I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C запись 0x%02X reg 0x%02X: %s", dev_addr, reg, esp_err_to_name(ret));
    }

    i2c_master_bus_rm_device(dev_handle);
    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t hal_i2c_read_reg(uint8_t dev_addr, uint8_t reg, uint8_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };

    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret;

    if (!i2c_bus_lock("read_reg")) return ESP_ERR_TIMEOUT;

    ret = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка добавления I2C устройства 0x%02X: %s", dev_addr, esp_err_to_name(ret));
        xSemaphoreGive(s_mutex);
        return ret;
    }

    ret = i2c_master_transmit_receive(dev_handle, &reg, 1, data, 1, I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C чтение 0x%02X reg 0x%02X: %s", dev_addr, reg, esp_err_to_name(ret));
    }

    i2c_master_bus_rm_device(dev_handle);
    xSemaphoreGive(s_mutex);
    return ret;
}
