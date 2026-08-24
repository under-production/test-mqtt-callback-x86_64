/* ESP-IDF esp_random.h compatibility shim: /dev/urandom ベースの乱数生成。 */
#pragma once

#include <stddef.h>
#include <stdint.h>

uint32_t esp_random(void);
void esp_fill_random(void *buffer, size_t length);
