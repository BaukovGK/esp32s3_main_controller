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

/* Сброс флага «первый опрос пройден» — для тестирования
 * H-modbus-initial-state path'а в драйверах. */
void mock_mb_clear_first_poll(uint8_t slave_addr);

void mock_mb_set_online(uint8_t slave_addr, bool online);
