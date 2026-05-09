# API Specifications

Формальные машиночитаемые спецификации API контроллера RO-установки.

## Файлы

| Файл              | Формат         | Что описывает                                              |
|-------------------|----------------|------------------------------------------------------------|
| `asyncapi.yaml`   | AsyncAPI 2.6.0 | MQTT-топики (publish/subscribe) с JSON-схемами payload     |
| `openapi.yaml`    | OpenAPI 3.0.3  | HTTP REST API встроенного веб-сервера + статика HMI        |

## AsyncAPI (MQTT)

В файле зафиксированы:

- `ro_plant/status/*` — публикация всех снимков (state, IO, аналоги P1..P4/T,
  расходы Q1..Q4, проводимость s1..s4, мощность LP/HP, телеметрия, дозатор,
  интерлоки, диагностика);
- `ro_plant/alarms`, `ro_plant/availability` — события и LWT;
- `homeassistant/{sensor,binary_sensor}/ro_plant/.../config` — Home
  Assistant MQTT Discovery (~21 сущность);
- `ro_plant/command/*`, `ro_plant/settings/#` — входящие команды и
  обновление подсекций конфигурации в рантайме.

Для каждого канала указаны QoS / retain (см. шапку `asyncapi.yaml`),
JSON-схема payload точно соответствует `snprintf` в
`components/mqtt_app/mqtt_publish.c`. Поля, которые могут быть `null`
(датчик offline или нет первого опроса), помечены `nullable: true`.

### Как смотреть / валидировать

```bash
# Web редактор (онлайн):
#   https://studio.asyncapi.com → File → Import → выбрать asyncapi.yaml

# CLI-валидация:
npx -y @asyncapi/cli@latest validate doc/api/asyncapi.yaml

# Сгенерировать HTML-документацию:
npx -y @asyncapi/cli@latest generate fromTemplate \
    doc/api/asyncapi.yaml @asyncapi/html-template \
    -o doc/api/_html-asyncapi
```

## OpenAPI (REST)

В файле зафиксированы все 17 endpoints:

- 5 GET (`/api/v1/status`, `/config`, `/mqtt/status`, `/alarms`,
  `/diagnostics`) и 3 GET статики (`/`, `/style.css`, `/app.js`);
- 9 POST (`/api/v1/command`, `/silence`, `/manual/do`, `/doser/enable`,
  `/config/{web_auth,pressure,doser,washing,timeouts,mqtt}`).

Авторизация — HTTP Basic. При пустом `username` в `config.web_auth`
проверка пропускает запросы (backward-compat); при непустом —
все endpoints (включая GET) требуют `Authorization: Basic ...`.
Глобальная `security` объявлена как `[basicAuth] OR {}` — соответствует
условному включению auth по конфигу.

Формат ошибок единый: `{"ok": false, "error": "..."}` со статусом
400 / 409 / 401. Успех POST: `{"ok": true}`.

### Как смотреть / валидировать

```bash
# Swagger Editor (онлайн):
#   https://editor.swagger.io → File → Import file → openapi.yaml

# Локальный Swagger UI через Docker:
docker run --rm -p 8080:8080 \
    -e SWAGGER_JSON=/api/openapi.yaml \
    -v "$(pwd)/doc/api:/api" \
    swaggerapi/swagger-ui

# CLI-валидация:
npx -y @apidevtools/swagger-cli validate doc/api/openapi.yaml

# Альтернатива (Redocly):
npx -y @redocly/cli lint doc/api/openapi.yaml
```

## Соответствие коду

Спецификации сгенерированы вручную из исходников:

- `components/mqtt_app/mqtt_publish.c` — все `esp_mqtt_client_publish()`;
- `components/mqtt_app/mqtt_subscribe.c` — `esp_mqtt_client_subscribe()`
  и обработчики `handle_*`;
- `components/mqtt_app/mqtt_app.c` — LWT, online-availability;
- `components/web_server/web_api_status.c` — GET endpoints;
- `components/web_server/web_api_command.c` — POST endpoints;
- `components/web_server/web_static.c` — статика HMI;
- `components/services/include/alarm_manager.h` + `alarm_manager.c` —
  enum кодов и категорий аварий;
- `components/process/state_machine.c` — имена состояний и подсостояний.

**Правило синхронизации:** при изменении публичного API (новый топик,
новый endpoint, изменение схемы payload) — обнови соответствующий YAML.
Иначе клиенты HMI / Home Assistant / диспетчерской системы начнут
расходиться с реальностью контроллера.

## Известные расхождения / TODO

- `/api/v1/status` возвращает массив `cond` из 3 элементов, в то время
  как MQTT публикует 4 канала проводимости (`s1..s4`, добавлен `s4`
  концентрат 2026-05-09). См. `cond_names` в `web_api_status.c:148`.
- Поле `wdt_stale` в `ro_plant/status/diagnostics` — заглушка (всегда 0).
