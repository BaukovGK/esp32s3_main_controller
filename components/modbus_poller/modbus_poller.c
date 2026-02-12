/**
 * @file modbus_poller.c
 * @brief Modbus RTU Master — менеджер циклического опроса
 *
 * Инициализирует esp-modbus v2 master, определяет Data Dictionary для
 * всех опрашиваемых устройств и выполняет циклический опрос по расписанию.
 */
#include "modbus_poller.h"
#include "board_config.h"

#include "esp_modbus_master.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "mb_poller";

/* --- Порог для определения offline (ошибок подряд) --- */
#define DEVICE_OFFLINE_THRESHOLD  10

/* --- Периоды опроса, мс --- */
#define MB_POLL_PERIOD_AI_MS      100
#define MB_POLL_PERIOD_FLOW_MS    1000
#define MB_POLL_PERIOD_VOL_MS     2000
#define MB_POLL_PERIOD_COND_MS    3000

/* --- Прочие таймауты --- */
#define MB_RESPONSE_TIMEOUT_MS    1000
#define MB_BUS_INIT_DELAY_MS      500
#define MB_POLL_TASK_INTERVAL_MS  10

/* --- Макрос для строковых литералов в дескрипторах --- */
#define STR(s) ((char *)(s))

/* --- Контекст esp-modbus master --- */
static void *s_master_ctx = NULL;

/* --- Внутренняя структура записи опроса --- */
typedef struct {
    modbus_cid_t cid;
    uint8_t      slave_addr;
    uint32_t     period_ms;
    int64_t      last_poll_ms;
    size_t       reg_count;     /* количество uint16_t регистров */
    uint16_t    *data_buf;
    uint32_t     error_count;
} poll_entry_t;

/* --- Буферы для хранения сырых регистров --- */
static uint16_t s_ai_data[CID_AI_REG_COUNT];
static uint16_t s_flow_data[CID_FLOW_RATE_REG_COUNT];
static uint16_t s_volume_data[CID_FLOW_VOL_REG_COUNT];
static uint16_t s_cond10_data[CID_COND10_REG_COUNT];
static uint16_t s_cond11_data[CID_COND11_REG_COUNT];

/* --- Таблица опроса --- */
static poll_entry_t s_poll_table[] = {
    { CID_AI_CHANNELS, MB_ADDR_WAVESHARE_AI, MB_POLL_PERIOD_AI_MS,   0, CID_AI_REG_COUNT,        s_ai_data,     0 },
    { CID_FLOW_RATES,  MB_ADDR_URZH2KM,     MB_POLL_PERIOD_FLOW_MS, 0, CID_FLOW_RATE_REG_COUNT, s_flow_data,   0 },
    { CID_FLOW_VOLUMES,MB_ADDR_URZH2KM,     MB_POLL_PERIOD_VOL_MS,  0, CID_FLOW_VOL_REG_COUNT,  s_volume_data, 0 },
    { CID_COND_ADDR10, MB_ADDR_SL21_201,    MB_POLL_PERIOD_COND_MS, 0, CID_COND10_REG_COUNT,    s_cond10_data, 0 },
    { CID_COND_ADDR11, MB_ADDR_SL21_101,    MB_POLL_PERIOD_COND_MS, 0, CID_COND11_REG_COUNT,    s_cond11_data, 0 },
};
#define POLL_TABLE_SIZE (sizeof(s_poll_table) / sizeof(s_poll_table[0]))

/* --- Mutex для защиты данных --- */
static SemaphoreHandle_t s_data_mutex;

/* --- Data Dictionary для esp-modbus v2 --- */
static const mb_parameter_descriptor_t s_device_params[] = {
    /* CID_AI_CHANNELS: Waveshare AI, slave 1, input regs 0x0000, 8 regs */
    {
        .cid            = CID_AI_CHANNELS,
        .param_key      = "AI_ch",
        .param_units    = "raw",
        .mb_slave_addr  = MB_ADDR_WAVESHARE_AI,
        .mb_param_type  = MB_PARAM_INPUT,
        .mb_reg_start   = 0x0000,
        .mb_size        = CID_AI_REG_COUNT,
        .param_offset   = 0,
        .param_type     = PARAM_TYPE_ASCII,
        .param_size     = CID_AI_REG_COUNT * 2,
        .param_opts     = { .opt1 = 0, .opt2 = 0, .opt3 = 0 },
        .access         = PAR_PERMS_READ
    },
    /* CID_FLOW_RATES: УРЖ2КМ расход, slave 2, holding regs 0x0000, 8 regs */
    {
        .cid            = CID_FLOW_RATES,
        .param_key      = "Flow",
        .param_units    = "raw",
        .mb_slave_addr  = MB_ADDR_URZH2KM,
        .mb_param_type  = MB_PARAM_HOLDING,
        .mb_reg_start   = 0x0000,
        .mb_size        = CID_FLOW_RATE_REG_COUNT,
        .param_offset   = 0,
        .param_type     = PARAM_TYPE_ASCII,
        .param_size     = CID_FLOW_RATE_REG_COUNT * 2,
        .param_opts     = { .opt1 = 0, .opt2 = 0, .opt3 = 0 },
        .access         = PAR_PERMS_READ
    },
    /* CID_FLOW_VOLUMES: УРЖ2КМ объём, slave 2, holding regs 0x0036, 16 regs */
    {
        .cid            = CID_FLOW_VOLUMES,
        .param_key      = "Vol",
        .param_units    = "raw",
        .mb_slave_addr  = MB_ADDR_URZH2KM,
        .mb_param_type  = MB_PARAM_HOLDING,
        .mb_reg_start   = 0x0036,
        .mb_size        = CID_FLOW_VOL_REG_COUNT,
        .param_offset   = 0,
        .param_type     = PARAM_TYPE_ASCII,
        .param_size     = CID_FLOW_VOL_REG_COUNT * 2,
        .param_opts     = { .opt1 = 0, .opt2 = 0, .opt3 = 0 },
        .access         = PAR_PERMS_READ
    },
    /* CID_COND_ADDR10: СЛ21 addr 10, holding regs 0x0001, 6 regs */
    {
        .cid            = CID_COND_ADDR10,
        .param_key      = "Cond10",
        .param_units    = "raw",
        .mb_slave_addr  = MB_ADDR_SL21_201,
        .mb_param_type  = MB_PARAM_HOLDING,
        .mb_reg_start   = 0x0001,
        .mb_size        = CID_COND10_REG_COUNT,
        .param_offset   = 0,
        .param_type     = PARAM_TYPE_ASCII,
        .param_size     = CID_COND10_REG_COUNT * 2,
        .param_opts     = { .opt1 = 0, .opt2 = 0, .opt3 = 0 },
        .access         = PAR_PERMS_READ
    },
    /* CID_COND_ADDR11: СЛ21 addr 11, holding regs 0x0001, 3 regs */
    {
        .cid            = CID_COND_ADDR11,
        .param_key      = "Cond11",
        .param_units    = "raw",
        .mb_slave_addr  = MB_ADDR_SL21_101,
        .mb_param_type  = MB_PARAM_HOLDING,
        .mb_reg_start   = 0x0001,
        .mb_size        = CID_COND11_REG_COUNT,
        .param_offset   = 0,
        .param_type     = PARAM_TYPE_ASCII,
        .param_size     = CID_COND11_REG_COUNT * 2,
        .param_opts     = { .opt1 = 0, .opt2 = 0, .opt3 = 0 },
        .access         = PAR_PERMS_READ
    },
};
#define NUM_DEVICE_PARAMS (sizeof(s_device_params) / sizeof(s_device_params[0]))

esp_err_t modbus_poller_init(void)
{
    s_data_mutex = xSemaphoreCreateMutex();
    if (s_data_mutex == NULL) {
        ESP_LOGE(TAG, "Не удалось создать mutex");
        return ESP_ERR_NO_MEM;
    }

    /* 1. Создание esp-modbus v2 serial master */
    mb_communication_info_t comm = {
        .ser_opts = {
            .mode             = MB_RTU,
            .port             = BOARD_RS485_UART_PORT,
            .uid              = 0,
            .response_tout_ms = MB_RESPONSE_TIMEOUT_MS,
            .baudrate         = BOARD_RS485_BAUDRATE,
            .data_bits        = UART_DATA_8_BITS,
            .stop_bits        = UART_STOP_BITS_1,
            .parity           = UART_PARITY_DISABLE,
        }
    };

    esp_err_t err = mbc_master_create_serial(&comm, &s_master_ctx);
    if (err != ESP_OK || s_master_ctx == NULL) {
        ESP_LOGE(TAG, "mbc_master_create_serial ошибка: %s", esp_err_to_name(err));
        return err;
    }

    /* 2. Назначение GPIO для UART (после create, до start) */
    err = uart_set_pin(BOARD_RS485_UART_PORT,
                       BOARD_RS485_TX_GPIO,
                       BOARD_RS485_RX_GPIO,
                       BOARD_RS485_RTS_GPIO,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin ошибка: %s", esp_err_to_name(err));
        return err;
    }

    /* 3. Регистрация Data Dictionary */
    err = mbc_master_set_descriptor(s_master_ctx, s_device_params, NUM_DEVICE_PARAMS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_master_set_descriptor ошибка: %s", esp_err_to_name(err));
        return err;
    }

    /* 4. RS-485 полудуплекс */
    err = uart_set_mode(BOARD_RS485_UART_PORT, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_mode ошибка: %s", esp_err_to_name(err));
        return err;
    }

    /* 5. Старт стека */
    err = mbc_master_start(s_master_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_master_start ошибка: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Modbus master инициализирован: %d CID, UART%d %d бод",
             (int)NUM_DEVICE_PARAMS, BOARD_RS485_UART_PORT, BOARD_RS485_BAUDRATE);
    return ESP_OK;
}

void modbus_poller_task(void *arg)
{
    ESP_LOGI(TAG, "Задача опроса запущена");

    /* Начальная задержка для стабилизации шины */
    vTaskDelay(pdMS_TO_TICKS(MB_BUS_INIT_DELAY_MS));

    while (1) {
        int64_t now = esp_timer_get_time() / 1000;  /* мс */

        for (int i = 0; i < POLL_TABLE_SIZE; i++) {
            poll_entry_t *entry = &s_poll_table[i];

            if ((now - entry->last_poll_ms) < entry->period_ms) {
                continue;
            }

            /* Временный буфер для чтения */
            uint8_t temp_buf[CID_FLOW_VOL_REG_COUNT * 2]; /* максимальный размер */
            uint8_t param_type = 0;

            esp_err_t err = mbc_master_get_parameter(
                s_master_ctx, entry->cid, temp_buf, &param_type);

            xSemaphoreTake(s_data_mutex, portMAX_DELAY);
            if (err == ESP_OK) {
                /* Конвертация из массива байт в uint16_t (big-endian Modbus) */
                for (size_t r = 0; r < entry->reg_count; r++) {
                    entry->data_buf[r] = (uint16_t)(temp_buf[r * 2] << 8) |
                                          temp_buf[r * 2 + 1];
                }
                if (entry->error_count > 0) {
                    ESP_LOGI(TAG, "Slave %d CID %d: связь восстановлена",
                             entry->slave_addr, entry->cid);
                }
                entry->error_count = 0;
            } else {
                entry->error_count++;
                if (entry->error_count == 1 ||
                    entry->error_count == DEVICE_OFFLINE_THRESHOLD) {
                    ESP_LOGW(TAG, "Slave %d CID %d: ошибка %s (cnt=%lu)",
                             entry->slave_addr, entry->cid,
                             esp_err_to_name(err),
                             (unsigned long)entry->error_count);
                }
            }
            xSemaphoreGive(s_data_mutex);

            entry->last_poll_ms = now;
        }

        vTaskDelay(pdMS_TO_TICKS(MB_POLL_TASK_INTERVAL_MS));
    }
}

/* --- Потокобезопасные getter-функции --- */

static esp_err_t get_raw_data(const uint16_t *src, size_t src_count,
                              uint16_t *out, size_t out_count)
{
    if (out == NULL || out_count < src_count) {
        return ESP_ERR_INVALID_SIZE;
    }
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    memcpy(out, src, src_count * sizeof(uint16_t));
    xSemaphoreGive(s_data_mutex);
    return ESP_OK;
}

esp_err_t modbus_poller_get_ai_raw(uint16_t *out, size_t count)
{
    return get_raw_data(s_ai_data, CID_AI_REG_COUNT, out, count);
}

esp_err_t modbus_poller_get_flow_raw(uint16_t *out, size_t count)
{
    return get_raw_data(s_flow_data, CID_FLOW_RATE_REG_COUNT, out, count);
}

esp_err_t modbus_poller_get_volume_raw(uint16_t *out, size_t count)
{
    return get_raw_data(s_volume_data, CID_FLOW_VOL_REG_COUNT, out, count);
}

esp_err_t modbus_poller_get_cond10_raw(uint16_t *out, size_t count)
{
    return get_raw_data(s_cond10_data, CID_COND10_REG_COUNT, out, count);
}

esp_err_t modbus_poller_get_cond11_raw(uint16_t *out, size_t count)
{
    return get_raw_data(s_cond11_data, CID_COND11_REG_COUNT, out, count);
}

/* --- Статус устройств --- */

bool modbus_poller_is_device_online(uint8_t slave_addr)
{
    uint32_t max_errors = 0;
    bool found = false;
    for (int i = 0; i < POLL_TABLE_SIZE; i++) {
        if (s_poll_table[i].slave_addr == slave_addr) {
            found = true;
            if (s_poll_table[i].error_count > max_errors) {
                max_errors = s_poll_table[i].error_count;
            }
        }
    }
    if (!found) {
        return false;
    }
    return max_errors < DEVICE_OFFLINE_THRESHOLD;
}

uint32_t modbus_poller_get_error_count(uint8_t slave_addr)
{
    uint32_t max_errors = 0;
    for (int i = 0; i < POLL_TABLE_SIZE; i++) {
        if (s_poll_table[i].slave_addr == slave_addr) {
            if (s_poll_table[i].error_count > max_errors) {
                max_errors = s_poll_table[i].error_count;
            }
        }
    }
    return max_errors;
}
