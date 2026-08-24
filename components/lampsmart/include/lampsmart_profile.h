#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define LAMPSMART_UUID_STRING_LEN 37

typedef struct {
    char uuid_string[LAMPSMART_UUID_STRING_LEN];
    uint32_t ca4;
    uint16_t device_src;
    uint8_t f008_target4[4];
    uint8_t uuid77_device_addr[5];
    uint8_t uuid77_address2[2];
    uint8_t uuid77_param5;
} lampsmart_profile_t;

esp_err_t lampsmart_profile_from_uuid(lampsmart_profile_t *profile,
                                      const char *uuid_string);
void lampsmart_profile_set_default(lampsmart_profile_t *profile);


