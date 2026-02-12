/**
 * @file mock_config_manager.h
 * @brief Мок: управляемая конфигурация
 */
#pragma once

#include "config_manager.h"

/* Получить mutable-указатель на тестовую конфигурацию */
plant_config_t *mock_config_get_mutable(void);

/* Установить значения по умолчанию */
void mock_config_set_defaults(void);
