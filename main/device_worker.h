#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t device_worker_initialize(void);
void device_worker_set_temperature_sensor_enabled(bool enabled);
esp_err_t device_worker_start(void);


