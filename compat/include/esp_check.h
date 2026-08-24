/* ESP-IDF esp_check.h compatibility shim. */
#pragma once

#include "esp_err.h"
#include "esp_log.h"

#define ESP_RETURN_ON_ERROR(x, tag, format, ...)                             \
    do {                                                                     \
        esp_err_t __err_rc = (x);                                            \
        if (__err_rc != ESP_OK) {                                            \
            ESP_LOGE(tag, format, ##__VA_ARGS__);                            \
            return __err_rc;                                                 \
        }                                                                    \
    } while (0)

#define ESP_RETURN_ON_FALSE(cond, err, tag, format, ...)                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ESP_LOGE(tag, format, ##__VA_ARGS__);                            \
            return (err);                                                    \
        }                                                                    \
    } while (0)

#define ESP_GOTO_ON_ERROR(x, label, tag, format, ...)                        \
    do {                                                                     \
        ret = (x);                                                           \
        if (ret != ESP_OK) {                                                 \
            ESP_LOGE(tag, format, ##__VA_ARGS__);                            \
            goto label;                                                      \
        }                                                                    \
    } while (0)

#define ESP_GOTO_ON_FALSE(cond, err, label, tag, format, ...)                \
    do {                                                                     \
        if (!(cond)) {                                                      \
            ret = (err);                                                    \
            ESP_LOGE(tag, format, ##__VA_ARGS__);                            \
            goto label;                                                      \
        }                                                                    \
    } while (0)
