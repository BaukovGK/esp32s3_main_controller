/**
 * @file hal_nvs.c
 * @brief Обёртка NVS для хранения конфигурации
 */
#include "hal_nvs.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "hal_nvs";
#define NVS_NAMESPACE "ro_plant"

esp_err_t hal_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition повреждена, стираем...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS инициализирована, namespace: \"%s\"", NVS_NAMESPACE);
    }
    return ret;
}

esp_err_t hal_nvs_set_i32(const char *key, int32_t value)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = nvs_set_i32(handle, key, value);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t hal_nvs_get_i32(const char *key, int32_t *value)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_get_i32(handle, key, value);
    nvs_close(handle);
    return ret;
}

esp_err_t hal_nvs_set_float(const char *key, float value)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = nvs_set_blob(handle, key, &value, sizeof(float));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t hal_nvs_get_float(const char *key, float *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    size_t len = sizeof(float);
    ret = nvs_get_blob(handle, key, value, &len);
    nvs_close(handle);
    return ret;
}

esp_err_t hal_nvs_set_str(const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = nvs_set_str(handle, key, value);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t hal_nvs_get_str(const char *key, char *value, size_t max_len)
{
    if (value == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    size_t len = max_len;
    ret = nvs_get_str(handle, key, value, &len);
    nvs_close(handle);
    return ret;
}
