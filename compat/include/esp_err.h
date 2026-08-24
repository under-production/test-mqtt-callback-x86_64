/* ESP-IDF esp_err.h compatibility shim for x86_64 Linux port.
 * ESP32/ESP-IDF固有の型・マクロをLinux上で再現するための互換ヘッダ。
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>

typedef int esp_err_t;

#define ESP_OK                    0
#define ESP_FAIL                  -1
#define ESP_ERR_NO_MEM            0x101
#define ESP_ERR_INVALID_ARG       0x102
#define ESP_ERR_INVALID_STATE     0x103
#define ESP_ERR_INVALID_SIZE      0x104
#define ESP_ERR_NOT_FOUND         0x105
#define ESP_ERR_NOT_SUPPORTED     0x106
#define ESP_ERR_TIMEOUT           0x107
#define ESP_ERR_INVALID_RESPONSE  0x108
#define ESP_ERR_INVALID_CRC       0x109
#define ESP_ERR_INVALID_VERSION   0x10A
#define ESP_ERR_INVALID_MAC       0x10B
#define ESP_ERR_NOT_FINISHED      0x10C
#define ESP_ERR_NOT_ALLOWED       0x10D

#define ESP_ERR_NVS_NOT_INITIALIZED    0x1101
#define ESP_ERR_NVS_NOT_FOUND          0x1102
#define ESP_ERR_NVS_NO_FREE_PAGES      0x1103
#define ESP_ERR_NVS_NEW_VERSION_FOUND  0x1104

const char *esp_err_to_name(esp_err_t code);

#define ESP_ERROR_CHECK(x)                                                   \
    do {                                                                     \
        esp_err_t __err_rc = (x);                                            \
        if (__err_rc != ESP_OK) {                                            \
            fprintf(stderr, "ESP_ERROR_CHECK failed: %s at %s:%d (%s)\n",    \
                    esp_err_to_name(__err_rc), __FILE__, __LINE__, #x);       \
            abort();                                                         \
        }                                                                    \
    } while (0)
