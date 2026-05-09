# Конфигурация MQTT — Контроллер установки обратного осмоса

**Версия:** 1.0
**Дата:** 2026-02-09
**Брокер по умолчанию:** `mqtt://192.168.1.1:1883`
**Client ID:** `ro_plant`

---

## 1. Параметры подключения

| Параметр | Значение по умолч. | NVS ключ | Диапазон |
|----------|--------------------|-----------|---------|
| `broker_uri` | `mqtt://192.168.1.1:1883` | `mqtt_uri` | строка, макс. 63 символа |
| `username` | *(пусто)* | `mqtt_user` | строка, макс. 31 символ |
| `password` | *(пусто)* | `mqtt_pass` | строка, макс. 31 символ |
| `client_id` | `ro_plant` | `mqtt_id` | строка, макс. 31 символ |
| `publish_interval_s` | `5` | `mqtt_intv` | 1 — 60 с |
| `enabled` | `1` | `mqtt_en` | 0 / 1 |

**Last Will & Testament:**
- Топик: `ro_plant/availability`
- Сообщение: `offline`
- QoS: 1, Retain: true

**Reconnect:** 5000 мс

---

## 2. Публикуемые топики (Publish)

### 2.1 Состояние контроллера

**Топик:** `ro_plant/status/state`
**QoS:** 1, **Retain:** true
**Интервал:** каждые `publish_interval_s` секунд

```json
{
  "state": "IDLE|AUTO|WASHING|MANUAL|FAULT",
  "auto_sub": "STARTING_PUMP1|RAMP|STARTING_PUMP2|FILLING_INTERM|STARTING_PUMP3|RUNNING|STOPPING" | null,
  "wash_sub": "WAIT_HEAT|HEATING|WAIT_SUPPLY|SUPPLY|WAIT_DRAIN|DRAIN|DONE" | null,
  "fault_flags": 0
}
```

**Значения `state`:**
| Значение  | Описание |
|-----------|----------|
| `IDLE`    | Ожидание, все агрегаты выключены |
| `AUTO`    | Автоматическая фильтрация |
| `WASHING` | Химическая промывка мембран |
| `MANUAL`  | Ручное управление |
| `FAULT`   | Авария |

**`fault_flags` — битовая маска:**
| Бит | Флаг | Описание |
|-----|-------|----------|
| 0 | `INTERLOCK_ESTOP` | Аварийная кнопка |
| 1 | `INTERLOCK_SOURCE_EMPTY` | Исходная ёмкость пуста |
| 2 | `INTERLOCK_INTERM_EMPTY` | Промежуточная ёмкость пуста |
| 3 | `INTERLOCK_P1_HIGH` | Превышение давления P1 |
| 4 | `INTERLOCK_P3_HIGH` | Превышение давления P3 |
| 5 | `INTERLOCK_P4_HIGH` | Превышение давления P4 |
| 6 | `INTERLOCK_T_HIGH` | Перегрев |
| 7 | `INTERLOCK_FILTER_DP` | Засорение фильтра |
| 8 | `INTERLOCK_PUMP1_TIMEOUT` | Таймаут подтверждения насоса подачи |
| 9 | `INTERLOCK_PUMP2_TIMEOUT` | Таймаут подтверждения насоса 1-й ст. |
| 10 | `INTERLOCK_PUMP3_TIMEOUT` | Таймаут подтверждения насоса 2-й ст. |

---

### 2.2 Дискретные входы/выходы

**Топик:** `ro_plant/status/io`
**QoS:** 0, **Retain:** false

```json
{
  "di": 255,
  "do": 5
}
```

**`di` — битовая маска (бит 0 = DI1):**
| Бит | DI | Назначение | Логика |
|-----|----|------------|--------|
| 0 | DI1 | Уровень исходной — низкий | 0=пусто, 1=есть вода |
| 1 | DI2 | Уровень промежуточной — низкий | 0=пусто, 1=есть вода |
| 2 | DI3 | Уровень промежуточной — высокий | 0=не полная, 1=полная |
| 3 | DI4 | Уровень пермеатной — высокий | 0=не полная, 1=полная |
| 4 | DI5 | Аварийный стоп | 0=авария, 1=норма |
| 5 | DI6 | Подтверждение насоса подачи | 1=контактор вкл |
| 6 | DI7 | Подтверждение насоса 1-й ст. | 1=контактор вкл |
| 7 | DI8 | Подтверждение насоса 2-й ст. | 1=контактор вкл |

**`do` — битовая маска (бит 0 = RO1):**
| Бит | DO | Назначение |
|-----|----|------------|
| 0 | RO1 | Насос подачи |
| 1 | RO2 | Насос 1-й ступени |
| 2 | RO3 | Насос 2-й ступени |
| 3 | RO4 | Нагреватель (ТЭН) |
| 4 | RO5 | Дозатор |
| 5-7 | RO6-RO8 | Резерв |

---

### 2.3 Аналоговые входы (давления, температура)

**Топик:** `ro_plant/status/analog/{NAME}`
где `{NAME}` = `P1`, `P2`, `P3`, `P4`, `T`

```json
{
  "value": 3.45,
  "unit": "bar",
  "fault": false
}
```

| Канал | Топик | Единицы | Диапазон датчика |
|-------|-------|---------|------------------|
| P1 | `ro_plant/status/analog/P1` | bar | 0 — 6 |
| P2 | `ro_plant/status/analog/P2` | bar | 0 — 6 |
| P3 | `ro_plant/status/analog/P3` | bar | 0 — 40 |
| P4 | `ro_plant/status/analog/P4` | bar | 0 — 10 |
| T | `ro_plant/status/analog/T` | °C | 0 — 100 |

**`fault`** = true при неисправности датчика 4–20 мА:
- **обрыв линии** (ток < 3.5 мА, raw < 3500 мкА);
- **короткое замыкание** (ток > 20.5 мА, raw > 20500 мкА).

При `fault = true` поле `value` = `null` (внутри передаётся NaN).

---

### 2.4 Расходомеры

**Топик:** `ro_plant/status/flow/{NAME}`
где `{NAME}` = `Q1`, `Q2`, `Q3`, `Q4`

```json
{
  "flow": 1.25,
  "volume": 123.45,
  "ok": true
}
```

| Канал | Топик | Описание |
|-------|-------|----------|
| Q1 | `ro_plant/status/flow/Q1` | Входной расход (после насоса подачи) |
| Q2 | `ro_plant/status/flow/Q2` | Рецикл концентрата 1-й ступени |
| Q3 | `ro_plant/status/flow/Q3` | Пермеат 2-й ступени (чистая вода) |
| Q4 | `ro_plant/status/flow/Q4` | Концентрат 2-й ступени |

- `flow` — расход, м³/ч
- `volume` — накопленный объём, м³

---

### 2.5 Кондуктометры

**Топик:** `ro_plant/status/conductivity/{NAME}`
где `{NAME}` = `s1`, `s2`, `s3`, `s4`

```json
{
  "conductivity": 450.0,
  "temperature": 25.3,
  "ok": true
}
```

| Канал | Топик | Источник | Описание |
|-------|-------|----------|----------|
| σ1 | `ro_plant/status/conductivity/s1` | slave 10 (SL21-201) X1/t1 | Исходная (питательная) вода |
| σ2 | `ro_plant/status/conductivity/s2` | slave 10 X2/t2 | Пермеат 1-й ступени |
| σ3 | `ro_plant/status/conductivity/s3` | slave 11 (SL21-101) X1/t1 | Пермеат 2-й ступени (товарный) |
| σ4 | `ro_plant/status/conductivity/s4` | slave 11 X2/t2 | Концентрат |

- `conductivity` — удельная электропроводность, мкСм/см
- `temperature` — температура раствора в точке замера, °C
- 4 логических канала собираются из двух 2-канальных приборов СЛ21 (адреса 10 и 11). Расширение с 3 до 4 каналов выполнено 2026-05-09.

---

### 2.5a Счётчики электроэнергии KWS-306L (добавлено 2026-05-09)

**Топик:** `ro_plant/status/power/{NAME}`
где `{NAME}` = `lp` (НД-насос, slave 20) либо `hp` (ВД-насос, slave 21)

QoS 0, без Retain. Период публикации совпадает с прочими статусными топиками (каждый цикл `mqtt_publish_full_status`, ~1 с).

```json
{
  "voltage": 230.5,
  "current": 4.21,
  "power": 970.5,
  "energy": 12.34,
  "temperature": 38.0,
  "online": true
}
```

| Канал | Топик | Slave | Описание |
|-------|-------|-------|----------|
| `lp` | `ro_plant/status/power/lp` | 20 | НД-насос (PUMP_LP) |
| `hp` | `ro_plant/status/power/hp` | 21 | ВД-насос (PUMP_HP) |

Поля:

| Поле | Тип | Единицы | Описание |
|------|-----|---------|----------|
| `voltage` | float\|null | В | Напряжение (raw 0x000E × 0.1) |
| `current` | float\|null | А | Ток (raw 0x0010 × 0.001) |
| `power` | float\|null | Вт | Активная мощность (raw 0x0012 × 0.1) |
| `energy` | float\|null | кВт·ч | Накопленная энергия (raw 0x001A × 0.01) |
| `temperature` | float\|null | °C | Температура корпуса (raw 0x001B × 1) |
| `online` | bool | — | Устройство отвечает на Modbus-шине |

> Все числовые поля становятся `null` при `valid=false` (offline или до первого опроса) — потребитель не должен принять `0.0` за валидное измерение.

---

### 2.6 Телеметрия (расчётные параметры)

**Топик:** `ro_plant/status/telemetry`

```json
{
  "filter_dp": 0.35,
  "stage1_feed": 2.50,
  "recovery2": 85.0,
  "recovery_sys": 75.0,
  "sel1": 97.5,
  "sel2": 99.1
}
```

| Поле | Описание | Формула | Единицы |
|------|----------|---------|---------|
| `filter_dp` | Перепад на фильтре | P1 - P2 | бар |
| `stage1_feed` | Подача на 1-ю ступень | Q1 + Q2 | м³/ч |
| `recovery2` | Извлечение 2-й ступени | Q3/(Q3+Q4)×100 | % |
| `recovery_sys` | Общее извлечение | Q3/Q1×100 | % |
| `sel1` | Селективность 1-й ступени | (σ1-σ2)/σ1×100 | % |
| `sel2` | Селективность 2-й ступени | (σ2-σ3)/σ2×100 | % |

---

### 2.7 Дозатор

**Топик:** `ro_plant/status/doser`

```json
{
  "state": "OFF|RUNNING|PAUSE",
  "enabled": true
}
```

**TODO:** при добавлении дозирования в WASHING сюда добавится поле `phase` (OFF/AUTO/WASH) — см. README.md.

---

### 2.8 Блокировки

**Топик:** `ro_plant/status/interlocks`

```json
{
  "flags": 0,
  "estop": false,
  "filter_warn": false
}
```

---

### 2.9 Аварии

**Топик:** `ro_plant/alarms`
**QoS:** 1, **Retain:** false

```json
{
  "id": 42,
  "ts": 1707500000,
  "cat": "CRITICAL|ALARM|WARNING|INFO",
  "code": "ESTOP|SOURCE_EMPTY|P1_HIGH|...",
  "value": 6.2,
  "active": true
}
```

**Коды аварий:**
| Код | Hex | Категория | Описание |
|-----|-----|-----------|----------|
| `ESTOP` | 0x0001 | CRITICAL | Аварийная кнопка |
| `SOURCE_EMPTY` | 0x0002 | WARNING | Исходная ёмкость пуста |
| `INTERM_EMPTY` | 0x0003 | WARNING | Промежуточная ёмкость пуста |
| `P1_HIGH` | 0x0010 | ALARM | Давление P1 высокое |
| `P3_HIGH` | 0x0011 | ALARM | Давление P3 высокое |
| `P4_HIGH` | 0x0012 | ALARM | Давление P4 высокое |
| `T_HIGH` | 0x0020 | WARNING | Перегрев |
| `FILTER_DP` | 0x0030 | WARNING | Засорение фильтра |
| `PUMP1_TIMEOUT` | 0x0040 | ALARM | Таймаут насоса подачи |
| `PUMP2_TIMEOUT` | 0x0041 | ALARM | Таймаут насоса 1-й ст. |
| `PUMP3_TIMEOUT` | 0x0042 | ALARM | Таймаут насоса 2-й ст. |
| `SENSOR_FAULT_P1..T` | 0x0050-0x0054 | ALARM | Обрыв датчика |
| `MQTT_DISCONNECT` | 0x0060 | INFO | MQTT отключение |
| `MODBUS_OFFLINE` | 0x0070 | WARNING | Modbus устройство offline |
| `STATE_CHANGE` | 0x0080 | INFO | Смена режима |
| `FAULT_RESET` | 0x0081 | INFO | Сброс аварии |
| `MANUAL_DEP` | 0x0082 | WARNING | Нарушение зависимости агрегатов (MANUAL) |
| `SYSTEM_START` | 0x0090 | INFO | Старт системы |

---

### 2.10 Диагностика

**Топик:** `ro_plant/status/diagnostics`
**Интервал:** каждые 30 с (каждый 6-й цикл публикации)

```json
{
  "heap_free": 180000,
  "heap_min": 120000,
  "uptime_s": 86400,
  "stack": {
    "modbus": 1024,
    "io": 512,
    "process": 2048,
    "watchdog": 512,
    "mqtt": 4096
  },
  "modbus": {
    "errors": [0, 2, 0, 0],
    "online": [true, true, true, true]
  },
  "wdt_stale": 0
}
```

**`modbus.online` / `modbus.errors` — по индексу:**

Список Modbus-устройств формируется динамически опросчиком (`modbus_poller_get_slave_addrs`) с дедупликацией по slave-адресу. Текущий перечень (порядок соответствует порядку появления в таблице опроса):

| Индекс | Адрес | Устройство |
|--------|-------|------------|
| 0 | 1 | Waveshare AI 8CH (4–20 мА аналоговые входы) |
| 1 | 2 | УРЖ2КМ (расходомер) |
| 2 | 10 | СЛ21-201 (2-канальный кондуктометр: σ1, σ2) |
| 3 | 11 | СЛ21-101 (2-канальный кондуктометр: σ3, σ4) |
| 4 | 20 | KWS-306L (трёхфазный счётчик НД-насоса, опрос 2 с) |
| 5 | 21 | KWS-306L (трёхфазный счётчик ВД-насоса, опрос 2 с) |

Размер массивов `modbus.online` / `modbus.errors` в JSON равен фактическому числу slave-адресов; при добавлении новых устройств в `modbus_poller` они автоматически попадают в этот список.

> Публикация значений KWS-306L в `ro_plant/status/power/{lp,hp}` реализована — см. §2.5a.

---

### 2.11 Availability

**Топик:** `ro_plant/availability`
**QoS:** 1, **Retain:** true

- При подключении: `online`
- При отключении (LWT): `offline`

---

## 3. Подписки (Subscribe)

### 3.1 Команды режима

**Топик:** `ro_plant/command/mode`
**QoS:** 1

| Payload | Команда | Описание |
|---------|---------|----------|
| `start_auto` | CMD_START_AUTO | Запуск автоматического режима |
| `stop` | CMD_STOP | Остановка (→ IDLE) |
| `start_washing` | CMD_START_WASHING | Запуск промывки |
| `confirm_wash` | CMD_CONFIRM_WASH_PHASE | Подтверждение текущей фазы промывки |
| `set_manual` | CMD_SET_MANUAL | Переход в ручной режим |
| `reset_fault` | CMD_RESET_FAULT | Сброс аварии (→ IDLE, если E-STOP снят) |

---

### 3.2 Управление насосами (только в MANUAL)

**Топик:** `ro_plant/command/pump`
**QoS:** 1

```json
{"mask": 5}
```

`mask` — битовая маска DO (бит 0 = RO1). Пример: `5` = 0b00000101 = RO1 + RO3.

**Игнорируется** если не в режиме `MANUAL`.

---

### 3.3 Управление дозатором

**Топик:** `ro_plant/command/doser`
**QoS:** 1

```json
{"enabled": true}
```

---

### 3.4 Управление нагревателем (только в MANUAL)

**Топик:** `ro_plant/command/heater`
**QoS:** 1

```json
{"on": true}
```

**Игнорируется** если не в режиме `MANUAL`.

---

### 3.5 Настройки

**Топик:** `ro_plant/settings/{section}`
**QoS:** 1

Секции и их поля:

#### `ro_plant/settings/pressure`
```json
{
  "p1_max": 5.5,
  "p3_max": 35.0,
  "p4_max": 8.0,
  "filter_dp_warn": 1.0
}
```

#### `ro_plant/settings/doser`
```json
{
  "run_time_min": 5,
  "cycle_time_min": 60
}
```

#### `ro_plant/settings/washing`
```json
{
  "target_temp_C": 35.0,
  "max_temp_C": 40.0,
  "t_overshoot_C": 45.0,
  "hysteresis_C": 2.0,
  "heat_timeout_min": 30,
  "supply_time_min": 20,
  "drain_time_min": 5
}
```

#### `ro_plant/settings/timeouts`
```json
{
  "pump_confirm_ms": 3000,
  "pump_ramp_ms": 15000
}
```

Все поля **опциональны** — можно отправить только изменённые. Значения сохраняются в NVS.

---

## 4. Home Assistant MQTT Discovery

При подключении к брокеру контроллер автоматически публикует конфигурации для **33 entities** в Home Assistant (с 2026-05-09 добавлены `ro_plant_s4` и 10 сенсоров KWS-306L: V/A/W/kWh/°C для каждого из двух насосов).

**Формат топика Discovery:**
`homeassistant/{type}/ro_plant/{object_id}/config`

### 4.1 Sensor Entities

| Object ID | Название | Топик данных | Единицы |
|-----------|----------|-------------|---------|
| `ro_plant_state` | RO State | `.../status/state` | — |
| `ro_plant_faults` | RO Fault Flags | `.../status/state` | — |
| `ro_plant_p1` | RO P1 | `.../status/analog/P1` | bar |
| `ro_plant_p2` | RO P2 | `.../status/analog/P2` | bar |
| `ro_plant_p3` | RO P3 | `.../status/analog/P3` | bar |
| `ro_plant_p4` | RO P4 | `.../status/analog/P4` | bar |
| `ro_plant_temp` | RO Temperature | `.../status/analog/T` | °C |
| `ro_plant_q1` | RO Q1 Flow | `.../status/flow/Q1` | m³/h |
| `ro_plant_q2` | RO Q2 Flow | `.../status/flow/Q2` | m³/h |
| `ro_plant_q3` | RO Q3 Flow | `.../status/flow/Q3` | m³/h |
| `ro_plant_q4` | RO Q4 Flow | `.../status/flow/Q4` | m³/h |
| `ro_plant_s1` | RO Feed Conductivity | `.../status/conductivity/s1` | µS/cm |
| `ro_plant_s2` | RO Perm1 Conductivity | `.../status/conductivity/s2` | µS/cm |
| `ro_plant_s3` | RO Perm2 Conductivity | `.../status/conductivity/s3` | µS/cm |
| `ro_plant_s4` | RO Concentrate Conductivity | `.../status/conductivity/s4` | µS/cm |
| `ro_plant_lp_voltage` | RO LP Pump Voltage | `.../status/power/lp` | V |
| `ro_plant_lp_current` | RO LP Pump Current | `.../status/power/lp` | A |
| `ro_plant_lp_power` | RO LP Pump Power | `.../status/power/lp` | W |
| `ro_plant_lp_energy` | RO LP Pump Energy | `.../status/power/lp` | kWh |
| `ro_plant_lp_temperature` | RO LP Pump Temperature | `.../status/power/lp` | °C |
| `ro_plant_hp_voltage` | RO HP Pump Voltage | `.../status/power/hp` | V |
| `ro_plant_hp_current` | RO HP Pump Current | `.../status/power/hp` | A |
| `ro_plant_hp_power` | RO HP Pump Power | `.../status/power/hp` | W |
| `ro_plant_hp_energy` | RO HP Pump Energy | `.../status/power/hp` | kWh |
| `ro_plant_hp_temperature` | RO HP Pump Temperature | `.../status/power/hp` | °C |
| `ro_plant_filter_dp` | RO Filter dP | `.../status/telemetry` | bar |
| `ro_plant_recovery` | RO System Recovery | `.../status/telemetry` | % |
| `ro_plant_sel1` | RO Stage1 Selectivity | `.../status/telemetry` | % |
| `ro_plant_sel2` | RO Stage2 Selectivity | `.../status/telemetry` | % |
| `ro_plant_heap` | RO Free Heap | `.../status/diagnostics` | B |
| `ro_plant_uptime_diag` | RO Uptime | `.../status/diagnostics` | s |

### 4.2 Binary Sensor Entities

| Object ID | Название | Топик данных | Device Class |
|-----------|----------|-------------|-------------|
| `ro_plant_estop` | RO E-STOP | `.../status/interlocks` | safety |
| `ro_plant_filter_warn` | RO Filter Warning | `.../status/interlocks` | problem |

### 4.3 Device Info

```json
{
  "identifiers": ["ro_plant_001"],
  "name": "RO Plant Controller",
  "manufacturer": "Custom",
  "model": "ESP32-S3 RO",
  "sw_version": "1.0.0"
}
```

---

## 5. Дерево топиков

```
ro_plant/
├── availability                    → "online" / "offline" (LWT)
├── status/
│   ├── state                       → {state, auto_sub, wash_sub, fault_flags}
│   ├── io                          → {di, do}
│   ├── analog/
│   │   ├── P1                      → {value, unit, fault}
│   │   ├── P2                      → {value, unit, fault}
│   │   ├── P3                      → {value, unit, fault}
│   │   ├── P4                      → {value, unit, fault}
│   │   └── T                       → {value, unit, fault}
│   ├── flow/
│   │   ├── Q1                      → {flow, volume, ok}
│   │   ├── Q2                      → {flow, volume, ok}
│   │   ├── Q3                      → {flow, volume, ok}
│   │   └── Q4                      → {flow, volume, ok}
│   ├── conductivity/
│   │   ├── s1                      → {conductivity, temperature, ok}  (Feed)
│   │   ├── s2                      → {conductivity, temperature, ok}  (Perm1)
│   │   ├── s3                      → {conductivity, temperature, ok}  (Perm2)
│   │   └── s4                      → {conductivity, temperature, ok}  (Conc)
│   ├── power/
│   │   ├── lp                      → {voltage, current, power, energy, temperature, online}  (KWS slave 20)
│   │   └── hp                      → {voltage, current, power, energy, temperature, online}  (KWS slave 21)
│   ├── telemetry                   → {filter_dp, stage1_feed, recovery2, recovery_sys, sel1, sel2}
│   ├── doser                       → {state, enabled}
│   ├── interlocks                  → {flags, estop, filter_warn}
│   └── diagnostics                 → {heap_free, heap_min, uptime_s, stack, modbus, wdt_stale}
├── alarms                          → {id, ts, cat, code, value, active}
├── command/
│   ├── mode                        ← "start_auto|stop|start_washing|confirm_wash|set_manual|reset_fault"
│   ├── pump                        ← {"mask": N}       (только MANUAL)
│   ├── doser                       ← {"enabled": bool}
│   └── heater                      ← {"on": bool}      (только MANUAL)
└── settings/
    ├── pressure                    ← {p1_max, p3_max, p4_max, filter_dp_warn}
    ├── doser                       ← {run_time_min, cycle_time_min}
    ├── washing                     ← {target_temp_C, max_temp_C, t_overshoot_C, hysteresis_C, heat_timeout_min, supply_time_min, drain_time_min}
    └── timeouts                    ← {pump_confirm_ms, pump_ramp_ms}
```
