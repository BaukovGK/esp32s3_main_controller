# Документация: power_meter (Драйвер счётчиков электроэнергии KWS-306L)

**Файлы:**
- Заголовочный: `include/power_meter.h`
- Реализация: `power_meter.c`

---

## Общее описание

Драйвер для трёхфазных счётчиков электроэнергии **KWS-306L**, подключённых по RS-485 (Modbus RTU). Каждый счётчик измеряет потребление одного из двух насосов установки обратного осмоса:

- **slave 20** (`MB_ADDR_KWS_PUMP_LP`) — НД-насос (низкое давление, насос подачи)
- **slave 21** (`MB_ADDR_KWS_PUMP_HP`) — ВД-насос (высокое давление, насос RO)

Драйвер выполняет:
1. Чтение блока 14 регистров `0x000E..0x001B` через `modbus_poller_get_kws_lp_raw` / `_hp_raw` (FC `0x03`).
2. Конвертация raw-регистров в инженерные единицы.
3. Хранение последнего снимка под `portMUX_TYPE` спинлоком (по образцу conductivity, Phase-4 M-4).
4. Контроль online-статуса каждого насоса независимо (через `modbus_poller_is_device_online`).
5. Возврат `NaN` через геттеры при offline или до первого успешного опроса.

**Не входит в эту реализацию (следующие шаги):**
- Управление реле через `0x003F` (FC `0x06`).
- Интеграция в `state_machine` (interlocks по low-current / no-current / over-power).
- Публикация в MQTT.

---

## Регистровая карта KWS-306L (подтверждено заказчиком)

Опрашиваемый блок 14 регистров одним запросом `0x000E..0x001B`:

| Hex адрес | Смещение в блоке | Параметр       | Тип     | Множитель        | Единицы |
|-----------|------------------|----------------|---------|------------------|---------|
| `0x000E`  | 0                | Voltage        | uint16  | × 0.1            | В       |
| `0x000F`  | 1                | (неизвестно)   | uint16  | —                | TODO    |
| `0x0010`  | 2                | Current        | uint16  | × 0.001          | А       |
| `0x0011`  | 3                | (неизвестно)   | uint16  | —                | TODO    |
| `0x0012`  | 4                | Power (active) | uint16  | × 0.1            | Вт      |
| `0x0013..0x0019` | 5..11     | (неизвестно)   | uint16  | —                | TODO    |
| `0x001A`  | 12               | Energy         | uint16  | × 0.01           | кВт·ч   |
| `0x001B`  | 13               | Temperature    | uint16  | × 1              | °C      |

Регистр управления реле (не читается этим блоком):

| Hex адрес | Параметр  | FC     | Значения     |
|-----------|-----------|--------|--------------|
| `0x003F`  | Реле      | `0x06` | 1=Вкл / 0=Выкл |

---

## Известные пробелы / TODO datasheet

1. **Energy uint16 × 0.01 → max 655.35 кВт·ч** — переполнится за дни работы. Реально, скорее всего, `0x001A+0x001B` представляют собой `uint32` energy, а temperature расположена в другом регистре. Нужно опросить полный блок `0x0000..0x0040` при первом подключении.
2. **3-фазная природа KWS-306L** — в карте присутствует только один канал U/I/P, но устройство трёхфазное. Скорее всего, фазные значения (Ua/Ub/Uc, Ia/Ib/Ic, Pa/Pb/Pc) расположены в неизвестных регистрах `0x000F`, `0x0011`, `0x0013..0x0019`. Запросить datasheet у поставщика.
3. **Temperature** — единицы (`°C`) и масштаб (`× 1`) предположительные. Возможны варианты `× 0.1` или `× 0.01`.
4. Регистр `0x003F` (реле) — функциональность не реализована, оставлена TODO для следующего этапа.

---

## Публичный API

### `power_meter_init(void)`
Однократная инициализация. Зануляет state, online/valid = false для обоих насосов.

### `power_meter_update(void)`
Периодически вызывается из основного цикла. Запрашивает свежие raw-регистры через `modbus_poller`, конвертирует, обновляет state под локом.

### `power_meter_get_data(pump_id_t pump, power_meter_data_t *out)`
Атомарно копирует снимок в буфер вызывающего. NULL-out / `pump >= PUMP_COUNT` — no-op.

### Геттеры одиночных значений
```c
float power_meter_get_voltage(pump_id_t pump);
float power_meter_get_current(pump_id_t pump);
float power_meter_get_power(pump_id_t pump);
float power_meter_get_energy(pump_id_t pump);
float power_meter_get_temperature(pump_id_t pump);
```
Возвращают `NaN`, если `data.valid == false` или `pump >= PUMP_COUNT`.

### `power_meter_is_online(pump_id_t pump)`
`true` только при `online && first_poll_done` (поведение `modbus_poller_is_device_online`).

---

## Структура данных

```c
typedef struct {
    float voltage_V;        /* В,    raw 0x000E × 0.1   */
    float current_A;        /* А,    raw 0x0010 × 0.001 */
    float power_W;          /* Вт,   raw 0x0012 × 0.1   */
    float energy_kWh;       /* кВт·ч raw 0x001A × 0.01  */
    float temperature_C;    /* °C    raw 0x001B × 1     */
    bool  online;           /* устройство отвечает на шине */
    bool  valid;            /* online && first_poll_done && OK */
} power_meter_data_t;
```

---

## Константы

### Публичные (`power_meter.h`)
| Константа     | Значение | Описание                             |
|---------------|----------|--------------------------------------|
| `PUMP_LP`     | 0        | Идентификатор НД-насоса (slave 20)   |
| `PUMP_HP`     | 1        | Идентификатор ВД-насоса (slave 21)   |
| `PUMP_COUNT`  | 2        | Количество насосов                   |

### Приватные (`power_meter.c`)
| Константа               | Значение | Описание                            |
|-------------------------|----------|-------------------------------------|
| `KWS_REG_OFFSET_VOLTAGE`| 0        | Смещение voltage в блоке `0x000E..0x001B` |
| `KWS_REG_OFFSET_CURRENT`| 2        | Смещение current                    |
| `KWS_REG_OFFSET_POWER`  | 4        | Смещение power                      |
| `KWS_REG_OFFSET_ENERGY` | 12       | Смещение energy                     |
| `KWS_REG_OFFSET_TEMP`   | 13       | Смещение temperature                |
| `KWS_VOLTAGE_SCALE`     | 0.1f     | raw → В                             |
| `KWS_CURRENT_SCALE`     | 0.001f   | raw → А                             |
| `KWS_POWER_SCALE`       | 0.1f     | raw → Вт                            |
| `KWS_ENERGY_SCALE`      | 0.01f    | raw → кВт·ч                         |
| `KWS_TEMP_SCALE`        | 1.0f     | raw → °C                            |

---

## Поведение при ошибках

| Источник                                     | Поведение                              |
|----------------------------------------------|----------------------------------------|
| `modbus_poller_get_kws_*_raw` → `ESP_ERR_INVALID_STATE` (до первого опроса) | `valid=false`, геттеры → `NaN` |
| `modbus_poller_get_kws_*_raw` → `ESP_ERR_TIMEOUT`                            | `valid=false`, геттеры → `NaN` |
| `modbus_poller_is_device_online()` → `false`                                 | `online=false`, `valid=false`, геттеры → `NaN` |
| `pump >= PUMP_COUNT`                                                         | геттеры → `NaN`, `_get_data` no-op    |
| `out == NULL` в `_get_data`                                                  | no-op                                  |

---

## Параметры опроса (modbus_poller.c)

| Параметр              | Значение           | Описание                        |
|-----------------------|--------------------|---------------------------------|
| `MB_POLL_PERIOD_KWS_MS` | `2000`            | Период опроса блока, мс         |
| `CID_KWS_REG_COUNT`   | `14`               | Размер блока в регистрах        |
| `mb_reg_start`        | `0x000E`           | Начальный адрес блока           |
| `mb_param_type`       | `MB_PARAM_HOLDING` | FC `0x03`                       |

---

## Тесты

Файл: `test/test_power_meter.c`. Использует `mock_modbus_poller`. 6 тестов:

1. `test_pm_basic_all_fields_read` — типовые значения 230 В / 5 А / 1100 Вт / 12.5 кВт·ч / 45 °C.
2. `test_pm_voltage_hex_round_trip` — сырые hex-значения (`0x08FC` = 230 В и т.д.).
3. `test_pm_offline_yields_nan` — `mock_mb_set_online(false)` → геттеры NaN.
4. `test_pm_two_pumps_independent` — НД и ВД с разными значениями (220 В / 380 В).
5. `test_pm_no_first_poll_yields_nan` — `mock_mb_clear_first_poll` → `valid=false`, NaN.
6. `test_pm_one_pump_offline_other_ok` — изоляция отказа одного из двух насосов.
