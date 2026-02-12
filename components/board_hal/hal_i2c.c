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
        .glitch_ignore_cnt = 7,
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

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    ret = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка добавления I2C устройства 0x%02X: %s", dev_addr, esp_err_to_name(ret));
        xSemaphoreGive(s_mutex);
        return ret;
    }

    uint8_t buf[2] = { reg, data };
    ret = i2c_master_transmit(dev_handle, buf, sizeof(buf), 100);
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

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    ret = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка добавления I2C устройства 0x%02X: %s", dev_addr, esp_err_to_name(ret));
        xSemaphoreGive(s_mutex);
        return ret;
    }

    ret = i2c_master_transmit_receive(dev_handle, &reg, 1, data, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C чтение 0x%02X reg 0x%02X: %s", dev_addr, reg, esp_err_to_name(ret));
    }

    i2c_master_bus_rm_device(dev_handle);
    xSemaphoreGive(s_mutex);
    return ret;
}
