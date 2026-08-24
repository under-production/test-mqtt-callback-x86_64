/* ESP-IDF esp_log.h compatibility shim: printf ベースのロガー実装。 */
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <time.h>

typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE,
} esp_log_level_t;

static inline long long esp_log_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

#define ESP_LOG_LEVEL_LOCAL(level, tag, format, ...)                         \
    esp_log_write_line((level), (tag), (format), ##__VA_ARGS__)

void esp_log_write_line(esp_log_level_t level, const char *tag,
                         const char *format, ...);

#define ESP_LOGE(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_WARN,  tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO,  tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

void esp_log_buffer_hex_internal(const char *tag, const void *buffer,
                                  uint16_t buff_len, esp_log_level_t level);
#define ESP_LOG_BUFFER_HEX_LEVEL(tag, buffer, buff_len, level)               \
    esp_log_buffer_hex_internal((tag), (buffer), (buff_len), (level))
