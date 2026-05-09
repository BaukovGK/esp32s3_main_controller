# doc_mqtt_publish.md -- Документация модуля mqtt_publish

## Файл

| Файл | Путь |
|------|------|
| Реализация | `components/mqtt_app/mqtt_publish.c` |
| Объявления (extern) | `components/mqtt_app/mqtt_app.c` (extern-прототипы) |

---

## 1. Общее описание

Модуль `mqtt_publish.c` реализует всю логику публикации данных через MQTT. Он отвечает за:

- **Периодическую публикацию полного статуса** установки обратного осмоса: состояние машины состояний, дискретный и аналоговый ввод-вывод, расходомеры, кондуктометры, телеметрия, дозатор, блокировки.
- **Публикацию аварийных событий** при их возникновении.
- **Публикацию availability** (`"online"`) при подключении к брокеру.
- **Публикацию диагностической информации** (heap, uptime, стеки задач, Modbus-статистика).
- **Home Assistant MQTT Discovery** -- автоматическую регистрацию всех сущностей (sensor, binary_sensor) в Home Assistant.

Все функции модуля объявлены как `extern`-прототипы в `mqtt_app.c` и вызываются из `mqtt_task` или `mqtt_event_handler`.

---

## 2. Зависимости (include)

| Заголовок | Назначение |
|-----------|------------|
| `mqtt_client.h` | ESP-IDF MQTT клиент -- `esp_mqtt_client_handle_t`, `esp_mqtt_client_publish` |
| `cJSON.h` | Формирование JSON для Home Assistant Discovery |
| `esp_log.h` | Макросы логирования `ESP_LOGx` |
| `state_machine.h` | Машина состояний -- `state_machine_get_status()`, типы `sm_status_t`, `sm_state_t`, `auto_substate_t`, `wash_substate_t` |
| `interlocks.h` | Блокировки -- `interlocks_check()`, тип `interlock_result_t` |
| `doser.h` | Дозатор -- `doser_get_state()`, `doser_is_enabled()`, тип `doser_state_t` |
| `telemetry.h` | Телеметрия -- `telemetry_get()`, тип `telemetry_data_t` |
| `analog_input.h` | Аналоговый ввод -- `analog_input_get_data()`, тип `ai_data_t` |
| `flowmeter.h` | Расходомеры -- `flowmeter_get_data()`, тип `flowmeter_data_t`, константа `FLOW_CHANNEL_COUNT` |
| `conductivity.h` | Кондуктометры -- `conductivity_get_data()`, тип `conductivity_data_t`, константа `COND_CHANNEL_COUNT` |
| `hal_gpio.h` | HAL GPIO -- `hal_gpio_read_di()`, `hal_gpio_read_do_state()` |
| `alarm_manager.h` | Аварии -- `alarm_category_str()`, `alarm_code_str()`, тип `alarm_entry_t` |
| `diagnostics.h` | Диагностика -- `diagnostics_collect()`, тип `diagnostics_data_t` |
| `<math.h>` | Функция `isnan()` для обработки NaN в float-значениях |
| `<stdio.h>` | `snprintf` для форматирования строк |
| `<string.h>` | `strcmp` для сравнения строк |

---

## 3. Определения (#define) и константы

| Имя | Значение | Описание |
|-----|----------|----------|
| `HA_ENTITY_COUNT` | `sizeof(s_ha_entities) / sizeof(s_ha_entities[0])` | Количество сущностей Home Assistant Discovery. Вычисляется автоматически из размера массива `s_ha_entities`. На данный момент = **22**. |

---

## 4. Типы данных (typedef / struct)

### `ha_entity_t`

```c
typedef struct {
    const char *object_id;
    const char *name;
    const char *state_topic;
    const char *value_template;
    const char *unit;
    const char *device_class;
    const char *icon;
    const char *entity_type;
} ha_entity_t;
```

**Описание:** Структура описания одной сущности Home Assistant для MQTT Discovery. Используется для табличной генерации discovery-конфигураций.

| Поле | Тип | Описание |
|------|-----|----------|
| `object_id` | `const char *` | Уникальный идентификатор сущности в HA (используется как `unique_id` и часть discovery-топика) |
| `name` | `const char *` | Отображаемое имя сущности в HA |
| `state_topic` | `const char *` | MQTT-топик, из которого HA читает значение |
| `value_template` | `const char *` | Jinja2-шаблон для извлечения значения из JSON (напр. `{{ value_json.state }}`) |
| `unit` | `const char *` | Единица измерения (напр. `"bar"`, `"%"`, `"B"`). `NULL` если не применимо. |
| `device_class` | `const char *` | Класс устройства HA (напр. `"pressure"`, `"temperature"`, `"safety"`). `NULL` если не задан. |
| `icon` | `const char *` | Иконка MDI (напр. `"mdi:water-pump"`). `NULL` если используется иконка по умолчанию для device_class. |
| `entity_type` | `const char *` | Тип платформы HA: `"sensor"` или `"binary_sensor"` |

---

## 5. Статические переменные

| Переменная | Тип | Описание |
|------------|-----|----------|
| `TAG` | `const char *` | Тег логирования: `"mqtt_pub"` |
| `s_ha_entities[]` | `const ha_entity_t[22]` | Массив описаний всех сущностей Home Assistant Discovery (см. раздел 10) |

---

## 6. Функции

### 6.1. Публичные функции (вызываются из mqtt_app.c)

---

#### `mqtt_publish_full_status`

```c
void mqtt_publish_full_status(esp_mqtt_client_handle_t client);
```

**Описание:**
Публикация полного статуса установки. Вызывается периодически из `mqtt_task` (каждый цикл с интервалом `publish_interval_s`). Собирает данные из всех подсистем и публикует в соответствующие топики.

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `client` | `esp_mqtt_client_handle_t` | Хендл активного MQTT-клиента |

**Возвращаемое значение:** нет (`void`).

**Публикуемые топики (8 групп):**

##### 6.1.1. State -- состояние машины состояний

**Топик:** `ro_plant/status/state` (QoS 1, Retain)

```json
{
  "state": "AUTO",
  "auto_sub": "RUNNING",
  "wash_sub": null,
  "fault_flags": 0
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `state` | string | Текущее состояние: `"IDLE"`, `"AUTO"`, `"WASHING"`, `"MANUAL"`, `"FAULT"` |
| `auto_sub` | string/null | Подсостояние AUTO: `"STARTING_PUMP1"`, `"RAMP"`, `"STARTING_PUMP2"`, `"FILLING_INTERM"`, `"STARTING_PUMP3"`, `"RUNNING"`, `"STOPPING"`. `null` если state != AUTO. |
| `wash_sub` | string/null | Подсостояние WASHING: `"WAIT_HEAT"`, `"HEATING"`, `"WAIT_SUPPLY"`, `"SUPPLY"`, `"WAIT_DRAIN"`, `"DRAIN"`, `"DONE"`. `null` если state != WASHING. |
| `fault_flags` | число | Битовая маска активных аварий |

##### 6.1.2. IO -- дискретный ввод-вывод

**Топик:** `ro_plant/status/io` (QoS 0, без Retain)

```json
{
  "di": 15,
  "do": 3
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `di` | число | Битовая маска дискретных входов (DI) |
| `do` | число | Битовая маска дискретных выходов (DO) |

##### 6.1.3. Analog -- аналоговые датчики

**Топики:** `ro_plant/status/analog/{name}` (QoS 0, без Retain)

Где `{name}` принимает значения: `P1`, `P2`, `P3`, `P4`, `T`.

```json
{
  "value": 3.45,
  "unit": "bar",
  "fault": false
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `value` | число/null | Текущее значение датчика. `null` при NaN (неисправность). |
| `unit` | string | Единица измерения: `"bar"` для P1-P4, `"C"` для T |
| `fault` | bool | Флаг неисправности канала |

**Датчики:**

| Имя | Единица | Назначение |
|-----|---------|------------|
| `P1` | bar | Давление на входе (перед фильтрами) |
| `P2` | bar | Давление между ступенями |
| `P3` | bar | Давление на выходе 1-й ступени |
| `P4` | bar | Давление на выходе 2-й ступени |
| `T` | C | Температура воды |

##### 6.1.4. Flow -- расходомеры

**Топики:** `ro_plant/status/flow/{name}` (QoS 0, без Retain)

Где `{name}` принимает значения: `Q1`, `Q2`, `Q3`, `Q4`.

```json
{
  "flow": 1.23,
  "volume": 456.78,
  "ok": true
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `flow` | число/null | Текущий расход (м3/ч). `null` при NaN. |
| `volume` | число/null | Накопленный объём (м3). `null` при NaN. |
| `ok` | bool | Флаг работоспособности канала |

##### 6.1.5. Conductivity -- кондуктометры

**Топики:** `ro_plant/status/conductivity/{name}` (QoS 0, без Retain)

Где `{name}` принимает значения: `s1`, `s2`, `s3`, `s4` (4 канала, `COND_CHANNEL_COUNT = 4`).

```json
{
  "conductivity": 150.5,
  "temperature": 25.3,
  "ok": true
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `conductivity` | число/null | Электропроводность (мкСм/см). `null` при NaN. |
| `temperature` | число/null | Температура (градусы Цельсия). `null` при NaN. |
| `ok` | bool | Флаг работоспособности канала |

**Датчики:**

| Имя | Источник Modbus | Назначение |
|-----|-----------------|------------|
| `s1` | slave 10 (SL21-201) X1/t1 | Кондуктивность входной (питательной) воды (Feed) |
| `s2` | slave 10 X2/t2 | Кондуктивность пермеата 1-й ступени |
| `s3` | slave 11 (SL21-101) X1/t1 | Кондуктивность пермеата 2-й ступени (товарный) |
| `s4` | slave 11 X2/t2 | Кондуктивность концентрата (добавлен 2026-05-09) |

##### 6.1.6. Telemetry -- вычисленные параметры

**Топик:** `ro_plant/status/telemetry` (QoS 0, без Retain)

```json
{
  "filter_dp": 0.35,
  "stage1_feed": 2.5,
  "recovery2": 75.0,
  "recovery_sys": 80.0,
  "sel1": 98.5,
  "sel2": 99.1
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `filter_dp` | число/null | Перепад давления на фильтрах (бар) |
| `stage1_feed` | число/null | Подача на 1-ю ступень (м3/ч) |
| `recovery2` | число/null | Степень извлечения 2-й ступени (%) |
| `recovery_sys` | число/null | Системная степень извлечения (%) |
| `sel1` | число/null | Селективность 1-й ступени (%) |
| `sel2` | число/null | Селективность 2-й ступени (%) |

##### 6.1.7. Doser -- состояние дозатора

**Топик:** `ro_plant/status/doser` (QoS 0, без Retain)

```json
{
  "state": "RUNNING",
  "enabled": true
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `state` | string | Состояние дозатора: `"OFF"`, `"RUNNING"`, `"PAUSE"` |
| `enabled` | bool | Разрешение работы дозатора |

##### 6.1.8. Interlocks -- блокировки

**Топик:** `ro_plant/status/interlocks` (QoS 0, без Retain)

```json
{
  "flags": 0,
  "estop": false,
  "filter_warn": false
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `flags` | число | Битовая маска активных блокировок |
| `estop` | bool | Аварийная остановка (E-STOP) активна |
| `filter_warn` | bool | Предупреждение о загрязнении фильтра |

---

#### `mqtt_publish_alarm`

```c
void mqtt_publish_alarm(esp_mqtt_client_handle_t client, const alarm_entry_t *alarm);
```

**Описание:**
Публикация одного аварийного события. Вызывается из `mqtt_task` при извлечении аварии из кольцевого буфера.

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `client` | `esp_mqtt_client_handle_t` | Хендл активного MQTT-клиента |
| `alarm` | `const alarm_entry_t *` | Указатель на структуру аварийного события |

**Возвращаемое значение:** нет (`void`).

**Топик:** `ro_plant/alarms` (QoS 1, без Retain)

**Формат payload (JSON):**

```json
{
  "id": 12,
  "ts": 1700000000,
  "cat": "WARNING",
  "code": "P1_HIGH",
  "value": 12.5,
  "active": true
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `id` | число | Уникальный идентификатор аварии |
| `ts` | число | Временная метка (Unix timestamp в секундах, конвертируется из микросекунд) |
| `cat` | string | Категория аварии (строковое представление из `alarm_category_str()`) |
| `code` | string | Код аварии (строковое представление из `alarm_code_str()`) |
| `value` | число | Значение параметра, вызвавшего аварию |
| `active` | bool | `true` -- авария активна (поднята), `false` -- авария снята |

---

#### `mqtt_publish_online`

```c
void mqtt_publish_online(esp_mqtt_client_handle_t client);
```

**Описание:**
Публикация сообщения о доступности ("online") в топик availability. Вызывается при подключении к брокеру (обработчик `MQTT_EVENT_CONNECTED`).

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `client` | `esp_mqtt_client_handle_t` | Хендл активного MQTT-клиента |

**Возвращаемое значение:** нет (`void`).

**Топик:** `ro_plant/availability` (QoS 1, Retain)

**Payload:** `"online"` (текст).

**Примечание:** Сообщение `"offline"` публикуется брокером автоматически через механизм LWT (Last Will and Testament), настроенный в `mqtt_app_start`.

---

#### `mqtt_publish_diagnostics`

```c
void mqtt_publish_diagnostics(esp_mqtt_client_handle_t client);
```

**Описание:**
Публикация диагностической информации. Вызывается из `mqtt_task` каждый 6-й цикл (примерно каждые 30 секунд при интервале публикации 5 секунд).

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `client` | `esp_mqtt_client_handle_t` | Хендл активного MQTT-клиента |

**Возвращаемое значение:** нет (`void`).

**Топик:** `ro_plant/status/diagnostics` (QoS 0, без Retain)

**Формат payload (JSON):**

```json
{
  "heap_free": 120000,
  "heap_min": 95000,
  "uptime_s": 86400,
  "stack": {
    "mqtt": 2048,
    "sm": 1536,
    "analog": 1024
  },
  "modbus": {
    "errors": [0, 0, 5, 0],
    "online": [true, true, false, true]
  },
  "wdt_stale": 0
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `heap_free` | число | Текущий объём свободной кучи (байт) |
| `heap_min` | число | Минимальный объём свободной кучи за время работы (байт) |
| `uptime_s` | число | Время работы системы (секунды, конвертируется из микросекунд) |
| `stack` | объект | Словарь: имя задачи -> свободный стек (байт). Перечисляются все зарегистрированные задачи. |
| `modbus.errors` | массив | Количество ошибок Modbus по каждому slave-устройству. Размер — динамический (по `mb_count` из `diagnostics_data_t`); список slaves берётся из `modbus_poller_get_slave_addrs` и дедуплицируется. |
| `modbus.online` | массив | Статус онлайн каждого slave-устройства Modbus. Размер совпадает с `modbus.errors`. |
| `wdt_stale` | число | Флаг "зависших" задач Watchdog (всегда 0 в текущей реализации) |

---

#### `mqtt_publish_ha_discovery`

```c
void mqtt_publish_ha_discovery(esp_mqtt_client_handle_t client);
```

**Описание:**
Публикация конфигурации Home Assistant MQTT Discovery для всех зарегистрированных сущностей. Вызывается при каждом подключении к брокеру (обработчик `MQTT_EVENT_CONNECTED`). Перебирает массив `s_ha_entities` и для каждой сущности формирует и публикует JSON-конфигурацию.

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `client` | `esp_mqtt_client_handle_t` | Хендл активного MQTT-клиента |

**Возвращаемое значение:** нет (`void`).

**Discovery-топик:** `homeassistant/{entity_type}/ro_plant/{object_id}/config` (QoS 1, Retain)

**Формат JSON конфигурации:**

```json
{
  "name": "RO P1",
  "unique_id": "ro_plant_p1",
  "state_topic": "ro_plant/status/analog/P1",
  "value_template": "{{ value_json.value }}",
  "availability_topic": "ro_plant/availability",
  "unit_of_measurement": "bar",
  "device_class": "pressure",
  "device": {
    "identifiers": ["ro_plant_001"],
    "name": "RO Plant Controller",
    "manufacturer": "Custom",
    "model": "ESP32-S3 RO",
    "sw_version": "1.0.0"
  }
}
```

Для `binary_sensor` дополнительно добавляются:
```json
{
  "payload_on": "true",
  "payload_off": "false"
}
```

---

### 6.2. Статические функции (вспомогательные)

---

#### `state_to_str`

```c
static const char *state_to_str(sm_state_t st);
```

**Описание:** Преобразует перечисление состояний машины состояний в строку.

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `st` | `sm_state_t` | Значение перечисления состояния |

**Возвращаемое значение:** `const char *` -- строковое представление.

**Маппинг:**

| Индекс | Строка |
|--------|--------|
| 0 | `"IDLE"` |
| 1 | `"AUTO"` |
| 2 | `"WASHING"` |
| 3 | `"MANUAL"` |
| 4 | `"FAULT"` |
| >= 5 | `"UNKNOWN"` |

---

#### `auto_sub_str`

```c
static const char *auto_sub_str(auto_substate_t sub);
```

**Описание:** Преобразует перечисление подсостояний режима AUTO в строку.

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `sub` | `auto_substate_t` | Значение перечисления подсостояния AUTO |

**Возвращаемое значение:** `const char *` -- строковое представление.

**Маппинг:**

| Индекс | Строка | Описание |
|--------|--------|----------|
| 0 | `"STARTING_PUMP1"` | Запуск первого насоса |
| 1 | `"RAMP"` | Разгон (выход на режим) |
| 2 | `"STARTING_PUMP2"` | Запуск второго насоса |
| 3 | `"FILLING_INTERM"` | Заполнение промежуточной ёмкости |
| 4 | `"STARTING_PUMP3"` | Запуск третьего насоса |
| 5 | `"RUNNING"` | Рабочий режим |
| 6 | `"STOPPING"` | Остановка |
| >= 7 | `"UNKNOWN"` | Неизвестное подсостояние |

---

#### `wash_sub_str`

```c
static const char *wash_sub_str(wash_substate_t sub);
```

**Описание:** Преобразует перечисление подсостояний режима WASHING в строку.

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `sub` | `wash_substate_t` | Значение перечисления подсостояния WASHING |

**Возвращаемое значение:** `const char *` -- строковое представление.

**Маппинг:**

| Индекс | Строка | Описание |
|--------|--------|----------|
| 0 | `"WAIT_HEAT"` | Ожидание начала нагрева |
| 1 | `"HEATING"` | Нагрев раствора |
| 2 | `"WAIT_SUPPLY"` | Ожидание подачи |
| 3 | `"SUPPLY"` | Подача раствора промывки |
| 4 | `"WAIT_DRAIN"` | Ожидание дренажа |
| 5 | `"DRAIN"` | Дренаж (слив) |
| 6 | `"DONE"` | Промывка завершена |
| >= 7 | `"UNKNOWN"` | Неизвестное подсостояние |

---

#### `doser_state_str`

```c
static const char *doser_state_str(doser_state_t st);
```

**Описание:** Преобразует перечисление состояний дозатора в строку.

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `st` | `doser_state_t` | Значение перечисления состояния дозатора |

**Возвращаемое значение:** `const char *` -- строковое представление.

**Маппинг:**

| Индекс | Строка | Описание |
|--------|--------|----------|
| 0 | `"OFF"` | Дозатор выключен |
| 1 | `"RUNNING"` | Дозатор работает (дозирует) |
| 2 | `"PAUSE"` | Дозатор на паузе (ожидание следующего цикла) |
| >= 3 | `"UNKNOWN"` | Неизвестное состояние |

---

#### `fmt_float`

```c
static int fmt_float(char *buf, size_t sz, float val);
```

**Описание:** Форматирует значение `float` для вставки в JSON. Если значение `NaN` -- выводит строку `null` (без кавычек, т.к. это JSON null). В противном случае -- число с двумя знаками после запятой.

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `buf` | `char *` | Выходной буфер для строки |
| `sz` | `size_t` | Размер буфера |
| `val` | `float` | Форматируемое значение |

**Возвращаемое значение:** `int` -- количество записанных символов (результат `snprintf`).

**Примеры:**
- `fmt_float(buf, 16, 3.14159)` -> `"3.14"`
- `fmt_float(buf, 16, NAN)` -> `"null"`

---

#### `add_device_obj`

```c
static void add_device_obj(cJSON *root);
```

**Описание:** Добавляет объект `device` в JSON-конфигурацию Home Assistant Discovery. Все сущности привязываются к одному устройству, что позволяет группировать их в HA UI.

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `root` | `cJSON *` | Корневой JSON-объект, к которому добавляется поле `device` |

**Возвращаемое значение:** нет (`void`).

**Генерируемый JSON-фрагмент:**

```json
{
  "device": {
    "identifiers": ["ro_plant_001"],
    "name": "RO Plant Controller",
    "manufacturer": "Custom",
    "model": "ESP32-S3 RO",
    "sw_version": "1.0.0"
  }
}
```

| Поле | Значение | Описание |
|------|----------|----------|
| `identifiers` | `["ro_plant_001"]` | Массив идентификаторов устройства. HA использует для группировки сущностей. |
| `name` | `"RO Plant Controller"` | Отображаемое имя устройства |
| `manufacturer` | `"Custom"` | Производитель |
| `model` | `"ESP32-S3 RO"` | Модель контроллера |
| `sw_version` | `"1.0.0"` | Версия прошивки |

---

## 7. Полная структура MQTT-топиков публикации

| # | Топик | QoS | Retain | Периодичность | Описание |
|---|-------|-----|--------|---------------|----------|
| 1 | `ro_plant/availability` | 1 | Да | При подключении | Availability: `"online"` / `"offline"` (LWT) |
| 2 | `ro_plant/status/state` | 1 | Да | Каждый цикл | Состояние машины состояний |
| 3 | `ro_plant/status/io` | 0 | Нет | Каждый цикл | Дискретный ввод-вывод |
| 4 | `ro_plant/status/analog/P1` | 0 | Нет | Каждый цикл | Датчик давления P1 |
| 5 | `ro_plant/status/analog/P2` | 0 | Нет | Каждый цикл | Датчик давления P2 |
| 6 | `ro_plant/status/analog/P3` | 0 | Нет | Каждый цикл | Датчик давления P3 |
| 7 | `ro_plant/status/analog/P4` | 0 | Нет | Каждый цикл | Датчик давления P4 |
| 8 | `ro_plant/status/analog/T` | 0 | Нет | Каждый цикл | Датчик температуры |
| 9 | `ro_plant/status/flow/Q1` | 0 | Нет | Каждый цикл | Расходомер Q1 |
| 10 | `ro_plant/status/flow/Q2` | 0 | Нет | Каждый цикл | Расходомер Q2 |
| 11 | `ro_plant/status/flow/Q3` | 0 | Нет | Каждый цикл | Расходомер Q3 |
| 12 | `ro_plant/status/flow/Q4` | 0 | Нет | Каждый цикл | Расходомер Q4 |
| 13 | `ro_plant/status/conductivity/s1` | 0 | Нет | Каждый цикл | Кондуктометр Feed (slave 10 X1) |
| 14 | `ro_plant/status/conductivity/s2` | 0 | Нет | Каждый цикл | Кондуктометр Perm1 (slave 10 X2) |
| 15 | `ro_plant/status/conductivity/s3` | 0 | Нет | Каждый цикл | Кондуктометр Perm2 (slave 11 X1) |
| 16 | `ro_plant/status/conductivity/s4` | 0 | Нет | Каждый цикл | Кондуктометр Conc (slave 11 X2) |
| 17 | `ro_plant/status/telemetry` | 0 | Нет | Каждый цикл | Вычисленные параметры |
| 18 | `ro_plant/status/doser` | 0 | Нет | Каждый цикл | Состояние дозатора |
| 19 | `ro_plant/status/interlocks` | 0 | Нет | Каждый цикл | Блокировки |
| 20 | `ro_plant/status/diagnostics` | 0 | Нет | Каждый 6-й цикл | Диагностика |
| 21 | `ro_plant/alarms` | 1 | Нет | По событию | Аварийные события |

---

## 8. Home Assistant Discovery -- полный список сущностей (22 шт.)

### 8.1. Группа: Состояние установки (2 sensor)

| # | object_id | name | state_topic | value_template | unit | device_class | icon | entity_type |
|---|-----------|------|-------------|----------------|------|-------------|------|-------------|
| 1 | `ro_plant_state` | RO State | `ro_plant/status/state` | `{{ value_json.state }}` | -- | -- | `mdi:water-pump` | `sensor` |
| 2 | `ro_plant_faults` | RO Fault Flags | `ro_plant/status/state` | `{{ value_json.fault_flags }}` | -- | -- | `mdi:alert-circle` | `sensor` |

### 8.2. Группа: Давления (4 sensor) и температура (1 sensor)

| # | object_id | name | state_topic | value_template | unit | device_class | icon | entity_type |
|---|-----------|------|-------------|----------------|------|-------------|------|-------------|
| 3 | `ro_plant_p1` | RO P1 | `ro_plant/status/analog/P1` | `{{ value_json.value }}` | bar | pressure | -- | `sensor` |
| 4 | `ro_plant_p2` | RO P2 | `ro_plant/status/analog/P2` | `{{ value_json.value }}` | bar | pressure | -- | `sensor` |
| 5 | `ro_plant_p3` | RO P3 | `ro_plant/status/analog/P3` | `{{ value_json.value }}` | bar | pressure | -- | `sensor` |
| 6 | `ro_plant_p4` | RO P4 | `ro_plant/status/analog/P4` | `{{ value_json.value }}` | bar | pressure | -- | `sensor` |
| 7 | `ro_plant_temp` | RO Temperature | `ro_plant/status/analog/T` | `{{ value_json.value }}` | C (градусы) | temperature | -- | `sensor` |

### 8.3. Группа: Расходомеры (4 sensor)

| # | object_id | name | state_topic | value_template | unit | device_class | icon | entity_type |
|---|-----------|------|-------------|----------------|------|-------------|------|-------------|
| 8 | `ro_plant_q1` | RO Q1 Flow | `ro_plant/status/flow/Q1` | `{{ value_json.flow }}` | m3/h | -- | `mdi:water` | `sensor` |
| 9 | `ro_plant_q2` | RO Q2 Flow | `ro_plant/status/flow/Q2` | `{{ value_json.flow }}` | m3/h | -- | `mdi:water` | `sensor` |
| 10 | `ro_plant_q3` | RO Q3 Flow | `ro_plant/status/flow/Q3` | `{{ value_json.flow }}` | m3/h | -- | `mdi:water` | `sensor` |
| 11 | `ro_plant_q4` | RO Q4 Flow | `ro_plant/status/flow/Q4` | `{{ value_json.flow }}` | m3/h | -- | `mdi:water` | `sensor` |

### 8.4. Группа: Кондуктометры (3 sensor)

| # | object_id | name | state_topic | value_template | unit | device_class | icon | entity_type |
|---|-----------|------|-------------|----------------|------|-------------|------|-------------|
| 12 | `ro_plant_s1` | RO Feed Conductivity | `ro_plant/status/conductivity/s1` | `{{ value_json.conductivity }}` | uS/cm | -- | `mdi:flash` | `sensor` |
| 13 | `ro_plant_s2` | RO Perm1 Conductivity | `ro_plant/status/conductivity/s2` | `{{ value_json.conductivity }}` | uS/cm | -- | `mdi:flash` | `sensor` |
| 14 | `ro_plant_s3` | RO Perm2 Conductivity | `ro_plant/status/conductivity/s3` | `{{ value_json.conductivity }}` | uS/cm | -- | `mdi:flash` | `sensor` |

> **Примечание:** В MQTT публикуется 4 канала (`s1..s4`, `COND_CHANNEL_COUNT = 4`), но HA Discovery пока зарегистрирован только для трёх. Регистрация `ro_plant_s4` (концентрат) — TODO; будет добавлено вместе с расширением `s_ha_entities`.

### 8.5. Группа: Телеметрия (4 sensor)

| # | object_id | name | state_topic | value_template | unit | device_class | icon | entity_type |
|---|-----------|------|-------------|----------------|------|-------------|------|-------------|
| 15 | `ro_plant_filter_dp` | RO Filter dP | `ro_plant/status/telemetry` | `{{ value_json.filter_dp }}` | bar | pressure | -- | `sensor` |
| 16 | `ro_plant_recovery` | RO System Recovery | `ro_plant/status/telemetry` | `{{ value_json.recovery_sys }}` | % | -- | `mdi:percent` | `sensor` |
| 17 | `ro_plant_sel1` | RO Stage1 Selectivity | `ro_plant/status/telemetry` | `{{ value_json.sel1 }}` | % | -- | `mdi:percent` | `sensor` |
| 18 | `ro_plant_sel2` | RO Stage2 Selectivity | `ro_plant/status/telemetry` | `{{ value_json.sel2 }}` | % | -- | `mdi:percent` | `sensor` |

### 8.6. Группа: Бинарные датчики (2 binary_sensor)

| # | object_id | name | state_topic | value_template | device_class | icon | entity_type | payload_on | payload_off |
|---|-----------|------|-------------|----------------|-------------|------|-------------|------------|-------------|
| 19 | `ro_plant_estop` | RO E-STOP | `ro_plant/status/interlocks` | `{{ value_json.estop }}` | safety | `mdi:alert-octagon` | `binary_sensor` | `"true"` | `"false"` |
| 20 | `ro_plant_filter_warn` | RO Filter Warning | `ro_plant/status/interlocks` | `{{ value_json.filter_warn }}` | problem | `mdi:filter` | `binary_sensor` | `"true"` | `"false"` |

### 8.7. Группа: Диагностика (2 sensor)

| # | object_id | name | state_topic | value_template | unit | device_class | icon | entity_type |
|---|-----------|------|-------------|----------------|------|-------------|------|-------------|
| 21 | `ro_plant_heap` | RO Free Heap | `ro_plant/status/diagnostics` | `{{ value_json.heap_free }}` | B | data_size | `mdi:memory` | `sensor` |
| 22 | `ro_plant_uptime_diag` | RO Uptime | `ro_plant/status/diagnostics` | `{{ value_json.uptime_s }}` | s | duration | -- | `sensor` |

---

## 9. Discovery-топики (полный список)

Каждая сущность публикуется в топик формата:

```
homeassistant/{entity_type}/ro_plant/{object_id}/config
```

Полный перечень:

| # | Discovery-топик |
|---|-----------------|
| 1 | `homeassistant/sensor/ro_plant/ro_plant_state/config` |
| 2 | `homeassistant/sensor/ro_plant/ro_plant_faults/config` |
| 3 | `homeassistant/sensor/ro_plant/ro_plant_p1/config` |
| 4 | `homeassistant/sensor/ro_plant/ro_plant_p2/config` |
| 5 | `homeassistant/sensor/ro_plant/ro_plant_p3/config` |
| 6 | `homeassistant/sensor/ro_plant/ro_plant_p4/config` |
| 7 | `homeassistant/sensor/ro_plant/ro_plant_temp/config` |
| 8 | `homeassistant/sensor/ro_plant/ro_plant_q1/config` |
| 9 | `homeassistant/sensor/ro_plant/ro_plant_q2/config` |
| 10 | `homeassistant/sensor/ro_plant/ro_plant_q3/config` |
| 11 | `homeassistant/sensor/ro_plant/ro_plant_q4/config` |
| 12 | `homeassistant/sensor/ro_plant/ro_plant_s1/config` |
| 13 | `homeassistant/sensor/ro_plant/ro_plant_s2/config` |
| 14 | `homeassistant/sensor/ro_plant/ro_plant_s3/config` |
| 15 | `homeassistant/sensor/ro_plant/ro_plant_filter_dp/config` |
| 16 | `homeassistant/sensor/ro_plant/ro_plant_recovery/config` |
| 17 | `homeassistant/sensor/ro_plant/ro_plant_sel1/config` |
| 18 | `homeassistant/sensor/ro_plant/ro_plant_sel2/config` |
| 19 | `homeassistant/binary_sensor/ro_plant/ro_plant_estop/config` |
| 20 | `homeassistant/binary_sensor/ro_plant/ro_plant_filter_warn/config` |
| 21 | `homeassistant/sensor/ro_plant/ro_plant_heap/config` |
| 22 | `homeassistant/sensor/ro_plant/ro_plant_uptime_diag/config` |

---

## 10. Особенности реализации

### Обработка NaN
Все float-значения от аналоговых датчиков, расходомеров, кондуктометров и телеметрии форматируются через `fmt_float`. Значения `NaN` (неисправность датчика, отсутствие данных) передаются как JSON `null`, что позволяет Home Assistant корректно обрабатывать недоступные значения.

### Ручное формирование JSON
Для статусных сообщений (разделы 6.1.1--6.1.8) используется ручное формирование JSON через `snprintf` (а не cJSON). Это сделано для:
- Минимизации потребления heap (cJSON аллоцирует память динамически).
- Повышения скорости (нет парсинга и сериализации через DOM).
- Возможности вставки `null` (без кавычек) для NaN-значений, что затруднительно через cJSON.

### cJSON для Discovery
Для Home Assistant Discovery используется cJSON, т.к.:
- JSON-структура более сложная (вложенные объекты, массивы).
- Discovery публикуется только при подключении (не каждый цикл), поэтому производительность менее критична.
- Проще поддерживать корректность JSON при изменении структуры.

### Устройство HA (device)
Все 22 сущности привязываются к одному устройству `ro_plant_001` через функцию `add_device_obj`. В интерфейсе Home Assistant все сущности будут сгруппированы под одним устройством "RO Plant Controller".

### Буфер публикации
Используются локальные буферы:
- `buf[256]` -- для статусных сообщений.
- `buf[384]` -- для диагностики (бОльший буфер из-за вложенной структуры).
- `topic[80]` -- для формирования имён топиков с переменной частью.
