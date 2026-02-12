/**
 * @file web_api_status.c
 * @brief GET-обработчики REST API: /api/v1/status, /api/v1/config
 */
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"

#include "state_machine.h"
#include "interlocks.h"
#include "doser.h"
#include "telemetry.h"
#include "config_manager.h"
#include "analog_input.h"
#include "flowmeter.h"
#include "conductivity.h"
#include "hal_gpio.h"
#include "alarm_manager.h"
#include "mqtt_app.h"
#include "diagnostics.h"

#include <math.h>
#include <string.h>

/* ===== Вспомогательные функции ===== */

static void add_float_or_null(cJSON *obj, const char *name, float val)
{
    if (isnan(val)) {
        cJSON_AddNullToObject(obj, name);
    } else {
        cJSON_AddNumberToObject(obj, name, val);
    }
}

static const char *state_to_str(sm_state_t st)
{
    static const char *names[] = {"IDLE", "AUTO", "WASHING", "MANUAL", "FAULT"};
    return (st < sizeof(names)/sizeof(names[0])) ? names[st] : "UNKNOWN";
}

static const char *auto_sub_to_str(auto_substate_t sub)
{
    static const char *names[] = {
        "STARTING_PUMP1", "RAMP", "STARTING_PUMP2", "FILLING_INTERM",
        "STARTING_PUMP3", "RUNNING", "STOPPING"
    };
    return (sub < sizeof(names)/sizeof(names[0])) ? names[sub] : "UNKNOWN";
}

static const char *wash_sub_to_str(wash_substate_t sub)
{
    static const char *names[] = {"HEATING", "SUPPLY", "DRAIN", "DONE"};
    return (sub < sizeof(names)/sizeof(names[0])) ? names[sub] : "UNKNOWN";
}

static const char *doser_state_to_str(doser_state_t st)
{
    static const char *names[] = {"OFF", "RUNNING", "PAUSE"};
    return (st < sizeof(names)/sizeof(names[0])) ? names[st] : "UNKNOWN";
}

/* ===== GET /api/v1/status ===== */

static esp_err_t status_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    /* Состояние КА */
    sm_status_t st = state_machine_get_status();
    cJSON_AddStringToObject(root, "state", state_to_str(st.state));

    if (st.state == SM_AUTO) {
        cJSON_AddStringToObject(root, "auto_sub", auto_sub_to_str(st.auto_sub));
    } else {
        cJSON_AddNullToObject(root, "auto_sub");
    }

    if (st.state == SM_WASHING) {
        cJSON_AddStringToObject(root, "wash_sub", wash_sub_to_str(st.wash_sub));
    } else {
        cJSON_AddNullToObject(root, "wash_sub");
    }

    cJSON_AddNumberToObject(root, "fault_flags", st.fault_flags);

    /* DI/DO */
    cJSON_AddNumberToObject(root, "di", hal_gpio_read_di());
    cJSON_AddNumberToObject(root, "do", hal_gpio_read_do_state());

    /* Блокировки */
    interlock_result_t ilk;
    interlocks_check(st.state == SM_MANUAL, &ilk);

    cJSON *j_ilk = cJSON_AddObjectToObject(root, "interlocks");
    cJSON_AddNumberToObject(j_ilk, "flags", ilk.active_flags);
    cJSON_AddBoolToObject(j_ilk, "estop", ilk.estop_active);
    cJSON_AddBoolToObject(j_ilk, "filter_warn", ilk.filter_warn);
    cJSON *j_allow = cJSON_AddArrayToObject(j_ilk, "allow");
    cJSON_AddItemToArray(j_allow, cJSON_CreateBool(ilk.allow_pump_feed));
    cJSON_AddItemToArray(j_allow, cJSON_CreateBool(ilk.allow_pump_stage1));
    cJSON_AddItemToArray(j_allow, cJSON_CreateBool(ilk.allow_pump_stage2));
    cJSON_AddItemToArray(j_allow, cJSON_CreateBool(ilk.allow_heater));
    cJSON_AddItemToArray(j_allow, cJSON_CreateBool(ilk.allow_doser));

    /* Дозатор */
    cJSON *j_doser = cJSON_AddObjectToObject(root, "doser");
    cJSON_AddStringToObject(j_doser, "state", doser_state_to_str(doser_get_state()));
    cJSON_AddBoolToObject(j_doser, "enabled", doser_is_enabled());

    /* Аналоговые входы */
    static const char *ai_names[] = {"P1", "P2", "P3", "P4", "T"};
    static const char *ai_units[] = {"bar", "bar", "bar", "bar", "C"};
    ai_data_t ai;
    analog_input_get_data(&ai);

    cJSON *j_analog = cJSON_AddArrayToObject(root, "analog");
    for (int i = 0; i < 5; i++) {
        cJSON *ch = cJSON_CreateObject();
        cJSON_AddStringToObject(ch, "name", ai_names[i]);
        add_float_or_null(ch, "value", ai.channels[i].value);
        cJSON_AddStringToObject(ch, "unit", ai_units[i]);
        cJSON_AddBoolToObject(ch, "fault", ai.channels[i].fault);
        cJSON_AddItemToArray(j_analog, ch);
    }

    /* Расходомеры */
    static const char *flow_names[] = {"Q1", "Q2", "Q3", "Q4"};
    flowmeter_data_t fm;
    flowmeter_get_data(&fm);

    cJSON *j_flow = cJSON_AddArrayToObject(root, "flow");
    for (int i = 0; i < FLOW_CHANNEL_COUNT; i++) {
        cJSON *ch = cJSON_CreateObject();
        cJSON_AddStringToObject(ch, "name", flow_names[i]);
        add_float_or_null(ch, "flow", fm.flow_m3h[i]);
        add_float_or_null(ch, "volume", fm.volume_m3[i]);
        cJSON_AddBoolToObject(ch, "ok", fm.channel_ok[i]);
        cJSON_AddItemToArray(j_flow, ch);
    }

    /* Кондуктометры */
    static const char *cond_names[] = {"\xCF\x83" "1", "\xCF\x83" "2", "\xCF\x83" "3"};
    conductivity_data_t cd;
    conductivity_get_data(&cd);

    cJSON *j_cond = cJSON_AddArrayToObject(root, "cond");
    for (int i = 0; i < COND_CHANNEL_COUNT; i++) {
        cJSON *ch = cJSON_CreateObject();
        cJSON_AddStringToObject(ch, "name", cond_names[i]);
        add_float_or_null(ch, "value", cd.conductivity_uS[i]);
        add_float_or_null(ch, "temp", cd.temperature_C[i]);
        cJSON_AddBoolToObject(ch, "ok", cd.channel_ok[i]);
        cJSON_AddItemToArray(j_cond, ch);
    }

    /* Телеметрия */
    const telemetry_data_t *tel = telemetry_get();
    cJSON *j_tel = cJSON_AddObjectToObject(root, "telemetry");
    add_float_or_null(j_tel, "filter_dp", tel->filter_dp);
    add_float_or_null(j_tel, "stage1_feed", tel->stage1_feed_m3h);
    add_float_or_null(j_tel, "recovery2", tel->stage2_recovery_pct);
    add_float_or_null(j_tel, "recovery_sys", tel->system_recovery_pct);
    add_float_or_null(j_tel, "sel1", tel->stage1_selectivity);
    add_float_or_null(j_tel, "sel2", tel->stage2_selectivity);

    /* Uptime */
    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000LL));

    /* Отправка */
    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free((void *)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ===== GET /api/v1/config ===== */

static esp_err_t config_get_handler(httpd_req_t *req)
{
    const plant_config_t *cfg = config_manager_get();
    cJSON *root = cJSON_CreateObject();

    cJSON *j_pres = cJSON_AddObjectToObject(root, "pressure");
    cJSON_AddNumberToObject(j_pres, "p1_max", cfg->pressure.p1_max);
    cJSON_AddNumberToObject(j_pres, "p3_max", cfg->pressure.p3_max);
    cJSON_AddNumberToObject(j_pres, "p4_max", cfg->pressure.p4_max);
    cJSON_AddNumberToObject(j_pres, "filter_dp_warn", cfg->pressure.filter_dp_warn);

    cJSON *j_dos = cJSON_AddObjectToObject(root, "doser");
    cJSON_AddNumberToObject(j_dos, "run_time_min", cfg->doser.run_time_min);
    cJSON_AddNumberToObject(j_dos, "cycle_time_min", cfg->doser.cycle_time_min);

    cJSON *j_wash = cJSON_AddObjectToObject(root, "washing");
    cJSON_AddNumberToObject(j_wash, "target_temp_C", cfg->washing.target_temp_C);
    cJSON_AddNumberToObject(j_wash, "max_temp_C", cfg->washing.max_temp_C);
    cJSON_AddNumberToObject(j_wash, "t_overshoot_C", cfg->washing.t_overshoot_C);

    cJSON *j_tout = cJSON_AddObjectToObject(root, "timeouts");
    cJSON_AddNumberToObject(j_tout, "pump_confirm_ms", cfg->timeouts.pump_confirm_ms);
    cJSON_AddNumberToObject(j_tout, "pump_ramp_ms", cfg->timeouts.pump_ramp_ms);

    cJSON *j_mqtt = cJSON_AddObjectToObject(root, "mqtt");
    cJSON_AddStringToObject(j_mqtt, "broker_uri", cfg->mqtt.broker_uri);
    cJSON_AddStringToObject(j_mqtt, "username", cfg->mqtt.username);
    cJSON_AddStringToObject(j_mqtt, "password", cfg->mqtt.password);
    cJSON_AddStringToObject(j_mqtt, "client_id", cfg->mqtt.client_id);
    cJSON_AddNumberToObject(j_mqtt, "publish_interval_s", cfg->mqtt.publish_interval_s);
    cJSON_AddNumberToObject(j_mqtt, "enabled", cfg->mqtt.enabled);

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free((void *)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ===== GET /api/v1/mqtt/status ===== */

static esp_err_t mqtt_status_get_handler(httpd_req_t *req)
{
    const config_mqtt_t *cfg = &config_manager_get()->mqtt;
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"connected\":%s,\"broker\":\"%s\",\"enabled\":%s}",
             mqtt_app_is_connected() ? "true" : "false",
             cfg->broker_uri,
             cfg->enabled ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, -1);
}

/* ===== GET /api/v1/alarms ===== */

static esp_err_t alarms_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    /* Активные аварии */
    alarm_entry_t active[16];
    int active_cnt = alarm_get_active(active, 16);
    cJSON *j_active = cJSON_AddArrayToObject(root, "active");
    for (int i = 0; i < active_cnt; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "id", active[i].id);
        cJSON_AddNumberToObject(e, "ts", (double)(active[i].timestamp_us / 1000000LL));
        cJSON_AddStringToObject(e, "cat", alarm_category_str(active[i].category));
        cJSON_AddStringToObject(e, "code", alarm_code_str(active[i].code));
        cJSON_AddNumberToObject(e, "value", active[i].value);
        cJSON_AddItemToArray(j_active, e);
    }

    /* Последние записи истории */
    alarm_entry_t hist[16];
    int hist_cnt = alarm_get_history(hist, 16);
    cJSON *j_hist = cJSON_AddArrayToObject(root, "history");
    for (int i = 0; i < hist_cnt; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "id", hist[i].id);
        cJSON_AddNumberToObject(e, "ts", (double)(hist[i].timestamp_us / 1000000LL));
        cJSON_AddStringToObject(e, "cat", alarm_category_str(hist[i].category));
        cJSON_AddStringToObject(e, "code", alarm_code_str(hist[i].code));
        cJSON_AddBoolToObject(e, "active", hist[i].active);
        cJSON_AddItemToArray(j_hist, e);
    }

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free((void *)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ===== GET /api/v1/diagnostics ===== */

static esp_err_t diagnostics_get_handler(httpd_req_t *req)
{
    diagnostics_data_t diag;
    diagnostics_collect(&diag);

    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "heap_free", diag.free_heap);
    cJSON_AddNumberToObject(root, "heap_min", diag.min_free_heap);
    cJSON_AddNumberToObject(root, "uptime_s", (double)(diag.uptime_us / 1000000LL));

    /* Стеки задач */
    cJSON *j_stack = cJSON_AddObjectToObject(root, "stack");
    for (int i = 0; i < diag.task_count; i++) {
        cJSON_AddNumberToObject(j_stack, diag.tasks[i].name, diag.tasks[i].stack_free);
    }

    /* Modbus */
    cJSON *j_mb = cJSON_AddObjectToObject(root, "modbus");
    cJSON *j_err = cJSON_AddArrayToObject(j_mb, "errors");
    cJSON *j_onl = cJSON_AddArrayToObject(j_mb, "online");
    for (int i = 0; i < 4; i++) {
        cJSON_AddItemToArray(j_err, cJSON_CreateNumber(diag.mb_errors[i]));
        cJSON_AddItemToArray(j_onl, cJSON_CreateBool(diag.mb_online[i]));
    }

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free((void *)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ===== Регистрация ===== */

void web_api_register_status(httpd_handle_t server)
{
    static const httpd_uri_t uri_status = {
        .uri = "/api/v1/status", .method = HTTP_GET, .handler = status_get_handler
    };
    static const httpd_uri_t uri_config = {
        .uri = "/api/v1/config", .method = HTTP_GET, .handler = config_get_handler
    };
    static const httpd_uri_t uri_mqtt_status = {
        .uri = "/api/v1/mqtt/status", .method = HTTP_GET, .handler = mqtt_status_get_handler
    };
    static const httpd_uri_t uri_alarms = {
        .uri = "/api/v1/alarms", .method = HTTP_GET, .handler = alarms_get_handler
    };
    static const httpd_uri_t uri_diag = {
        .uri = "/api/v1/diagnostics", .method = HTTP_GET, .handler = diagnostics_get_handler
    };

    httpd_register_uri_handler(server, &uri_status);
    httpd_register_uri_handler(server, &uri_config);
    httpd_register_uri_handler(server, &uri_mqtt_status);
    httpd_register_uri_handler(server, &uri_alarms);
    httpd_register_uri_handler(server, &uri_diag);
}
