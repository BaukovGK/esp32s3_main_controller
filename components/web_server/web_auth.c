/**
 * @file web_auth.c
 * @brief HTTP Basic Auth для встроенного веб-сервера (Phase-4, H-13).
 *
 * Логика: при пустом username в config_web_auth_t авторизация выключена
 * (поведение до Phase-4 — backward-compat). При непустом username каждый
 * handler первой строкой вызывает web_auth_check(req); функция проверяет
 * заголовок Authorization и при несовпадении возвращает 401.
 *
 * Хранится pre-encoded base64 строка "Basic <base64(user:pass)>" — это
 * позволяет сравнивать с заголовком без декодирования. При смене конфига
 * кэш пересчитывается через web_auth_refresh().
 *
 * Hardening (review C-6, C-7):
 *  - C-6: сравнение строк — constant-time (mbedtls_ct_memcmp). Чтобы
 *    раннее «return» по разнице длин не утекало timing-ом длину совпавшего
 *    префикса, всегда прогоняем ct-сравнение по фиксированному размеру
 *    буфера (sizeof(s_expected)). Хвост за пределами реальных строк — это
 *    нулевые байты, идентичные у обеих сторон (буферы предварительно
 *    обнуляются), поэтому хвост не вносит ложного несовпадения.
 *  - C-7: web_auth_refresh() может выполняться параллельно с
 *    web_auth_check() из httpd-task. Доступ к s_expected/s_enabled
 *    защищён короткоживущим мьютексом. Альтернативу с double-buffering
 *    + atomic-указателем рассматривали — текущая нагрузка (auth раз
 *    на запрос, refresh — изредка через config endpoint) не оправдывает
 *    усложнения.
 *
 * Логи НЕ содержат credentials, base64-токен и его хеш.
 */
#include "web_auth.h"
#include "config_manager.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "mbedtls/constant_time.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "web_auth";

/* Закэшированная строка вида "Basic <base64(user:pass)>". Пустая если auth выкл.
 * Размер выровнен с приёмным буфером в web_auth_check() — это важно для
 * constant-time сравнения по фиксированной длине. */
#define WEB_AUTH_BUF_SZ 160

static char             s_expected[WEB_AUTH_BUF_SZ];
static size_t           s_expected_len = 0;
static bool             s_enabled      = false;
static SemaphoreHandle_t s_auth_lock   = NULL;

/* Длина мьютекс-таймаута в тиках. Лок берётся всегда коротко, но
 * portMAX_DELAY использовать не хочется — лучше зашитый верхний потолок. */
#define WEB_AUTH_LOCK_TIMEOUT pdMS_TO_TICKS(500)

static void rebuild_cache_locked(void)
{
    /* caller владеет s_auth_lock */
    config_web_auth_t a;
    plant_config_t cfg;
    config_manager_get_copy(&cfg);
    a = cfg.web_auth;

    /* Полное обнуление буфера: гарантирует, что «хвост» за реальной
     * строкой содержит нули, что важно для константного сравнения. */
    memset(s_expected, 0, sizeof(s_expected));
    s_expected_len = 0;

    if (a.username[0] == '\0') {
        s_enabled = false;
        return;
    }

    char raw[80];
    int  raw_len = snprintf(raw, sizeof(raw), "%s:%s", a.username, a.password);
    if (raw_len <= 0 || raw_len >= (int)sizeof(raw)) {
        ESP_LOGE(TAG, "user:pass слишком длинный — auth ВЫКЛ");
        s_enabled = false;
        return;
    }

    unsigned char b64[128];
    size_t b64_len = 0;
    int r = mbedtls_base64_encode(b64, sizeof(b64) - 1, &b64_len,
                                  (const unsigned char *)raw, raw_len);
    if (r != 0) {
        ESP_LOGE(TAG, "base64 encode failed: -0x%x — auth ВЫКЛ", -r);
        s_enabled = false;
        return;
    }
    b64[b64_len] = '\0';

    int n = snprintf(s_expected, sizeof(s_expected), "Basic %s", (char *)b64);
    if (n <= 0 || n >= (int)sizeof(s_expected)) {
        /* Не должны сюда попадать — sizeof(s_expected)=160 покрывает любые
         * валидные user:pass до 80 байт. Перестраховка: на всякий случай
         * затираем буфер, чтобы не оставить полу-обновлённую строку. */
        memset(s_expected, 0, sizeof(s_expected));
        s_enabled = false;
        return;
    }
    s_expected_len = (size_t)n;
    s_enabled = true;
    ESP_LOGI(TAG, "Auth активирована для '%s'", a.username);
}

static void rebuild_cache(void)
{
    if (s_auth_lock == NULL) {
        /* init ещё не вызывался — обновляем без блокировки (вызов из
         * web_auth_init безопасен, в этот момент httpd ещё не стартовал). */
        rebuild_cache_locked();
        return;
    }
    if (xSemaphoreTake(s_auth_lock, WEB_AUTH_LOCK_TIMEOUT) != pdTRUE) {
        ESP_LOGW(TAG, "refresh: не удалось взять lock");
        return;
    }
    rebuild_cache_locked();
    xSemaphoreGive(s_auth_lock);
}

void web_auth_init(void)
{
    if (s_auth_lock == NULL) {
        s_auth_lock = xSemaphoreCreateMutex();
        if (s_auth_lock == NULL) {
            /* Без мьютекса auth не может быть thread-safe — отключаем во
             * избежание гонок на смене credentials в рантайме. */
            ESP_LOGE(TAG, "не удалось создать mutex — auth ВЫКЛ");
            s_enabled = false;
            return;
        }
    }
    rebuild_cache();
}

void web_auth_refresh(void)
{
    rebuild_cache();
}

bool web_auth_check(httpd_req_t *req)
{
    /* Снимок флага без лока — допустимо: при гонке worst-case прогоним
     * лишнее сравнение, корректность не страдает (см. ниже — финальное
     * решение принимается под локом). */
    if (!s_enabled) return true;

    /* Приёмный буфер выровнен по размеру с s_expected — это нужно для
     * constant-time сравнения по фиксированной длине. */
    char header[WEB_AUTH_BUF_SZ];
    memset(header, 0, sizeof(header));

    esp_err_t r = httpd_req_get_hdr_value_str(req, "Authorization",
                                              header, sizeof(header));
    /* r может быть ESP_ERR_HTTPD_RESULT_TRUNC если заголовок длиннее буфера —
     * в этом случае header заполнен первыми (sizeof-1) байтами, что заведомо
     * не равно валидному "Basic <b64>". Мы всё равно прогоним ct-сравнение,
     * чтобы не дать тайминг-сигнала о наличии/отсутствии заголовка. */

    bool ok = false;

    if (xSemaphoreTake(s_auth_lock, WEB_AUTH_LOCK_TIMEOUT) == pdTRUE) {
        bool   enabled  = s_enabled;
        size_t exp_len  = s_expected_len;

        /* C-6: ВСЕГДА сравниваем по полному размеру буфера. Оба буфера
         * предварительно занулены, реальные строки заканчиваются NUL и
         * далее — нули. Если r != ESP_OK, header — это по-прежнему
         * нулевой буфер, и сравнение даст «не совпало», но за то же
         * время, что и при содержательной разнице.
         *
         * Дополнительно сверяем длины через ct-операции, чтобы заголовок
         * длиной ровно до s_expected_len, отличающийся только хвостом из
         * нулей, не считался валидным. На практике snprintf-результат
         * содержит ровно одну NUL-границу, и совпадение обеих длин
         * необходимо. */
        size_t hdr_len   = strnlen(header, sizeof(header));
        int    cmp       = mbedtls_ct_memcmp(header, s_expected, sizeof(s_expected));
        int    len_diff  = (int)(hdr_len ^ exp_len);    /* 0 iff длины равны */

        ok = enabled && (r == ESP_OK) && (cmp == 0) && (len_diff == 0);

        xSemaphoreGive(s_auth_lock);
    } else {
        ESP_LOGW(TAG, "check: не удалось взять lock");
        ok = false;
    }

    /* Затираем приёмный буфер — credential-шум не должен оставаться на
     * стеке дольше необходимого. */
    memset(header, 0, sizeof(header));

    if (ok) return true;

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"RO Plant\"");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":false,\"error\":\"unauthorized\"}", -1);
    return false;
}
