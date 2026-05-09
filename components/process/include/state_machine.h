/**
 * @file state_machine.h
 * @brief Конечный автомат установки обратного осмоса
 *
 * Состояния: IDLE → AUTO → WASHING → MANUAL → FAULT
 *
 * AUTO подсостояния:
 *   STARTING_PUMP1 → RAMP → STARTING_PUMP2 → FILLING_INTERM →
 *   STARTING_PUMP3 → RUNNING → STOPPING
 *
 * WASHING подсостояния:
 *   HEATING → SUPPLY → DRAIN → DONE
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SM_IDLE,
    SM_AUTO,
    SM_WASHING,
    SM_MANUAL,
    SM_FAULT
} sm_state_t;

typedef enum {
    AUTO_STARTING_PUMP1,
    AUTO_RAMP,
    AUTO_STARTING_PUMP2,
    AUTO_FILLING_INTERM,
    AUTO_STARTING_PUMP3,
    AUTO_RUNNING,
    AUTO_STOPPING
} auto_substate_t;

typedef enum {
    WASH_WAIT_HEAT,     /* Ожидание подтверждения оператора: фаза нагрева */
    WASH_HEATING,       /* Нагрев с гистерезисом ТЭНа */
    WASH_WAIT_SUPPLY,   /* Ожидание подтверждения: фаза подачи */
    WASH_SUPPLY,        /* Подача горячей воды через мембраны */
    WASH_WAIT_DRAIN,    /* Ожидание подтверждения: фаза дренажа */
    WASH_DRAIN,         /* Дренаж */
    WASH_DONE
} wash_substate_t;

typedef enum {
    CMD_NONE,
    CMD_START_AUTO,
    CMD_STOP,
    CMD_START_WASHING,
    CMD_CONFIRM_WASH_PHASE,  /* Подтверждение текущей фазы промывки с HMI */
    CMD_SET_MANUAL,
    CMD_RESET_FAULT
} sm_command_t;

typedef struct {
    sm_state_t      state;
    auto_substate_t auto_sub;
    wash_substate_t wash_sub;
    uint32_t        fault_flags;
} sm_status_t;

void state_machine_init(void);

/**
 * @brief Обновить КА (вызывать каждые 100мс из ProcessTask)
 */
void state_machine_update(void);

/**
 * @brief Отправить команду КА
 */
void state_machine_send_command(sm_command_t cmd);

/**
 * @brief Прямое управление DO в режиме MANUAL
 * @param mask  битовая маска DO (бит 0 = RO1)
 */
void state_machine_manual_set_do(uint8_t mask);

/**
 * @brief Получить текущую желаемую маску DO в режиме MANUAL
 */
uint8_t state_machine_get_manual_do_mask(void);

sm_status_t state_machine_get_status(void);
sm_state_t  state_machine_get_state(void);

/**
 * @brief Конвертация состояний в строковые имена (общие утилиты)
 */
const char *sm_state_name(sm_state_t st);
const char *sm_auto_sub_name(auto_substate_t sub);
const char *sm_wash_sub_name(wash_substate_t sub);

#ifdef __cplusplus
}
#endif
