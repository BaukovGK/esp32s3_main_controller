/**
 * @file hal_rgb.c
 * @brief WS2812 RGB-LED через managed компонент led_strip.
 *
 * Потокобезопасность:
 *   На текущий момент ВСЕ вызовы hal_rgb_set / hal_rgb_off / hal_rgb_tick
 *   идут только из process_task (см. rgb_set_for_state() и hal_rgb_tick()
 *   в components/process/process_task.c). Поэтому статические поля
 *   s_r/s_g/s_b/s_blink/s_blink_phase/s_blink_on защищать мьютексом не нужно —
 *   де-факто это однопоточный модуль.
 *
 *   ВНИМАНИЕ: если в будущем появятся вызовы из httpd / mqtt / иного контекста
 *   (например, тестовая ручка "включи LED фиолетовым"), необходимо добавить
 *   мьютекс по образцу hal_buzzer.c. При этом учесть, что apply_color()
 *   вызывает led_strip_refresh() через RMT — операция может блокировать
 *   до ~100 мкс, поэтому держать лок нужно ТОЛЬКО на время чтения/записи
 *   полей, а apply_color() вызывать с локальной копии после release.
 */
#include "hal_rgb.h"
#include "board_config.h"

#include "esp_log.h"
#include "led_strip.h"

#include <stdbool.h>

static const char *TAG = "hal_rgb";

#define BLINK_HALF_PERIOD_TICKS  5  /* 5*100мс = 500мс */

static led_strip_handle_t s_strip = NULL;
static uint8_t s_r, s_g, s_b;
static bool    s_blink = false;
static int     s_blink_phase = 0;
static bool    s_blink_on = true;

static void apply_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return;
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

esp_err_t hal_rgb_init(void)
{
    /* led_strip 2.5.x: формат пикселей задаётся через led_pixel_format
     * (в 3.x переименовали в color_component_format). По умолчанию GRB
     * для WS2812 — этого хватает. */
    led_strip_config_t strip_config = {
        .strip_gpio_num = BOARD_RGB_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags = { .with_dma = false },
    };

    esp_err_t r = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device: %s", esp_err_to_name(r));
        s_strip = NULL;
        return r;
    }
    apply_color(0, 0, 0);
    s_r = s_g = s_b = 0;
    s_blink = false;
    s_blink_phase = 0;
    s_blink_on = true;
    ESP_LOGI(TAG, "RGB LED на GPIO%d инициализирован", BOARD_RGB_LED_GPIO);
    return ESP_OK;
}

/* Вызывать ТОЛЬКО из process_task — см. шапку файла. */
void hal_rgb_set(uint8_t r, uint8_t g, uint8_t b, bool blink)
{
    if (r == s_r && g == s_g && b == s_b && blink == s_blink) return;

    s_r = r; s_g = g; s_b = b;
    s_blink = blink;
    s_blink_phase = 0;
    s_blink_on = true;
    apply_color(r, g, b);
}

/* Вызывать ТОЛЬКО из process_task — см. шапку файла. */
void hal_rgb_off(void)
{
    hal_rgb_set(0, 0, 0, false);
}

/* Вызывать ТОЛЬКО из process_task (тик 100 мс). */
void hal_rgb_tick(void)
{
    if (!s_blink || !s_strip) return;
    s_blink_phase++;
    if (s_blink_phase >= BLINK_HALF_PERIOD_TICKS) {
        s_blink_phase = 0;
        s_blink_on = !s_blink_on;
        if (s_blink_on) apply_color(s_r, s_g, s_b);
        else            apply_color(0, 0, 0);
    }
}
