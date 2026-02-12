/**
 * @file mqtt_publish.c
 * @brief Публикация статуса + Home Assistant MQTT Discovery
 */
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_log.h"

#include "state_machine.h"
#include "interlocks.h"
#include "doser.h"
#include "telemetry.h"
#include "analog_input.h"
#include "flowmeter.h"
#include "conductivity.h"
#include "hal_gpio.h"
#include "alarm_manager.h"
#include "diagnostics.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "mqtt_pub";

/* --- Вспомогательные --- */

static const char *state_to_str(sm_state_t st)
{
    static const char *names[] = {"IDLE", "AUTO", "WASHING", "MANUAL", "FAULT"};
    return (st < sizeof(names) / sizeof(names[0])) ? names[st] : "UNKNOWN";
}

static const char *auto_sub_str(auto_substate_t sub)
{
    static const char *names[] = {
        "STARTING_PUMP1", "RAMP", "STARTING_PUMP2", "FILLING_INTERM",
        "STARTING_PUMP3", "RUNNING", "STOPPING"
    };
    return (sub < sizeof(names) / sizeof(names[0])) ? names[sub] : "UNKNOWN";
}

static const char *wash_sub_str(wash_substate_t sub)
{
    static const char *names[] = {
        "WAIT_HEAT", "HEATING", "WAIT_SUPPLY", "SUPPLY",
        "WAIT_DRAIN", "DRAIN", "DONE"
    };
    return (sub < sizeof(names) / sizeof(names[0])) ? names[sub] : "UNKNOWN";
}

static const char *doser_state_str(doser_state_t st)
{
    static const char *names[] = {"OFF", "RUNNING", "PAUSE"};
    return (st < sizeof(names) / sizeof(names[0])) ? names[st] : "UNKNOWN";
}

/* Форматирование float: NaN → "null", иначе число */
static int fmt_float(char *buf, size_t sz, float val)
{
    if (isnan(val)) return snprintf(buf, sz, "null");
    return snprintf(buf, sz, "%.2f", val);
}

/* --- Публикация статуса --- */

void mqtt_publish_full_status(esp_mqtt_client_handle_t client)
{
    char buf[256];
    char topic[80];

    /* 1. State */
    sm_status_t st = state_machine_get_status();
    snprintf(buf, sizeof(buf),
             "{\"state\":\"%s\",\"auto_sub\":%s%s%s,\"wash_sub\":%s%s%s,\"fault_flags\":%lu}",
             state_to_str(st.state),
             st.state == SM_AUTO ? "\"" : "",
             st.state == SM_AUTO ? auto_sub_str(st.auto_sub) : "null",
             st.state == SM_AUTO ? "\"" : "",
             st.state == SM_WASHING ? "\"" : "",
             st.state == SM_WASHING ? wash_sub_str(st.wash_sub) : "null",
             st.state == SM_WASHING ? "\"" : "",
             (unsigned long)st.fault_flags);
    esp_mqtt_client_publish(client, "ro_plant/status/state", buf, 0, 1, 1);

    /* 2. IO */
    snprintf(buf, sizeof(buf), "{\"di\":%u,\"do\":%u}",
             hal_gpio_read_di(), hal_gpio_read_do_state());
    esp_mqtt_client_publish(client, "ro_plant/status/io", buf, 0, 0, 0);

    /* 3. Analog */
    static const char *ai_names[] = {"P1", "P2", "P3", "P4", "T"};
    static const char *ai_units[] = {"bar", "bar", "bar", "bar", "C"};
    ai_data_t ai;
    analog_input_get_data(&ai);

    for (int i = 0; i < 5; i++) {
        char vbuf[16];
        fmt_float(vbuf, sizeof(vbuf), ai.channels[i].value);
        snprintf(buf, sizeof(buf), "{\"value\":%s,\"unit\":\"%s\",\"fault\":%s}",
                 vbuf, ai_units[i], ai.channels[i].fault ? "true" : "false");
        snprintf(topic, sizeof(topic), "ro_plant/status/analog/%s", ai_names[i]);
        esp_mqtt_client_publish(client, topic, buf, 0, 0, 0);
    }

    /* 4. Flow */
    static const char *flow_names[] = {"Q1", "Q2", "Q3", "Q4"};
    flowmeter_data_t fm;
    flowmeter_get_data(&fm);

    for (int i = 0; i < FLOW_CHANNEL_COUNT; i++) {
        char fbuf[16], vbuf[16];
        fmt_float(fbuf, sizeof(fbuf), fm.flow_m3h[i]);
        fmt_float(vbuf, sizeof(vbuf), fm.volume_m3[i]);
        snprintf(buf, sizeof(buf), "{\"flow\":%s,\"volume\":%s,\"ok\":%s}",
                 fbuf, vbuf, fm.channel_ok[i] ? "true" : "false");
        snprintf(topic, sizeof(topic), "ro_plant/status/flow/%s", flow_names[i]);
        esp_mqtt_client_publish(client, topic, buf, 0, 0, 0);
    }

    /* 5. Conductivity */
    static const char *cond_names[] = {"s1", "s2", "s3"};
    conductivity_data_t cd;
    conductivity_get_data(&cd);

    for (int i = 0; i < COND_CHANNEL_COUNT; i++) {
        char cbuf[16], tbuf[16];
        fmt_float(cbuf, sizeof(cbuf), cd.conductivity_uS[i]);
        fmt_float(tbuf, sizeof(tbuf), cd.temperature_C[i]);
        snprintf(buf, sizeof(buf), "{\"conductivity\":%s,\"temperature\":%s,\"ok\":%s}",
                 cbuf, tbuf, cd.channel_ok[i] ? "true" : "false");
        snprintf(topic, sizeof(topic), "ro_plant/status/conductivity/%s", cond_names[i]);
        esp_mqtt_client_publish(client, topic, buf, 0, 0, 0);
    }

    /* 6. Telemetry */
    const telemetry_data_t *tel = telemetry_get();
    char v1[16], v2[16], v3[16], v4[16], v5[16], v6[16];
    fmt_float(v1, sizeof(v1), tel->filter_dp);
    fmt_float(v2, sizeof(v2), tel->stage1_feed_m3h);
    fmt_float(v3, sizeof(v3), tel->stage2_recovery_pct);
    fmt_float(v4, sizeof(v4), tel->system_recovery_pct);
    fmt_float(v5, sizeof(v5), tel->stage1_selectivity);
    fmt_float(v6, sizeof(v6), tel->stage2_selectivity);
    snprintf(buf, sizeof(buf),
             "{\"filter_dp\":%s,\"stage1_feed\":%s,\"recovery2\":%s,"
             "\"recovery_sys\":%s,\"sel1\":%s,\"sel2\":%s}",
             v1, v2, v3, v4, v5, v6);
    esp_mqtt_client_publish(client, "ro_plant/status/telemetry", buf, 0, 0, 0);

    /* 7. Doser */
    snprintf(buf, sizeof(buf), "{\"state\":\"%s\",\"enabled\":%s}",
             doser_state_str(doser_get_state()),
             doser_is_enabled() ? "true" : "false");
    esp_mqtt_client_publish(client, "ro_plant/status/doser", buf, 0, 0, 0);

    /* 8. Interlocks */
    interlock_result_t ilk;
    interlocks_check(st.state == SM_MANUAL, &ilk);
    snprintf(buf, sizeof(buf), "{\"flags\":%lu,\"estop\":%s,\"filter_warn\":%s}",
             (unsigned long)ilk.active_flags,
             ilk.estop_active ? "true" : "false",
             ilk.filter_warn ? "true" : "false");
    esp_mqtt_client_publish(client, "ro_plant/status/interlocks", buf, 0, 0, 0);
}

/* --- Публикация аварии --- */

void mqtt_publish_alarm(esp_mqtt_client_handle_t client, const alarm_entry_t *alarm)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"id\":%lu,\"ts\":%lld,\"cat\":\"%s\",\"code\":\"%s\","
             "\"value\":%.1f,\"active\":%s}",
             (unsigned long)alarm->id,
             (long long)(alarm->timestamp_us / 1000000LL),
             alarm_category_str(alarm->category),
             alarm_code_str(alarm->code),
             alarm->value,
             alarm->active ? "true" : "false");
    esp_mqtt_client_publish(client, "ro_plant/alarms", buf, 0, 1, 0);
}

/* --- Online availability --- */

void mqtt_publish_online(esp_mqtt_client_handle_t client)
{
    esp_mqtt_client_publish(client, "ro_plant/availability", "online", 0, 1, 1);
}

/* --- Публикация диагностики --- */

void mqtt_publish_diagnostics(esp_mqtt_client_handle_t client)
{
    diagnostics_data_t diag;
    diagnostics_collect(&diag);

    char buf[384];
    int pos = 0;

    /* Система */
    pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "{\"heap_free\":%lu,\"heap_min\":%lu,\"uptime_s\":%lld,",
                    (unsigned long)diag.free_heap,
                    (unsigned long)diag.min_free_heap,
                    (long long)(diag.uptime_us / 1000000LL));

    /* Стеки задач */
    pos += snprintf(buf + pos, sizeof(buf) - pos, "\"stack\":{");
    for (int i = 0; i < diag.task_count; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s\"%s\":%lu",
                        i ? "," : "",
                        diag.tasks[i].name,
                        (unsigned long)diag.tasks[i].stack_free);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "},");

    /* Modbus */
    pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "\"modbus\":{\"errors\":[%lu,%lu,%lu,%lu],"
                    "\"online\":[%s,%s,%s,%s]},",
                    (unsigned long)diag.mb_errors[0],
                    (unsigned long)diag.mb_errors[1],
                    (unsigned long)diag.mb_errors[2],
                    (unsigned long)diag.mb_errors[3],
                    diag.mb_online[0] ? "true" : "false",
                    diag.mb_online[1] ? "true" : "false",
                    diag.mb_online[2] ? "true" : "false",
                    diag.mb_online[3] ? "true" : "false");

    /* Watchdog stale (убираем последнюю запятую в закрытии) */
    snprintf(buf + pos, sizeof(buf) - pos, "\"wdt_stale\":0}");

    esp_mqtt_client_publish(client, "ro_plant/status/diagnostics", buf, 0, 0, 0);
}

/* --- Home Assistant Discovery --- */

/* Таблица датчиков для автоматической регистрации */
typedef struct {
    const char *object_id;
    const char *name;
    const char *state_topic;
    const char *value_template;
    const char *unit;
    const char *device_class;
    const char *icon;
    const char *entity_type;  /* "sensor", "binary_sensor" */
} ha_entity_t;

static const ha_entity_t s_ha_entities[] = {
    /* Состояние */
    {"ro_plant_state", "RO State", "ro_plant/status/state",
     "{{ value_json.state }}", NULL, NULL, "mdi:water-pump", "sensor"},
    {"ro_plant_faults", "RO Fault Flags", "ro_plant/status/state",
     "{{ value_json.fault_flags }}", NULL, NULL, "mdi:alert-circle", "sensor"},

    /* Давления */
    {"ro_plant_p1", "RO P1", "ro_plant/status/analog/P1",
     "{{ value_json.value }}", "bar", "pressure", NULL, "sensor"},
    {"ro_plant_p2", "RO P2", "ro_plant/status/analog/P2",
     "{{ value_json.value }}", "bar", "pressure", NULL, "sensor"},
    {"ro_plant_p3", "RO P3", "ro_plant/status/analog/P3",
     "{{ value_json.value }}", "bar", "pressure", NULL, "sensor"},
    {"ro_plant_p4", "RO P4", "ro_plant/status/analog/P4",
     "{{ value_json.value }}", "bar", "pressure", NULL, "sensor"},
    {"ro_plant_temp", "RO Temperature", "ro_plant/status/analog/T",
     "{{ value_json.value }}", "\xC2\xB0" "C", "temperature", NULL, "sensor"},

    /* Расходомеры */
    {"ro_plant_q1", "RO Q1 Flow", "ro_plant/status/flow/Q1",
     "{{ value_json.flow }}", "m\xC2\xB3/h", NULL, "mdi:water", "sensor"},
    {"ro_plant_q2", "RO Q2 Flow", "ro_plant/status/flow/Q2",
     "{{ value_json.flow }}", "m\xC2\xB3/h", NULL, "mdi:water", "sensor"},
    {"ro_plant_q3", "RO Q3 Flow", "ro_plant/status/flow/Q3",
     "{{ value_json.flow }}", "m\xC2\xB3/h", NULL, "mdi:water", "sensor"},
    {"ro_plant_q4", "RO Q4 Flow", "ro_plant/status/flow/Q4",
     "{{ value_json.flow }}", "m\xC2\xB3/h", NULL, "mdi:water", "sensor"},

    /* Кондуктометры */
    {"ro_plant_s1", "RO Feed Conductivity", "ro_plant/status/conductivity/s1",
     "{{ value_json.conductivity }}", "\xC2\xB5S/cm", NULL, "mdi:flash", "sensor"},
    {"ro_plant_s2", "RO Perm1 Conductivity", "ro_plant/status/conductivity/s2",
     "{{ value_json.conductivity }}", "\xC2\xB5S/cm", NULL, "mdi:flash", "sensor"},
    {"ro_plant_s3", "RO Perm2 Conductivity", "ro_plant/status/conductivity/s3",
     "{{ value_json.conductivity }}", "\xC2\xB5S/cm", NULL, "mdi:flash", "sensor"},

    /* Телеметрия */
    {"ro_plant_filter_dp", "RO Filter dP", "ro_plant/status/telemetry",
     "{{ value_json.filter_dp }}", "bar", "pressure", NULL, "sensor"},
    {"ro_plant_recovery", "RO System Recovery", "ro_plant/status/telemetry",
     "{{ value_json.recovery_sys }}", "%", NULL, "mdi:percent", "sensor"},
    {"ro_plant_sel1", "RO Stage1 Selectivity", "ro_plant/status/telemetry",
     "{{ value_json.sel1 }}", "%", NULL, "mdi:percent", "sensor"},
    {"ro_plant_sel2", "RO Stage2 Selectivity", "ro_plant/status/telemetry",
     "{{ value_json.sel2 }}", "%", NULL, "mdi:percent", "sensor"},

    /* Бинарные */
    {"ro_plant_estop", "RO E-STOP", "ro_plant/status/interlocks",
     "{{ value_json.estop }}", NULL, "safety", "mdi:alert-octagon", "binary_sensor"},
    {"ro_plant_filter_warn", "RO Filter Warning", "ro_plant/status/interlocks",
     "{{ value_json.filter_warn }}", NULL, "problem", "mdi:filter", "binary_sensor"},

    /* Диагностика */
    {"ro_plant_heap", "RO Free Heap", "ro_plant/status/diagnostics",
     "{{ value_json.heap_free }}", "B", "data_size", "mdi:memory", "sensor"},
    {"ro_plant_uptime_diag", "RO Uptime", "ro_plant/status/diagnostics",
     "{{ value_json.uptime_s }}", "s", "duration", NULL, "sensor"},
};

#define HA_ENTITY_COUNT (sizeof(s_ha_entities) / sizeof(s_ha_entities[0]))

static void add_device_obj(cJSON *root)
{
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    cJSON *ids = cJSON_AddArrayToObject(dev, "identifiers");
    cJSON_AddItemToArray(ids, cJSON_CreateString("ro_plant_001"));
    cJSON_AddStringToObject(dev, "name", "RO Plant Controller");
    cJSON_AddStringToObject(dev, "manufacturer", "Custom");
    cJSON_AddStringToObject(dev, "model", "ESP32-S3 RO");
    cJSON_AddStringToObject(dev, "sw_version", "1.0.0");
}

void mqtt_publish_ha_discovery(esp_mqtt_client_handle_t client)
{
    ESP_LOGI(TAG, "Публикация HA Discovery (%d entities)...", (int)HA_ENTITY_COUNT);

    for (int i = 0; i < (int)HA_ENTITY_COUNT; i++) {
        const ha_entity_t *e = &s_ha_entities[i];

        cJSON *root = cJSON_CreateObject();
        if (!root) continue;

        cJSON_AddStringToObject(root, "name", e->name);
        cJSON_AddStringToObject(root, "unique_id", e->object_id);
        cJSON_AddStringToObject(root, "state_topic", e->state_topic);
        cJSON_AddStringToObject(root, "value_template", e->value_template);
        cJSON_AddStringToObject(root, "availability_topic", "ro_plant/availability");

        if (e->unit)
            cJSON_AddStringToObject(root, "unit_of_measurement", e->unit);
        if (e->device_class)
            cJSON_AddStringToObject(root, "device_class", e->device_class);
        if (e->icon)
            cJSON_AddStringToObject(root, "icon", e->icon);

        /* Для binary_sensor: payload_on/off */
        if (strcmp(e->entity_type, "binary_sensor") == 0) {
            cJSON_AddStringToObject(root, "payload_on", "true");
            cJSON_AddStringToObject(root, "payload_off", "false");
        }

        add_device_obj(root);

        /* Топик: homeassistant/<type>/ro_plant/<id>/config */
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/%s/ro_plant/%s/config",
                 e->entity_type, e->object_id);

        char *json = cJSON_PrintUnformatted(root);
        if (json) {
            esp_mqtt_client_publish(client, topic, json, 0, 1, 1);
            free(json);
        }

        cJSON_Delete(root);
    }

    ESP_LOGI(TAG, "HA Discovery опубликован");
}
