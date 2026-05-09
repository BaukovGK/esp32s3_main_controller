# ESP32-S3 MC — Контроллер установки обратного осмоса

Микроконтроллерная система управления установкой обратного осмоса (RO) на базе платы **Waveshare ESP32-S3-ETH-8DI-8RO**.

## Аппаратная платформа

| Параметр | Значение |
|----------|----------|
| MCU | ESP32-S3 |
| Плата | Waveshare ESP32-S3-ETH-8DI-8RO |
| Flash | 16 MB |
| PSRAM | Octal, 80 MHz |
| Ethernet | W5500 (SPI) |
| Дискретные входы | 8 (DI1–DI8, оптоизолированные) |
| Релейные выходы | 8 (RO1–RO8, через TCA9554 I2C) |
| RS-485 | Modbus RTU (опрос датчиков) |
| I2C | TCA9554 (выходы), PCF85063 (RTC) |

## Архитектура компонентов

```
main/                  Точка входа, инициализация всех подсистем
components/
├── board_hal/         HAL: GPIO (дебаунс 50мс), I2C (mutex), NVS, UART
├── ethernet_init/     Инициализация W5500 Ethernet (DHCP)
├── modbus_poller/     Modbus RTU мастер — опрос до 4 slave-устройств
├── drivers/           Драйверы датчиков:
│   ├── analog_input   Давление/температура (4–20мА, скользящее среднее N=8)
│   ├── flowmeter      Расходомер УРЖ2КМ (word-swapped IEEE 754)
│   └── conductivity   Кондуктометры СЛ21 (3 канала, 2 прибора)
├── process/           Логика управления:
│   ├── state_machine  КА: IDLE → AUTO → WASH → ERROR → MANUAL
│   ├── interlocks     Блокировки (E-STOP, давление, температура, сухой ход)
│   ├── doser          Дозатор антискаланта (циклический таймер)
│   └── process_task   Главный цикл 100мс
├── services/          Системные сервисы:
│   ├── config_manager Конфигурация в NVS (потокобезопасная, с валидацией)
│   ├── alarm_manager  Аварии: 4 уровня, кольцевой буфер, callbacks
│   ├── watchdog_task  Watchdog: 3с → safe state, 10с → restart
│   ├── diagnostics    Heap, стеки задач, статус Modbus-устройств
│   └── telemetry      Расчётные параметры (перепад, recovery, селективность)
├── mqtt_app/          MQTT-клиент:
│   ├── mqtt_app       Подключение к брокеру (LWT, авторизация)
│   ├── mqtt_publish   Телеметрия + Home Assistant Discovery
│   └── mqtt_subscribe Приём команд (старт, стоп, промывка, настройки)
└── web_server/        HTTP-сервер (порт 80):
    ├── web_static     Встроенный веб-интерфейс (SPIFFS)
    ├── web_api_status  GET: /api/status, /api/config, /api/alarms, /api/diag
    └── web_api_command POST: команды, ручное управление, настройки
```

## Задачи FreeRTOS

| Задача | Стек | Приоритет | Период | Назначение |
|--------|------|-----------|--------|------------|
| io_task | 2 KB | 6 | 10 мс | Дебаунс GPIO, E-STOP |
| modbus_poller_task | 4 KB | 6 | — | Опрос Modbus-устройств |
| process_task | 8 KB | 5 | 100 мс | Датчики → КА → выходы |
| watchdog_task | 2 KB | 7 | 1 с | Контроль process_task |
| mqtt_task | — | — | — | Публикация и подписки MQTT |
| httpd | — | — | — | Обработка HTTP-запросов |

## Modbus-устройства

| Устройство | Назначение |
|------------|------------|
| Waveshare AI 8CH | 8 аналоговых входов (4–20мА): давление P1–P4, температура |
| УРЖ2КМ | Ультразвуковой расходомер (4 канала Q/V) |
| СЛ21 #1 (addr 10) | Кондуктометр: вход + пермеат 1-й ступени |
| СЛ21 #2 (addr 11) | Кондуктометр: пермеат 2-й ступени |

## Таблица разделов (16 MB)

| Раздел | Смещение | Размер | Назначение |
|--------|----------|--------|------------|
| nvs | 0x9000 | 24 KB | Конфигурация |
| phy_init | 0xF000 | 4 KB | PHY калибровка |
| factory | 0x10000 | 3 MB | Приложение |
| storage | 0x310000 | 1 MB | SPIFFS (веб-интерфейс) |

## Сборка и прошивка

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Для полной пересборки после изменения конфигурации:

```bash
idf.py fullclean && idf.py set-target esp32s3 && idf.py build
```

## Конфигурация

Все параметры хранятся в NVS и настраиваются через:
- **Веб-интерфейс** — POST-запросы к `/api/config/...`
- **MQTT** — подписки на топики команд
- **menuconfig** — `idf.py menuconfig` (Ethernet, UART, пины)

## TODO: расширение Modbus

Список целевых slave-устройств не определён. Когда появится конкретика,
надо собрать по каждому устройству:

1. Тип устройства (датчик давления / pH / ORP / частотник насоса / ...)
2. Modbus адрес slave (1..247)
3. Тип регистров: Holding (FC03/06/16) / Input (FC04) / Coil / Discrete
4. Стартовый адрес и количество регистров
5. Формат данных: uint16 / int16 / uint32 (hi-lo) / IEEE754 / IEEE754 word-swap / scale factor
6. Период опроса (быстрый <500мс / медленный >1с)
7. Нужна ли запись (сейчас все READ)

После сбора списка — выполнить рефакторинг poller'а под обобщённый дескриптор
(`mb_device_descriptor_t` + `modbus_poller_register/read/is_online_h`), вместо
текущей жёсткой схемы с per-CID enum + статическими буферами + per-CID
геттерами. Это устранит правки в 6 местах при добавлении устройства.

См. предложение архитектуры в `doc/` (раздел про Modbus extension).

## Phase-1: фиксы отказоустойчивости (P0)

Реализованные критические фиксы (см. `doc/` для деталей):

| ID | Что | Где |
|---|---|---|
| K-1 | NaN от датчика → блокировка зависимого агрегата (`INTERLOCK_SENSOR_FAULT_*`) | `interlocks.c` |
| K-2 | Readback TCA9554 раз в секунду + alarm `DO_READBACK_FAIL` | `hal_gpio.c` + `process_task.c` |
| K-3 | DEADLOCK fix: spinlock → FreeRTOS mutex для DO state | `hal_gpio.c` |
| K-5 | Watchdog покрывает process / io / modbus задачи (handle-based API) | `watchdog_task.c`, `app_main.c` |
| K-6 | `portMAX_DELAY` → конечные таймауты (100–200 мс) на всех mutex'ах | `hal_i2c.c`, `modbus_poller.c` |
| K-7 | Mutex в `alarm_manager` (раньше — гонки между process/mqtt/httpd) | `alarm_manager.c` |
| K-8 | Алармы `MODBUS_OFFLINE` поднимаются при потере связи | `process_task.c` |

## Phase-2: надёжность (P1)

| ID | Что | Где |
|---|---|---|
| K-4 | Persistence SM-state + fault_flags в NVS. После перезагрузки в AUTO/WASHING — FAULT с `INTERLOCK_UNEXPECTED_RESTART`, оператор должен сделать reset. | `state_machine.c` |
| M-2 | `esp_reset_reason()` логируется при старте, поднимается `ALARM_UNEXPECTED_RESTART` при panic/WDT/brownout | `app_main.c` |
| H-9 | Backoff опроса offline Modbus-устройств: error_count >= 10 → опрос раз в 30 сек (вместо каждого периода) | `modbus_poller.c` |
| H-1 | `config_manager_get_pressure/doser/washing/timeouts(out)` — атомарные снимки секций для горячих путей (interlocks, SM, doser) | `config_manager.c`, `interlocks.c`, `state_machine.c`, `doser.c` |
| H-6 | `mqtt_app_reconnect()` сериализован через mutex — два параллельных POST `/api/v1/config/mqtt` больше не приводят к double-destroy | `mqtt_app.c` |

## TODO: дозатор в WASHING

> **Статус:** в backlog. Изменение требований относительно ТЗ v2.0 (где явно: «Не включается в режиме WASHING»). Не реализовано.
>
> **Текущее поведение:** дозатор работает только в `SM_AUTO` подсостоянии `AUTO_RUNNING`. В `SM_WASHING` всегда выключен.

### Что нужно сделать

**Цель:** дозатор должен работать с собственной периодикой во время промывки (`SM_WASHING`), для подачи промывочного реагента (CIP — кислотного / щелочного / антискалантового).

### Архитектурный план

1. **Конфиг** ([components/services/include/config_manager.h](components/services/include/config_manager.h))
   ```c
   typedef struct {
       int32_t run_time_min;        /* AUTO: время работы, мин (5) */
       int32_t cycle_time_min;      /* AUTO: период, мин (60) */
       int32_t wash_run_time_min;   /* WASHING: время, мин (по умолч. ?) */
       int32_t wash_cycle_time_min; /* WASHING: период, мин (по умолч. ?) */
   } config_doser_t;
   ```
   - **Дефолты для WASHING нужно согласовать** — мой первый подход 2/10 мин был умозрительным. Зависит от используемого реагента и объёма мембран.
   - Добавить валидацию + инвариант `wash_run < wash_cycle`.
   - Добавить NVS-ключи `dos_wrun`, `dos_wcyc`.

2. **API дозатора** ([components/process/include/doser.h](components/process/include/doser.h))
   - Заменить `doser_update(bool auto_running)` на `doser_update(doser_phase_t phase)`:
     ```c
     typedef enum { DOSER_PHASE_OFF, DOSER_PHASE_AUTO, DOSER_PHASE_WASH } doser_phase_t;
     ```
   - Добавить `doser_get_phase()` для диагностики.
   - При смене phase **сбрасывать таймер** — иначе при переходе AUTO→WASH дозатор может застрять в PAUSE со старыми параметрами.

3. **Логика выбора фазы** ([components/process/process_task.c](components/process/process_task.c))
   - `SM_AUTO + AUTO_RUNNING` → `DOSER_PHASE_AUTO`
   - `SM_WASHING + ?` → `DOSER_PHASE_WASH` ← **что считать «активной» подфазой WASHING для дозирования?**
     Кандидаты (где работает насос подачи):
     - `WASH_HEATING` — нагрев горячей водой
     - `WASH_WAIT_SUPPLY` — ожидание оператора, насос работает
     - `WASH_SUPPLY` — основная фаза подачи через мембраны
     Уточнить у технолога: дозируется ли реагент уже на нагреве или только на SUPPLY?
   - `WASH_WAIT_HEAT`, `WASH_WAIT_DRAIN`, `WASH_DRAIN`, `WASH_DONE` → `DOSER_PHASE_OFF`
   - Иначе → `DOSER_PHASE_OFF`.

4. **MQTT/web** — добавить поле `phase` в `ro_plant/status/doser`:
   ```json
   { "state": "OFF|RUNNING|PAUSE", "phase": "OFF|AUTO|WASH", "enabled": true }
   ```
   - `ro_plant/settings/doser` принимает `wash_run_time_min`, `wash_cycle_time_min`.
   - HA Discovery: добавить `RO Doser Phase` sensor.
   - GET `/api/v1/status` — поле `doser.phase`.
   - POST `/api/v1/config/doser` — новые поля.

5. **Тесты** ([test/test_doser.c](test/test_doser.c))
   - `test_doser_wash_phase_uses_wash_periods` — проверить, что WASH использует свои параметры.
   - `test_doser_phase_change_resets_cycle` — проверить сброс таймера при AUTO→WASH.
   - `test_doser_wash_config_change` — динамическое изменение wash-периода.

6. **ТЗ** ([doc/ro_plant_firmware_spec_v2.md](doc/ro_plant_firmware_spec_v2.md))
   - Убрать критерий «Не включается в режиме WASHING».
   - Добавить новый критерий: «дозатор включается с периодикой `wash_*` в активных подфазах WASHING».

### Открытые вопросы перед реализацией

1. **Какой реагент дозируется в WASHING?** (антискалант / кислота / щёлочь / биоцид). От этого зависят дефолты и название параметра в UI.
2. **В каких подфазах WASHING нужно дозировать?** (только SUPPLY, или с HEATING тоже).
3. **Какие значения по умолчанию для `wash_run_time_min` / `wash_cycle_time_min`?**
4. **Один и тот же насос-дозатор для AUTO и WASHING (RO5), или это два разных насоса?** Если два — нужен второй DO и отдельный модуль.

Без ответов на эти вопросы прежняя реализация была бы преждевременной — параметры пришлось бы переопределять при первой же приёмке.

---

## Phase-4: расширения и завершение backlog'а

| ID | Что | Где |
|---|---|---|
| **M-4** | Spinlock на горячих данных драйверов (`analog_input`, `flowmeter`, `conductivity`). Update собирает локальный snapshot, потом атомарно публикует под mux. Readers (mqtt_task / httpd) больше не получают разорванные снимки. | [drivers/](components/drivers/) |
| **M-6** | `modbus_poller_get_slave_addrs()` — расширяемый список адресов. `diagnostics` теперь итерируется по живой таблице poller'а (раньше был жёстко прописан массив 4 устройств). MQTT/web публикация переписана на массив объектов `[{addr,errors,online}, ...]`. | [modbus_poller.c/h](components/modbus_poller/), [diagnostics.c/h](components/services/) |
| **L-4 ext (RGB)** | `hal_rgb` — WS2812 на GPIO 38 через managed `espressif/led_strip`. Цвет привязан к состоянию SM с приоритетом активных аварий: красный мигающий = CRITICAL, красный = FAULT, оранжевый мигающий = WAIT_*, синий = WASHING, зелёный = AUTO_RUNNING, фиолетовый = MANUAL, тусклый белый = IDLE. Тики моргания каждый цикл (100мс), пересчёт цвета раз в 5 сек. | [hal_rgb.c/h](components/board_hal/) |
| **Silence** | MQTT `ro_plant/command/silence` + REST `POST /api/v1/silence` — глушение buzzer'а до следующего изменения списка аварий. Замена аппаратной кнопки (на плате нет свободных DI). | [mqtt_subscribe.c](components/mqtt_app/), [web_api_command.c](components/web_server/) |
| **H-13** | Опциональная HTTP Basic Auth — config-gated: пустой `web_auth.username` = выкл (по умолчанию, обратная совместимость). При непустом — все API endpoints проверяют заголовок `Authorization`, статика открыта (для UX браузера). Pre-encoded base64 кэшируется через mbedtls. | [web_auth.c/h](components/web_server/), [config_manager.c](components/services/) |

## Phase-3: промышленная зрелость (P2-P3)

| ID | Что | Где |
|---|---|---|
| L-5 | Host-тесты драйверов: flowmeter (word-swap IEEE-754), conductivity (uint32 + temp), analog_input (4-20мА + MA-фильтр + offline) | `test/test_flowmeter.c`, `test_conductivity.c`, `test_analog_input.c`, `mocks/mock_modbus_poller.c` |
| L-1 | Default-ветки в SM-switch'ах. При повреждении enum (EMI, corruption) — переход в FAULT вместо undefined behavior | `state_machine.c` |
| M-3 | Heap monitoring: `ALARM_LOW_HEAP` при < 16 КБ, снимается при > 20 КБ (гистерезис 4 КБ против flap) | `process_task.c` |
| M-7 | Soft-fail в init для web_server и mqtt_app. Отказ сетевых сервисов больше не валит контроллер | `app_main.c` |
| L-4 | `hal_buzzer` — пьезо-зуммер на GPIO 46. Паттерны OFF/SHORT/SLOW/CONTINUOUS по самой опасной активной аварии | `hal_buzzer.c/h`, `process_task.c` |

Тесты: **9 host-suites, 60 проверок** — все зелёные. Phase-4 не добавила новых тестов (изменения касаются IDF-зависимых модулей: led_strip, mbedtls — не покрываются host-моками).

### Не реализовано

- **MQTT ACL** — серверная настройка broker'а, не код прошивки. Если контроллер в недоверенной сети — настроить ACL на самом MQTT-брокере.
- **Hardware silence button** — на плате нет свободных DI (все 8 заняты: см. `board_config.h`). Замена: MQTT/REST silence (реализовано).
- **Modbus extension** — ждёт списка устройств (см. TODO выше).
- **Дозатор в WASHING** — отложено по решению (см. TODO выше).
