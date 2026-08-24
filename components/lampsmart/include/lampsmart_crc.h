#pragma once

#include <stddef.h>
#include <stdint.h>

uint16_t lampsmart_crc16_ccitt_update(uint16_t crc, uint8_t b);
uint16_t lampsmart_crc16_ccitt_range(const uint8_t *data, size_t start,
                                     size_t len, uint16_t seed);


