#include "lampsmart_crc.h"

uint16_t lampsmart_crc16_ccitt_update(uint16_t crc, uint8_t b)
{
    crc ^= (uint16_t)b << 8;
    for (int i = 0; i < 8; ++i) {
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                              : (uint16_t)(crc << 1);
    }
    return crc;
}

uint16_t lampsmart_crc16_ccitt_range(const uint8_t *data, size_t start,
                                     size_t len, uint16_t seed)
{
    uint16_t crc = seed;
    for (size_t i = start; i < start + len; ++i) {
        crc = lampsmart_crc16_ccitt_update(crc, data[i]);
    }
    return crc;
}


