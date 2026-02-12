/**
 * @file state_machine.c
 * @brief Конечный автомат установки обратного осмоса
 */
#include "state_machine.h"
#include "interlocks.h"
#include "hal_gpio.h"
#include "config_manager.h"
#include "board_config.h"
#include "analog_input.h"
#include "alarm_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
#include <math.h>

static const char *TAG = "sm";

/* Спинлок для защиты разделяемых данных (ProcessTask ↔ MQTT task) */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static sm_state_t       s_state = SM_IDLE;
static auto_substate_t  s_auto_sub = AUTO_STARTING_PUMP1;
static wash_substate_t  s_wash_sub = WASH_WAIT_HEAT;
static uint32_t         s_fault_flags = 0;
static sm_command_t     s_pending_cmd = CMD_NONE;

/* Желаемые состояния выходов */
static bool s_want_pump_feed = false;
static bool s_want_pump_stage1 = false;
static bool s_want_pump_stage2 = false;
static bool s_want_heater = false;

/* MANUAL: прямая маска DO */
static uint8_t s_manual_do_mask = 0;

/* Метка времени входа в подсостояние */
static int64_t s_step_start_time = 0;

/* Метки запуска насосов для проверки подтверждения */
static int64_t s_pump_start_us[3] = {0, 0, 0};

/* ===== Вспомогательные функции ===== */

static void all_outputs_off(void)
{
    s_want_pump_feed = false;
    s_want_pump_stage1 = false;
    s_want_pump_stage2 = false;
    s_want_heater = false;
}

static void enter_fault(uint32_t flags)
{
    s_fault_flags |= flags;
    all_outputs_off();
    hal_gpio_write_do(0x00);  /* Аварийное отключение всех выходов */
    if (s_state != SM_FAULT) {
        ESP_LOGE(TAG, "АВАРИЯ! Флаги: 0x%04lX", (unsigned long)s_fault_flags);
        s_state = SM_FAULT;
    }
}

/* ===== Общие утилиты конвертации имён (экспортируемые) ===== */

const char *sm_state_name(sm_state_t st)
{
    static const char *names[] = {"IDLE", "AUTO", "WASHING", "MANUAL", "FAULT"};
    return (st < sizeof(names)/sizeof(names[0])) ? names[st] : "UNKNOWN";
}

const char *sm_auto_sub_name(auto_substate_t sub)
{
    static const char *names[] = {
        "STARTING_PUMP1", "RAMP", "STARTING_PUMP2", "FILLING_INTERM",
        "STARTING_PUMP3", "RUNNING", "STOPPING"
    };
    return (sub < sizeof(names)/sizeof(names[0])) ? names[sub] : "UNKNOWN";
}

const char *sm_wash_sub_name(wash_substate_t sub)
{
    static const char *names[] = {
        "WAIT_HEAT", "HEATING", "WAIT_SUPPLY", "SUPPLY",
        "WAIT_DRAIN", "DRAIN", "DONE"
    };
    return (sub < sizeof(names)/sizeof(names[0])) ? names[sub] : "UNKNOWN";
}

static void set_state(sm_state_t new_state)
{
    if (s_state != new_state) {
        ESP_LOGI(TAG, "Состояние: %s → %s", sm_state_name(s_state), sm_state_name(new_state));
        s_state = new_state;
        s_step_start_time = esp_timer_get_time();
    }
}

static void set_auto_sub(auto_substate_t sub)
{
    if (s_auto_sub != sub) {
        ESP_LOGI(TAG, "AUTO sub: %d → %d", s_auto_sub, sub);
        s_auto_sub = sub;
        s_step_start_time = esp_timer_get_time();
    }
}

/**
 * @brief Проверка подтверждения насоса
 * @param idx       Индекс 0..2 (pump1..pump3)
 * @param pump_on   true если насос фактически включён (после интерлоков)
 * @param di_pin    Номер DI подтверждения (BOARD_DI_PUMP1_CONF и т.д.)
 * @return true если OK (подтверждён или таймер не истёк), false если таймаут
 */
static bool check_pump_confirmation(uint8_t idx, bool pump_on, uint8_t di_pin)
{
    int64_t now = esp_timer_get_time();
    const plant_config_t *cfg = config_manager_get();
    int64_t confirm_us = (int64_t)cfg->timeouts.pump_confirm_ms * 1000LL;

    if (!pump_on) {
        s_pump_start_us[idx] = 0;
        return true;
    }

    /* Первый цикл с включённым насосом — запомнить время */
    if (s_pump_start_us[idx] == 0) {
        s_pump_start_us[idx] = now;
        return true;
    }

    /* Проверить DI подтверждения */
    uint8_t di = hal_gpio_read_di();
    bool confirmed = (di & (1 << (di_pin - 1))) != 0;
    if (confirmed) {
        s_pump_start_us[idx] = 0;  /* Подтверждён, сброс таймера */
        return true;
    }

    /* Таймаут? */
    return (now - s_pump_start_us[idx]) < confirm_us;
}

/* ===== Обработчики подсостояний ===== */

static void update_auto(const interlock_result_t *ilk)
{
    const plant_config_t *cfg = config_manager_get();
    int64_t elapsed = esp_timer_get_time() - s_step_start_time;
    int64_t ramp_us = (int64_t)cfg->timeouts.pump_ramp_ms * 1000LL;
    uint8_t di = hal_gpio_read_di();

    switch (s_auto_sub) {
    case AUTO_STARTING_PUMP1:
        s_want_pump_feed = true;
        /* Ждём подтверждение DI6 — таймаут проверяется в check_pump_confirmation */
        if (di & (1 << (BOARD_DI_PUMP1_CONF - 1))) {
            ESP_LOGI(TAG, "Pump1 подтверждён, разгон УПП...");
            set_auto_sub(AUTO_RAMP);
        }
        break;

    case AUTO_RAMP:
        s_want_pump_feed = true;
        if (elapsed >= ramp_us) {
            set_auto_sub(AUTO_STARTING_PUMP2);
        }
        break;

    case AUTO_STARTING_PUMP2:
        s_want_pump_feed = true;
        s_want_pump_stage1 = true;
        if (di & (1 << (BOARD_DI_PUMP2_CONF - 1))) {
            ESP_LOGI(TAG, "Pump2 подтверждён");
            set_auto_sub(AUTO_FILLING_INTERM);
        }
        break;

    case AUTO_FILLING_INTERM: {
        s_want_pump_feed = true;
        s_want_pump_stage1 = true;
        /* Ждём появления уровня в промбаке перед запуском 3-го насоса.
         * DI2 (NC): бит=1 → датчик замкнут → вода есть; бит=0 → пуст */
        bool interm_not_empty = (di & (1 << (BOARD_DI_INTERM_EMPTY - 1))) != 0;
        if (interm_not_empty) {
            ESP_LOGI(TAG, "Промбак заполнен — запуск насоса 3");
            set_auto_sub(AUTO_STARTING_PUMP3);
        }
        break;
    }

    case AUTO_STARTING_PUMP3:
        s_want_pump_feed = true;
        s_want_pump_stage1 = true;
        s_want_pump_stage2 = true;
        if (di & (1 << (BOARD_DI_PUMP3_CONF - 1))) {
            ESP_LOGI(TAG, "Pump3 подтверждён — все насосы запущены");
            set_auto_sub(AUTO_RUNNING);
        }
        break;

    case AUTO_RUNNING: {
        bool interm_full   = (di & (1 << (BOARD_DI_INTERM_FULL - 1))) != 0;
        bool permeate_full = (di & (1 << (BOARD_DI_PERMEATE_FULL - 1))) != 0;

        /* Бак пермеата полон → остановка */
        if (permeate_full) {
            ESP_LOGI(TAG, "Бак пермеата полон — остановка");
            set_auto_sub(AUTO_STOPPING);
            break;
        }

        /* Промбак полон → стоп 1-я ступень, 2-я продолжает */
        if (interm_full) {
            s_want_pump_feed = false;
            s_want_pump_stage1 = false;
        } else {
            s_want_pump_feed = true;
            s_want_pump_stage1 = true;
        }
        s_want_pump_stage2 = true;
        break;
    }

    case AUTO_STOPPING:
        all_outputs_off();
        memset(s_pump_start_us, 0, sizeof(s_pump_start_us));
        set_state(SM_IDLE);
        break;
    }
}

/**
 * @brief Гистерезис ТЭНа: T<(target-hyst)→ВКЛ, T>=target→ВЫКЛ, T>max→АВАРИЯ
 *        Гистерезис берётся из конфига (по умолч. 2°C).
 * @return true — нормально, false — перегрев (АВАРИЯ)
 */
static bool heater_hysteresis(float t, const config_washing_t *w)
{
    if (isnan(t)) {
        s_want_heater = false;
        return true;
    }
    if (t > w->max_temp_C) {
        return false;  /* Перегрев */
    }
    if (t < (w->target_temp_C - w->hysteresis_C)) {
        s_want_heater = true;
    } else if (t >= w->target_temp_C) {
        s_want_heater = false;
    }
    /* В зоне (target-hyst)..target — сохраняется предыдущее состояние */
    return true;
}

/* Pending wash confirm flag (set by CMD_CONFIRM_WASH_PHASE) */
static bool s_wash_confirm_pending = false;

static void set_wash_sub(wash_substate_t sub)
{
    if (s_wash_sub != sub) {
        ESP_LOGI(TAG, "WASH sub: %d → %d", s_wash_sub, sub);
        s_wash_sub = sub;
        s_step_start_time = esp_timer_get_time();
    }
}

static void update_washing(const interlock_result_t *ilk)
{
    const plant_config_t *cfg = config_manager_get();
    const config_washing_t *w = &cfg->washing;
    float t = analog_input_get_value(AI_CH_T);
    int64_t elapsed = esp_timer_get_time() - s_step_start_time;
    int64_t heat_limit_us  = (int64_t)w->heat_timeout_min * 60LL * 1000000LL;
    int64_t supply_limit_us = (int64_t)w->supply_time_min * 60LL * 1000000LL;
    int64_t drain_limit_us  = (int64_t)w->drain_time_min  * 60LL * 1000000LL;

    /* Проверка подтверждения оператора */
    bool confirmed = s_wash_confirm_pending;
    s_wash_confirm_pending = false;

    switch (s_wash_sub) {
    case WASH_WAIT_HEAT:
        /* Ожидание подтверждения оператора для начала нагрева */
        all_outputs_off();
        if (confirmed) {
            ESP_LOGI(TAG, "Промывка: оператор подтвердил фазу нагрева");
            set_wash_sub(WASH_HEATING);
        }
        break;

    case WASH_HEATING:
        /* Нагрев: насос подачи + ТЭН с гистерезисом */
        s_want_pump_feed = true;
        if (!heater_hysteresis(t, w)) {
            ESP_LOGE(TAG, "Промывка: перегрев (%.1f°C > max %.1f°C)!", t, w->max_temp_C);
            enter_fault(INTERLOCK_T_HIGH);
            return;
        }
        if (elapsed >= heat_limit_us) {
            ESP_LOGW(TAG, "Промывка: таймаут нагрева %ld мин", (long)w->heat_timeout_min);
            enter_fault(INTERLOCK_T_HIGH);
            return;
        }
        if (!isnan(t) && t >= w->target_temp_C) {
            ESP_LOGI(TAG, "Промывка: температура достигнута (%.1f°C)", t);
            set_wash_sub(WASH_WAIT_SUPPLY);
        }
        break;

    case WASH_WAIT_SUPPLY:
        /* Ожидание подтверждения: подача горячей воды */
        s_want_pump_feed = true;
        /* Поддерживаем температуру пока ждём подтверждение */
        if (!heater_hysteresis(t, w)) {
            enter_fault(INTERLOCK_T_HIGH);
            return;
        }
        if (confirmed) {
            ESP_LOGI(TAG, "Промывка: оператор подтвердил фазу подачи");
            set_wash_sub(WASH_SUPPLY);
        }
        break;

    case WASH_SUPPLY:
        /* Подача горячей воды через мембраны, ТЭН с гистерезисом */
        s_want_pump_feed = true;
        s_want_pump_stage1 = true;
        if (!heater_hysteresis(t, w)) {
            ESP_LOGE(TAG, "Промывка: перегрев (%.1f°C > max %.1f°C)!", t, w->max_temp_C);
            enter_fault(INTERLOCK_T_HIGH);
            return;
        }
        if (elapsed >= supply_limit_us) {
            ESP_LOGI(TAG, "Промывка: подача завершена (%ld мин)", (long)w->supply_time_min);
            set_wash_sub(WASH_WAIT_DRAIN);
        }
        break;

    case WASH_WAIT_DRAIN:
        /* Ожидание подтверждения: дренаж */
        all_outputs_off();
        if (confirmed) {
            ESP_LOGI(TAG, "Промывка: оператор подтвердил фазу дренажа");
            set_wash_sub(WASH_DRAIN);
        }
        break;

    case WASH_DRAIN:
        /* Дренаж: выключить всё */
        all_outputs_off();
        if (elapsed >= drain_limit_us) {
            set_wash_sub(WASH_DONE);
        }
        break;

    case WASH_DONE:
        all_outputs_off();
        ESP_LOGI(TAG, "Промывка завершена");
        set_state(SM_IDLE);
        break;
    }
}

/* ===== Публичный API ===== */

void state_machine_init(void)
{
    s_state = SM_IDLE;
    s_auto_sub = AUTO_STARTING_PUMP1;
    s_wash_sub = WASH_WAIT_HEAT;
    s_fault_flags = 0;
    s_pending_cmd = CMD_NONE;
    s_manual_do_mask = 0;
    s_wash_confirm_pending = false;
    all_outputs_off();
    memset(s_pump_start_us, 0, sizeof(s_pump_start_us));
    hal_gpio_write_do(0x00);
    ESP_LOGI(TAG, "Конечный автомат инициализирован");
}

void state_machine_send_command(sm_command_t cmd)
{
    portENTER_CRITICAL(&s_mux);
    s_pending_cmd = cmd;
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "Команда: %d", cmd);
}

void state_machine_manual_set_do(uint8_t mask)
{
    portENTER_CRITICAL(&s_mux);
    if (s_state == SM_MANUAL) {
        s_manual_do_mask = mask;
    }
    portEXIT_CRITICAL(&s_mux);
}

uint8_t state_machine_get_manual_do_mask(void)
{
    portENTER_CRITICAL(&s_mux);
    uint8_t mask = s_manual_do_mask;
    portEXIT_CRITICAL(&s_mux);
    return mask;
}

sm_status_t state_machine_get_status(void)
{
    portENTER_CRITICAL(&s_mux);
    sm_status_t st = {
        .state = s_state,
        .auto_sub = s_auto_sub,
        .wash_sub = s_wash_sub,
        .fault_flags = s_fault_flags,
    };
    portEXIT_CRITICAL(&s_mux);
    return st;
}

sm_state_t state_machine_get_state(void)
{
    portENTER_CRITICAL(&s_mux);
    sm_state_t st = s_state;
    portEXIT_CRITICAL(&s_mux);
    return st;
}

void state_machine_update(void)
{
    /* 1. Проверка блокировок */
    interlock_result_t ilk;
    interlocks_check(s_state == SM_MANUAL, &ilk);

    /* 2. E-STOP → FAULT */
    if (ilk.estop_active && s_state != SM_FAULT) {
        enter_fault(INTERLOCK_ESTOP);
    }

    /* 3. Обработка команды (атомарное чтение + сброс) */
    portENTER_CRITICAL(&s_mux);
    sm_command_t cmd = s_pending_cmd;
    s_pending_cmd = CMD_NONE;
    portEXIT_CRITICAL(&s_mux);

    switch (s_state) {
    case SM_IDLE:
        all_outputs_off();
        if (cmd == CMD_START_AUTO) {
            set_state(SM_AUTO);
            s_auto_sub = AUTO_STARTING_PUMP1;
            memset(s_pump_start_us, 0, sizeof(s_pump_start_us));
        } else if (cmd == CMD_START_WASHING) {
            set_state(SM_WASHING);
            s_wash_sub = WASH_WAIT_HEAT;
            s_wash_confirm_pending = false;
        } else if (cmd == CMD_SET_MANUAL) {
            set_state(SM_MANUAL);
            s_manual_do_mask = 0;
        }
        break;

    case SM_AUTO:
        if (cmd == CMD_STOP) {
            set_auto_sub(AUTO_STOPPING);
        }
        update_auto(&ilk);
        break;

    case SM_WASHING:
        if (cmd == CMD_STOP) {
            all_outputs_off();
            set_state(SM_IDLE);
        } else {
            if (cmd == CMD_CONFIRM_WASH_PHASE) {
                s_wash_confirm_pending = true;
            }
            update_washing(&ilk);
        }
        break;

    case SM_MANUAL:
        if (cmd == CMD_STOP) {
            s_manual_do_mask = 0;
            hal_gpio_write_do(0x00);
            set_state(SM_IDLE);
        } else if (!ilk.estop_active) {
            /* Проверка зависимостей агрегатов (WARNING, не блокировка) */
            uint8_t m = s_manual_do_mask;
            bool feed_on   = (m & (1 << (BOARD_DO_PUMP_FEED - 1))) != 0;
            bool stage1_on = (m & (1 << (BOARD_DO_PUMP_STAGE1 - 1))) != 0;
            bool stage2_on = (m & (1 << (BOARD_DO_PUMP_STAGE2 - 1))) != 0;
            bool heater_on_m = (m & (1 << (BOARD_DO_HEATER - 1))) != 0;

            if (stage1_on && !feed_on) {
                alarm_raise(ALARM_MANUAL_DEP_WARN, ALARM_CAT_WARNING, (float)BOARD_DO_PUMP_STAGE1);
            }
            if (stage2_on && !stage1_on) {
                alarm_raise(ALARM_MANUAL_DEP_WARN, ALARM_CAT_WARNING, (float)BOARD_DO_PUMP_STAGE2);
            }
            if (heater_on_m && !feed_on) {
                alarm_raise(ALARM_MANUAL_DEP_WARN, ALARM_CAT_WARNING, (float)BOARD_DO_HEATER);
            }

            hal_gpio_write_do(s_manual_do_mask);
        }
        return;  /* В MANUAL не применяем apply_outputs */

    case SM_FAULT:
        all_outputs_off();
        hal_gpio_write_do(0x00);
        if (cmd == CMD_RESET_FAULT && !ilk.estop_active) {
            s_fault_flags = 0;
            memset(s_pump_start_us, 0, sizeof(s_pump_start_us));
            set_state(SM_IDLE);
            ESP_LOGI(TAG, "Авария сброшена");
        }
        return;  /* В FAULT не применяем выходы */
    }

    /* 4. Применение выходов с учётом интерлоков */
    bool pump1_on = s_want_pump_feed   && ilk.allow_pump_feed;
    bool pump2_on = s_want_pump_stage1 && ilk.allow_pump_stage1;
    bool pump3_on = s_want_pump_stage2 && ilk.allow_pump_stage2;
    bool heater_on = s_want_heater     && ilk.allow_heater;

    hal_gpio_write_do_pin(BOARD_DO_PUMP_FEED,   pump1_on);
    hal_gpio_write_do_pin(BOARD_DO_PUMP_STAGE1,  pump2_on);
    hal_gpio_write_do_pin(BOARD_DO_PUMP_STAGE2,  pump3_on);
    hal_gpio_write_do_pin(BOARD_DO_HEATER,       heater_on);

    /* 5. Проверка подтверждения насосов (AUTO и WASHING) */
    if (s_state == SM_AUTO || s_state == SM_WASHING) {
        if (!check_pump_confirmation(0, pump1_on, BOARD_DI_PUMP1_CONF)) {
            ESP_LOGE(TAG, "Таймаут подтверждения pump1!");
            enter_fault(INTERLOCK_PUMP1_TIMEOUT);
            return;
        }
        if (!check_pump_confirmation(1, pump2_on, BOARD_DI_PUMP2_CONF)) {
            ESP_LOGE(TAG, "Таймаут подтверждения pump2!");
            enter_fault(INTERLOCK_PUMP2_TIMEOUT);
            return;
        }
        if (!check_pump_confirmation(2, pump3_on, BOARD_DI_PUMP3_CONF)) {
            ESP_LOGE(TAG, "Таймаут подтверждения pump3!");
            enter_fault(INTERLOCK_PUMP3_TIMEOUT);
            return;
        }
    }
}
