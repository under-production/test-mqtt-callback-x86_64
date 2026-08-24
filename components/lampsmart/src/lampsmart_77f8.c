#include "lampsmart_77f8.h"

#include <stdlib.h>
#include <string.h>

#include "lampsmart_crc.h"

static uint8_t bit_reverse_byte(uint8_t value)
{
    value = (uint8_t)(((value & 0xF0u) >> 4) | ((value & 0x0Fu) << 4));
    value = (uint8_t)(((value & 0xCCu) >> 2) | ((value & 0x33u) << 2));
    return (uint8_t)(((value & 0xAAu) >> 1) | ((value & 0x55u) << 1));
}

static void ble_whitening(const uint8_t *input, uint8_t *output, size_t len)
{
    uint8_t lfsr = 0x53;
    for (size_t i = 0; i < len; ++i) {
        uint8_t out = 0;
        for (int bit = 0; bit < 8; ++bit) {
            uint8_t old = lfsr;
            uint8_t whiten = (old & 0x40u) >> 6;
            out |= (uint8_t)((((input[i] >> bit) & 1u) ^ whiten) << bit);
            uint8_t feedback = (old >> 6) & 1u;
            lfsr = (uint8_t)(((old << 1) & 0xFEu) | feedback);
            if (feedback) lfsr ^= 0x10u;
        }
        output[i] = out;
    }
}

static void whitening_for_rf_packet(const uint8_t *input, uint8_t *output,
                                    size_t len)
{
    uint8_t padded_in[39] = {0};
    uint8_t padded_out[39] = {0};
    if (len > 26) abort();
    memcpy(&padded_in[13], input, len);
    ble_whitening(padded_in, padded_out, 13 + len);
    memcpy(output, &padded_out[13], len);
}

static void build_payload26(const lampsmart_profile_t *profile, uint16_t seq,
                            uint8_t command, const uint8_t payload2[2],
                            uint8_t param4, uint8_t rand_lo, uint8_t out26[26])
{
    uint8_t logical[24] = {0};
    uint8_t plain[26] = {0};
    uint8_t reversed[26] = {0};
    logical[0] = (profile->uuid77_device_addr[0] & 0x80u) ? 0xAA : 0x55;
    memcpy(&logical[1], profile->uuid77_device_addr, 5);
    logical[6] = profile->uuid77_device_addr[4];
    logical[7] = profile->uuid77_device_addr[4];
    logical[8] = command;
    logical[9] = profile->uuid77_address2[0];
    logical[10] = profile->uuid77_address2[1];
    logical[11] = payload2[0]; logical[12] = payload2[1];
    logical[13] = param4; logical[14] = (uint8_t)(seq & 0xFFu);
    logical[16] = (uint8_t)(profile->uuid77_param5 ^ rand_lo);
    logical[17] = (uint8_t)(((seq >> 8) & 0xFFu) ^ rand_lo);
    logical[19] = rand_lo;
    uint16_t crc1 = lampsmart_crc16_ccitt_range(logical, 8, 12,
                                                 (uint16_t)~rand_lo);
    logical[20] = (uint8_t)(crc1 >> 8); logical[21] = (uint8_t)crc1;
    uint16_t seed2 = lampsmart_crc16_ccitt_range(logical, 1, 5, 0xFFFFu);
    uint16_t crc2 = lampsmart_crc16_ccitt_range(logical, 8, 14, seed2);
    logical[22] = (uint8_t)(crc2 >> 8); logical[23] = (uint8_t)crc2;
    memcpy(&plain[2], logical, 24);
    for (int i = 0; i < 26; ++i) reversed[i] = bit_reverse_byte(plain[i]);
    whitening_for_rf_packet(reversed, out26, 26);
}

void lampsmart_77f8_build_adv31(const lampsmart_profile_t *profile,
                                uint16_t seq, uint8_t command,
                                const uint8_t payload2[2], uint8_t param4,
                                uint8_t rand_lo,
                                uint8_t out31[LAMPSMART_ADV_LEN])
{
    out31[0] = 0x02; out31[1] = 0x01; out31[2] = 0x01;
    out31[3] = 0x1B; out31[4] = 0x03;
    build_payload26(profile, seq, command, payload2, param4, rand_lo, &out31[5]);
}


