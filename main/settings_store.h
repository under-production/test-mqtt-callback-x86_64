#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t settings_store_load_alerts(int8_t *high, int8_t *low);
esp_err_t settings_store_save_high(int8_t value);
esp_err_t settings_store_save_low(int8_t value);
esp_err_t settings_store_load_switch_enabled(bool *enabled);
esp_err_t settings_store_save_switch_enabled(bool enabled);


