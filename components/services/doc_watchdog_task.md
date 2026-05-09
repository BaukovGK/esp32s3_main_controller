# watchdog_task -- Программный сторожевой таймер для нескольких задач

## Описание

Модуль `watchdog_task` реализует **программный watchdog**, отслеживающий работоспособность нескольких FreeRTOS-задач одновременно (Phase-1, K-5).

**До Phase-1** отслеживалась только `process_task`. Это означало:
- зависание `io_task` (raw E-STOP) → не детектируется,
- зависание `modbus_poller_task` → все датчики «замерзают», все NaN-проверки проходят, защита по давлению отключается.

**После Phase-1**:
- любая задача может зарегистрироваться через `watchdog_register()`,
- получает целочисленный handle,
- при каждом полезном цикле вызывает `watchdog_feed_h(handle)`,
- если счётчик клиента не меняется N секунд → отключение DO; M секунд → `esp_restart()`.

Поведение при зависании (per-client пороги):

| Клиент   | stale_off_s | stale_reboot_s | Назначение |
|----------|-------------|----------------|-----------|
| process  | 3           | 10             | Главный цикл управления |
| io       | 3           | 5              | Debounce DI + raw E-STOP — критично |
| modbus   | 15          | 0              | Polling RS-485 — некритично, без рестарта |

**Файлы:**
- Заголовочный: `include/watchdog_task.h`
- Реализация: `watchdog_task.c`

---

## Зависимости

| Заголовок               | Назначение                                               |
|-------------------------|----------------------------------------------------------|
| `watchdog_task.h`       | Собственный заголовочный файл                            |
| `hal_gpio.h`            | Аварийное отключение DO при stale (`hal_gpio_write_do(0)`) |
| `esp_log.h`             | Логирование ESP-IDF                                      |
| `esp_system.h`          | `esp_restart()` для рестарта                             |
| `freertos/FreeRTOS.h`   | Ядро FreeRTOS                                            |
| `freertos/task.h`       | `vTaskDelay()`                                           |
| `freertos/semphr.h`     | Mutex регистрации                                        |
| `<stdatomic.h>`         | Atomic-инкремент счётчиков feed                          |

---

## Константы

| Константа              | Значение | Описание                                                  |
|------------------------|----------|-----------------------------------------------------------|
| `WDT_CHECK_INTERVAL_MS`| 1000     | Период проверки клиентов watchdog'ом                      |
| `WDT_MAX_CLIENTS`      | 6        | Максимум зарегистрированных клиентов                      |
| `WDT_INVALID_HANDLE`   | -1       | Возвращается из `watchdog_register` при ошибке            |

---

## Структура клиента (внутренняя)

```c
typedef struct {
    const char *name;
    atomic_uint_fast32_t counter;  // инкрементируется feed'ом
    uint32_t   last_seen;          // последнее значение counter
    uint32_t   stale_off_s;        // секунд stale → DO=0
    uint32_t   stale_reboot_s;     // секунд stale → esp_restart()
    int        stale_count;
    bool       in_use;
} wdt_client_t;
```

Массив `s_clients[WDT_MAX_CLIENTS]` глобален. Регистрация `watchdog_register()` сериализуется через mutex `s_reg_lock`. Feed (`watchdog_feed_h`) — без блокировок (атомарный инкремент), быстрый и безопасный из любого контекста.

---

## Публичные функции

### `watchdog_register`

```c
int watchdog_register(const char *name, uint32_t stale_off_s, uint32_t stale_reboot_s);
```

**Описание:** Регистрирует нового клиента, возвращает handle (>=0) либо `WDT_INVALID_HANDLE`.

| Параметр | Назначение |
|---|---|
| `name` | Имя для логов (статический литерал, не копируется). |
| `stale_off_s` | Через сколько секунд stale → `hal_gpio_write_do(0x00)`. Для критичных задач 3. |
| `stale_reboot_s` | Через сколько секунд stale → `esp_restart()`. 0 = не перезагружать. |

**Вызов:** ДО `xTaskCreate` соответствующей задачи. Handle передаётся задаче через arg.

---

### `watchdog_feed_h`

```c
void watchdog_feed_h(int handle);
```

**Описание:** Кормит клиента с указанным handle. Невалидный handle игнорируется. Атомарный инкремент `counter` без mutex.

---

### `watchdog_feed`

```c
void watchdog_feed(void);
```

**Совместимость**: кормит handle 0 («process»). Если ещё не зарегистрирован — регистрируется автоматически с порогами 3/10 секунд. Используется в legacy-коде через `process_task`.

---

### `watchdog_task`

```c
void watchdog_task(void *arg);
```

**Точка входа FreeRTOS-задачи**, prio 7, стек 2K. Алгоритм:

1. `vTaskDelay(1 сек)`.
2. Для каждого `in_use` клиента:
   - Снять `cur = atomic_load(counter)`.
   - Если `cur == last_seen`: `stale_count++`.
     - При `stale_count == stale_off_s`: лог + `hal_gpio_write_do(0x00)`.
     - При `stale_count >= stale_reboot_s` (если `>0`): `esp_restart()`.
   - Иначе: лог восстановления (если был alarm), `stale_count = 0`, `last_seen = cur`.

---

## Пример использования (`app_main.c`)

Handle передаётся через `arg` в `xTaskCreate`. Поскольку валидный handle == 0
(первый зарегистрированный клиент), нельзя кодировать «нет watchdog'а» как
`arg == NULL` и одновременно проверять `if (arg) ...` — handle 0 потеряется.
Поэтому в `watchdog_task.h` определены два макроса:

```c
#define WDT_HANDLE_TO_ARG(h)  ((void *)(intptr_t)((h) + 1))
#define WDT_ARG_TO_HANDLE(a)  ((a) ? (int)((intptr_t)(a) - 1) : WDT_INVALID_HANDLE)
```

На отправке прибавляем 1, на приёме отнимаем 1. `arg == NULL` →
`handle = WDT_INVALID_HANDLE` (-1), задача может уйти в legacy fallback.

```c
int wdt_process = watchdog_register("process", 3, 10);
int wdt_io      = watchdog_register("io",      3,  5);
int wdt_modbus  = watchdog_register("modbus", 15,  0);

xTaskCreate(modbus_poller_task, "modbus", ..., WDT_HANDLE_TO_ARG(wdt_modbus), ...);
xTaskCreate(io_task,            "io",     ..., WDT_HANDLE_TO_ARG(wdt_io),     ...);
xTaskCreate(process_task,       "process",..., WDT_HANDLE_TO_ARG(wdt_process),...);
xTaskCreate(watchdog_task,      "watchdog",...,  NULL,                         ...);
```

В задаче:
```c
void process_task(void *arg) {
    int h = WDT_ARG_TO_HANDLE(arg);   // -1 если arg == NULL
    while (1) {
        ... работа ...
        if (h >= 0) watchdog_feed_h(h);
        vTaskDelay(...);
    }
}
```

## Thread-safety регистрации (C-8)

`register_internal` сначала полностью заполняет slot (`name`, `counter=0`,
`last_seen=0`, пороги, `stale_count=0`, `in_use=true`) и только потом
публикует его через `atomic_store(&s_count, cur+1, memory_order_release)`.

`watchdog_task` и `watchdog_feed_h` читают `s_count` через
`atomic_load(..., memory_order_acquire)`. Acquire/release-парность
гарантирует: если поток видит инкремент `s_count`, он также видит
все записи в slot, выполненные до release-store. Это исключает гонку,
при которой watchdog_task мог бы прочитать частично заполненный slot.

---

## Поведение при отказах разных задач

| Зависшая задача   | Через 3 с | Через 5 с | Через 10 с | Через 15 с |
|-------------------|-----------|-----------|------------|------------|
| process_task      | DO=0      | —         | restart    | —          |
| io_task           | DO=0      | restart   | —          | —          |
| modbus_poller_task| —         | —         | —          | DO=0       |

`modbus` без рестарта потому, что отказ Modbus сам по себе не требует панического перезапуска: алармы `ALARM_MODBUS_OFFLINE` поднимаются из `process_task`, а `analog_input_get_value` начинает возвращать NaN — сработают `INTERLOCK_SENSOR_FAULT_*`.
