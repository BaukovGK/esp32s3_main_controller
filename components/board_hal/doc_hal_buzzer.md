# hal_buzzer — Пьезо-зуммер аварийной сигнализации (Phase-3, L-4)

## Описание

Модуль `hal_buzzer` управляет пьезо-зуммером, подключённым к `BOARD_BUZZER_GPIO` (GPIO 46). Реализует автомат программных паттернов: BEEP/PULSE формируется в `process_task` без аппаратного PWM/RMT, через периодический tick.

Звук — стандарт промышленной автоматики: оператор слышит активную аварию даже если не смотрит на экран. До Phase-3 buzzer был объявлен в board_config.h, но не использовался.

**Файлы:**
- Заголовочный: `components/board_hal/include/hal_buzzer.h`
- Реализация: `components/board_hal/hal_buzzer.c`

## Зависимости

| Заголовок | Назначение |
|---|---|
| `hal_buzzer.h` | Собственный заголовок |
| `board_config.h` | `BOARD_BUZZER_GPIO` (46) |
| `driver/gpio.h` | `gpio_config`, `gpio_set_level` |
| `esp_log.h` | Логирование |

## Паттерны (`buzzer_pattern_t`)

| Значение | Описание | Применение |
|---|---|---|
| `BUZZER_PATTERN_OFF` | Выключен | Нет активных аварий |
| `BUZZER_PATTERN_SHORT` | 100 мс ВКЛ + 4900 мс ВЫКЛ (раз в 5 сек) | WARNING |
| `BUZZER_PATTERN_SLOW` | 500 мс ВКЛ / 500 мс ВЫКЛ | ALARM |
| `BUZZER_PATTERN_CONTINUOUS` | Постоянный сигнал | CRITICAL |

## API

### `esp_err_t hal_buzzer_init(void)`
Конфигурирует GPIO как PUSH-PULL output, устанавливает в 0. Soft-fail: ошибка не валит систему — `app_main` продолжит без звука.

### `void hal_buzzer_set_pattern(buzzer_pattern_t p)`
Устанавливает паттерн. Сбрасывает фазу автомата (новый паттерн начинает играть с нуля).

### `void hal_buzzer_tick(void)`
**Вызывать раз в 100 мс** из `process_task`. Один шаг автомата паттернов: инкрементирует фазу, переключает GPIO.

### `void hal_buzzer_silence(void)`
Принудительная остановка. Используется кнопкой "silence" (если будет добавлена) или при reset_fault.

## Интеграция

`process_task` каждый цикл:
1. Вызывает `hal_buzzer_tick()` (быстрый, без поиска по аварийному списку).
2. Раз в 5 секунд (вместе с heap-check) вызывает `buzzer_pattern_for_active_alarms()` — ищет максимальную категорию по активным авариям и обновляет паттерн через `hal_buzzer_set_pattern()`.

```c
buzzer_pattern_for_active_alarms() →
    if (active CRITICAL) → CONTINUOUS
    if (active ALARM)    → SLOW
    if (active WARNING)  → SHORT
    else                 → OFF
```

## Не реализовано (TODO для Phase-4)

- **Hardware silence button**: кнопка на 1 из DI-входов для немедленного отключения паттерна без ожидания reset_fault.
- **RGB LED индикация**: BOARD_RGB_LED_GPIO (GPIO 38) — вероятно WS2812. Цвет по состоянию SM (зелёный=AUTO_RUNNING, синий=transitions, красный=FAULT). Требует led_strip компонент ESP-IDF.
- **MQTT-команда silence**: `ro_plant/command/silence` для удалённого глушения через Home Assistant.
