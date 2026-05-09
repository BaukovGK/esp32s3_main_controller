# mb_device_check — старт-ап health-check Modbus-устройств

Модуль `mb_device_check.c` исполняется один раз при старте системы (через
одноразовую FreeRTOS-задачу `health_check_task` в `app_main.c`) и
проверяет все 6 устройств на шине RS-485:

| Устройство     | Slave | Что проверяется                                            |
|----------------|------:|-------------------------------------------------------------|
| Waveshare AI   |  1    | FW version, device addr, channel modes (+ автокоррекция)  |
| УРЖ2КМ         |  2    | sanity-диапазон расходов (NaN/inf, \|F\| < 10000 м³/ч)     |
| СЛ21 #10       | 10    | conductivity 0..10000 µS/cm, t° -20..+80 °C (FEED, PERM1)  |
| СЛ21 #11       | 11    | то же (PERM2, CONC)                                         |
| KWS-306L LP    | 20    | voltage 50..280 В, t° < 100 °C                              |
| KWS-306L HP    | 21    | то же                                                       |

## Алярмы

Все нарушения публикуются через `alarm_raise()` с категорией `ALARM`
(не CRITICAL — это диагностика, не interlock):

- `ALARM_DEV_CHECK_FAILED` (0x00D0) — общий код «health-check провалился»;
  `value` = число неудачных устройств.
- `ALARM_AI_BAD_MODE` (0x00D1) — после автокоррекции канал AI не в 4–20mA;
  `value` = номер канала (1..8).
- `ALARM_AI_WRONG_ADDR` (0x00D2) — Waveshare AI отвечает на 1, но в
  регистре 0x4000 другое значение.
- `ALARM_AI_VERSION_MISMATCH` (0x00D3) — FW < V1.00. Не interlock —
  предупреждение оператору.
- `ALARM_SL21_RANGE_OOR` (0x00D5) — выход conductivity или t° за
  ожидаемый диапазон.
- `ALARM_URZH_RANGE_OOR` (0x00D6) — NaN/inf/огромный расход в snapshot.
- `ALARM_KWS_RANGE_OOR` (0x00D7) — voltage или t° KWS вне sanity.

## Автокоррекция Waveshare AI mode

Это **не костыль, а officially-supported one-time setup**, описанный в
`doc/modbus_signal_map.md` §4.4. При первой подаче питания на новый
модуль (или после reset настроек) каналы могут быть в режиме `0` (0..5В)
вместо `3` (4..20 мА). Без коррекции наша конверсия `analog_input.c`
(трактующая raw как мкА) даст бессмысленные значения давления.

Алгоритм коррекции:

1. Читаем `0x1000..0x1007` (FC 0x03) — текущие режимы 8 каналов.
2. Если хоть один != `0x0003` — пишем `0x0003 ×8` через FC 0x10.
3. Ждём 200 мс (внутренний commit EEPROM устройства).
4. Перечитываем `0x1000..0x1007` — каждый != 3 → `ALARM_AI_BAD_MODE`.

После успешной коррекции устройство сохраняет настройки в энергонезависимой
памяти, поэтому следующий старт займёт только ре-проверку без записи.

## Почему задержка 5 секунд

`modbus_poller_task` имеет периоды опроса 100 мс (AI), 1 с (flow), 2 с
(volume, KWS), 3 с (cond). Snapshot'ы драйверов (`flowmeter_data_t`,
`conductivity_data_t`, `power_meter_data_t`) обновляются только когда
`flowmeter_update()`/`conductivity_update()`/`power_meter_update()`
вызывается из `process_task`. За 5 секунд гарантированно есть как минимум
одно обновление каждого snapshot'а, и sanity-проверки получают
актуальные данные.

Без задержки health-check бы видел `valid=false` на всех KWS/SL21/URZH
(до первого `*_update()`), что привело бы к ложным offline-репортам.

## Как отключить

В текущей реализации hard-coded — для отключения нужно закомментировать
вызов `xTaskCreate(health_check_task, ...)` в `app_main.c`. В будущем
можно вынести флаг в `config_manager_t.diagnostics.run_health_check_at_boot`
(по умолчанию true).

## Ограничения

- `check_waveshare_ai()` обращается напрямую через
  `modbus_poller_read_holding/_write_holding`, что обходит `mbc_master_get_parameter`
  CID-based path. esp-modbus v2 сериализует доступ к шине внутри
  `mbc_master_send_request`, поэтому коллизий с poll task'ом нет.
- offline-устройства (`is_device_online() == false`) пропускаются с
  warn-логом — это **не** считается failure'ом health-check'а, поскольку
  offline уже сигналится отдельным алармом (`ALARM_KWS_OFFLINE` /
  `ALARM_MODBUS_OFFLINE`).
- Sanity-диапазоны намеренно широкие — это **не** замена interlock'ам.
  Цель: поймать заведомо-некорректные данные (отрицательная мощность,
  conductivity 1e9, температура 9999 °C), которые указывают на
  неправильный регистр / битый прибор.
