/**
 * @file esp_log.h
 * @brief Stub для host-тестов: ESP_LOGx макросы
 *
 * По умолчанию — no-op. С -DTEST_VERBOSE_LOG — printf в stdout.
 */
#pragma once

#include <stdio.h>

#ifdef TEST_VERBOSE_LOG
  #define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
  #define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
  #define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
  #define ESP_LOGD(tag, fmt, ...) printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
  #define ESP_LOGE(tag, fmt, ...) ((void)0)
  #define ESP_LOGW(tag, fmt, ...) ((void)0)
  #define ESP_LOGI(tag, fmt, ...) ((void)0)
  #define ESP_LOGD(tag, fmt, ...) ((void)0)
#endif
