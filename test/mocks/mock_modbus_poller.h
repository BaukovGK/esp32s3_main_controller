/**
 * @file mock_modbus_poller.h
 * @brief Мок Modbus-poller'а для тестирования драйверов (analog/flow/cond).
 *
 * Тесты задают сырые регистры через mock_mb_set_*(), затем вызывают
 * <driver>_update(), читают результаты через <driver>_get_*().
 *
 * Phase-5 (H-modbus-initial-state): mock_mb_set_*() помечает «первый
 * опрос пройден» для соответствующего CID. После mock_mb_reset() ни одна
 * запись не помечена → raw-геттеры возвращают ESP_ERR_INVALID_STATE и
 * is_device_online() возвращает false (точно как production).
 */
#pragma once

#include "modbus_poller.h"
#include <stddef.h>

void mock_mb_reset(void);

void mock_mb_set_ai(const uint16_t *data, size_t count);
void mock_mb_set_flow(const uint16_t *data, size_t count);
void mock_mb_set_volume(const uint16_t *data, size_t count);
void mock_mb_set_cond10(const uint16_t *data, size_t count);
void mock_mb_set_cond11(const uint16_t *data, size_t count);
void mock_mb_set_kws_lp(const uint16_t *data, size_t count);
void mock_mb_set_kws_hp(const uint16_t *data, size_t count);

/* Сброс флага «первый опрос пройден» — для тестирования
 * H-modbus-initial-state path'а в драйверах. */
void mock_mb_clear_first_poll(uint8_t slave_addr);

void mock_mb_set_online(uint8_t slave_addr, bool online);

/* === Health-check helpers (mb_device_check) === */

/**
 * @brief Задать буфер «отвечающих» holding-регистров для конкретного
 *        slave/start_addr. modbus_poller_read_holding() с совпадающим
 *        slave_addr возвращает скопированные значения, иначе — ESP_ERR_TIMEOUT.
 *
 * Если addr попадает в окно [base..base+count-1] — возвращается копия из
 * буфера. Если не задано буфера — ESP_ERR_TIMEOUT.
 */
void mock_mb_set_holding(uint8_t slave_addr, uint16_t base_addr,
                         const uint16_t *data, size_t count);

/**
 * @brief Полностью забыть отклики на read_holding (моделируется неответ).
 */
void mock_mb_clear_holding(void);

/**
 * @brief Получить число вызовов write_holding с момента последнего
 *        mock_mb_clear_writes(). Используется для проверки что
 *        автокоррекция действительно записала корректное значение.
 */
int mock_mb_get_write_count(void);

/**
 * @brief Получить параметры последнего write_holding вызова.
 */
void mock_mb_get_last_write(uint8_t *slave_out, uint16_t *addr_out,
                            uint16_t *data_out, size_t *count_out,
                            size_t data_max);

/**
 * @brief Сбросить счётчик write'ов и last-write буфер.
 *        Также: после mock_mb_set_holding() запись через write_holding
 *        автоматически обновит соответствующий read-буфер — это нужно для
 *        теста «после автокоррекции повторное чтение возвращает 0x0003».
 */
void mock_mb_clear_writes(void);

/**
 * @brief Заставить следующий N-ный read_holding вернуть указанную ошибку.
 *        Используется для тестирования путей восстановления.
 */
void mock_mb_set_read_error(esp_err_t err);
