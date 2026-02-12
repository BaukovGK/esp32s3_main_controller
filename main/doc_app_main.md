# app_main.c — Точка входа контроллера установки обратного осмоса

## Описание файла

Файл `app_main.c` является главной точкой входа прошивки контроллера установки обратного осмоса на базе ESP32-S3. Выполняет:

- Последовательную инициализацию всех аппаратных абстракций (HAL): NVS, I2C, GPIO, UART
- Инициализацию Ethernet (W5500 по SPI) и TCP/IP стека
- Инициализацию Modbus RTU Master для опроса периферийных устройств
- Инициализацию менеджера конфигурации, драйверов датчиков и логики процесса
- Запуск FreeRTOS-задач: Modbus-опрос, ввод/вывод, управление процессом, watchdog
- Запуск HTTP-сервера с REST API и веб-интерфейсом
- Условный запуск MQTT-клиента

**Файл реализации:** `main/app_main.c`

---

## Зависимости (включаемые заголовки)

| Заголовок | Назначение |
|-----------|------------|
| `<stdio.h>` | Стандартный ввод/вывод (не используется явно) |
| `<string.h>` | Строковые функции (не используется явно в данном файле) |
| `freertos/FreeRTOS.h` | FreeRTOS: `xTaskCreate()`, `vTaskDelay()`, `pdMS_TO_TICKS()` |
| `freertos/task.h` | FreeRTOS задачи: `TaskHandle_t` |
| `esp_netif.h` | Сетевой интерфейс ESP-IDF: `esp_netif_init()`, `esp_netif_new()`, `esp_netif_attach()` |
| `esp_eth.h` | Ethernet API: `esp_eth_start()`, `esp_eth_ioctl()`, `esp_eth_new_netif_glue()` |
| `esp_event.h` | Система событий: `esp_event_loop_create_default()`, `esp_event_handler_register()` |
| `esp_log.h` | Макросы логирования |
| `esp_check.h` | Макросы проверки ошибок: `ESP_ERROR_CHECK()`, `ESP_RETURN_ON_ERROR()` |
| `board_config.h` | Конфигурация платы (GPIO, адреса устройств, параметры шин) |
| `hal_nvs.h` | HAL NVS: `hal_nvs_init()` |
| `hal_i2c.h` | HAL I2C: `hal_i2c_init()` |
| `hal_gpio.h` | HAL GPIO: `hal_gpio_init()`, `hal_gpio_read_di()`, `hal_gpio_write_do()`, `hal_gpio_is_estop_raw()`, `hal_gpio_debounce_process()`, `hal_gpio_read_do_state()` |
| `hal_uart.h` | HAL UART: `hal_uart_init()` |
| `modbus_poller.h` | Modbus Master: `modbus_poller_init()`, `modbus_poller_task()` |
| `ethernet_init.h` | Инициализация Ethernet: `example_eth_init()` |
| `config_manager.h` | Менеджер конфигурации: `config_manager_init()`, `config_manager_get()` |
| `analog_input.h` | Аналоговые входы: `analog_input_init()` |
| `flowmeter.h` | Расходомеры: `flowmeter_init()` |
| `conductivity.h` | Кондуктометры: `conductivity_init()` |
| `interlocks.h` | Блокировки: `interlocks_init()` |
| `state_machine.h` | Конечный автомат: `state_machine_init()` |
| `doser.h` | Дозатор: `doser_init()` |
| `telemetry.h` | Телеметрия: `telemetry_init()` |
| `process_task.h` | Задача управления процессом: `process_task()` |
| `watchdog_task.h` | Задача watchdog: `watchdog_task()` |
| `web_server.h` | HTTP-сервер: `web_server_start()` |
| `alarm_manager.h` | Менеджер аварий: `alarm_manager_init()` |
| `diagnostics.h` | Диагностика: `diagnostics_register_task()` |
| `mqtt_app.h` | MQTT-клиент: `mqtt_app_start()` |

---

## Константы и макросы

### Параметры задач FreeRTOS

| Константа            | Значение | Описание                                       |
|----------------------|----------|------------------------------------------------|
| `TASK_MODBUS_STACK`  | 4096     | Размер стека задачи Modbus, байт               |
| `TASK_MODBUS_PRIO`   | 6        | Приоритет задачи Modbus                        |
| `TASK_IO_STACK`      | 2048     | Размер стека задачи IO, байт                   |
| `TASK_IO_PRIO`       | 6        | Приоритет задачи IO                            |
| `TASK_PROCESS_STACK` | 8192     | Размер стека задачи Process, байт              |
| `TASK_PROCESS_PRIO`  | 5        | Приоритет задачи Process                       |
| `TASK_WDT_STACK`     | 2048     | Размер стека задачи Watchdog, байт             |
| `TASK_WDT_PRIO`      | 7        | Приоритет задачи Watchdog                      |
| `IO_TASK_CYCLE_MS`   | 10       | Период цикла задачи IO (debounce + E-STOP), мс |

---

## Типы данных (typedef / struct)

В данном файле не определены пользовательские типы данных.

---

## Внутренние статические переменные

| Переменная | Тип | Описание |
|------------|-----|----------|
| `TAG` | `const char *` | Тег логирования, значение `"app_main"` |

---

## Функции

### `static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)`

**Область видимости:** внутренняя (static)

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `arg` | `void *` | Пользовательские данные (не используются, `NULL`) |
| `event_base` | `esp_event_base_t` | База событий (всегда `ETH_EVENT`) |
| `event_id` | `int32_t` | Идентификатор события |
| `event_data` | `void *` | Данные события (указатель на `esp_eth_handle_t`) |

**Возвращаемое значение:** нет (`void`)

**Описание:**

Обработчик событий Ethernet-драйвера. Регистрируется для событий `ETH_EVENT` с фильтром `ESP_EVENT_ANY_ID`. Обрабатывает следующие события:

| Событие | Действие |
|---------|----------|
| `ETHERNET_EVENT_CONNECTED` | Получает MAC-адрес через `esp_eth_ioctl(ETH_CMD_G_MAC_ADDR)` и логирует его в формате `XX:XX:XX:XX:XX:XX` |
| `ETHERNET_EVENT_DISCONNECTED` | Логирует предупреждение об отключении |
| `ETHERNET_EVENT_START` | Логирует информацию о запуске |
| `ETHERNET_EVENT_STOP` | Логирует информацию об остановке |

---

### `static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)`

**Область видимости:** внутренняя (static)

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `arg` | `void *` | Пользовательские данные (не используются, `NULL`) |
| `event_base` | `esp_event_base_t` | База событий (всегда `IP_EVENT`) |
| `event_id` | `int32_t` | Идентификатор события (всегда `IP_EVENT_ETH_GOT_IP`) |
| `event_data` | `void *` | Данные события (указатель на `ip_event_got_ip_t`) |

**Возвращаемое значение:** нет (`void`)

**Описание:**

Обработчик события получения IP-адреса через DHCP или статическую конфигурацию. Извлекает структуру `ip_event_got_ip_t` из `event_data` и логирует:
- IP-адрес
- Маску подсети
- Адрес шлюза

Форматирование осуществляется через макросы `IPSTR` и `IP2STR()`.

---

### `static void io_task(void *arg)`

**Область видимости:** внутренняя (static)

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `arg` | `void *` | Не используется (стандартная сигнатура задачи FreeRTOS) |

**Возвращаемое значение:** нет (бесконечный цикл)

**Описание:**

FreeRTOS-задача обработки дискретного ввода/вывода. Работает с периодом `IO_TASK_CYCLE_MS` (10 мс). Выполняет три основные функции:

1. **Быстрая проверка E-STOP (аварийная остановка):**
   - Вызывает `hal_gpio_is_estop_raw()` для прямой проверки состояния кнопки аварийной остановки **без debounce** (для минимальной задержки реакции < 10 мс).
   - При активном E-STOP немедленно сбрасывает все дискретные выходы в 0: `hal_gpio_write_do(0x00)`.

2. **Debounce обработка DI:**
   - Вызывает `hal_gpio_debounce_process()` для подавления дребезга контактов дискретных входов.

3. **Логирование изменений DI:**
   - Считывает текущие дискретные входы через `hal_gpio_read_di()`.
   - Сравнивает с предыдущим значением (хранится в локальной переменной `prev_di`).
   - При изменении логирует старое и новое значение в шестнадцатеричном формате.

**Локальные переменные:**

| Переменная | Тип | Описание |
|------------|-----|----------|
| `prev_di` | `uint8_t` | Предыдущее значение дискретных входов (для обнаружения изменений). Инициализируется 0. |

---

### `static esp_err_t init_ethernet(void)`

**Область видимости:** внутренняя (static)

**Параметры:** нет

**Возвращаемое значение:**
- `ESP_OK` — Ethernet инициализирован и запущен
- Код ошибки — при сбое на любом этапе

**Описание:**

Выполняет полную инициализацию Ethernet-подсистемы:

1. **Инициализация Ethernet-драйверов:** Вызывает `example_eth_init()` для создания драйверов (W5500 по SPI). Получает массив дескрипторов и их количество.

2. **Инициализация TCP/IP стека:** Вызывает `esp_netif_init()` для инициализации сетевого стека ESP-IDF.

3. **Создание цикла событий:** Вызывает `esp_event_loop_create_default()` для создания системного цикла событий.

4. **Создание сетевого интерфейса:** Для первого Ethernet-порта:
   - Создаёт конфигурацию `ESP_NETIF_DEFAULT_ETH()`
   - Создаёт объект netif через `esp_netif_new()`
   - Создаёт glue-объект через `esp_eth_new_netif_glue()` для связи Ethernet-драйвера с netif
   - Привязывает их через `esp_netif_attach()`

5. **Регистрация обработчиков событий:**
   - `ETH_EVENT` (все ID) -> `eth_event_handler`
   - `IP_EVENT` (`IP_EVENT_ETH_GOT_IP`) -> `got_ip_event_handler`

6. **Запуск Ethernet-драйверов:** Вызывает `esp_eth_start()` для каждого дескриптора.

При ошибке на любом этапе возвращает код ошибки через макрос `ESP_RETURN_ON_ERROR()`.

---

### `void app_main(void)`

**Область видимости:** публичная (точка входа ESP-IDF)

**Параметры:** нет

**Возвращаемое значение:** нет (`void`)

**Описание:**

Главная функция приложения, вызываемая FreeRTOS после загрузки. Выполняет последовательную инициализацию всех подсистем контроллера:

**Этап 1: NVS (Non-Volatile Storage)**
```c
ESP_ERROR_CHECK(hal_nvs_init());
```
Инициализация энергонезависимого хранилища для сохранения конфигурации.

**Этап 2: I2C шина**
```c
ESP_ERROR_CHECK(hal_i2c_init());
```
Инициализация I2C-шины для коммуникации с расширителем портов TCA9554 (дискретные выходы DO) и RTC (часы реального времени).

**Этап 3: GPIO**
```c
ESP_ERROR_CHECK(hal_gpio_init());
```
Инициализация дискретных входов (DI, прямые GPIO) и выходов (DO, через TCA9554 по I2C).

**Этап 4: UART RS-485**
```c
ESP_ERROR_CHECK(hal_uart_init());
```
Инициализация UART для связи по RS-485 с Modbus-устройствами.

**Этап 5: Ethernet**
```c
ESP_ERROR_CHECK(init_ethernet());
```
Инициализация Ethernet W5500 по SPI, TCP/IP стека, обработчиков событий.

**Этап 6: Modbus Master**
```c
ESP_ERROR_CHECK(modbus_poller_init());
```
Инициализация Modbus RTU Master, регистрация Data Dictionary.

**Этап 7: Конфигурация**
```c
ESP_ERROR_CHECK(config_manager_init());
```
Загрузка конфигурации из NVS или установка значений по умолчанию.

**Этап 8: Драйверы датчиков**
```c
analog_input_init();
flowmeter_init();
conductivity_init();
```
Инициализация драйверов: аналоговые входы (давления P1-P4, температура T), расходомеры (Q1-Q4), кондуктометры (sigma1-sigma3).

**Этап 9: Логика процесса**
```c
interlocks_init();
state_machine_init();
doser_init();
telemetry_init();
```
Инициализация: блокировки безопасности, конечный автомат процесса, дозатор антискаланта, модуль расчёта телеметрии.

**Этап 9.5: Менеджер аварий**
```c
ESP_ERROR_CHECK(alarm_manager_init());
```
Инициализация подсистемы управления авариями.

**Этап 10: Запуск FreeRTOS-задач**

| Задача | Функция | Имя | Стек | Приоритет | Описание |
|--------|---------|-----|------|-----------|----------|
| Modbus | `modbus_poller_task` | `"modbus"` | `TASK_MODBUS_STACK` (4096) байт | `TASK_MODBUS_PRIO` (6) | Циклический опрос Modbus-устройств |
| IO | `io_task` | `"io"` | `TASK_IO_STACK` (2048) байт | `TASK_IO_PRIO` (6) | Обработка DI/DO, E-STOP, debounce |
| Process | `process_task` | `"process"` | `TASK_PROCESS_STACK` (8192) байт | `TASK_PROCESS_PRIO` (5) | Основная логика управления процессом |
| Watchdog | `watchdog_task` | `"watchdog"` | `TASK_WDT_STACK` (2048) байт | `TASK_WDT_PRIO` (7) | Контроль работоспособности |

Каждая задача после создания регистрируется в модуле диагностики через `diagnostics_register_task()` для мониторинга свободного стека.

**Этап 11: HTTP-сервер**
```c
ESP_ERROR_CHECK(web_server_start());
```
Запуск HTTP-сервера с REST API и раздачей статических файлов веб-интерфейса.

**Этап 12: MQTT-клиент (условный)**
```c
if (config_manager_get()->mqtt.enabled) {
    ESP_ERROR_CHECK(mqtt_app_start());
}
```
MQTT-клиент запускается только если он включён в конфигурации (`mqtt.enabled`). Публикует телеметрию на настроенный брокер.

---

## Порядок приоритетов задач

| Приоритет | Константа | Задача | Обоснование |
|-----------|-----------|--------|-------------|
| 7 (высший) | `TASK_WDT_PRIO` | `watchdog` | Контроль работоспособности должен выполняться без задержек |
| 6 | `TASK_MODBUS_PRIO` | `modbus` | Своевременный опрос датчиков критичен для безопасности |
| 6 | `TASK_IO_PRIO` | `io` | Обработка E-STOP и DI/DO требует минимальной задержки |
| 5 | `TASK_PROCESS_PRIO` | `process` | Логика управления может допускать небольшие задержки |

---

## Последовательность инициализации

Порядок инициализации важен из-за зависимостей между модулями:

```
NVS -> I2C -> GPIO -> UART -> Ethernet -> Modbus -> Config ->
  -> Датчики (AI, Flow, Cond) ->
  -> Логика (Interlocks, SM, Doser, Telemetry) ->
  -> Аварии ->
  -> Задачи FreeRTOS ->
  -> HTTP-сервер ->
  -> MQTT (условно)
```

**Ключевые зависимости:**
- I2C должен быть инициализирован до GPIO (TCA9554 работает по I2C)
- UART должен быть до Modbus (Modbus использует UART для RS-485)
- Ethernet и TCP/IP стек должны быть до HTTP-сервера и MQTT
- Config Manager должен быть до HTTP-сервера (REST API использует конфигурацию)
- Все драйверы датчиков должны быть до запуска задачи процесса

---

## Архитектурные замечания

- Все этапы инициализации обёрнуты в `ESP_ERROR_CHECK()`, что приводит к `abort()` при любой ошибке — это обеспечивает раннее обнаружение проблем при запуске.
- Задача `io_task` определена непосредственно в `app_main.c`, так как она тесно связана с HAL GPIO и не выделена в отдельный компонент.
- HTTP-сервер запускается в контексте задачи `app_main`, а не как отдельная задача — он создаёт свою задачу внутренне.
- После завершения `app_main()` задача `main` остаётся активной в FreeRTOS (ESP-IDF не завершает её автоматически), но весь функционал работает через запущенные задачи.
