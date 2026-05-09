# Инструкция для Claude Code сессий

> Этот файл автоматически читается агентом Claude Code при старте сессии.
> Здесь зафиксированы правила работы с проектом, чтобы каждая новая сессия не задавала тех же вопросов.

## Git workflow (строго)

| Что | Куда |
|---|---|
| **Bugfix** | прямо в `dev` (commit + push) |
| **Новая фича** | feature-ветка `feature/<descr>` от `dev` → PR в `dev` |
| **Релиз** | PR `dev` → `main` (только когда проект полностью рабочий на стенде) |

- `main` защищена, прямой push заблокирован — слияние через GitHub PR review
- `dev` — рабочая, можно коммитить напрямую
- НЕ пытайтесь делать `git checkout main` + merge + push — система блокирует
- Если нужен PR — попросите пользователя создать через GitHub UI, либо используйте `gh pr create` с явным разрешением пользователя

## Тесты

- 11+ host test suites в `test/`. Прогон:
  ```bash
  cd /Users/bgk/projects/RO_system/Controller
  cmake --build test/build
  ctest --test-dir test/build --output-on-failure
  ```
- Каждый коммит должен оставлять тесты зелёными (текущий baseline: 11/11)
- Новые модули с публичным API покрываются hosт-тестами по образцу `test/test_*.c`
- Mock'и в `test/mocks/`, stub'ы для ESP-IDF API в `test/stubs/`

## Целевая платформа

- ESP-IDF **5.4.4 LTS** (НЕ 6.x — там mqtt component переехал в managed_components, у нас не работает)
- ESP32-S3 на плате Waveshare ESP32-S3-ETH-8DI-8RO
- Сборка: `idf.py build` из корня проекта (на Windows через `build.bat`)
- При изменении managed_components — `dependencies.lock` коммитить вместе с правкой

## Modbus устройства

| Slave | Прибор | Адрес назначен |
|---:|---|---|
| 1 | Waveshare Analog Input 8CH (4-20 mA) | default |
| 2 | УРЖ2КМ 4-канальный расходомер | сменён с 1 |
| 10 | СЛ21-100Т #1 (FEED + PERM1) | сменён с 5 |
| 11 | СЛ21-100Т #2 (PERM2 + CONC) | сменён с 5 |
| 20 | KWS-306L 3-фазный (НД-насос) | сменён с 1 |
| 21 | KWS-306L 3-фазный (ВД-насос) | сменён с 1 |

Полная карта регистров: `doc/modbus_signal_map.md`. Адреса и параметры связи: `doc/modbus_addresses.md`.

## API спецификации

- **MQTT:** `doc/api/asyncapi.yaml` (AsyncAPI 2.6, 20 каналов)
- **REST:** `doc/api/openapi.yaml` (OpenAPI 3.0, 18 endpoints)

При изменении публичного API — синхронизировать YAML, иначе клиенты HMI/Home Assistant начнут расходиться.

## Принципы кода

- **Гонки**: статические структуры, к которым ходят разные FreeRTOS-задачи (mqtt/httpd/process), защищены `portMUX_TYPE` спинлоком (snapshot-pattern: writer строит локальную копию, копирует в `s_data` под локом одной операцией; reader читает поле под тем же локом).
- **Mutex таймауты**: всегда конечные. Использование `portMAX_DELAY` запрещено вне самых низкоуровневых hal-вызовов.
- **HAL не зависит от services**: HAL возвращает `esp_err_t`, alarm raises делает caller. Иначе цикл services↔board_hal.
- **NaN как «нет данных»**: при offline / sensor fault / before_first_poll драйверы возвращают NaN, а не 0. MQTT/REST публикуют `null` в JSON через `fmt_float()` / `add_float_or_null()`.
- **`_Static_assert`** для соответствия размеров массивов перечислениям (например `cond_names` vs `COND_CHANNEL_COUNT`) — было два OOB-бага из-за рассинхрона при расширении 3→4 канала, теперь не повторится.

## Что осталось до полностью рабочего проекта

- Стенд: перенастроить адреса всех приборов, СЛ21 baud → 9600
- Сборка под IDF 5.4.4 на target (проверить `_Static_assert` и линковку power_meter/mb_device_check)
- Заливка + проверка health-check на железе
- Сверка KWS-306L с datasheet (карта может быть неполная: фазные U/I, energy uint32)

См. `doc/modbus_signal_map.md` §7 (гэп-анализ) для актуального списка.
