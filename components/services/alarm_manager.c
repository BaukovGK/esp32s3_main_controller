/**
 * @file alarm_manager.c
 * @brief Менеджер аварий — кольцевой буфер, дедупликация, NVS-персистентность
 */
#include "alarm_manager.h"
#include "hal_nvs.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "alarm";

#define MAX_ACTIVE    32
#define MAX_HISTORY   64
#define MAX_CALLBACKS 4
#define NVS_SAVE_INTERVAL 10   /* Сохранять в NVS каждые N аварий */

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

/* --- Внутренние функции --- */

static void history_push(const alarm_entry_t *entry)
{
    s_history[s_history_head] = *entry;
    s_history_head = (s_history_head + 1) % MAX_HISTORY;
    if (s_history_count < MAX_HISTORY) {
        s_history_count++;
    }
}

static void notify_all(const alarm_entry_t *entry)
{
    for (int i = 0; i < s_cb_count; i++) {
        if (s_callbacks[i]) {
            s_callbacks[i](entry);
        }
    }
}

static void save_id_to_nvs(void)
{
    hal_nvs_set_i32("alm_id", (int32_t)s_next_id);
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
    if (s_cb_count >= MAX_CALLBACKS) return ESP_ERR_NO_MEM;
    s_callbacks[s_cb_count++] = cb;
    return ESP_OK;
}

void alarm_raise(alarm_code_t code, alarm_category_t cat, float value)
{
    /* Дедупликация: INFO-события не дедуплицируются (всегда регистрируются) */
    if (cat != ALARM_CAT_INFO) {
        int idx = find_active_by_code(code);
        if (idx >= 0) {
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

    ESP_LOGW(TAG, "ALARM [%s] %s val=%.1f id=%lu",
             alarm_category_str(cat), alarm_code_str(code),
             value, (unsigned long)entry.id);

    /* Уведомить callbacks */
    notify_all(&entry);

    /* Периодическое сохранение ID в NVS */
    s_save_counter++;
    if (s_save_counter >= NVS_SAVE_INTERVAL || cat == ALARM_CAT_CRITICAL) {
        save_id_to_nvs();
        s_save_counter = 0;
    }
}

void alarm_clear(alarm_code_t code)
{
    int idx = find_active_by_code(code);
    if (idx < 0) return; /* Не найдена */

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

    ESP_LOGI(TAG, "CLEAR %s id=%lu", alarm_code_str(code), (unsigned long)entry.id);

    notify_all(&entry);
}

int alarm_get_active(alarm_entry_t *out, int max_cnt)
{
    int cnt = (s_active_count < max_cnt) ? s_active_count : max_cnt;
    if (cnt > 0 && out) {
        memcpy(out, s_active, cnt * sizeof(alarm_entry_t));
    }
    return cnt;
}

int alarm_get_history(alarm_entry_t *out, int max_cnt)
{
    int cnt = (s_history_count < max_cnt) ? s_history_count : max_cnt;
    if (cnt <= 0 || !out) return 0;

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
    default:                    return "UNKNOWN";
    }
}
