#include "aircon_ir.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "aircon_ir";

enum {
    PANASONIC_STATE_LEN = 27,
    PANASONIC_FIRST_SECTION_LEN = 8,
    PANASONIC_RAW_LEN = 439,
};

static const uint32_t HEADER_MARK_US = 3500;
static const uint32_t HEADER_SPACE_US = 1750;
static const uint32_t BIT_MARK_US = 440;
static const uint32_t ZERO_SPACE_US = 420;
static const uint32_t ONE_SPACE_US = 1300;
static const uint32_t SECTION_GAP_US = 10000;


static const uint8_t s_base_state[PANASONIC_STATE_LEN] = {
    0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06,
    0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x32, 0x80,
    0xAF, 0x00, 0x00, 0x06, 0x60, 0x00, 0x00, 0x80,
    0x00, 0x06, 0x00,
};

static uint8_t mode_bits(aircon_mode_t mode)
{
    switch (mode) {
    case AIRCON_MODE_DRY: return 0x20;
    case AIRCON_MODE_COOL: return 0x30;
    case AIRCON_MODE_HEAT: return 0x40;
    case AIRCON_MODE_FAN: return 0x60;
    case AIRCON_MODE_AUTO:
    default: return 0x00;
    }
}

static uint8_t fan_bits(aircon_fan_t fan)
{
    switch (fan) {
    case AIRCON_FAN_MIN: return 0x30;
    case AIRCON_FAN_LOW: return 0x40;
    case AIRCON_FAN_MEDIUM: return 0x50;
    case AIRCON_FAN_HIGH: return 0x60;
    case AIRCON_FAN_MAX: return 0x70;
    case AIRCON_FAN_AUTO:
    default: return 0xA0;
    }
}

static uint8_t swing_bits(aircon_swing_t swing)
{
    switch (swing) {
    case AIRCON_SWING_HIGHEST: return 0x01;
    case AIRCON_SWING_HIGH: return 0x02;
    case AIRCON_SWING_MIDDLE: return 0x03;
    case AIRCON_SWING_LOW: return 0x04;
    case AIRCON_SWING_LOWEST: return 0x05;
    case AIRCON_SWING_AUTO:
    default: return 0x0F;
    }
}

static uint8_t checksum(const uint8_t state[PANASONIC_STATE_LEN])
{
    uint16_t sum = 0xF4;
    for (size_t i = 0; i < PANASONIC_STATE_LEN - 1; ++i) sum += state[i];
    return (uint8_t)sum;
}

static uint16_t remaining_timer_minutes(const aircon_state_t *settings)
{
    if (settings->timer_target_us == 0) return 0;
    const int64_t remaining_us = settings->timer_target_us - esp_timer_get_time();
    if (remaining_us <= 0) return 0;
    const int64_t minute_us = 60LL * 1000000LL;
    const int64_t minutes = (remaining_us + minute_us - 1) / minute_us;
    return minutes > 720 ? 720 : (uint16_t)minutes;
}

static void set_timer_fields(uint8_t state[PANASONIC_STATE_LEN],
                             bool timer_is_on, uint16_t minutes)
{
    if (minutes == 0) return;
    if (timer_is_on) {
        state[13] |= 0x02;
        state[18] = (uint8_t)(minutes & 0xFFU);
        state[19] = (uint8_t)((state[19] & 0xF0U) | ((minutes >> 8) & 0x0FU));
    } else {
        state[13] |= 0x04;
        state[19] = (uint8_t)((state[19] & 0x0FU) | ((minutes & 0x0FU) << 4));
        state[20] = (uint8_t)((state[20] & 0x80U) | ((minutes >> 4) & 0x7FU));
    }
}

static uint16_t build_state(const aircon_state_t *settings,
                            uint8_t state[PANASONIC_STATE_LEN])
{
    memcpy(state, s_base_state, PANASONIC_STATE_LEN);
    state[13] = mode_bits(settings->mode) | (settings->power ? 0x01 : 0x00);
    state[14] = (uint8_t)(settings->temperature_c * 2U);
    state[16] = fan_bits(settings->fan) | swing_bits(settings->swing);
    uint16_t remaining = remaining_timer_minutes(settings);
    set_timer_fields(state, settings->timer_is_on, remaining);
    state[26] = checksum(state);
    return remaining;
}

static size_t append_section(const uint8_t *bytes, size_t count, bool append_gap,
                             uint16_t raw[PANASONIC_RAW_LEN], size_t index)
{
    raw[index++] = HEADER_MARK_US;
    raw[index++] = HEADER_SPACE_US;
    for (size_t i = 0; i < count; ++i) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            raw[index++] = BIT_MARK_US;
            raw[index++] = ((bytes[i] >> bit) & 1U) ? ONE_SPACE_US : ZERO_SPACE_US;
        }
    }
    raw[index++] = BIT_MARK_US;
    if (append_gap) raw[index++] = SECTION_GAP_US;
    return index;
}

static size_t state_to_raw(const uint8_t state[PANASONIC_STATE_LEN],
                           uint16_t raw[PANASONIC_RAW_LEN])
{
    size_t index = append_section(state, PANASONIC_FIRST_SECTION_LEN, true, raw, 0);
    return append_section(state + PANASONIC_FIRST_SECTION_LEN,
                          PANASONIC_STATE_LEN - PANASONIC_FIRST_SECTION_LEN,
                          false, raw, index);
}

static esp_err_t send_raw(const uint16_t *raw_us, size_t raw_len)
{
    /* x86_64版: 実際のRMT/GPIO赤外線送信ハードウェアが存在しないため、
     * 送信タイミング列をログに要約出力するシミュレーションのみ行う。
     * MQTT経由のコマンド〜状態パブリッシュのフローはこれで検証できる。 */
    ESP_RETURN_ON_FALSE(raw_us && raw_len, ESP_ERR_INVALID_ARG, TAG, "invalid raw signal");
    ESP_LOGI(TAG, "[SIMULATED IR TX] %u timing symbols (no physical GPIO on this platform)",
             (unsigned)raw_len);
    return ESP_OK;
}

esp_err_t aircon_ir_init(void)
{
    ESP_LOGI(TAG,
             "IR TX simulated (物理的なGPIO/RMTハードウェアはこのプラットフォームには"
             "存在しません。MQTT検証用にロジックのみ再現しています)");
    return ESP_OK;
}

esp_err_t aircon_ir_send_state(const aircon_state_t *settings)
{
    ESP_RETURN_ON_FALSE(settings, ESP_ERR_INVALID_ARG, TAG, "settings is NULL");
    ESP_RETURN_ON_FALSE(settings->temperature_c >= 16 && settings->temperature_c <= 30,
                        ESP_ERR_INVALID_ARG, TAG, "temperature out of range");
    ESP_RETURN_ON_FALSE(settings->mode <= AIRCON_MODE_FAN &&
                        settings->fan <= AIRCON_FAN_MAX &&
                        settings->swing <= AIRCON_SWING_LOWEST,
                        ESP_ERR_INVALID_ARG, TAG, "enum value out of range");
    uint8_t state[PANASONIC_STATE_LEN];
    uint16_t raw[PANASONIC_RAW_LEN];
    uint16_t timer = build_state(settings, state);
    size_t raw_len = state_to_raw(state, raw);
    ESP_LOGI(TAG, "TX %s %s %uC fan=%s swing=%s timer=%s/%umin checksum=0x%02X raw=%u",
             settings->power ? "on" : "off", aircon_mode_to_string(settings->mode),
             settings->temperature_c, aircon_fan_to_string(settings->fan),
             aircon_swing_to_string(settings->swing),
             timer ? (settings->timer_is_on ? "on" : "off") : "none",
             timer, state[26], (unsigned)raw_len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, state, sizeof(state), ESP_LOG_INFO);
    return send_raw(raw, raw_len);
}

const char *aircon_mode_to_string(aircon_mode_t mode)
{
    static const char *names[] = {"auto", "dry", "cool", "heat", "fan"};
    return (unsigned)mode < sizeof(names) / sizeof(names[0]) ? names[mode] : "unknown";
}

const char *aircon_fan_to_string(aircon_fan_t fan)
{
    static const char *names[] = {"auto", "min", "low", "medium", "high", "max"};
    return (unsigned)fan < sizeof(names) / sizeof(names[0]) ? names[fan] : "unknown";
}

const char *aircon_swing_to_string(aircon_swing_t swing)
{
    static const char *names[] = {"auto", "highest", "high", "middle", "low", "lowest"};
    return (unsigned)swing < sizeof(names) / sizeof(names[0]) ? names[swing] : "unknown";
}

static bool lookup(const char *text, const char *const *names, size_t count, int *value)
{
    if (!text || !value) return false;
    for (size_t i = 0; i < count; ++i) {
        if (strcasecmp(text, names[i]) == 0) {
            *value = (int)i;
            return true;
        }
    }
    return false;
}

bool aircon_mode_from_string(const char *text, aircon_mode_t *mode)
{
    static const char *names[] = {"auto", "dry", "cool", "heat", "fan"};
    int value;
    if (!lookup(text, names, sizeof(names) / sizeof(names[0]), &value)) return false;
    *mode = (aircon_mode_t)value;
    return true;
}

bool aircon_fan_from_string(const char *text, aircon_fan_t *fan)
{
    static const char *names[] = {"auto", "min", "low", "medium", "high", "max"};
    int value;
    if (!lookup(text, names, sizeof(names) / sizeof(names[0]), &value)) return false;
    *fan = (aircon_fan_t)value;
    return true;
}

bool aircon_swing_from_string(const char *text, aircon_swing_t *swing)
{
    static const char *names[] = {"auto", "highest", "high", "middle", "low", "lowest"};
    int value;
    if (!lookup(text, names, sizeof(names) / sizeof(names[0]), &value)) return false;
    *swing = (aircon_swing_t)value;
    return true;
}


