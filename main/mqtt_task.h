#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t mqtt_task_start(void);
uint32_t mqtt_task_failure_count(void);

