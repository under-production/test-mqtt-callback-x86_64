#pragma once

#include "esp_err.h"

esp_err_t room_temperature_init(void);
esp_err_t room_temperature_read(float *temperature_c);
esp_err_t room_temperature_reinitialize(void);
void room_temperature_deinit(void);
