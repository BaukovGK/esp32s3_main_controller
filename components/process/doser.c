/**
 * @file doser.c
 * @brief Таймер дозатора антискаланта
 */
#include "doser.h"
#include "config_manager.h"
#include "hal_gpio.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "doser";

static doser_state_t s_state = DOSER_OFF;
static bool s_enabled = true;
static int64_t s_timer_start = 0;

void doser_init(void)
{
    s_state = DOSER_OFF;
    s_enabled = true;
    s_timer_start = 0;
    hal_gpio_write_do_pin(BOARD_DO_DOSER, false);
    ESP_LOGI(TAG, "Дозатор инициализирован");
}

void doser_enable(bool en)
{
    s_enabled = en;
}

bool doser_is_enabled(void)
{
    return s_enabled;
}

doser_state_t doser_get_state(void)
{
    return s_state;
}

void doser_update(bool auto_running)
{
    const plant_config_t *cfg = config_manager_get();
    int64_t now = esp_timer_get_time();

    /* Дозатор активен только в AUTO + если включён */
    if (!auto_running || !s_enabled) {
        if (s_state != DOSER_OFF) {
            s_state = DOSER_OFF;
            s_timer_start = 0;
            hal_gpio_write_do_pin(BOARD_DO_DOSER, false);
            ESP_LOGI(TAG, "Дозатор ВЫКЛ");
        }
        return;
    }

    int64_t run_us = (int64_t)cfg->doser.run_time_min * 60 * 1000000LL;
    int64_t cycle_us = (int64_t)cfg->doser.cycle_time_min * 60 * 1000000LL;
    int64_t pause_us = cycle_us - run_us;

    switch (s_state) {
    case DOSER_OFF:
        /* Первый запуск — начинаем дозирование */
        s_state = DOSER_RUNNING;
        s_timer_start = now;
        hal_gpio_write_do_pin(BOARD_DO_DOSER, true);
        ESP_LOGI(TAG, "Дозатор ВКЛ (%ld мин)", (long)cfg->doser.run_time_min);
        break;

    case DOSER_RUNNING:
        if ((now - s_timer_start) >= run_us) {
            s_state = DOSER_PAUSE;
            s_timer_start = now;
            hal_gpio_write_do_pin(BOARD_DO_DOSER, false);
            ESP_LOGI(TAG, "Дозатор ПАУЗА (%ld мин)",
                     (long)(cfg->doser.cycle_time_min - cfg->doser.run_time_min));
        }
        break;

    case DOSER_PAUSE:
        if ((now - s_timer_start) >= pause_us) {
            s_state = DOSER_RUNNING;
            s_timer_start = now;
            hal_gpio_write_do_pin(BOARD_DO_DOSER, true);
            ESP_LOGI(TAG, "Дозатор ВКЛ (%ld мин)", (long)cfg->doser.run_time_min);
        }
        break;
    }
}
