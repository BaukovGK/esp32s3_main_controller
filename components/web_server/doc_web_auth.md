# web_auth — HTTP Basic Auth для веб-сервера (Phase-4, H-13)

## Описание

Опциональная HTTP Basic Auth для всех API-endpoint'ов. **Backward-compat**: при пустом `web_auth.username` (значение по умолчанию) проверка отключена — поведение полностью совпадает с прошивкой до Phase-4.

При непустом username каждый handler первой строкой вызывает `web_auth_check(req)`; функция сверяет заголовок `Authorization: Basic <base64>` с pre-encoded ожидаемым значением. При несовпадении возвращает 401 + `WWW-Authenticate: Basic realm="RO Plant"`.

**Файлы:**
- Заголовок: `components/web_server/include/web_auth.h`
- Реализация: `components/web_server/web_auth.c`
- Зависимость: `mbedtls` (компонент ESP-IDF) для `mbedtls_base64_encode`.

## Логика кэша

При `web_auth_init()` (вызывается из `web_server_start`) и при `web_auth_refresh()` (после изменения конфига):

1. Читается `config.web_auth.username` + `password`.
2. Если username пустой — `s_enabled = false`, проверка пропускается.
3. Иначе формируется строка `"user:pass"`, кодируется в base64, оборачивается в `"Basic <b64>"` и кэшируется в `s_expected[160]`.
4. На каждом запросе сравнивается ровно с этой строкой — без декодирования заголовка (быстрее, без аллокаций).

## API

### `void web_auth_init(void)`
Первичная инициализация кэша. Вызывается в `web_server_start` после `config_manager_init`.

### `void web_auth_refresh(void)`
Перечитать конфиг. Вызывается после `POST /api/v1/config/web_auth`.

### `bool web_auth_check(httpd_req_t *req)`
- `true` — авторизован (или auth выключена), handler продолжает работу.
- `false` — отказ, **функция уже отправила 401**, caller должен сразу `return ESP_OK`.

## Защищённые endpoints

| Endpoint | Метод | Защищён |
|---|---|---|
| `/api/v1/status` | GET | да |
| `/api/v1/config` | GET | да |
| `/api/v1/mqtt/status` | GET | да |
| `/api/v1/alarms` | GET | да |
| `/api/v1/diagnostics` | GET | да |
| `/api/v1/command` | POST | да (через `recv_json`) |
| `/api/v1/silence` | POST | да |
| `/api/v1/manual/do` | POST | да |
| `/api/v1/doser/enable` | POST | да (через `recv_json`) |
| `/api/v1/config/*` | POST | да (через `recv_json`) |
| `/api/v1/config/web_auth` | POST | да (через `recv_json`) |
| `/`, `/style.css`, `/app.js` | GET | **нет** — статика, чтобы браузер мог открыть страницу логина |

## Включение/настройка

Через REST:
```bash
curl -X POST http://192.168.x.y/api/v1/config/web_auth \
     -H "Authorization: Basic $(echo -n 'admin:current_pwd' | base64)" \
     -d '{"username":"operator","password":"new_password"}'
```

Через MQTT (только в plain JSON-формате settings):
```
ro_plant/settings/web_auth → { "username": "...", "password": "..." }
```

(MQTT-обработчик пока не подключён — TODO; web-настройки достаточно для большинства сценариев.)

## Отключение

Прислать пустой username:
```json
{ "username": "", "password": "" }
```

NVS-ключи `web_user`/`web_pass` сохранятся пустыми, при старте кэш будет в выключенном состоянии.

## Безопасность

- **Базовая защита**, не эквивалент TLS. Учётные данные передаются по сети открытым текстом (Basic Auth = base64).
- Для production-развёртывания в недоверенной сети — обязательно TLS reverse proxy (nginx/HAProxy) перед контроллером, или использование esp-tls + `httpd_ssl_start`. Это отдельная задача (TODO).
- Пароль в NVS хранится как plain text. Для повышения защиты — шифровать NVS через `CONFIG_NVS_ENCRYPTION=y`.

## Не реализовано

- **TLS** для самого httpd — нужно `esp_https_server` + сертификат.
- **MQTT settings/web_auth** — настройка через MQTT (сейчас только web).
- **Rate limiting** для попыток логина — при простой Basic Auth не обязательно, но в недоверенной сети полезно.
