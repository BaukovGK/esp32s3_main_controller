/**
 * @file mqtt_subscribe.c
 * @brief Обработка входящих MQTT-команд и настроек
 */
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_log.h"

#include "state_machine.h"
#include "doser.h"
#include "config_manager.h"
#include "board_config.h"

#include <string.h>

static const char *TAG = "mqtt_sub";

/* --- Обработчики команд --- */

static void handle_mode(const char *payload, int len)
{
    sm_command_t cmd = CMD_NONE;
    if (strncmp(payload, "start_auto", len) == 0)        cmd = CMD_START_AUTO;
    else if (strncmp(payload, "stop", len) == 0)          cmd = CMD_STOP;
    else if (strncmp(payload, "start_washing", len) == 0) cmd = CMD_START_WASHING;
    else if (strncmp(payload, "confirm_wash", len) == 0)  cmd = CMD_CONFIRM_WASH_PHASE;
    else if (strncmp(payload, "set_manual", len) == 0)    cmd = CMD_SET_MANUAL;
    else if (strncmp(payload, "reset_fault", len) == 0)   cmd = CMD_RESET_FAULT;

    if (cmd != CMD_NONE) {
        ESP_LOGI(TAG, "MQTT команда режима: %.*s", len, payload);
        state_machine_send_command(cmd);
    }
}

static void handle_pump(const char *data, int len)
{
    if (state_machine_get_state() != SM_MANUAL) {
        ESP_LOGW(TAG, "Команда pump игнорирована: не в MANUAL");
        return;
    }

    char buf[64];
    int l = (len < 63) ? len : 63;
    memcpy(buf, data, l);
    buf[l] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) return;

    cJSON *j_mask = cJSON_GetObjectItem(root, "mask");
    if (j_mask && cJSON_IsNumber(j_mask)) {
        uint8_t mask = (uint8_t)j_mask->valueint;
        state_machine_manual_set_do(mask);
        ESP_LOGI(TAG, "MQTT manual DO: 0x%02X", mask);
    }
    cJSON_Delete(root);
}

static void handle_doser(const char *data, int len)
{
    char buf[64];
    int l = (len < 63) ? len : 63;
    memcpy(buf, data, l);
    buf[l] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) return;

    cJSON *j_en = cJSON_GetObjectItem(root, "enabled");
    if (j_en && cJSON_IsBool(j_en)) {
        bool en = cJSON_IsTrue(j_en);
        doser_enable(en);
        ESP_LOGI(TAG, "MQTT дозатор: %s", en ? "ВКЛ" : "ВЫКЛ");
    }
    cJSON_Delete(root);
}

static void handle_heater(const char *data, int len)
{
    if (state_machine_get_state() != SM_MANUAL) {
        ESP_LOGW(TAG, "Команда heater игнорирована: не в MANUAL");
        return;
    }

    char buf[64];
    int l = (len < 63) ? len : 63;
    memcpy(buf, data, l);
    buf[l] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) return;

    cJSON *j_on = cJSON_GetObjectItem(root, "on");
    if (j_on && cJSON_IsBool(j_on)) {
        /* Читаем желаемую маску SM (не аппаратное состояние) и переключаем бит ТЭНа */
        uint8_t mask = state_machine_get_manual_do_mask();
        if (cJSON_IsTrue(j_on)) {
            mask |= (1 << (BOARD_DO_HEATER - 1));
        } else {
            mask &= ~(1 << (BOARD_DO_HEATER - 1));
        }
        state_machine_manual_set_do(mask);
        ESP_LOGI(TAG, "MQTT нагреватель: %s", cJSON_IsTrue(j_on) ? "ВКЛ" : "ВЫКЛ");
    }
    cJSON_Delete(root);
}

static void handle_settings(const char *section, int section_len,
                             const char *data, int data_len)
{
    char sec[32], buf[256];
    int sl = (section_len < 31) ? section_len : 31;
    int dl = (data_len < 255) ? data_len : 255;
    memcpy(sec, section, sl); sec[sl] = '\0';
    memcpy(buf, data, dl); buf[dl] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) return;

    cJSON *j;

    if (strcmp(sec, "pressure") == 0) {
        config_pressure_t cfg = config_manager_get()->pressure;
        if ((j = cJSON_GetObjectItem(root, "p1_max")) && cJSON_IsNumber(j))
            cfg.p1_max = (float)j->valuedouble;
        if ((j = cJSON_GetObjectItem(root, "p3_max")) && cJSON_IsNumber(j))
            cfg.p3_max = (float)j->valuedouble;
        if ((j = cJSON_GetObjectItem(root, "p4_max")) && cJSON_IsNumber(j))
            cfg.p4_max = (float)j->valuedouble;
        if ((j = cJSON_GetObjectItem(root, "filter_dp_warn")) && cJSON_IsNumber(j))
            cfg.filter_dp_warn = (float)j->valuedouble;
        config_manager_set_pressure(&cfg);
        ESP_LOGI(TAG, "MQTT settings/pressure обновлены");

    } else if (strcmp(sec, "doser") == 0) {
        config_doser_t cfg = config_manager_get()->doser;
        if ((j = cJSON_GetObjectItem(root, "run_time_min")) && cJSON_IsNumber(j))
            cfg.run_time_min = j->valueint;
        if ((j = cJSON_GetObjectItem(root, "cycle_time_min")) && cJSON_IsNumber(j))
            cfg.cycle_time_min = j->valueint;
        config_manager_set_doser(&cfg);
        ESP_LOGI(TAG, "MQTT settings/doser обновлены");

    } else if (strcmp(sec, "washing") == 0) {
        config_washing_t cfg = config_manager_get()->washing;
        if ((j = cJSON_GetObjectItem(root, "target_temp_C")) && cJSON_IsNumber(j))
            cfg.target_temp_C = (float)j->valuedouble;
        if ((j = cJSON_GetObjectItem(root, "max_temp_C")) && cJSON_IsNumber(j))
            cfg.max_temp_C = (float)j->valuedouble;
        if ((j = cJSON_GetObjectItem(root, "t_overshoot_C")) && cJSON_IsNumber(j))
            cfg.t_overshoot_C = (float)j->valuedouble;
        if ((j = cJSON_GetObjectItem(root, "hysteresis_C")) && cJSON_IsNumber(j))
            cfg.hysteresis_C = (float)j->valuedouble;
        if ((j = cJSON_GetObjectItem(root, "heat_timeout_min")) && cJSON_IsNumber(j))
            cfg.heat_timeout_min = j->valueint;
        if ((j = cJSON_GetObjectItem(root, "supply_time_min")) && cJSON_IsNumber(j))
            cfg.supply_time_min = j->valueint;
        if ((j = cJSON_GetObjectItem(root, "drain_time_min")) && cJSON_IsNumber(j))
            cfg.drain_time_min = j->valueint;
        config_manager_set_washing(&cfg);
        ESP_LOGI(TAG, "MQTT settings/washing обновлены");

    } else if (strcmp(sec, "timeouts") == 0) {
        config_timeouts_t cfg = config_manager_get()->timeouts;
        if ((j = cJSON_GetObjectItem(root, "pump_confirm_ms")) && cJSON_IsNumber(j))
            cfg.pump_confirm_ms = j->valueint;
        if ((j = cJSON_GetObjectItem(root, "pump_ramp_ms")) && cJSON_IsNumber(j))
            cfg.pump_ramp_ms = j->valueint;
        config_manager_set_timeouts(&cfg);
        ESP_LOGI(TAG, "MQTT settings/timeouts обновлены");
    }

    cJSON_Delete(root);
}

/* --- Публичные функции --- */

void mqtt_subscribe_all(esp_mqtt_client_handle_t client)
{
    esp_mqtt_client_subscribe(client, "ro_plant/command/mode", 1);
    esp_mqtt_client_subscribe(client, "ro_plant/command/pump", 1);
    esp_mqtt_client_subscribe(client, "ro_plant/command/doser", 1);
    esp_mqtt_client_subscribe(client, "ro_plant/command/heater", 1);
    esp_mqtt_client_subscribe(client, "ro_plant/settings/#", 1);
    ESP_LOGI(TAG, "Подписан на 5 топиков команд");
}

void mqtt_subscribe_handle_message(const char *topic, int topic_len,
                                    const char *data, int data_len)
{
    /* ro_plant/command/mode (21 символ) */
    if (topic_len == 21 && strncmp(topic, "ro_plant/command/mode", 21) == 0) {
        handle_mode(data, data_len);
    }
    /* ro_plant/command/pump (21 символ) */
    else if (topic_len == 21 && strncmp(topic, "ro_plant/command/pump", 21) == 0) {
        handle_pump(data, data_len);
    }
    /* ro_plant/command/doser (22 символа) */
    else if (topic_len == 22 && strncmp(topic, "ro_plant/command/doser", 22) == 0) {
        handle_doser(data, data_len);
    }
    /* ro_plant/command/heater (23 символа) */
    else if (topic_len == 23 && strncmp(topic, "ro_plant/command/heater", 23) == 0) {
        handle_heater(data, data_len);
    }
    /* ro_plant/settings/<section> (18+ символов) */
    else if (topic_len > 18 && strncmp(topic, "ro_plant/settings/", 18) == 0) {
        handle_settings(topic + 18, topic_len - 18, data, data_len);
    }
}
