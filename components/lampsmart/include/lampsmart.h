#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lampsmart_profile.h"

typedef enum {
    LAMPSMART_SEND_MODE_COMPAT = 0,
    LAMPSMART_SEND_MODE_F008_ONLY = 1,
} lampsmart_send_mode_t;

typedef struct {
    lampsmart_profile_t profile;
    uint8_t f008_key2;
    uint16_t uuid77_seq;
    uint16_t f008_rand16_default;
    lampsmart_send_mode_t send_mode;
} lampsmart_context_t;

esp_err_t lampsmart_init_from_uuid(lampsmart_context_t *ctx,
                                   const char *uuid_string);
void lampsmart_init_default(lampsmart_context_t *ctx);
void lampsmart_set_send_mode(lampsmart_context_t *ctx,
                             lampsmart_send_mode_t mode);
esp_err_t lampsmart_power_on(lampsmart_context_t *ctx);
esp_err_t lampsmart_power_off(lampsmart_context_t *ctx);
esp_err_t lampsmart_night(lampsmart_context_t *ctx);
esp_err_t lampsmart_brightness(lampsmart_context_t *ctx, float brightness,
                               bool app_style);
esp_err_t lampsmart_color_temp(lampsmart_context_t *ctx, float brightness,
                               float color_temp, bool invert_flag);
esp_err_t lampsmart_sleep_timer(lampsmart_context_t *ctx, uint16_t minutes);
esp_err_t lampsmart_pairing(lampsmart_context_t *ctx);
esp_err_t lampsmart_suspend(void);
esp_err_t lampsmart_resume(void);


