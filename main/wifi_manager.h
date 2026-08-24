#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t wifi_manager_init(void);
void wifi_manager_process(void);
esp_err_t wifi_manager_stop(void);
esp_err_t wifi_manager_restart(void);
uint32_t wifi_manager_failure_count(void);
