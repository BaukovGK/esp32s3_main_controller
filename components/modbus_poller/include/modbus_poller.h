/**
 * @file modbus_poller.h
 * @brief Modbus RTU Master — менеджер циклического опроса
 *
 * Использует esp-modbus компонент для RTU Master.
 * Опрашивает устройства по расписанию, хранит сырые данные регистров.
 * Потокобезопасный доступ к данным через getter-функции.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Идентификаторы наборов регистров (CID) --- */
typedef enum {
    CID_AI_CHANNELS = 0,    /* Waveshare AI: slave 1, input regs 0x0000-0x0007 */
    CID_FLOW_RATES,         /* УРЖ2КМ расход: slave 2, holding regs 0x0000-0x0007 */
    CID_FLOW_VOLUMES,       /* УРЖ2КМ объём: slave 2, holding regs 0x0036-0x003D */
    CID_COND_ADDR10,        /* СЛ21 addr 10: holding regs 0x0001-0x0006 (X1+t1+X2+t2) */
    CID_COND_ADDR11,        /* СЛ21 addr 11: holding regs 0x0001-0x0006 (X1+t1+X2+t2) */
    CID_KWS_PUMP_LP,        /* KWS-306L slave 20: holding regs 0x000E-0x001B (НД-насос) */
    CID_KWS_PUMP_HP,        /* KWS-306L slave 21: holding regs 0x000E-0x001B (ВД-насос) */
    /* Health-check (mb_device_check.c): опрашиваются однократно через
     * modbus_poller_read_holding/_write_holding, в s_poll_table[] не входят. */
    CID_AI_MODES,           /* Waveshare AI: holding 0x1000..0x1007 (RW) — режимы каналов */
    CID_AI_VERSION,         /* Waveshare AI: holding 0x8000 (R) — FW version */
    CID_AI_DEV_ADDR,        /* Waveshare AI: holding 0x4000 (R) — device address */
    CID_COUNT
} modbus_cid_t;

/* --- Размеры буферов (в uint16_t регистрах) --- */
#define CID_AI_REG_COUNT        8
#define CID_FLOW_RATE_REG_COUNT 8
#define CID_FLOW_VOL_REG_COUNT  8   /* было 16; V5..V8 не сверены с протоколом ТЕСС, до сверки игнорируем */
#define CID_COND10_REG_COUNT    6
/* CID_COND11: расширено с 3 до 6 регистров (2026-05-09) — теперь читаем
 * обе ячейки X1/X2 и оба термодатчика t1/t2 второго блока СЛ21. Это
 * даёт 4-й логический канал проводимости COND_CH_CONC. */
#define CID_COND11_REG_COUNT    6
/* KWS-306L: 14 регистров одним блоком 0x000E..0x001B (U/I/P/E/Temp).
 * Дырки между известными регистрами держим из-за неизвестных промежуточных
 * значений — TODO опросить полный блок 0x0000..0x0040 при первом подключении.
 * Возможно, energy на самом деле uint32 в 0x001A+0x001B (тогда temperature
 * в другом регистре — нужен datasheet KWS-306L). */
#define CID_KWS_REG_COUNT       14

/* --- Health-check блоки (mb_device_check) --- */
#define CID_AI_MODES_COUNT       8   /* 0x1000..0x1007: режимы 8 каналов AI */
#define CID_AI_VERSION_COUNT     1   /* 0x8000: FW version (BCD V*100 + v) */
#define CID_AI_DEV_ADDR_COUNT    1   /* 0x4000: device address (1..247) */

/**
 * @brief Инициализация esp-modbus master и регистрация параметров
 * @return ESP_OK при успехе
 */
esp_err_t modbus_poller_init(void);

/**
 * @brief FreeRTOS задача циклического опроса
 * @param arg не используется
 */
void modbus_poller_task(void *arg);

/**
 * @brief Получить сырые данные аналоговых входов (Waveshare AI, slave 1)
 *
 * Phase-5 (C-9/C-10/H-modbus-initial-state): на любом ошибочном пути
 * выходной буфер зануляется ДО проверок. Caller безопасно может читать
 * out даже при ESP_ERR_*. Лок берётся на ≤50 мс, чтобы readers не висли
 * при зависании poll task'а.
 *
 * @param out буфер для CID_AI_REG_COUNT регистров (зануляется на входе)
 * @param count размер буфера в uint16_t (должен быть >= CID_AI_REG_COUNT)
 * @retval ESP_OK              данные актуальны
 * @retval ESP_ERR_INVALID_SIZE буфер мал
 * @retval ESP_ERR_TIMEOUT     не удалось взять лок данных за 50 мс
 * @retval ESP_ERR_INVALID_STATE до сих пор не было ни одного успешного опроса
 * @retval ESP_ERR_NOT_FOUND   CID не найден в таблице (внутренняя ошибка)
 */
esp_err_t modbus_poller_get_ai_raw(uint16_t *out, size_t count);

/**
 * @brief Получить сырые регистры расхода (УРЖ2КМ, slave 2)
 * @see modbus_poller_get_ai_raw для семантики ошибок.
 */
esp_err_t modbus_poller_get_flow_raw(uint16_t *out, size_t count);

/**
 * @brief Получить сырые регистры объёма (УРЖ2КМ, slave 2)
 * @see modbus_poller_get_ai_raw для семантики ошибок.
 */
esp_err_t modbus_poller_get_volume_raw(uint16_t *out, size_t count);

/**
 * @brief Получить сырые регистры кондуктометра (СЛ21, addr 10)
 * @see modbus_poller_get_ai_raw для семантики ошибок.
 */
esp_err_t modbus_poller_get_cond10_raw(uint16_t *out, size_t count);

/**
 * @brief Получить сырые регистры кондуктометра (СЛ21, addr 11)
 * @see modbus_poller_get_ai_raw для семантики ошибок.
 */
esp_err_t modbus_poller_get_cond11_raw(uint16_t *out, size_t count);

/**
 * @brief Получить сырые регистры счётчика KWS-306L НД-насоса (slave 20)
 *        Блок 14 регистров 0x000E..0x001B.
 * @see modbus_poller_get_ai_raw для семантики ошибок.
 */
esp_err_t modbus_poller_get_kws_lp_raw(uint16_t *out, size_t count);

/**
 * @brief Получить сырые регистры счётчика KWS-306L ВД-насоса (slave 21)
 *        Блок 14 регистров 0x000E..0x001B.
 * @see modbus_poller_get_ai_raw для семантики ошибок.
 */
esp_err_t modbus_poller_get_kws_hp_raw(uint16_t *out, size_t count);

/**
 * @brief Проверить доступность Modbus-устройства
 *
 * Phase-5 (C-9/H-modbus-initial-state): возвращает false до первого
 * успешного опроса (нельзя путать «никогда не отвечал» с «отвечает
 * нулями»). На таймаут лока — также false (consider unsafe by default).
 *
 * @param slave_addr адрес устройства
 * @return true если устройство было успешно опрошено хотя бы раз и
 *         текущий error_count ниже порога offline.
 */
bool modbus_poller_is_device_online(uint8_t slave_addr);

/**
 * @brief Получить счётчик ошибок для устройства
 *
 * Phase-5 (C-9): чтение под локом; на таймаут / неизвестный slave /
 * до первого опроса — возвращает 0 (нет данных об ошибках).
 *
 * @param slave_addr адрес устройства
 * @return количество ошибок подряд (0 = нет ошибок или нет данных)
 */
uint32_t modbus_poller_get_error_count(uint8_t slave_addr);

/**
 * @brief Phase-4 (M-6): получить список уникальных адресов опрашиваемых
 *        устройств. Используется в diagnostics — раньше адреса дублировались
 *        жёстко прописанным массивом.
 *
 * @param out      буфер для адресов
 * @param max_cnt  размер буфера
 * @return         фактическое количество записанных адресов
 */
size_t modbus_poller_get_slave_addrs(uint8_t *out, size_t max_cnt);

/**
 * @brief Однократное чтение holding-регистров (FC 0x03) у произвольного slave.
 *
 * Используется health-check'ом (`mb_device_check.c`) для нерегулярных
 * запросов вне циклического опроса (FW version, device addr, channel modes).
 * Под капотом обращается к `mbc_master_send_request()` esp-modbus v2; стек
 * сериализует доступ к шине, поэтому конкуренции с poll task'ом не возникает.
 *
 * @param slave  адрес устройства (1..247)
 * @param addr   стартовый адрес регистра
 * @param out    буфер ≥ count uint16_t (host-endian, БЕЗ word-swap)
 * @param count  количество регистров (1..125)
 * @retval ESP_OK              успех
 * @retval ESP_ERR_INVALID_ARG out==NULL или count==0
 * @retval ESP_ERR_INVALID_STATE стек ещё не инициализирован
 * @retval ESP_ERR_TIMEOUT     не получен ответ за MB_RESPONSE_TIMEOUT_MS
 * @retval ESP_FAIL            slave вернул exception или другая ошибка протокола
 */
esp_err_t modbus_poller_read_holding(uint8_t slave, uint16_t addr,
                                     uint16_t *out, size_t count);

/**
 * @brief Однократная запись holding-регистров.
 *
 * При count==1 использует FC 0x06 (Write Single Register), иначе FC 0x10
 * (Write Multiple Registers). Применяется для one-time setup устройств
 * (например, перевод Waveshare AI в режим 4–20 mA).
 *
 * @param slave  адрес устройства
 * @param addr   стартовый адрес
 * @param data   буфер ≥ count uint16_t (host-endian)
 * @param count  количество регистров (1..123 для FC 0x10)
 * @retval ESP_OK / ESP_ERR_*  как у `modbus_poller_read_holding`.
 */
esp_err_t modbus_poller_write_holding(uint8_t slave, uint16_t addr,
                                      const uint16_t *data, size_t count);

#ifdef __cplusplus
}
#endif
