#pragma once

#include <stdint.h>

#include "lampsmart_defs.h"
#include "lampsmart_profile.h"

#define LAMPSMART_77F8_PARAM4_NORMAL  0x00u
#define LAMPSMART_77F8_PARAM4_PAIRING 0x81u

void lampsmart_77f8_build_adv31(const lampsmart_profile_t *profile,
                                uint16_t seq, uint8_t command,
                                const uint8_t payload2[2], uint8_t param4,
                                uint8_t rand_lo,
                                uint8_t out31[LAMPSMART_ADV_LEN]);


