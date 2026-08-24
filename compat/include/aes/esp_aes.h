/* ESP-IDF aes/esp_aes.h compatibility shim.
 * ESP32のAESアクセラレータAPIを、Linux上ではmbedTLSのソフトウェアAES実装で
 * そのまま代替する。呼び出し側 (lampsmart_f008.c) は無変更で動作する。
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include <mbedtls/aes.h>

#define ESP_AES_ENCRYPT 1
#define ESP_AES_DECRYPT 0

typedef struct {
    mbedtls_aes_context ctx;
} esp_aes_context;

void esp_aes_init(esp_aes_context *ctx);
void esp_aes_free(esp_aes_context *ctx);
int esp_aes_setkey(esp_aes_context *ctx, const unsigned char *key,
                    unsigned int keybits);
int esp_aes_crypt_ecb(esp_aes_context *ctx, int mode,
                       const unsigned char input[16],
                       unsigned char output[16]);
