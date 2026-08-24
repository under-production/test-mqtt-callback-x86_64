/* ESP-IDF互換シムの実体実装 (x86_64 Linux向け)。 */
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "aes/esp_aes.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

/* ---- esp_err_to_name ---- */
const char *esp_err_to_name(esp_err_t code)
{
    switch (code) {
    case ESP_OK: return "ESP_OK";
    case ESP_FAIL: return "ESP_FAIL";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE: return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_RESPONSE: return "ESP_ERR_INVALID_RESPONSE";
    case ESP_ERR_INVALID_CRC: return "ESP_ERR_INVALID_CRC";
    case ESP_ERR_INVALID_VERSION: return "ESP_ERR_INVALID_VERSION";
    case ESP_ERR_INVALID_MAC: return "ESP_ERR_INVALID_MAC";
    case ESP_ERR_NOT_FINISHED: return "ESP_ERR_NOT_FINISHED";
    case ESP_ERR_NOT_ALLOWED: return "ESP_ERR_NOT_ALLOWED";
    case ESP_ERR_NVS_NOT_INITIALIZED: return "ESP_ERR_NVS_NOT_INITIALIZED";
    case ESP_ERR_NVS_NOT_FOUND: return "ESP_ERR_NVS_NOT_FOUND";
    case ESP_ERR_NVS_NO_FREE_PAGES: return "ESP_ERR_NVS_NO_FREE_PAGES";
    case ESP_ERR_NVS_NEW_VERSION_FOUND: return "ESP_ERR_NVS_NEW_VERSION_FOUND";
    default: return "ESP_ERR_UNKNOWN";
    }
}

/* ---- esp_log ---- */
static pthread_mutex_t s_log_lock = PTHREAD_MUTEX_INITIALIZER;

static char level_char(esp_log_level_t level)
{
    switch (level) {
    case ESP_LOG_ERROR: return 'E';
    case ESP_LOG_WARN: return 'W';
    case ESP_LOG_INFO: return 'I';
    case ESP_LOG_DEBUG: return 'D';
    case ESP_LOG_VERBOSE: return 'V';
    default: return '?';
    }
}

void esp_log_write_line(esp_log_level_t level, const char *tag,
                         const char *format, ...)
{
    pthread_mutex_lock(&s_log_lock);
    FILE *out = (level == ESP_LOG_ERROR || level == ESP_LOG_WARN) ? stderr : stdout;
    fprintf(out, "%c (%lld) %s: ", level_char(level), esp_log_now_ms(), tag);
    va_list args;
    va_start(args, format);
    vfprintf(out, format, args);
    va_end(args);
    fprintf(out, "\n");
    fflush(out);
    pthread_mutex_unlock(&s_log_lock);
}

void esp_log_buffer_hex_internal(const char *tag, const void *buffer,
                                  uint16_t buff_len, esp_log_level_t level)
{
    const unsigned char *bytes = (const unsigned char *)buffer;
    char line[3 * 16 + 1];
    for (uint16_t offset = 0; offset < buff_len; offset += 16) {
        int pos = 0;
        uint16_t chunk = (buff_len - offset) < 16 ? (buff_len - offset) : 16;
        for (uint16_t i = 0; i < chunk; ++i) {
            pos += snprintf(&line[pos], sizeof(line) - pos, "%02x ", bytes[offset + i]);
        }
        esp_log_write_line(level, tag, "%s", line);
    }
}

/* ---- esp_timer ---- */
int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

/* ---- esp_random ---- */
uint32_t esp_random(void)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static FILE *urandom = NULL;
    uint32_t value = 0;
    pthread_mutex_lock(&lock);
    if (!urandom) urandom = fopen("/dev/urandom", "rb");
    if (urandom && fread(&value, sizeof(value), 1, urandom) != 1) {
        value = (uint32_t)time(NULL);
    }
    pthread_mutex_unlock(&lock);
    return value;
}

void esp_fill_random(void *buffer, size_t length)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static FILE *urandom = NULL;
    pthread_mutex_lock(&lock);
    if (!urandom) urandom = fopen("/dev/urandom", "rb");
    if (!urandom || fread(buffer, 1, length, urandom) != length) {
        unsigned char *bytes = buffer;
        for (size_t i = 0; i < length; ++i) bytes[i] = (unsigned char)(time(NULL) + i);
    }
    pthread_mutex_unlock(&lock);
}

/* ---- esp_aes (mbedTLS wrapper) ---- */
void esp_aes_init(esp_aes_context *ctx) { mbedtls_aes_init(&ctx->ctx); }
void esp_aes_free(esp_aes_context *ctx) { mbedtls_aes_free(&ctx->ctx); }

int esp_aes_setkey(esp_aes_context *ctx, const unsigned char *key,
                    unsigned int keybits)
{
    return mbedtls_aes_setkey_enc(&ctx->ctx, key, keybits);
}

int esp_aes_crypt_ecb(esp_aes_context *ctx, int mode,
                       const unsigned char input[16], unsigned char output[16])
{
    return mbedtls_aes_crypt_ecb(&ctx->ctx,
                                  mode == ESP_AES_ENCRYPT ? MBEDTLS_AES_ENCRYPT
                                                           : MBEDTLS_AES_DECRYPT,
                                  input, output);
}
