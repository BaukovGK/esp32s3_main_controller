/**
 * @file alarm_manager.c
 * @brief Менеджер аварий — кольцевой буфер, дедупликация, NVS-персистентность
 *
 * Потокобезопасность: все публичные функции защищены mutex'ом s_lock.
 * Критично: к alarm_raise/alarm_clear/alarm_get_* обращаются process_task,
 * mqtt_task и httpd параллельно.
 */
#include "alarm_manager.h"
#include "hal_nvs.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "alarm";

#define MAX_ACTIVE    32
#define MAX_HISTORY   64
#define MAX_CALLBACKS 4
#define NVS_SAVE_INTERVAL 10   /* Сохранять в NVS каждые N аварий */
#define LOCK_TIMEOUT_MS   100  /* Таймаут захвата mutex'а */

/* Активные аварии */
static alarm_entry_t s_active[MAX_ACTIVE];
static int s_active_count = 0;

/* Кольцевой буфер истории */
static alarm_entry_t s_history[MAX_HISTORY];
static int s_history_head = 0;
static int s_history_count = 0;

/* Callbacks */
static alarm_notify_cb_t s_callbacks[MAX_CALLBACKS];
static int s_cb_count = 0;

/* Счётчик ID */
static uint32_t s_next_id = 1;
static uint32_t s_save_counter = 0;

/* Защита всей внутренней структуры */
static SemaphoreHandle_t s_lock = NULL;

static inline bool lock_take(void)
{
    if (s_lock == NULL) return true;  /* Защита от вызова до init() */
    return xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) == pdTRUE;
}

static inline void lock_give(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

/* --- Внутренние функции --- */

static void history_push(const alarm_entry_t *entry)
{
    s_history[s_history_head] = *entry;
    s_history_head = (s_history_head + 1) % MAX_HISTORY;
    if (s_history_count < MAX_HISTORY) {
        s_history_count++;
    }
}

static int find_active_by_code(alarm_code_t code)
{
    for (int i = 0; i < s_active_count; i++) {
        if (s_active[i].code == code) {
            return i;
        }
    }
    return -1;
}

/* --- Публичные функции --- */

esp_err_t alarm_manager_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            ESP_LOGE(TAG, "Не удалось создать mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    s_active_count = 0;
    s_history_head = 0;
    s_history_count = 0;
    s_cb_count = 0;
    s_save_counter = 0;

    /* Загрузить счётчик ID из NVS */
    int32_t saved_id = 1;
    if (hal_nvs_get_i32("alm_id", &saved_id) == ESP_OK && saved_id > 0) {
        s_next_id = (uint32_t)saved_id;
    } else {
        s_next_id = 1;
    }

    ESP_LOGI(TAG, "Менеджер аварий инициализирован, next_id=%lu", (unsigned long)s_next_id);

    /* Запись о старте системы */
    alarm_raise(ALARM_SYSTEM_START, ALARM_CAT_INFO, 0);

    return ESP_OK;
}

esp_err_t alarm_manager_register_notify(alarm_notify_cb_t cb)
{
    if (cb == NULL) return ESP_ERR_INVALID_ARG;
    if (!lock_take()) return ESP_ERR_TIMEOUT;
    if (s_cb_count >= MAX_CALLBACKS) {
        lock_give();
        return ESP_ERR_NO_MEM;
    }
    s_callbacks[s_cb_count++] = cb;
    lock_give();
    return ESP_OK;
}

void alarm_raise(alarm_code_t code, alarm_category_t cat, float value)
{
    if (!lock_take()) {
        ESP_LOGE(TAG, "alarm_raise: lock timeout, drop code=0x%04X", code);
        return;
    }

    /* Дедупликация: INFO-события не дедуплицируются (всегда регистрируются) */
    if (cat != ALARM_CAT_INFO) {
        int idx = find_active_by_code(code);
        if (idx >= 0) {
            lock_give();
            return; /* Уже активна */
        }
    }

    alarm_entry_t entry = {
        .id = s_next_id++,
        .timestamp_us = esp_timer_get_time(),
        .category = cat,
        .code = code,
        .value = value,
        .active = true,
    };

    /* Добавить в активные (кроме INFO — они только в историю) */
    if (cat != ALARM_CAT_INFO && s_active_count < MAX_ACTIVE) {
        s_active[s_active_count++] = entry;
    }

    /* В историю */
    history_push(&entry);

    /* Снимок callbacks для уведомления вне lock'а (callback может ходить
     * в alarm API повторно — избегаем deadlock'а) */
    alarm_notify_cb_t cbs[MAX_CALLBACKS];
    int cb_n = s_cb_count;
    for (int i = 0; i < cb_n; i++) cbs[i] = s_callbacks[i];

    /* Периодическое сохранение ID в NVS — берём решение здесь, NVS-call
     * делаем без lock'а */
    s_save_counter++;
    bool need_save = (s_save_counter >= NVS_SAVE_INTERVAL || cat == ALARM_CAT_CRITICAL);
    if (need_save) s_save_counter = 0;
    uint32_t id_snapshot = s_next_id;

    lock_give();

    ESP_LOGW(TAG, "ALARM [%s] %s val=%.1f id=%lu",
             alarm_category_str(cat), alarm_code_str(code),
             value, (unsigned long)entry.id);

    for (int i = 0; i < cb_n; i++) {
        if (cbs[i]) cbs[i](&entry);
    }

    if (need_save) {
        hal_nvs_set_i32("alm_id", (int32_t)id_snapshot);
    }
}

void alarm_clear(alarm_code_t code)
{
    if (!lock_take()) {
        ESP_LOGE(TAG, "alarm_clear: lock timeout, code=0x%04X", code);
        return;
    }

    int idx = find_active_by_code(code);
    if (idx < 0) {
        lock_give();
        return; /* Не найдена */
    }

    /* Создаём запись о снятии */
    alarm_entry_t entry = {
        .id = s_next_id++,
        .timestamp_us = esp_timer_get_time(),
        .category = s_active[idx].category,
        .code = code,
        .value = 0,
        .active = false,
    };

    /* Удаляем из активных (сдвигаем массив) */
    for (int i = idx; i < s_active_count - 1; i++) {
        s_active[i] = s_active[i + 1];
    }
    s_active_count--;

    history_push(&entry);

    /* Снимок callbacks для вызова вне lock'а */
    alarm_notify_cb_t cbs[MAX_CALLBACKS];
    int cb_n = s_cb_count;
    for (int i = 0; i < cb_n; i++) cbs[i] = s_callbacks[i];

    lock_give();

    ESP_LOGI(TAG, "CLEAR %s id=%lu", alarm_code_str(code), (unsigned long)entry.id);

    for (int i = 0; i < cb_n; i++) {
        if (cbs[i]) cbs[i](&entry);
    }
}

int alarm_get_active(alarm_entry_t *out, int max_cnt)
{
    if (!lock_take()) return 0;
    int cnt = (s_active_count < max_cnt) ? s_active_count : max_cnt;
    if (cnt > 0 && out) {
        memcpy(out, s_active, cnt * sizeof(alarm_entry_t));
    }
    lock_give();
    return cnt;
}

int alarm_get_history(alarm_entry_t *out, int max_cnt)
{
    if (!out || max_cnt <= 0) return 0;
    if (!lock_take()) return 0;

    int cnt = (s_history_count < max_cnt) ? s_history_count : max_cnt;
    if (cnt <= 0) {
        lock_give();
        return 0;
    }

    /* Читаем из кольцевого буфера, начиная с самой старой */
    int start;
    if (s_history_count < MAX_HISTORY) {
        start = 0;
    } else {
        start = s_history_head; /* Самая старая запись */
    }

    /* Копируем последние cnt записей */
    int skip = s_history_count - cnt;
    for (int i = 0; i < cnt; i++) {
        int idx = (start + skip + i) % MAX_HISTORY;
        out[i] = s_history[idx];
    }
    lock_give();
    return cnt;
}

const char *alarm_category_str(alarm_category_t cat)
{
    switch (cat) {
    case ALARM_CAT_CRITICAL: return "CRITICAL";
    case ALARM_CAT_ALARM:    return "ALARM";
    case ALARM_CAT_WARNING:  return "WARNING";
    case ALARM_CAT_INFO:     return "INFO";
    default:                 return "UNKNOWN";
    }
}

const char *alarm_code_str(alarm_code_t code)
{
    switch (code) {
    case ALARM_ESTOP:           return "ESTOP";
    case ALARM_SOURCE_EMPTY:    return "SOURCE_EMPTY";
    case ALARM_INTERM_EMPTY:    return "INTERM_EMPTY";
    case ALARM_P1_HIGH:         return "P1_HIGH";
    case ALARM_P3_HIGH:         return "P3_HIGH";
    case ALARM_P4_HIGH:         return "P4_HIGH";
    case ALARM_T_HIGH:          return "T_HIGH";
    case ALARM_FILTER_DP:       return "FILTER_DP";
    case ALARM_PUMP1_TIMEOUT:   return "PUMP1_TIMEOUT";
    case ALARM_PUMP2_TIMEOUT:   return "PUMP2_TIMEOUT";
    case ALARM_PUMP3_TIMEOUT:   return "PUMP3_TIMEOUT";
    case ALARM_SENSOR_FAULT_P1: return "SENSOR_P1";
    case ALARM_SENSOR_FAULT_P2: return "SENSOR_P2";
    case ALARM_SENSOR_FAULT_P3: return "SENSOR_P3";
    case ALARM_SENSOR_FAULT_P4: return "SENSOR_P4";
    case ALARM_SENSOR_FAULT_T:  return "SENSOR_T";
    case ALARM_MQTT_DISCONNECT: return "MQTT_DISC";
    case ALARM_MODBUS_OFFLINE:  return "MODBUS_OFF";
    case ALARM_STATE_CHANGE:    return "STATE_CHG";
    case ALARM_FAULT_RESET:     return "FAULT_RST";
    case ALARM_MANUAL_DEP_WARN: return "MANUAL_DEP";
    case ALARM_SYSTEM_START:    return "SYS_START";
    case ALARM_DO_READBACK_FAIL:return "DO_READBACK";
    case ALARM_I2C_BUS_HUNG:    return "I2C_HUNG";
    case ALARM_MB_DATA_LOCK_HUNG: return "MB_LOCK_HUNG";
    case ALARM_UNEXPECTED_RESTART:return "UNEXP_RST";
    case ALARM_RESTART_DURING_OP: return "RST_DURING_OP";
    case ALARM_STEP_TIMEOUT:    return "STEP_TIMEOUT";
    case ALARM_LOW_HEAP:        return "LOW_HEAP";
    case ALARM_PUMP_LP_NO_CURRENT: return "PUMP_LP_NO_I";
    case ALARM_PUMP_HP_NO_CURRENT: return "PUMP_HP_NO_I";
    case ALARM_PUMP_LP_OVERTEMP:   return "PUMP_LP_OVT";
    case ALARM_PUMP_HP_OVERTEMP:   return "PUMP_HP_OVT";
    case ALARM_KWS_VOLTAGE_OOR:    return "KWS_V_OOR";
    case ALARM_KWS_OFFLINE:        return "KWS_OFFLINE";
    default:                    return "UNKNOWN";
    }
}
