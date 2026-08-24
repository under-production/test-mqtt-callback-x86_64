#include "lampsmart_profile.h"

#include <string.h>

#define DEFAULT_UUID "00000000-9317-5b6d-0000-12c000000000"

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex_u32(const char *s, size_t len, uint32_t *out)
{
    uint32_t value = 0;
    for (size_t i = 0; i < len; ++i) {
        int nibble = hex_nibble(s[i]);
        if (nibble < 0) return false;
        value = (value << 4) | (uint32_t)nibble;
    }
    *out = value;
    return true;
}

esp_err_t lampsmart_profile_from_uuid(lampsmart_profile_t *profile,
                                      const char *uuid_string)
{
    if (!profile || !uuid_string || strlen(uuid_string) != 36) {
        return ESP_ERR_INVALID_ARG;
    }
    if (uuid_string[8] != '-' || uuid_string[13] != '-' ||
        uuid_string[18] != '-' || uuid_string[23] != '-') {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t aaaa = 0, bbbb = 0, dddd = 0;
    if (!parse_hex_u32(&uuid_string[9], 4, &aaaa) ||
        !parse_hex_u32(&uuid_string[14], 4, &bbbb) ||
        !parse_hex_u32(&uuid_string[24], 4, &dddd)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(profile, 0, sizeof(*profile));
    memcpy(profile->uuid_string, uuid_string, 36);
    profile->uuid_string[36] = '\0';
    profile->ca4 = ((aaaa & 0xFFFFu) << 16) | (bbbb & 0xFFFFu);
    profile->device_src = (uint16_t)dddd;
    profile->f008_target4[0] = (uint8_t)(profile->ca4 & 0xFFu);
    profile->f008_target4[1] = (uint8_t)((profile->ca4 >> 8) & 0xFFu);
    profile->f008_target4[2] = (uint8_t)((profile->ca4 >> 16) & 0xFFu);
    profile->f008_target4[3] = (uint8_t)((profile->ca4 >> 24) & 0xFFu);
    const uint8_t device_addr[5] = {0x98, 0x43, 0xAF, 0x0B, 0x46};
    memcpy(profile->uuid77_device_addr, device_addr, sizeof(device_addr));
    profile->uuid77_address2[0] = (uint8_t)(profile->ca4 & 0xFFu);
    profile->uuid77_address2[1] =
        (uint8_t)(((profile->ca4 >> 12) & 0x0Fu) << 4);
    profile->uuid77_param5 = (uint8_t)(profile->device_src & 0xFFu);
    return ESP_OK;
}

void lampsmart_profile_set_default(lampsmart_profile_t *profile)
{
    (void)lampsmart_profile_from_uuid(profile, DEFAULT_UUID);
}


