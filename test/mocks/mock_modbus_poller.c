/**
 * @file mock_modbus_poller.c
 * @brief Мок: задаваемые буферы регистров + online-флаги для драйверов.
 *
 * Phase-5 (H-modbus-initial-state): моделирует production-семантику:
 *   - до первого mock_mb_set_*() raw-геттеры возвращают ESP_ERR_INVALID_STATE,
 *   - is_device_online() возвращает false до первого set'а.
 *   - mock_mb_set_*() помечает соответствующий CID как «опрошен».
 * Это нужно, чтобы тесты драйверов проверяли реальный путь
 * валидации, а не ноль-как-валидное-значение.
 */
#include "mock_modbus_poller.h"
#include "board_config.h"
#include "esp_err.h"
#include <string.h>

static uint16_t s_ai[CID_AI_REG_COUNT];
static uint16_t s_flow[CID_FLOW_RATE_REG_COUNT];
static uint16_t s_volume[CID_FLOW_VOL_REG_COUNT];
static uint16_t s_cond10[CID_COND10_REG_COUNT];
static uint16_t s_cond11[CID_COND11_REG_COUNT];
static uint16_t s_kws_lp[CID_KWS_REG_COUNT];
static uint16_t s_kws_hp[CID_KWS_REG_COUNT];

/* per-CID first_poll_done — выставляется первым mock_mb_set_*() */
static bool s_first_ai;
static bool s_first_flow;
static bool s_first_volume;
static bool s_first_cond10;
static bool s_first_cond11;
static bool s_first_kws_lp;
static bool s_first_kws_hp;

static bool s_online_ai;
static bool s_online_flow;
static bool s_online_cond10;
static bool s_online_cond11;
static bool s_online_kws_lp;
static bool s_online_kws_hp;

void mock_mb_reset(void)
{
    memset(s_ai, 0, sizeof(s_ai));
    memset(s_flow, 0, sizeof(s_flow));
    memset(s_volume, 0, sizeof(s_volume));
    memset(s_cond10, 0, sizeof(s_cond10));
    memset(s_cond11, 0, sizeof(s_cond11));
    memset(s_kws_lp, 0, sizeof(s_kws_lp));
    memset(s_kws_hp, 0, sizeof(s_kws_hp));

    /* После reset ни один CID не «опрошен» — модель чистого старта */
    s_first_ai = false;
    s_first_flow = false;
    s_first_volume = false;
    s_first_cond10 = false;
    s_first_cond11 = false;
    s_first_kws_lp = false;
    s_first_kws_hp = false;

    /* По умолчанию все устройства online (нормальный режим тестов).
     * Эффективный online = s_online_X && (любой CID этого slave опрошен). */
    s_online_ai = true;
    s_online_flow = true;
    s_online_cond10 = true;
    s_online_cond11 = true;
    s_online_kws_lp = true;
    s_online_kws_hp = true;

    /* Сброс health-check моков (read/write holding) — реализованы ниже,
     * объявления forward-declared для использования в reset. */
    extern void mock_mb_clear_holding(void);
    extern void mock_mb_clear_writes(void);
    mock_mb_clear_holding();
    mock_mb_clear_writes();
}

static void copy_clamped(uint16_t *dst, size_t dst_n, const uint16_t *src, size_t src_n)
{
    size_t n = (src_n < dst_n) ? src_n : dst_n;
    if (src) memcpy(dst, src, n * sizeof(uint16_t));
}

void mock_mb_set_ai(const uint16_t *data, size_t count)
{
    copy_clamped(s_ai, CID_AI_REG_COUNT, data, count);
    s_first_ai = true;
}
void mock_mb_set_flow(const uint16_t *data, size_t count)
{
    copy_clamped(s_flow, CID_FLOW_RATE_REG_COUNT, data, count);
    s_first_flow = true;
}
void mock_mb_set_volume(const uint16_t *data, size_t count)
{
    copy_clamped(s_volume, CID_FLOW_VOL_REG_COUNT, data, count);
    s_first_volume = true;
}
void mock_mb_set_cond10(const uint16_t *data, size_t count)
{
    copy_clamped(s_cond10, CID_COND10_REG_COUNT, data, count);
    s_first_cond10 = true;
}
void mock_mb_set_cond11(const uint16_t *data, size_t count)
{
    copy_clamped(s_cond11, CID_COND11_REG_COUNT, data, count);
    s_first_cond11 = true;
}
void mock_mb_set_kws_lp(const uint16_t *data, size_t count)
{
    copy_clamped(s_kws_lp, CID_KWS_REG_COUNT, data, count);
    s_first_kws_lp = true;
}
void mock_mb_set_kws_hp(const uint16_t *data, size_t count)
{
    copy_clamped(s_kws_hp, CID_KWS_REG_COUNT, data, count);
    s_first_kws_hp = true;
}

void mock_mb_clear_first_poll(uint8_t slave_addr)
{
    if (slave_addr == MB_ADDR_WAVESHARE_AI) {
        s_first_ai = false;
    } else if (slave_addr == MB_ADDR_URZH2KM) {
        s_first_flow = false;
        s_first_volume = false;
    } else if (slave_addr == MB_ADDR_SL21_201) {
        s_first_cond10 = false;
    } else if (slave_addr == MB_ADDR_SL21_101) {
        s_first_cond11 = false;
    } else if (slave_addr == MB_ADDR_KWS_PUMP_LP) {
        s_first_kws_lp = false;
    } else if (slave_addr == MB_ADDR_KWS_PUMP_HP) {
        s_first_kws_hp = false;
    }
}

void mock_mb_set_online(uint8_t slave_addr, bool online)
{
    if      (slave_addr == MB_ADDR_WAVESHARE_AI) s_online_ai     = online;
    else if (slave_addr == MB_ADDR_URZH2KM)      s_online_flow   = online;
    else if (slave_addr == MB_ADDR_SL21_201)     s_online_cond10 = online;
    else if (slave_addr == MB_ADDR_SL21_101)     s_online_cond11 = online;
    else if (slave_addr == MB_ADDR_KWS_PUMP_LP)  s_online_kws_lp = online;
    else if (slave_addr == MB_ADDR_KWS_PUMP_HP)  s_online_kws_hp = online;
}

/* === Реализация production API === */

esp_err_t modbus_poller_init(void)               { return ESP_OK; }
void      modbus_poller_task(void *arg)          { (void)arg; }

/* C-10: out зануляется на ошибочном пути.
 * H-modbus-initial-state: до первого set'а возвращаем ESP_ERR_INVALID_STATE. */
esp_err_t modbus_poller_get_ai_raw(uint16_t *out, size_t count)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (count < CID_AI_REG_COUNT) {
        memset(out, 0, count * sizeof(uint16_t));
        return ESP_ERR_INVALID_SIZE;
    }
    memset(out, 0, count * sizeof(uint16_t));
    if (!s_first_ai) return ESP_ERR_INVALID_STATE;
    memcpy(out, s_ai, sizeof(s_ai));
    return ESP_OK;
}

esp_err_t modbus_poller_get_flow_raw(uint16_t *out, size_t count)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (count < CID_FLOW_RATE_REG_COUNT) {
        memset(out, 0, count * sizeof(uint16_t));
        return ESP_ERR_INVALID_SIZE;
    }
    memset(out, 0, count * sizeof(uint16_t));
    if (!s_first_flow) return ESP_ERR_INVALID_STATE;
    memcpy(out, s_flow, sizeof(s_flow));
    return ESP_OK;
}

esp_err_t modbus_poller_get_volume_raw(uint16_t *out, size_t count)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (count < CID_FLOW_VOL_REG_COUNT) {
        memset(out, 0, count * sizeof(uint16_t));
        return ESP_ERR_INVALID_SIZE;
    }
    memset(out, 0, count * sizeof(uint16_t));
    if (!s_first_volume) return ESP_ERR_INVALID_STATE;
    memcpy(out, s_volume, sizeof(s_volume));
    return ESP_OK;
}

esp_err_t modbus_poller_get_cond10_raw(uint16_t *out, size_t count)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (count < CID_COND10_REG_COUNT) {
        memset(out, 0, count * sizeof(uint16_t));
        return ESP_ERR_INVALID_SIZE;
    }
    memset(out, 0, count * sizeof(uint16_t));
    if (!s_first_cond10) return ESP_ERR_INVALID_STATE;
    memcpy(out, s_cond10, sizeof(s_cond10));
    return ESP_OK;
}

esp_err_t modbus_poller_get_cond11_raw(uint16_t *out, size_t count)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (count < CID_COND11_REG_COUNT) {
        memset(out, 0, count * sizeof(uint16_t));
        return ESP_ERR_INVALID_SIZE;
    }
    memset(out, 0, count * sizeof(uint16_t));
    if (!s_first_cond11) return ESP_ERR_INVALID_STATE;
    memcpy(out, s_cond11, sizeof(s_cond11));
    return ESP_OK;
}

esp_err_t modbus_poller_get_kws_lp_raw(uint16_t *out, size_t count)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (count < CID_KWS_REG_COUNT) {
        memset(out, 0, count * sizeof(uint16_t));
        return ESP_ERR_INVALID_SIZE;
    }
    memset(out, 0, count * sizeof(uint16_t));
    if (!s_first_kws_lp) return ESP_ERR_INVALID_STATE;
    memcpy(out, s_kws_lp, sizeof(s_kws_lp));
    return ESP_OK;
}

esp_err_t modbus_poller_get_kws_hp_raw(uint16_t *out, size_t count)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (count < CID_KWS_REG_COUNT) {
        memset(out, 0, count * sizeof(uint16_t));
        return ESP_ERR_INVALID_SIZE;
    }
    memset(out, 0, count * sizeof(uint16_t));
    if (!s_first_kws_hp) return ESP_ERR_INVALID_STATE;
    memcpy(out, s_kws_hp, sizeof(s_kws_hp));
    return ESP_OK;
}

bool modbus_poller_is_device_online(uint8_t slave_addr)
{
    /* H-modbus-initial-state: устройство online только если флаг online ИСТИНА
     * И хотя бы один CID этого slave получил данные. */
    if (slave_addr == MB_ADDR_WAVESHARE_AI) return s_online_ai     && s_first_ai;
    if (slave_addr == MB_ADDR_URZH2KM)      return s_online_flow   && (s_first_flow || s_first_volume);
    if (slave_addr == MB_ADDR_SL21_201)     return s_online_cond10 && s_first_cond10;
    if (slave_addr == MB_ADDR_SL21_101)     return s_online_cond11 && s_first_cond11;
    if (slave_addr == MB_ADDR_KWS_PUMP_LP)  return s_online_kws_lp && s_first_kws_lp;
    if (slave_addr == MB_ADDR_KWS_PUMP_HP)  return s_online_kws_hp && s_first_kws_hp;
    return false;
}

uint32_t modbus_poller_get_error_count(uint8_t slave_addr)
{
    /* Если устройство не «опрошено», 0 ошибок (нет данных).
     * Иначе: online → 0, offline → 999 (как раньше). */
    bool any_polled = false;
    bool online_flag = false;
    if (slave_addr == MB_ADDR_WAVESHARE_AI) {
        any_polled = s_first_ai;          online_flag = s_online_ai;
    } else if (slave_addr == MB_ADDR_URZH2KM) {
        any_polled = s_first_flow || s_first_volume; online_flag = s_online_flow;
    } else if (slave_addr == MB_ADDR_SL21_201) {
        any_polled = s_first_cond10;      online_flag = s_online_cond10;
    } else if (slave_addr == MB_ADDR_SL21_101) {
        any_polled = s_first_cond11;      online_flag = s_online_cond11;
    } else if (slave_addr == MB_ADDR_KWS_PUMP_LP) {
        any_polled = s_first_kws_lp;      online_flag = s_online_kws_lp;
    } else if (slave_addr == MB_ADDR_KWS_PUMP_HP) {
        any_polled = s_first_kws_hp;      online_flag = s_online_kws_hp;
    }
    if (!any_polled) return 0;
    return online_flag ? 0 : 999;
}

/* === Health-check stubs: read/write holding ===
 *
 * mock хранит до MOCK_HOLDING_MAX «прописанных» окон регистров. Каждое окно
 * = (slave, base_addr, data[count]). modbus_poller_read_holding() ищет окно
 * с совпадающим slave и addr ∈ [base..base+count-1], копирует подходящий
 * срез. write_holding обновляет окно если оно есть, либо создаёт новое —
 * это нужно для теста «после автокоррекции повторное чтение видит запись».
 */
#define MOCK_HOLDING_MAX 8
#define MOCK_HOLDING_REGS 16

typedef struct {
    bool     used;
    uint8_t  slave;
    uint16_t base;
    size_t   count;
    uint16_t data[MOCK_HOLDING_REGS];
} mock_holding_t;

static mock_holding_t s_holdings[MOCK_HOLDING_MAX];
static esp_err_t s_read_error = ESP_OK;

static int s_write_count = 0;
static uint8_t  s_last_write_slave = 0;
static uint16_t s_last_write_addr  = 0;
static uint16_t s_last_write_data[MOCK_HOLDING_REGS];
static size_t   s_last_write_count = 0;

void mock_mb_set_holding(uint8_t slave_addr, uint16_t base_addr,
                         const uint16_t *data, size_t count)
{
    if (count > MOCK_HOLDING_REGS) count = MOCK_HOLDING_REGS;
    /* Поиск существующего окна с тем же (slave, base) — обновляем in-place */
    for (int i = 0; i < MOCK_HOLDING_MAX; i++) {
        if (s_holdings[i].used &&
            s_holdings[i].slave == slave_addr &&
            s_holdings[i].base  == base_addr) {
            s_holdings[i].count = count;
            if (data) memcpy(s_holdings[i].data, data, count * sizeof(uint16_t));
            else      memset(s_holdings[i].data, 0, count * sizeof(uint16_t));
            return;
        }
    }
    /* Иначе занять первый свободный слот */
    for (int i = 0; i < MOCK_HOLDING_MAX; i++) {
        if (!s_holdings[i].used) {
            s_holdings[i].used  = true;
            s_holdings[i].slave = slave_addr;
            s_holdings[i].base  = base_addr;
            s_holdings[i].count = count;
            if (data) memcpy(s_holdings[i].data, data, count * sizeof(uint16_t));
            else      memset(s_holdings[i].data, 0, count * sizeof(uint16_t));
            return;
        }
    }
}

void mock_mb_clear_holding(void)
{
    memset(s_holdings, 0, sizeof(s_holdings));
    s_read_error = ESP_OK;
}

void mock_mb_clear_writes(void)
{
    s_write_count = 0;
    s_last_write_slave = 0;
    s_last_write_addr  = 0;
    s_last_write_count = 0;
    memset(s_last_write_data, 0, sizeof(s_last_write_data));
}

int mock_mb_get_write_count(void)
{
    return s_write_count;
}

void mock_mb_get_last_write(uint8_t *slave_out, uint16_t *addr_out,
                            uint16_t *data_out, size_t *count_out,
                            size_t data_max)
{
    if (slave_out) *slave_out = s_last_write_slave;
    if (addr_out)  *addr_out  = s_last_write_addr;
    if (count_out) *count_out = s_last_write_count;
    if (data_out) {
        size_t n = (s_last_write_count < data_max) ? s_last_write_count : data_max;
        memcpy(data_out, s_last_write_data, n * sizeof(uint16_t));
    }
}

void mock_mb_set_read_error(esp_err_t err)
{
    s_read_error = err;
}

esp_err_t modbus_poller_read_holding(uint8_t slave, uint16_t addr,
                                     uint16_t *out, size_t count)
{
    if (out == NULL || count == 0) return ESP_ERR_INVALID_ARG;
    if (s_read_error != ESP_OK) return s_read_error;

    for (int i = 0; i < MOCK_HOLDING_MAX; i++) {
        if (!s_holdings[i].used)             continue;
        if (s_holdings[i].slave != slave)    continue;
        /* Запрашиваемый диапазон [addr..addr+count) должен лежать целиком в окне */
        if (addr < s_holdings[i].base)       continue;
        size_t offset = (size_t)(addr - s_holdings[i].base);
        if (offset + count > s_holdings[i].count) continue;
        memcpy(out, &s_holdings[i].data[offset], count * sizeof(uint16_t));
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t modbus_poller_write_holding(uint8_t slave, uint16_t addr,
                                      const uint16_t *data, size_t count)
{
    if (data == NULL || count == 0) return ESP_ERR_INVALID_ARG;
    s_write_count++;
    s_last_write_slave = slave;
    s_last_write_addr  = addr;
    size_t n = (count < MOCK_HOLDING_REGS) ? count : MOCK_HOLDING_REGS;
    s_last_write_count = n;
    memcpy(s_last_write_data, data, n * sizeof(uint16_t));

    /* Обновить соответствующее окно read-буфера: после write_holding
     * последующий read должен вернуть свежие значения (моделируем коммит
     * настроек устройством). */
    for (int i = 0; i < MOCK_HOLDING_MAX; i++) {
        if (!s_holdings[i].used)             continue;
        if (s_holdings[i].slave != slave)    continue;
        if (addr < s_holdings[i].base)       continue;
        size_t offset = (size_t)(addr - s_holdings[i].base);
        if (offset + count > s_holdings[i].count) continue;
        memcpy(&s_holdings[i].data[offset], data, count * sizeof(uint16_t));
        return ESP_OK;
    }
    /* Окна нет — создаём, чтобы тесты могли вызвать write без предварительного set */
    mock_mb_set_holding(slave, addr, data, count);
    return ESP_OK;
}

size_t modbus_poller_get_slave_addrs(uint8_t *out, size_t max_cnt)
{
    if (out == NULL || max_cnt == 0) return 0;
    memset(out, 0, max_cnt * sizeof(uint8_t));
    static const uint8_t s_test_addrs[] = {
        MB_ADDR_WAVESHARE_AI, MB_ADDR_URZH2KM,
        MB_ADDR_SL21_201, MB_ADDR_SL21_101,
        MB_ADDR_KWS_PUMP_LP, MB_ADDR_KWS_PUMP_HP,
    };
    size_t n = sizeof(s_test_addrs) / sizeof(s_test_addrs[0]);
    if (n > max_cnt) n = max_cnt;
    for (size_t i = 0; i < n; i++) out[i] = s_test_addrs[i];
    return n;
}
