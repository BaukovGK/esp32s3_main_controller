# ethernet_init.h / ethernet_init.c — Инициализация Ethernet-драйверов ESP-IDF

## Описание файла

Модуль `ethernet_init` является утилитой инициализации Ethernet-драйверов для проекта на базе ESP-IDF. Поддерживает два типа Ethernet-интерфейсов:

1. **Внутренний Ethernet MAC ESP32** — использует встроенный EMAC периферийного контроллера (для чипов с RMII-интерфейсом).
2. **SPI Ethernet модули** — внешние микросхемы Ethernet (W5500, DM9051, KSZ8851SNL), подключённые по шине SPI.

Конкретный тип и параметры Ethernet определяются через систему конфигурации Kconfig (sdkconfig). Модуль основан на примерах Espressif и распространяется под лицензией Unlicense / CC0-1.0.

**Заголовочный файл:** `components/ethernet_init/ethernet_init.h`
**Файл реализации:** `components/ethernet_init/ethernet_init.c`

---

## Зависимости (включаемые заголовки)

### ethernet_init.h

| Заголовок | Назначение |
|-----------|------------|
| `esp_eth_driver.h` | Типы драйвера Ethernet ESP-IDF: `esp_eth_handle_t`, `esp_eth_mac_t`, `esp_eth_phy_t` |

### ethernet_init.c

| Заголовок | Назначение |
|-----------|------------|
| `ethernet_init.h` | Собственный заголовочный файл |
| `esp_log.h` | Макросы логирования |
| `esp_check.h` | Макросы проверки ошибок: `ESP_GOTO_ON_FALSE`, `ESP_GOTO_ON_ERROR`, `ESP_RETURN_ON_FALSE`, `ESP_RETURN_ON_ERROR` |
| `esp_mac.h` | Работа с MAC-адресами: `esp_efuse_mac_get_default()`, `esp_derive_local_mac()` |
| `driver/gpio.h` | GPIO-драйвер: `gpio_install_isr_service()`, `gpio_uninstall_isr_service()` |
| `sdkconfig.h` | Автогенерированный файл конфигурации Kconfig |
| `driver/spi_master.h` | SPI-драйвер (условно, при `CONFIG_EXAMPLE_USE_SPI_ETHERNET`) |

---

## Константы и макросы

| Константа/Макрос | Значение | Описание |
|------------------|----------|----------|
| `SPI_ETHERNETS_NUM` | `CONFIG_EXAMPLE_SPI_ETHERNETS_NUM` или `0` | Количество SPI Ethernet модулей (из Kconfig). Если не определено — 0. |
| `INTERNAL_ETHERNETS_NUM` | `1` или `0` | Количество внутренних Ethernet-интерфейсов. Равно 1 при `CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET`, иначе 0. |
| `INIT_SPI_ETH_MODULE_CONFIG(eth_module_config, num)` | Макрос | Инициализирует элемент массива `spi_eth_module_config_t` значениями из Kconfig для SPI Ethernet модуля с заданным номером. Использует конкатенацию токенов `##num##` для доступа к CONFIG-константам конкретного модуля. |

### Kconfig-зависимые константы (используются из sdkconfig)

| Константа | Описание |
|-----------|----------|
| `CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET` | Включает инициализацию внутреннего EMAC |
| `CONFIG_EXAMPLE_USE_SPI_ETHERNET` | Включает инициализацию SPI Ethernet |
| `CONFIG_EXAMPLE_ETH_PHY_ADDR` | Адрес PHY для внутреннего Ethernet |
| `CONFIG_EXAMPLE_ETH_PHY_RST_GPIO` | GPIO сброса PHY |
| `CONFIG_EXAMPLE_ETH_MDC_GPIO` | GPIO линии MDC (SMI) |
| `CONFIG_EXAMPLE_ETH_MDIO_GPIO` | GPIO линии MDIO (SMI) |
| `CONFIG_EXAMPLE_ETH_PHY_IP101` / `RTL8201` / `LAN87XX` / `DP83848` / `KSZ80XX` | Выбор модели PHY |
| `CONFIG_EXAMPLE_ETH_SPI_HOST` | Номер SPI-хоста |
| `CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ` | Частота SPI (МГц) |
| `CONFIG_EXAMPLE_ETH_SPI_MISO_GPIO` | GPIO MISO |
| `CONFIG_EXAMPLE_ETH_SPI_MOSI_GPIO` | GPIO MOSI |
| `CONFIG_EXAMPLE_ETH_SPI_SCLK_GPIO` | GPIO SCLK |
| `CONFIG_EXAMPLE_ETH_SPI_CS0_GPIO` / `CS1_GPIO` | GPIO CS для каждого SPI Ethernet модуля |
| `CONFIG_EXAMPLE_ETH_SPI_INT0_GPIO` / `INT1_GPIO` | GPIO прерывания для каждого SPI Ethernet модуля |
| `CONFIG_EXAMPLE_ETH_SPI_POLLING0_MS` / `POLLING1_MS` | Период поллинга (мс) при отсутствии прерывания |
| `CONFIG_EXAMPLE_ETH_SPI_PHY_RST0_GPIO` / `RST1_GPIO` | GPIO сброса PHY для каждого SPI модуля |
| `CONFIG_EXAMPLE_ETH_SPI_PHY_ADDR0` / `ADDR1` | PHY-адрес для каждого SPI модуля |
| `CONFIG_EXAMPLE_USE_KSZ8851SNL` / `DM9051` / `W5500` | Выбор модели SPI Ethernet |
| `CONFIG_EXAMPLE_SPI_ETHERNETS_NUM` | Количество SPI Ethernet модулей (1 или 2) |

---

## Типы данных (typedef / struct)

### `spi_eth_module_config_t`

Внутренняя структура конфигурации одного SPI Ethernet модуля. Определена в `ethernet_init.c`.

| Поле | Тип | Описание |
|------|-----|----------|
| `spi_cs_gpio` | `uint8_t` | Номер GPIO для линии Chip Select (CS) SPI |
| `int_gpio` | `int8_t` | Номер GPIO для линии прерывания от модуля. Значение `-1` означает использование поллинга вместо прерываний. |
| `polling_ms` | `uint32_t` | Период опроса состояния модуля в миллисекундах (при отсутствии прерывания) |
| `phy_reset_gpio` | `int8_t` | Номер GPIO для сброса PHY. Значение `-1` — сброс не используется. |
| `phy_addr` | `uint8_t` | Адрес PHY на шине MDIO |
| `mac_addr` | `uint8_t *` | Указатель на массив MAC-адреса (6 байт). Если не `NULL`, используется вместо заводского. |

---

## Внутренние статические переменные

| Переменная | Тип | Описание |
|------------|-----|----------|
| `TAG` | `const char *` | Тег логирования, значение `"example_eth_init"` |
| `gpio_isr_svc_init_by_eth` | `bool` | Флаг, указывающий, что сервис обработки прерываний GPIO был установлен данным модулем (только при `CONFIG_EXAMPLE_USE_SPI_ETHERNET`). Необходим для корректной деинициализации — если сервис был установлен другим модулем, его не следует удалять. |

---

## Функции

### `static esp_eth_handle_t eth_init_internal(esp_eth_mac_t **mac_out, esp_eth_phy_t **phy_out)`

**Область видимости:** внутренняя (static)
**Условная компиляция:** `CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET`

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `mac_out` | `esp_eth_mac_t **` | Выходной указатель на созданный объект MAC. Может быть `NULL`, если объект не нужен вызывающему. |
| `phy_out` | `esp_eth_phy_t **` | Выходной указатель на созданный объект PHY. Может быть `NULL`. |

**Возвращаемое значение:**
- `esp_eth_handle_t` — дескриптор инициализированного Ethernet-драйвера при успехе
- `NULL` — при ошибке инициализации

**Описание:**

Инициализирует внутренний Ethernet-контроллер ESP32:

1. Создаёт конфигурации MAC и PHY со значениями по умолчанию.
2. Настраивает адрес PHY и GPIO сброса из Kconfig.
3. Создаёт конфигурацию ESP32 EMAC с настройкой SMI (MDC/MDIO).
4. При совместном использовании с SPI Ethernet уменьшает длину DMA-burst до 4 (`ETH_DMA_BURST_LEN_4`) для разделения ресурса DMA.
5. Создаёт MAC-объект через `esp_eth_mac_new_esp32()`.
6. Создаёт PHY-объект для выбранной в Kconfig модели (IP101, RTL8201, LAN87XX, DP83848 или KSZ80XX).
7. Устанавливает и запускает Ethernet-драйвер через `esp_eth_driver_install()`.
8. При ошибке выполняет очистку (деинсталляция драйвера, удаление MAC и PHY объектов).

---

### `static esp_err_t spi_bus_init(void)`

**Область видимости:** внутренняя (static)
**Условная компиляция:** `CONFIG_EXAMPLE_USE_SPI_ETHERNET`

**Параметры:** нет

**Возвращаемое значение:**
- `ESP_OK` — SPI-шина инициализирована успешно
- Код ошибки — при неудаче

**Описание:**

Инициализирует SPI-шину для Ethernet-модулей:

1. Если используются прерывания (GPIO INT >= 0), устанавливает сервис обработки прерываний GPIO (`gpio_install_isr_service()`). Если сервис уже установлен (`ESP_ERR_INVALID_STATE`), игнорирует ошибку. Устанавливает флаг `gpio_isr_svc_init_by_eth`.
2. Конфигурирует SPI-шину с пинами MISO, MOSI, SCLK из Kconfig (QUADWP и QUADHD отключены).
3. Инициализирует SPI-хост через `spi_bus_initialize()` с автоматическим выбором DMA-канала.

---

### `static esp_eth_handle_t eth_init_spi(spi_eth_module_config_t *spi_eth_module_config, esp_eth_mac_t **mac_out, esp_eth_phy_t **phy_out)`

**Область видимости:** внутренняя (static)
**Условная компиляция:** `CONFIG_EXAMPLE_USE_SPI_ETHERNET`

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `spi_eth_module_config` | `spi_eth_module_config_t *` | Указатель на конфигурацию конкретного SPI Ethernet модуля |
| `mac_out` | `esp_eth_mac_t **` | Выходной указатель на MAC-объект (может быть `NULL`) |
| `phy_out` | `esp_eth_phy_t **` | Выходной указатель на PHY-объект (может быть `NULL`) |

**Возвращаемое значение:**
- `esp_eth_handle_t` — дескриптор при успехе
- `NULL` — при ошибке

**Описание:**

Инициализирует один SPI Ethernet модуль:

1. Создаёт конфигурации MAC и PHY.
2. Настраивает PHY-адрес и GPIO сброса из переданной конфигурации.
3. Конфигурирует SPI-устройство (режим 0, частота из Kconfig, CS из конфигурации).
4. В зависимости от выбранной в Kconfig модели (KSZ8851SNL, DM9051 или W5500) создаёт соответствующие MAC и PHY объекты.
5. Устанавливает Ethernet-драйвер.
6. Если указан MAC-адрес в конфигурации — устанавливает его через `esp_eth_ioctl(ETH_CMD_S_MAC_ADDR)`.
7. При ошибке — очистка ресурсов.

---

### `esp_err_t example_eth_init(esp_eth_handle_t *eth_handles_out[], uint8_t *eth_cnt_out)`

**Объявлена в:** `ethernet_init.h`
**Реализована в:** `ethernet_init.c`

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `eth_handles_out` | `esp_eth_handle_t *[]` | Выходной указатель на массив дескрипторов Ethernet-драйверов. Память выделяется внутри функции через `calloc()`. |
| `eth_cnt_out` | `uint8_t *` | Выходной указатель на количество инициализированных Ethernet-интерфейсов |

**Возвращаемое значение:**
- `ESP_OK` — все Ethernet-интерфейсы инициализированы успешно
- `ESP_ERR_INVALID_ARG` — передан NULL-указатель
- `ESP_ERR_NO_MEM` — недостаточно памяти для массива дескрипторов
- `ESP_FAIL` — ошибка инициализации конкретного интерфейса

**Описание:**

Главная функция инициализации всех Ethernet-интерфейсов:

1. Проверяет входные указатели.
2. Выделяет память под массив дескрипторов (`SPI_ETHERNETS_NUM + INTERNAL_ETHERNETS_NUM` элементов).
3. При `CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET`: инициализирует внутренний Ethernet через `eth_init_internal()`.
4. При `CONFIG_EXAMPLE_USE_SPI_ETHERNET`:
   - Инициализирует SPI-шину.
   - Заполняет конфигурации SPI-модулей из Kconfig.
   - Для SPI-модулей без заводского MAC-адреса генерирует локальные MAC-адреса на основе базового MAC из eFuse (`esp_efuse_mac_get_default()` + `esp_derive_local_mac()`).
   - Инициализирует каждый SPI Ethernet модуль через `eth_init_spi()`.
5. Возвращает массив дескрипторов и их количество через выходные параметры.
6. При ошибке освобождает выделенную память.

**Ограничение:** Поддерживается максимум 2 SPI Ethernet модуля (при `CONFIG_EXAMPLE_SPI_ETHERNETS_NUM > 2` генерируется ошибка компиляции).

---

### `esp_err_t example_eth_deinit(esp_eth_handle_t *eth_handles, uint8_t eth_cnt)`

**Объявлена в:** `ethernet_init.h`
**Реализована в:** `ethernet_init.c`

**Параметры:**

| Параметр | Тип | Описание |
|----------|-----|----------|
| `eth_handles` | `esp_eth_handle_t *` | Массив дескрипторов Ethernet-драйверов для деинициализации |
| `eth_cnt` | `uint8_t` | Количество элементов в массиве |

**Возвращаемое значение:**
- `ESP_OK` — все интерфейсы деинициализированы
- `ESP_ERR_INVALID_ARG` — массив `NULL`
- Код ошибки — при неудаче деинсталляции драйвера

**Описание:**

Деинициализирует все Ethernet-драйверы:

1. Проверяет, что массив не `NULL`.
2. Для каждого дескриптора:
   - Получает указатели на MAC и PHY объекты.
   - Деинсталлирует Ethernet-драйвер.
   - Удаляет MAC и PHY объекты.
3. При `CONFIG_EXAMPLE_USE_SPI_ETHERNET`:
   - Освобождает SPI-шину.
   - Если сервис прерываний GPIO был установлен данным модулем — удаляет его (с предупреждением, так как сервис может использоваться другими модулями).
4. Освобождает массив дескрипторов.

**Важно:** Все Ethernet-драйверы должны быть остановлены (`esp_eth_stop()`) до вызова этой функции.

---

## Архитектурные замечания

- Модуль использует условную компиляцию на основе Kconfig, что делает его универсальным для различных конфигураций аппаратного обеспечения.
- В данном проекте (контроллер обратного осмоса на ESP32-S3) предположительно используется SPI Ethernet на базе W5500 (`CONFIG_EXAMPLE_USE_W5500`).
- MAC-адреса для SPI Ethernet модулей генерируются из базового MAC ESP32 с помощью стандартного механизма Locally Administered MAC, что удобно для тестирования, но не подходит для массового производства.
- Модуль основан на официальном примере Espressif и адаптирован для проекта.
