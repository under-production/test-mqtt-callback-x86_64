#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* x86_64版ではGPIO/RMTハードウェアが存在しないため、実送信は行わず
 * ログ出力によるシミュレーションのみ行う (aircon_ir.c 参照)。 */
#define AIRCON_IR_CARRIER_HZ         38000

typedef enum {
    AIRCON_MODE_AUTO = 0,
    AIRCON_MODE_DRY,
    AIRCON_MODE_COOL,
    AIRCON_MODE_HEAT,
    AIRCON_MODE_FAN,
} aircon_mode_t;

typedef enum {
    AIRCON_FAN_AUTO = 0,
    AIRCON_FAN_MIN,
    AIRCON_FAN_LOW,
    AIRCON_FAN_MEDIUM,
    AIRCON_FAN_HIGH,
    AIRCON_FAN_MAX,
} aircon_fan_t;

typedef enum {
    AIRCON_SWING_AUTO = 0,
    AIRCON_SWING_HIGHEST,
    AIRCON_SWING_HIGH,
    AIRCON_SWING_MIDDLE,
    AIRCON_SWING_LOW,
    AIRCON_SWING_LOWEST,
} aircon_swing_t;

typedef struct {
    bool power;
    bool timer_is_on;
    aircon_mode_t mode;
    uint8_t temperature_c;
    aircon_fan_t fan;
    aircon_swing_t swing;
    int64_t timer_target_us;
} aircon_state_t;

esp_err_t aircon_ir_init(void);
esp_err_t aircon_ir_send_state(const aircon_state_t *state);
const char *aircon_mode_to_string(aircon_mode_t mode);
const char *aircon_fan_to_string(aircon_fan_t fan);
const char *aircon_swing_to_string(aircon_swing_t swing);
bool aircon_mode_from_string(const char *text, aircon_mode_t *mode);
bool aircon_fan_from_string(const char *text, aircon_fan_t *fan);
bool aircon_swing_from_string(const char *text, aircon_swing_t *swing);


