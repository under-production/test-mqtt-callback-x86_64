#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "lampsmart_defs.h"
#include "lampsmart_profile.h"

esp_err_t lampsmart_f008_build_adv31(const lampsmart_profile_t *profile,
                                     uint8_t key2, uint16_t command,
                                     const uint8_t payload4[4], uint16_t rand16,
                                     uint8_t out31[LAMPSMART_ADV_LEN]);


