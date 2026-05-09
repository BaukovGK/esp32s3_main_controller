/**
 * @file hal_buzzer.c
 * @brief Реализация драйвера пьезо-зуммера.
 *
 * Потокобезопасность:
 *   hal_buzzer_set_pattern() и hal_buzzer_silence() могут вызываться
 *   из разных контекстов:
 *     - process_task (по результатам scan_active_alarms);
 *     - mqtt event task (handle_silence в mqtt_subscribe.c);
 *     - httpd task (POST /api/v1/silence в web_api_command.c).
 *   hal_buzzer_tick() вызывается из process_task (раз в 100 мс).
 *
 *   Все мутации общего состояния (s_pattern, s_phase_tick, s_output_state)
 *   защищены мьютексом s_lock. GPIO 46 — обычный push-pull без I2C, поэтому
 *   gpio_set_level() безопасно вызывать под локом (вызов короткий и не
 *   блокирующий, в отличие от led_strip/RMT в hal_rgb).
 */
#include "hal_buzzer.h"
#include "board_config.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "buzzer";

/* tick = 100 мс. Длительности в тиках. */
#define TICK_MS                    100
#define SHORT_BEEP_TICKS           1     /* 100 мс ВКЛ */
#define SHORT_PERIOD_TICKS         50    /* раз в 5 секунд */
#define SLOW_HALF_PERIOD_TICKS     5     /* 500 мс */
#define LOCK_TIMEOUT_MS            50

static buzzer_pattern_t s_pattern = BUZZER_PATTERN_OFF;
static int  s_phase_tick = 0;
static bool s_output_state = false;
static SemaphoreHandle_t s_lock = NULL;

static inline bool lock_take(void)
{
    if (!s_lock) return true;  /* до init() — однопоточный путь */
    return xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) == pdTRUE;
}

static inline void lock_give(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

/* Вызывается ТОЛЬКО под локом (либо до init() из hal_buzzer_init). */
static void set_gpio(bool on)
{
    if (on != s_output_state) {
        gpio_set_level(BOARD_BUZZER_GPIO, on ? 1 : 0);
        s_output_state = on;
    }
}

esp_err_t hal_buzzer_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            ESP_LOGE(TAG, "не удалось создать мьютекс");
            return ESP_ERR_NO_MEM;
        }
    }

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOARD_BUZZER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t r = gpio_config(&cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config: %s", esp_err_to_name(r));
        return r;
    }
    set_gpio(false);
    s_pattern = BUZZER_PATTERN_OFF;
    s_phase_tick = 0;
    ESP_LOGI(TAG, "Buzzer на GPIO%d инициализирован", BOARD_BUZZER_GPIO);
    return ESP_OK;
}

void hal_buzzer_set_pattern(buzzer_pattern_t p)
{
    if (!lock_take()) {
        ESP_LOGW(TAG, "set_pattern: не удалось взять лок за %d мс", LOCK_TIMEOUT_MS);
        return;
    }
    if (p != s_pattern) {
        s_pattern = p;
        s_phase_tick = 0;  /* сброс фазы — паттерн начинает играть с нуля */
        if (p == BUZZER_PATTERN_OFF) {
            set_gpio(false);
        }
    }
    lock_give();
}

void hal_buzzer_silence(void)
{
    if (!lock_take()) {
        ESP_LOGW(TAG, "silence: не удалось взять лок за %d мс", LOCK_TIMEOUT_MS);
        return;
    }
    s_pattern = BUZZER_PATTERN_OFF;
    set_gpio(false);
    lock_give();
}

void hal_buzzer_tick(void)
{
    if (!lock_take()) {
        /* Один пропущенный тик — не страшно; следующий тик придёт через 100 мс. */
        ESP_LOGW(TAG, "tick: не удалось взять лок за %d мс", LOCK_TIMEOUT_MS);
        return;
    }
    switch (s_pattern) {
    case BUZZER_PATTERN_OFF:
        set_gpio(false);
        break;

    case BUZZER_PATTERN_CONTINUOUS:
        set_gpio(true);
        break;

    case BUZZER_PATTERN_SLOW:
        /* 500мс ВКЛ + 500мс ВЫКЛ */
        s_phase_tick++;
        if (s_phase_tick >= 2 * SLOW_HALF_PERIOD_TICKS) s_phase_tick = 0;
        set_gpio(s_phase_tick < SLOW_HALF_PERIOD_TICKS);
        break;

    case BUZZER_PATTERN_SHORT:
        s_phase_tick++;
        if (s_phase_tick >= SHORT_PERIOD_TICKS) s_phase_tick = 0;
        set_gpio(s_phase_tick < SHORT_BEEP_TICKS);
        break;

    default:
        set_gpio(false);
        break;
    }
    lock_give();
}
