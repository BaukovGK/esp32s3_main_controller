# process_task.h / process_task.c

## Описание модуля

Модуль реализует **главную задачу управления процессом** (ProcessTask) -- центральный цикл, который координирует все подсистемы установки обратного осмоса. Задача выполняется с периодом **100 мс** и последовательно обновляет: драйверы датчиков, конечный автомат, дозатор, телеметрию, watchdog. Также выполняет периодическое логирование состояния системы каждые 5 секунд.

ProcessTask является единственным потребителем модулей `state_machine`, `doser`, `telemetry` и обновления датчиков. Все эти модули вызываются синхронно из одного контекста, что исключает необходимость дополнительной синхронизации между ними.

**Файлы:**
- `components/process/include/process_task.h` -- публичный заголовок (объявление задачи)
- `components/process/process_task.c` -- реализация цикла

**Параметры FreeRTOS задачи** (из комментария в заголовке):
- Период: 100 мс
- Приоритет: 5
- Размер стека: 8192 байт

---

## Зависимости (включаемые заголовки)

### process_task.h
Не имеет зависимостей (только стандартные директивы `extern "C"`).

### process_task.c
| Заголовок | Назначение |
|---|---|
| `"process_task.h"` | Собственный публичный заголовок |
| `"analog_input.h"` | Обновление и чтение аналоговых входов (давления P1-P4, температура T) |
| `"flowmeter.h"` | Обновление и чтение расходомеров |
| `"conductivity.h"` | Обновление и чтение кондуктометров |
| `"state_machine.h"` | Конечный автомат установки |
| `"doser.h"` | Управление дозатором антискаланта |
| `"telemetry.h"` | Обновление телеметрии (вычисляемые параметры) |
| `"watchdog_task.h"` | Сброс сторожевого таймера |
| `"hal_gpio.h"` | **Phase-1 (K-2)**: `hal_gpio_verify_do()` для readback TCA9554 |
| `"modbus_poller.h"` | **Phase-1 (K-8)**: `modbus_poller_is_device_online()` для контроля связи |
| `"alarm_manager.h"` | **Phase-1**: подъём `ALARM_DO_READBACK_FAIL`, `ALARM_I2C_BUS_HUNG`, `ALARM_MODBUS_OFFLINE` |
| `"board_config.h"` | **Phase-1**: Modbus-адреса устройств для проверки online |
| `"esp_log.h"` | Логирование ESP-IDF |
| `"freertos/FreeRTOS.h"` | API FreeRTOS |
| `"freertos/task.h"` | `vTaskDelay`, `pdMS_TO_TICKS` |
| `<math.h>` | `isnan` |

---

## Константы (#define)

| Константа | Значение | Описание |
|---|---|---|
| `PROCESS_CYCLE_MS` | `100` | Период основного цикла задачи, миллисекунды |
| `LOG_PERIOD_CYCLES` | `50` | Период логирования состояния системы в циклах обновления. При периоде цикла `PROCESS_CYCLE_MS` (100 мс) это составляет 50 * 100 = 5000 мс = **5 секунд**. |
| `VERIFY_DO_PERIOD` | `10` | **Phase-1 (K-2)**: период вызова `hal_gpio_verify_do()` в циклах. 10 циклов = 1 сек. |
| `MB_CHECK_PERIOD` | `30` | **Phase-1 (K-8)**: период проверки online-статуса Modbus-устройств. 30 циклов = 3 сек. |
| `HEAP_CHECK_PERIOD` | `50` | **Phase-3 (M-3)**: период мониторинга heap. 50 циклов = 5 сек. |
| `HEAP_LOW_THRESHOLD` | `16384` | **Phase-3 (M-3)**: порог `ALARM_LOW_HEAP`. При free_heap ниже — поднимаем аларм. |
| `HEAP_OK_HYSTERESIS` | `20480` | **Phase-3 (M-3)**: порог снятия аларма (с гистерезисом 4 КБ — без него аларм флапал бы у границы). |

---

## Внутренние статические переменные (process_task.c)

| Переменная | Тип | Описание |
|---|---|---|
| `TAG` | `const char *` | Тег для ESP-IDF логирования: `"process"` |

**Локальные переменные внутри `process_task`:**

| Переменная | Тип | Описание |
|---|---|---|
| `log_counter` | `int` | Счётчик циклов для периодического логирования. Инкрементируется каждый цикл, сбрасывается в 0 при достижении `LOG_PERIOD_CYCLES`. |

---

## Функции

### Внутренние (static) функции

#### `static const char *sm_state_str(sm_state_t st)`

**Сигнатура:**
```c
static const char *sm_state_str(sm_state_t st);
```

**Описание:** Преобразование номера состояния КА в строку для логирования.

**Параметры:**

| Параметр | Тип | Описание |
|---|---|---|
| `st` | `sm_state_t` | Состояние конечного автомата |

**Возвращаемое значение:** Указатель на строковый литерал: `"IDLE"`, `"AUTO"`, `"WASH"`, `"MANUAL"`, `"FAULT"` или `"?"` для невалидного значения.

**Примечание:** Эта функция дублирует `state_name()` из `state_machine.c`, но с сокращённым именем `"WASH"` вместо `"WASHING"`.

---

### Публичные функции

#### `void process_task(void *arg)`

**Сигнатура:**
```c
void process_task(void *arg);
```

**Описание:** Точка входа FreeRTOS задачи главного процесса. Содержит бесконечный цикл с периодом `PROCESS_CYCLE_MS` (100 мс), который последовательно обновляет все подсистемы установки.

**Параметры:**

| Параметр | Тип | Описание |
|---|---|---|
| `arg` | `void *` | Аргумент FreeRTOS задачи (не используется). |

**Возвращаемое значение:** Не возвращает (бесконечный цикл).

**Алгоритм основного цикла (каждые `PROCESS_CYCLE_MS` мс):**

1. **Обновление драйверов датчиков:**
   - `analog_input_update()` -- опрос аналоговых входов через Modbus (давления P1-P4, температура T).
   - `flowmeter_update()` -- обновление расходомеров.
   - `conductivity_update()` -- обновление кондуктометров.

2. **Обновление конечного автомата:**
   - `state_machine_update()` -- внутри вызывается `interlocks_check()`, обрабатывается текущее состояние, формируются и применяются выходы.

3. **Обновление дозатора:**
   - Получение текущего статуса КА через `state_machine_get_status()`.
   - Вычисление флага `auto_running`: `true` если `state == SM_AUTO` И `auto_sub == AUTO_RUNNING`.
   - `doser_update(auto_running)` -- обновление циклического таймера дозатора.
   - **TODO**: дозирование промывочного реагента в WASHING — см. README.md.

4. **Обновление телеметрии:**
   - `telemetry_update()` -- пересчёт вычисляемых параметров (перепад давления, процент рекуперации, селективность).

5. **Сброс watchdog:**
   - **Phase-1 (K-5)**: handle watchdog'а передаётся через `arg` задачи. `watchdog_feed_h(handle)` если valid; иначе fallback на legacy `watchdog_feed()`.

6. **Phase-1 (K-2): readback TCA9554 раз в секунду** (`VERIFY_DO_PERIOD = 10` циклов):
   - `hal_gpio_verify_do()` читает регистр OUTPUT TCA9554, сравнивает с `s_do_state`.
   - При `ESP_ERR_INVALID_STATE` (расхождение): `alarm_raise(ALARM_DO_READBACK_FAIL, ALARM_CAT_CRITICAL, 0)`.
   - При другой ошибке (I2C bus): `alarm_raise(ALARM_I2C_BUS_HUNG, ALARM_CAT_CRITICAL, 0)`.

7.4. **Phase-3 (L-4): tick buzzer'а** — каждый цикл `hal_buzzer_tick()` для воспроизведения паттерна (BEEP/PULSE).

7.5. **Phase-3 (M-3): мониторинг heap раз в 5 секунд** (`HEAP_CHECK_PERIOD = 50` циклов):
   - `esp_get_free_heap_size()`. При `< HEAP_LOW_THRESHOLD` (16 КБ) — `alarm_raise(ALARM_LOW_HEAP, WARNING, free_heap)`.
   - При восстановлении `> HEAP_OK_HYSTERESIS` (20 КБ) — `alarm_clear(ALARM_LOW_HEAP)`. Гистерезис 4 КБ предотвращает флаппинг.
   - **Phase-3 (L-4)**: одновременно пересчитывается паттерн buzzer'а через `buzzer_pattern_for_active_alarms()`.

7.6. **Phase-1 (K-8): контроль Modbus-связи раз в 3 секунды** (`MB_CHECK_PERIOD = 30` циклов):
   - Для каждого из 4 Modbus-адресов (Waveshare AI, УРЖ2КМ, СЛ21×2):
     - При переходе online → offline: `alarm_raise(ALARM_MODBUS_OFFLINE, ALARM_CAT_ALARM, slave_addr)`.
     - При переходе offline → online: `alarm_clear(ALARM_MODBUS_OFFLINE)`.
   - Раньше алармы `ALARM_MODBUS_OFFLINE` были объявлены в enum, но никогда не поднимались.

8. **Периодическое логирование (каждые 5 секунд):**
   - Инкремент `log_counter`. При достижении `LOG_PERIOD_CYCLES` (50):
     - Сброс счётчика.
     - Чтение аналоговых входов: P1 (канал 0), P2 (канал 1), P3 (канал 2), P4 (канал 3), T (канал 4).
     - Вывод лога с текущим состоянием КА и давлениями/температурой. NaN-значения заменяются на 0.
     - Чтение расходомеров: Q1 (канал 0), Q3 (канал 2).
     - Чтение кондуктометров: sigma1 (канал 0), sigma2 (канал 1), sigma3 (канал 2).
     - Вывод лога с расходами и электропроводностью.
     - Чтение телеметрии через `telemetry_get()`.
     - Вывод лога с перепадом давления на фильтре (`filter_dp`), процентом рекуперации (`system_recovery_pct`), селективностью 1-й и 2-й ступени.

9. **Ожидание:**
   - `vTaskDelay(pdMS_TO_TICKS(PROCESS_CYCLE_MS))` -- задержка до следующего цикла.

---

## Формат лога

Каждые 5 секунд выводятся три строки:

```
I (timestamp) process: [STATE] P1=x.xx P2=x.xx P3=x.x P4=x.xx T=x.x°C
I (timestamp) process:   Q1=x.xxx Q3=x.xxx sigma1=xxx sigma2=xxx sigma3=xxx µS/cm
I (timestamp) process:   dP=x.xx rec=x.x% sel1=x.x% sel2=x.x%
```

Где:
- `STATE` -- текущее состояние КА (`IDLE`, `AUTO`, `WASH`, `MANUAL`, `FAULT`)
- `P1`..`P4` -- давления в барах
- `T` -- температура в градусах Цельсия
- `Q1`, `Q3` -- расходы (м3/ч или л/мин в зависимости от конфигурации)
- `sigma1`..`sigma3` -- электропроводность в µS/cm (микросименс на сантиметр)
- `dP` -- перепад давления на фильтре (P1 - P2), бар
- `rec` -- процент рекуперации системы (%)
- `sel1`, `sel2` -- селективность 1-й и 2-й ступени (%)

---

## Диаграмма цикла

```
  +---> analog_input_update()
  |           |
  |           v
  |     flowmeter_update()
  |           |
  |           v
  |     conductivity_update()
  |           |
  |           v
  |     state_machine_update()     <-- включает interlocks_check()
  |           |                        и управление DO
  |           v
  |     doser_update(auto_running)
  |           |
  |           v
  |     telemetry_update()
  |           |
  |           v
  |     watchdog_feed()
  |           |
  |           v
  |     [лог каждые 50 циклов]
  |           |
  |           v
  |     vTaskDelay(PROCESS_CYCLE_MS)
  |           |
  +-----------+
```

## Взаимодействие с другими модулями

```
  +-------------------+
  |   process_task    |    Вызывает каждые PROCESS_CYCLE_MS мс:
  +-------------------+
       |   |   |   |   |   |
       v   |   |   |   |   v
  analog   |   |   |   | watchdog
  _input   |   |   |   |
           v   |   |   |
       flowmeter  |   |
               v   |   |
          conduct. |   |
                   v   |
           state_machine
            (interlocks)
                       v
                     doser
                       |
                       v
                   telemetry
```

**Примечание:** Все модули вызываются из одной задачи FreeRTOS, что гарантирует отсутствие гонок данных между ними. Межзадачная синхронизация (спинлоки) используется только в `state_machine` для команд от MQTT/HTTP.
