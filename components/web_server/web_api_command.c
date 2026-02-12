/**
 * @file web_api_command.c
 * @brief POST-обработчики REST API: command, manual, doser, config
 */
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"

#include "state_machine.h"
#include "doser.h"
#include "config_manager.h"
#include "mqtt_app.h"

#include <string.h>

static const char *TAG = "api_cmd";

/* Максимальный размер тела POST-запроса */
#define MAX_POST_SIZE 256

/* ===== Вспомогательные функции ===== */

static esp_err_t send_ok(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", -1);
}

static esp_err_t send_error(httpd_req_t *req, int status, const char *msg)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg);
    httpd_resp_set_status(req, (status == 400) ? "400 Bad Request" : "409 Conflict");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, -1);
}

static cJSON *recv_json(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > MAX_POST_SIZE) {
        send_error(req, 400, "invalid body size");
        return NULL;
    }

    char buf[MAX_POST_SIZE + 1];
    int received = httpd_req_recv(req, buf, total);
    if (received <= 0) {
        send_error(req, 400, "recv failed");
        return NULL;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        send_error(req, 400, "invalid JSON");
        return NULL;
    }
    return root;
}

/* ===== POST /api/v1/command ===== */

static esp_err_t command_post_handler(httpd_req_t *req)
{
    cJSON *root = recv_json(req);
    if (!root) return ESP_OK;

    cJSON *j_cmd = cJSON_GetObjectItem(root, "cmd");
    if (!j_cmd || !cJSON_IsString(j_cmd)) {
        cJSON_Delete(root);
        return send_error(req, 400, "missing 'cmd' string");
    }

    const char *cmd_str = j_cmd->valuestring;
    sm_command_t cmd;

    if (strcmp(cmd_str, "start_auto") == 0)      cmd = CMD_START_AUTO;
    else if (strcmp(cmd_str, "stop") == 0)        cmd = CMD_STOP;
    else if (strcmp(cmd_str, "start_washing") == 0) cmd = CMD_START_WASHING;
    else if (strcmp(cmd_str, "set_manual") == 0)  cmd = CMD_SET_MANUAL;
    else if (strcmp(cmd_str, "reset_fault") == 0) cmd = CMD_RESET_FAULT;
    else {
        cJSON_Delete(root);
        return send_error(req, 400, "unknown command");
    }

    ESP_LOGI(TAG, "Команда: %s", cmd_str);
    state_machine_send_command(cmd);

    cJSON_Delete(root);
    return send_ok(req);
}

/* ===== POST /api/v1/manual/do ===== */

static esp_err_t manual_do_post_handler(httpd_req_t *req)
{
    if (state_machine_get_state() != SM_MANUAL) {
        return send_error(req, 409, "not in MANUAL mode");
    }

    cJSON *root = recv_json(req);
    if (!root) return ESP_OK;

    cJSON *j_mask = cJSON_GetObjectItem(root, "mask");
    if (!j_mask || !cJSON_IsNumber(j_mask)) {
        cJSON_Delete(root);
        return send_error(req, 400, "missing 'mask' number");
    }

    uint8_t mask = (uint8_t)j_mask->valueint;
    state_machine_manual_set_do(mask);

    ESP_LOGI(TAG, "Manual DO: 0x%02X", mask);
    cJSON_Delete(root);
    return send_ok(req);
}

/* ===== POST /api/v1/doser/enable ===== */

static esp_err_t doser_enable_post_handler(httpd_req_t *req)
{
    cJSON *root = recv_json(req);
    if (!root) return ESP_OK;

    cJSON *j_en = cJSON_GetObjectItem(root, "enabled");
    if (!j_en || !cJSON_IsBool(j_en)) {
        cJSON_Delete(root);
        return send_error(req, 400, "missing 'enabled' bool");
    }

    bool en = cJSON_IsTrue(j_en);
    doser_enable(en);

    ESP_LOGI(TAG, "Дозатор: %s", en ? "ВКЛ" : "ВЫКЛ");
    cJSON_Delete(root);
    return send_ok(req);
}

/* ===== POST /api/v1/config/pressure ===== */

static esp_err_t config_pressure_post_handler(httpd_req_t *req)
{
    cJSON *root = recv_json(req);
    if (!root) return ESP_OK;

    config_pressure_t cfg = config_manager_get()->pressure;

    cJSON *j;
    if ((j = cJSON_GetObjectItem(root, "p1_max")) && cJSON_IsNumber(j))
        cfg.p1_max = (float)j->valuedouble;
    if ((j = cJSON_GetObjectItem(root, "p3_max")) && cJSON_IsNumber(j))
        cfg.p3_max = (float)j->valuedouble;
    if ((j = cJSON_GetObjectItem(root, "p4_max")) && cJSON_IsNumber(j))
        cfg.p4_max = (float)j->valuedouble;
    if ((j = cJSON_GetObjectItem(root, "filter_dp_warn")) && cJSON_IsNumber(j))
        cfg.filter_dp_warn = (float)j->valuedouble;

    config_manager_set_pressure(&cfg);

    cJSON_Delete(root);
    return send_ok(req);
}

/* ===== POST /api/v1/config/doser ===== */

static esp_err_t config_doser_post_handler(httpd_req_t *req)
{
    cJSON *root = recv_json(req);
    if (!root) return ESP_OK;

    config_doser_t cfg = config_manager_get()->doser;

    cJSON *j;
    if ((j = cJSON_GetObjectItem(root, "run_time_min")) && cJSON_IsNumber(j))
        cfg.run_time_min = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "cycle_time_min")) && cJSON_IsNumber(j))
        cfg.cycle_time_min = j->valueint;

    config_manager_set_doser(&cfg);

    cJSON_Delete(root);
    return send_ok(req);
}

/* ===== POST /api/v1/config/washing ===== */

static esp_err_t config_washing_post_handler(httpd_req_t *req)
{
    cJSON *root = recv_json(req);
    if (!root) return ESP_OK;

    config_washing_t cfg = config_manager_get()->washing;

    cJSON *j;
    if ((j = cJSON_GetObjectItem(root, "target_temp_C")) && cJSON_IsNumber(j))
        cfg.target_temp_C = (float)j->valuedouble;
    if ((j = cJSON_GetObjectItem(root, "max_temp_C")) && cJSON_IsNumber(j))
        cfg.max_temp_C = (float)j->valuedouble;
    if ((j = cJSON_GetObjectItem(root, "t_overshoot_C")) && cJSON_IsNumber(j))
        cfg.t_overshoot_C = (float)j->valuedouble;

    config_manager_set_washing(&cfg);

    cJSON_Delete(root);
    return send_ok(req);
}

/* ===== POST /api/v1/config/timeouts ===== */

static esp_err_t config_timeouts_post_handler(httpd_req_t *req)
{
    cJSON *root = recv_json(req);
    if (!root) return ESP_OK;

    config_timeouts_t cfg = config_manager_get()->timeouts;

    cJSON *j;
    if ((j = cJSON_GetObjectItem(root, "pump_confirm_ms")) && cJSON_IsNumber(j))
        cfg.pump_confirm_ms = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "pump_ramp_ms")) && cJSON_IsNumber(j))
        cfg.pump_ramp_ms = j->valueint;

    config_manager_set_timeouts(&cfg);

    cJSON_Delete(root);
    return send_ok(req);
}

/* ===== POST /api/v1/config/mqtt ===== */

static esp_err_t config_mqtt_post_handler(httpd_req_t *req)
{
    cJSON *root = recv_json(req);
    if (!root) return ESP_OK;

    config_mqtt_t cfg = config_manager_get()->mqtt;

    cJSON *j;
    if ((j = cJSON_GetObjectItem(root, "broker_uri")) && cJSON_IsString(j)) {
        strncpy(cfg.broker_uri, j->valuestring, sizeof(cfg.broker_uri) - 1);
        cfg.broker_uri[sizeof(cfg.broker_uri) - 1] = '\0';
    }
    if ((j = cJSON_GetObjectItem(root, "username")) && cJSON_IsString(j)) {
        strncpy(cfg.username, j->valuestring, sizeof(cfg.username) - 1);
        cfg.username[sizeof(cfg.username) - 1] = '\0';
    }
    if ((j = cJSON_GetObjectItem(root, "password")) && cJSON_IsString(j)) {
        strncpy(cfg.password, j->valuestring, sizeof(cfg.password) - 1);
        cfg.password[sizeof(cfg.password) - 1] = '\0';
    }
    if ((j = cJSON_GetObjectItem(root, "client_id")) && cJSON_IsString(j)) {
        strncpy(cfg.client_id, j->valuestring, sizeof(cfg.client_id) - 1);
        cfg.client_id[sizeof(cfg.client_id) - 1] = '\0';
    }
    if ((j = cJSON_GetObjectItem(root, "publish_interval_s")) && cJSON_IsNumber(j))
        cfg.publish_interval_s = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "enabled")) && cJSON_IsNumber(j))
        cfg.enabled = j->valueint;

    config_manager_set_mqtt(&cfg);

    ESP_LOGI(TAG, "MQTT конфиг обновлён: %s", cfg.broker_uri);

    /* Переподключение если MQTT был запущен */
    if (cfg.enabled) {
        mqtt_app_reconnect();
    } else {
        mqtt_app_stop();
    }

    cJSON_Delete(root);
    return send_ok(req);
}

/* ===== Регистрация ===== */

void web_api_register_commands(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        {"/api/v1/command",          HTTP_POST, command_post_handler, NULL},
        {"/api/v1/manual/do",        HTTP_POST, manual_do_post_handler, NULL},
        {"/api/v1/doser/enable",     HTTP_POST, doser_enable_post_handler, NULL},
        {"/api/v1/config/pressure",  HTTP_POST, config_pressure_post_handler, NULL},
        {"/api/v1/config/doser",     HTTP_POST, config_doser_post_handler, NULL},
        {"/api/v1/config/washing",   HTTP_POST, config_washing_post_handler, NULL},
        {"/api/v1/config/timeouts",  HTTP_POST, config_timeouts_post_handler, NULL},
        {"/api/v1/config/mqtt",      HTTP_POST, config_mqtt_post_handler, NULL},
    };

    for (int i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
}
