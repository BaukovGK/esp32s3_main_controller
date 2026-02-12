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
