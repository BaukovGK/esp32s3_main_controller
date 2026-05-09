/**
 * @file mock_hal_nvs.c
 * @brief Мок: key-value store в RAM (замена NVS)
 */
#include "mock_hal_nvs.h"
#include "hal_nvs.h"
#include <string.h>

#define NVS_MAX_ENTRIES 64
#define NVS_STR_MAX     64

typedef enum {
    NVS_T_NONE,
    NVS_T_I32,
    NVS_T_FLOAT,
    NVS_T_STR
} nvs_type_t;

typedef struct {
    char      key[16];
    nvs_type_t type;
    union {
        int32_t i32;
        float   f32;
        char    str[NVS_STR_MAX];
    };
} nvs_entry_t;

static nvs_entry_t s_store[NVS_MAX_ENTRIES];
static int s_count = 0;

static nvs_entry_t *find_entry(const char *key)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_store[i].key, key) == 0) {
            return &s_store[i];
        }
    }
    return NULL;
}

static nvs_entry_t *alloc_entry(const char *key)
{
    nvs_entry_t *e = find_entry(key);
    if (e) return e;
    if (s_count >= NVS_MAX_ENTRIES) return NULL;
    e = &s_store[s_count++];
    strncpy(e->key, key, sizeof(e->key) - 1);
    e->key[sizeof(e->key) - 1] = '\0';
    return e;
}

/* === Реализация production API === */

esp_err_t hal_nvs_init(void)
{
    return ESP_OK;
}

esp_err_t hal_nvs_set_i32(const char *key, int32_t value)
{
    nvs_entry_t *e = alloc_entry(key);
    if (!e) return ESP_ERR_NO_MEM;
    e->type = NVS_T_I32;
    e->i32 = value;
    return ESP_OK;
}

esp_err_t hal_nvs_get_i32(const char *key, int32_t *value)
{
    nvs_entry_t *e = find_entry(key);
    if (!e || e->type != NVS_T_I32) return ESP_ERR_NVS_NOT_FOUND;
    *value = e->i32;
    return ESP_OK;
}

esp_err_t hal_nvs_set_float(const char *key, float value)
{
    nvs_entry_t *e = alloc_entry(key);
    if (!e) return ESP_ERR_NO_MEM;
    e->type = NVS_T_FLOAT;
    e->f32 = value;
    return ESP_OK;
}

esp_err_t hal_nvs_get_float(const char *key, float *value)
{
    nvs_entry_t *e = find_entry(key);
    if (!e || e->type != NVS_T_FLOAT) return ESP_ERR_NVS_NOT_FOUND;
    *value = e->f32;
    return ESP_OK;
}

esp_err_t hal_nvs_set_str(const char *key, const char *value)
{
    nvs_entry_t *e = alloc_entry(key);
    if (!e) return ESP_ERR_NO_MEM;
    e->type = NVS_T_STR;
    strncpy(e->str, value, NVS_STR_MAX - 1);
    e->str[NVS_STR_MAX - 1] = '\0';
    return ESP_OK;
}

esp_err_t hal_nvs_get_str(const char *key, char *value, size_t max_len)
{
    nvs_entry_t *e = find_entry(key);
    if (!e || e->type != NVS_T_STR) return ESP_ERR_NVS_NOT_FOUND;
    strncpy(value, e->str, max_len - 1);
    value[max_len - 1] = '\0';
    return ESP_OK;
}

/* === Управление из тестов === */

void mock_nvs_reset(void)
{
    s_count = 0;
    memset(s_store, 0, sizeof(s_store));
}
