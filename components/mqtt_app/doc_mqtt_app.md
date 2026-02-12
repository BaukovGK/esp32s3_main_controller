# doc_mqtt_app.md -- Документация модуля mqtt_app

## Файлы

| Файл | Путь |
|------|------|
| Заголовочный файл | `components/mqtt_app/include/mqtt_app.h` |
| Реализация | `components/mqtt_app/mqtt_app.c` |

---

## 1. Общее описание

Модуль `mqtt_app` является ядром MQTT-подсистемы контроллера установки обратного осмоса (RO Plant). Он отвечает за:

- **Инициализацию и конфигурирование** MQTT-клиента на базе `esp_mqtt_client` (ESP-IDF).
- **Подключение к MQTT-брокеру** с поддержкой авторизации (логин/пароль) и Last Will Testament (LWT).
- **Управление жизненным циклом** MQTT-сессии: запуск, остановка, переподключение.
- **Оркестрацию** публикаций и подписок -- при подключении автоматически вызываются функции из `mqtt_publish.c` и `mqtt_subscribe.c`.
- **Создание FreeRTOS-задачи `MqttTask`**, которая периодически публикует полный статус, обрабатывает очередь аварий и публикует диагностику.
- **Интеграцию с alarm_manager** -- регистрирует callback для получения аварийных событий и помещает их в FreeRTOS-очередь для асинхронной публикации.

---

## 2. Зависимости (include)

### mqtt_app.h

| Заголовок | Назначение |
|-----------|------------|
| `esp_err.h` | Тип `esp_err_t` для кодов возврата |
| `<stdbool.h>` | Тип `bool` |

### mqtt_app.c

| Заголовок | Назначение |
|-----------|------------|
| `mqtt_app.h` | Собственный заголовок модуля |
| `mqtt_client.h` | ESP-IDF MQTT клиент (`esp_mqtt_client_*`) |
| `esp_log.h` | Макросы логирования `ESP_LOGx` |
| `esp_timer.h` | Таймеры ESP-IDF (подключён, но в данном файле явно не используется) |
| `freertos/FreeRTOS.h` | Ядро FreeRTOS |
| `freertos/task.h` | API задач FreeRTOS (`xTaskCreate`, `xTaskNotifyGive`, `ulTaskNotifyTake`, `vTaskDelete`) |
| `freertos/queue.h` | API очередей FreeRTOS (`xQueueCreate`, `xQueueSend`, `xQueueReceive`) |
| `config_manager.h` | Менеджер конфигурации -- чтение параметров MQTT (URI брокера, client_id, интервал и др.) |
| `alarm_manager.h` | Менеджер аварий -- регистрация callback `alarm_notify_cb`, вызовы `alarm_raise` / `alarm_clear` |
| `diagnostics.h` | Модуль диагностики -- регистрация задачи для мониторинга стеков |
| `<string.h>` | Стандартная библиотека строк |

---

## 3. Определения (#define) и константы

| Имя | Значение | Описание |
|-----|----------|----------|
| `ALARM_QUEUE_SIZE` | `16` | Размер FreeRTOS-очереди аварий. Определяет максимальное количество аварийных событий, которые могут быть буферизованы до обработки задачей `MqttTask`. При переполнении новые аварии отбрасываются. |
| `MQTT_TASK_STACK_SIZE` | `8192` | Размер стека (в байтах) FreeRTOS-задачи `mqtt`. |
| `MQTT_TASK_PRIORITY` | `4` | Приоритет FreeRTOS-задачи `mqtt`. |
| `MQTT_DIAG_CYCLE` | `6` | Частота публикации диагностики: каждый N-й цикл основного цикла задачи (при интервале 5 с -- примерно каждые 30 с). |
| `MQTT_RECONNECT_MS` | `5000` | Таймаут переподключения MQTT-клиента (в миллисекундах). Передаётся в `network.reconnect_timeout_ms` конфигурации ESP-IDF MQTT. |
| `MQTT_LWT_MSG` | `"offline"` | Текст LWT-сообщения. Публикуется брокером при разрыве соединения. Длина вычисляется как `sizeof(MQTT_LWT_MSG) - 1`. |

---

## 4. Внешние прототипы (extern)

Файл `mqtt_app.c` объявляет `extern`-прототипы функций, реализованных в `mqtt_publish.c` и `mqtt_subscribe.c`:

| Функция | Файл реализации | Описание |
|---------|-----------------|----------|
| `mqtt_publish_ha_discovery(client)` | `mqtt_publish.c` | Публикация конфигурации Home Assistant Discovery для всех сущностей |
| `mqtt_publish_full_status(client)` | `mqtt_publish.c` | Публикация полного статуса системы (состояние, IO, аналог, расход, и т.д.) |
| `mqtt_publish_alarm(client, alarm)` | `mqtt_publish.c` | Публикация одного аварийного события |
| `mqtt_publish_online(client)` | `mqtt_publish.c` | Публикация availability = "online" |
| `mqtt_publish_diagnostics(client)` | `mqtt_publish.c` | Публикация диагностической информации (heap, uptime, стеки, modbus) |
| `mqtt_subscribe_all(client)` | `mqtt_subscribe.c` | Подписка на все командные топики |
| `mqtt_subscribe_handle_message(topic, topic_len, data, data_len)` | `mqtt_subscribe.c` | Диспетчеризация входящих сообщений по обработчикам |

---

## 5. Статические переменные (внутреннее состояние модуля)

| Переменная | Тип | Описание |
|------------|-----|----------|
| `TAG` | `const char *` | Тег логирования: `"mqtt_app"` |
| `s_client` | `esp_mqtt_client_handle_t` | Хендл MQTT-клиента ESP-IDF. `NULL` когда клиент не создан. |
| `s_task_handle` | `TaskHandle_t` | Хендл FreeRTOS-задачи `MqttTask`. Используется для `xTaskNotifyGive` (пробуждение задачи) и `vTaskDelete` (остановка). |
| `s_connected` | `volatile bool` | Флаг состояния подключения. Устанавливается в `true` при `MQTT_EVENT_CONNECTED`, в `false` при `MQTT_EVENT_DISCONNECTED` и при остановке. Объявлен `volatile`, т.к. модифицируется из обработчика событий и читается из задачи. |
| `s_alarm_queue` | `QueueHandle_t` | FreeRTOS-очередь аварийных событий (элемент -- `alarm_entry_t`, глубина `ALARM_QUEUE_SIZE`). Создаётся через `xQueueCreate` в `mqtt_app_start`. Заполняется из `alarm_notify_cb` через `xQueueSend`, считывается в `mqtt_task` через `xQueueReceive`. |

---

## 6. Функции

### 6.1. Публичные функции (объявлены в mqtt_app.h)

---

#### `mqtt_app_start`

```c
esp_err_t mqtt_app_start(void);
```

**Описание:**
Запуск MQTT-клиента. Выполняет полную инициализацию MQTT-подсистемы:
1. Проверяет, не запущен ли клиент уже (защита от повторного вызова).
2. Создаёт FreeRTOS-очередь аварий `s_alarm_queue` глубиной `ALARM_QUEUE_SIZE` (если ещё не создана).
3. Читает конфигурацию MQTT из `config_manager` (URI брокера, client_id, логин, пароль, интервал публикации).
4. Конфигурирует MQTT-клиент ESP-IDF:
   - Адрес брокера.
   - Идентификатор клиента.
   - Last Will and Testament (топик `ro_plant/availability`, сообщение `MQTT_LWT_MSG`, длина `sizeof(MQTT_LWT_MSG) - 1`, QoS 1, retain).
   - Таймаут переподключения `MQTT_RECONNECT_MS` миллисекунд.
   - Авторизация (если username не пустой; password -- если также не пустой).
5. Создаёт MQTT-клиент (`esp_mqtt_client_init`).
6. Регистрирует обработчик событий `mqtt_event_handler` на все типы событий.
7. Регистрирует callback `alarm_notify_cb` в `alarm_manager`.
8. Запускает MQTT-клиент (неблокирующий вызов -- `esp_mqtt_client_start` создаёт внутреннюю задачу ESP-IDF).
9. Создаёт FreeRTOS-задачу `mqtt_task` ("mqtt", стек `MQTT_TASK_STACK_SIZE`, приоритет `MQTT_TASK_PRIORITY`).
10. Регистрирует задачу в модуле диагностики.

**Параметры:** нет.

**Возвращаемое значение:**
| Значение | Описание |
|----------|----------|
| `ESP_OK` | Успешный запуск (или клиент уже был запущен) |
| `ESP_ERR_NO_MEM` | Не удалось создать FreeRTOS-очередь аварий |
| `ESP_FAIL` | Ошибка создания MQTT-клиента или FreeRTOS-задачи |
| Другой `esp_err_t` | Ошибка от `esp_mqtt_client_start` |

**Обработка ошибок:**
- Если `xQueueCreate` возвращает `NULL` -- возврат `ESP_ERR_NO_MEM`.
- Если `esp_mqtt_client_init` возвращает `NULL` -- возврат `ESP_FAIL`.
- Если `esp_mqtt_client_start` завершается с ошибкой -- клиент уничтожается, возвращается код ошибки.
- Если `xTaskCreate` не может создать задачу -- клиент останавливается и уничтожается, возвращается `ESP_FAIL`.

---

#### `mqtt_app_stop`

```c
void mqtt_app_stop(void);
```

**Описание:**
Полная остановка MQTT-подсистемы:
1. Удаляет FreeRTOS-задачу `MqttTask` (если существует).
2. Останавливает MQTT-клиент (`esp_mqtt_client_stop`).
3. Уничтожает MQTT-клиент (`esp_mqtt_client_destroy`).
4. Сбрасывает флаг подключения `s_connected = false`.
5. Обнуляет хендлы `s_task_handle` и `s_client`.

**Параметры:** нет.

**Возвращаемое значение:** нет (`void`).

**Примечание:** LWT-сообщение `MQTT_LWT_MSG` (`"offline"`) будет опубликовано брокером при разрыве TCP-соединения. Однако при штатной остановке `mqtt_publish_online` не вызывается с `"offline"` -- брокер сделает это через механизм LWT.

---

#### `mqtt_app_is_connected`

```c
bool mqtt_app_is_connected(void);
```

**Описание:**
Проверяет текущее состояние MQTT-подключения.

**Параметры:** нет.

**Возвращаемое значение:**
| Значение | Описание |
|----------|----------|
| `true` | MQTT-клиент подключён к брокеру |
| `false` | Клиент не подключён (отключён, не инициализирован, или произошла ошибка) |

**Примечание:** Возвращает значение `volatile`-переменной `s_connected`, которая обновляется из обработчика событий MQTT. Потокобезопасна для чтения.

---

#### `mqtt_app_reconnect`

```c
esp_err_t mqtt_app_reconnect(void);
```

**Описание:**
Переподключение MQTT-клиента с новыми настройками. Выполняет последовательно:
1. `mqtt_app_stop()` -- полная остановка текущего клиента.
2. `mqtt_app_start()` -- создание и запуск нового клиента с текущей конфигурацией из `config_manager`.

**Параметры:** нет.

**Возвращаемое значение:** аналогично `mqtt_app_start()`.

**Применение:** вызывается при изменении MQTT-настроек через Web UI или другой интерфейс.

---

### 6.2. Статические функции (внутренние, mqtt_app.c)

---

#### `alarm_notify_cb`

```c
static void alarm_notify_cb(const alarm_entry_t *entry);
```

**Описание:**
Callback-функция, регистрируемая в `alarm_manager`. Вызывается при возникновении или снятии аварии. Помещает аварийное событие в FreeRTOS-очередь `s_alarm_queue` через `xQueueSend` и пробуждает `MqttTask` через `xTaskNotifyGive`.

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `entry` | `const alarm_entry_t *` | Указатель на структуру аварийного события. Копируется в очередь. |

**Возвращаемое значение:** нет (`void`).

**Особенности:**
- Вызывается из контекста обычной задачи (не ISR), поэтому используется `xQueueSend` (не `xQueueSendFromISR`).
- При переполнении очереди (все `ALARM_QUEUE_SIZE` слотов заняты) -- `xQueueSend` с нулевым таймаутом возвращает ошибку, событие **отбрасывается**.
- Пробуждает `MqttTask` даже если очередь переполнена (для обработки уже имеющихся записей).

---

#### `mqtt_event_handler`

```c
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data);
```

**Описание:**
Обработчик событий MQTT-клиента ESP-IDF. Регистрируется через `esp_mqtt_client_register_event` на все типы событий (`ESP_EVENT_ANY_ID`).

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `handler_args` | `void *` | Пользовательские аргументы (не используются, передаётся `NULL`) |
| `base` | `esp_event_base_t` | Базовый тип события (MQTT) |
| `event_id` | `int32_t` | Идентификатор события (`esp_mqtt_event_id_t`) |
| `event_data` | `void *` | Указатель на `esp_mqtt_event_t` -- данные события |

**Обрабатываемые события:**

| Событие | Действия |
|---------|----------|
| `MQTT_EVENT_CONNECTED` | Устанавливает `s_connected = true`. Публикует availability "online" (`mqtt_publish_online`). Публикует HA Discovery (`mqtt_publish_ha_discovery`). Подписывается на командные топики (`mqtt_subscribe_all`). Снимает аварию `ALARM_MQTT_DISCONNECT`. Пробуждает `MqttTask` для немедленной публикации статуса. |
| `MQTT_EVENT_DISCONNECTED` | Устанавливает `s_connected = false`. Поднимает аварию `ALARM_MQTT_DISCONNECT` с категорией `ALARM_CAT_INFO` и значением `0`. |
| `MQTT_EVENT_DATA` | Передаёт входящее сообщение в `mqtt_subscribe_handle_message` для диспетчеризации. |
| `MQTT_EVENT_ERROR` | Логирует тип ошибки на уровне `ESP_LOGE`. |
| Остальные | Игнорируются. |

---

#### `mqtt_task`

```c
static void mqtt_task(void *arg);
```

**Описание:**
Основная FreeRTOS-задача MQTT-подсистемы. Работает в бесконечном цикле с периодическим пробуждением.

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `arg` | `void *` | Аргумент задачи (не используется) |

**Алгоритм работы:**

1. Читает интервал публикации из конфигурации (`cfg->publish_interval_s`).
2. **Ожидание** через `ulTaskNotifyTake(pdTRUE, interval)`:
   - Пробуждается по таймауту (истечение интервала) **или** по уведомлению (`xTaskNotifyGive`).
   - Уведомление отправляется при: подключении к брокеру, получении аварии.
3. Обновляет интервал из конфигурации (на случай изменения через web/MQTT).
4. Если не подключён (`!s_connected || !s_client`) -- возвращается к ожиданию.
5. **Публикация аварий из очереди:** последовательно извлекает все аварии из FreeRTOS-очереди `s_alarm_queue` через `xQueueReceive` и публикует каждую.
6. **Публикация полного статуса** (`mqtt_publish_full_status`) -- вызывается каждый цикл.
7. **Публикация диагностики** (`mqtt_publish_diagnostics`) -- каждый `MQTT_DIAG_CYCLE`-й цикл (управляется счётчиком `diag_counter`).

**Примечание:** задача никогда не завершается самостоятельно. Удаляется извне через `vTaskDelete` в `mqtt_app_stop`.

---

## 7. Структура MQTT-топиков

### Availability (LWT)

| Топик | QoS | Retain | Описание |
|-------|-----|--------|----------|
| `ro_plant/availability` | 1 | Да | `"online"` при подключении, `MQTT_LWT_MSG` (`"offline"`) через LWT при обрыве |

### Публикация (подробнее см. `doc_mqtt_publish.md`)

| Топик | Периодичность |
|-------|---------------|
| `ro_plant/status/state` | Каждый цикл |
| `ro_plant/status/io` | Каждый цикл |
| `ro_plant/status/analog/{P1,P2,P3,P4,T}` | Каждый цикл |
| `ro_plant/status/flow/{Q1,Q2,Q3,Q4}` | Каждый цикл |
| `ro_plant/status/conductivity/{s1,s2,s3}` | Каждый цикл |
| `ro_plant/status/telemetry` | Каждый цикл |
| `ro_plant/status/doser` | Каждый цикл |
| `ro_plant/status/interlocks` | Каждый цикл |
| `ro_plant/status/diagnostics` | Каждый `MQTT_DIAG_CYCLE`-й цикл |
| `ro_plant/alarms` | По событию |
| `homeassistant/sensor/ro_plant/*/config` | При подключении |
| `homeassistant/binary_sensor/ro_plant/*/config` | При подключении |

### Подписка (подробнее см. `doc_mqtt_subscribe.md`)

| Топик | QoS |
|-------|-----|
| `ro_plant/command/mode` | 1 |
| `ro_plant/command/pump` | 1 |
| `ro_plant/command/doser` | 1 |
| `ro_plant/command/heater` | 1 |
| `ro_plant/settings/#` | 1 |

---

## 8. Архитектура и потоки данных

```
                    +-------------------+
                    |   alarm_manager   |
                    +--------+----------+
                             | alarm_notify_cb()
                             | xQueueSend()
                             v
                    +-------------------+
  config_manager -->|    mqtt_app.c     |
                    |                   |
                    |  s_alarm_queue    |
                    |  (FreeRTOS queue) |
                    +--------+----------+
                             |
                     MqttTask (FreeRTOS)
                             | xQueueReceive()
                             |
            +----------------+------------------+
            |                |                  |
            v                v                  v
   mqtt_publish.c    mqtt_subscribe.c    esp_mqtt_client
   (публикации)      (обработка         (TCP/TLS, брокер)
                      входящих команд)
```

**Потоки:**
- **Основной поток `MqttTask`**: периодическая публикация, обработка FreeRTOS-очереди аварий.
- **Обработчик событий MQTT** (внутренняя задача ESP-IDF): получение событий подключения/отключения/данных.
- **Callback аварий** (контекст alarm_manager): запись в FreeRTOS-очередь через `xQueueSend`.

---

## 9. Конфигурация

Модуль использует структуру `config_mqtt_t` из `config_manager`:

| Поле | Тип | Описание |
|------|-----|----------|
| `broker_uri` | `char[]` | URI брокера MQTT (напр. `mqtt://192.168.1.100:1883`) |
| `client_id` | `char[]` | Идентификатор клиента MQTT |
| `username` | `char[]` | Имя пользователя для авторизации (пустая строка = без авторизации) |
| `password` | `char[]` | Пароль для авторизации (пустая строка = без пароля) |
| `publish_interval_s` | `uint16_t` или аналог | Интервал публикации статуса в секундах |
