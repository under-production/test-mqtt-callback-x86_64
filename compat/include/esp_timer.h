/* ESP-IDF esp_timer.h compatibility shim: CLOCK_MONOTONICベースのマイクロ秒タイマー。 */
#pragma once

#include <stdint.h>

int64_t esp_timer_get_time(void);
