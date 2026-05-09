/**
 * @file mb_device_check.h
 * @brief Старт-ап health check всех Modbus-устройств RO-установки.
 *
 * После инициализации `modbus_poller_init()` и нескольких циклов опроса
 * (~3–5 сек на стабилизацию шины и заполнение snapshot'ов драйверов) этот
 * модуль обходит все 6 устройств:
 *
 *   - Waveshare Analog Input 8CH (slave 1)  — FW version, device addr,
 *     channel modes (одноразовый перевод в 4–20mA при необходимости);
 *   - УРЖ2КМ (slave 2)                       — sanity-диапазон расходов;
 *   - СЛ21 #10 / #11 (slaves 10, 11)         — sanity проводимости и t°;
 *   - KWS-306L LP / HP (slaves 20, 21)       — sanity напряжения / t°.
 *
 * Размещён в компоненте `drivers`, поскольку использует `flowmeter.h`,
 * `conductivity.h`, `power_meter.h` для получения уже отконвертированных
 * snapshot'ов (драйверы исполняют word-swap, scale и валидацию). Прямая
 * запись/чтение Waveshare-холдингов идёт через
 * `modbus_poller_read_holding/_write_holding`.
 *
 * Все нарушения публикуются как алармы (см. блок ALARM_DEV_CHECK_*,
 * ALARM_AI_*, ALARM_SL21_*, ALARM_URZH_*, ALARM_KWS_RANGE_OOR в
 * alarm_manager.h). Это диагностика, не interlock — установка продолжает
 * работать. Оператор по журналу видит, нужно ли проверить прибор.
 *
 * Закрывает гэп-анализ #3 (см. doc/modbus_signal_map.md §7) — one-time
 * setup Waveshare AI в режим 4–20mA.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Запустить health-check всех Modbus-устройств.
 *
 * Не блокирует основной поток на длинный период — каждое устройство
 * проверяется с таймаутом стека esp-modbus (по умолчанию 300 мс), на
 * неответ → пропуск с warn-логом. Идемпотентен — можно вызвать повторно
 * (например, после реконфигурации шины).
 *
 * @retval ESP_OK     все устройства прошли проверку
 * @retval ESP_FAIL   хотя бы одно устройство с проблемой (alarm поднят)
 */
esp_err_t mb_device_check_run(void);

#ifdef __cplusplus
}
#endif
