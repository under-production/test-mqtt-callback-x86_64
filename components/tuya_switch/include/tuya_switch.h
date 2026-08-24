#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define TUYA_SWITCH_LOCAL_KEY_SIZE 16

typedef struct {
    const char *ip;
    uint16_t port;
    const char *device_id;
    const char *local_key;
    uint32_t timeout_ms;
} tuya_switch_config_t;

esp_err_t tuya_switch_validate_config(const tuya_switch_config_t *config);
esp_err_t tuya_switch_get_power(const tuya_switch_config_t *config, bool *power);
esp_err_t tuya_switch_set_power_verified(const tuya_switch_config_t *config,
                                         bool requested_power,
                                         bool *actual_power);

