#include "lampsmart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "lampsmart_77f8.h"
#include "lampsmart_defs.h"
#include "lampsmart_f008.h"
#include "lampsmart_sender.h"

static const char *TAG = "lampsmart";

#define CMD_POWER_ON   0x10u
#define CMD_POWER_OFF  0x11u
#define CMD_NIGHT      0x23u
#define CMD_DIM_COLOR  0x21u
#define CMD_TIMER      0x41u
#define CMD_PAIRING    0x28u
#define REMOTE_BRIGHTNESS_MIN 0.015f

static float clamp01(float value)
{
    if (isnan(value) || value < 0.0f) return 0.0f;
    return value > 1.0f ? 1.0f : value;
}

static uint8_t channel_from_float(float value)
{
    int channel = (int)(255.0f * clamp01(value));
    if (channel < 0) channel = 0;
    if (channel > 255) channel = 255;
    return (uint8_t)channel;
}

static uint16_t next_rand16(const lampsmart_context_t *ctx)
{
    uint16_t value = (uint16_t)(esp_random() & 0xFFFFu);
    return value ? value : ctx->f008_rand16_default;
}

static uint8_t next_rand8(void)
{
    uint8_t value = (uint8_t)(esp_random() & 0xFFu);
    return value ? value : 0x53u;
}

static void log_hex31(const char *label, const uint8_t adv[LAMPSMART_ADV_LEN])
{
    char line[3 * LAMPSMART_ADV_LEN + 1];
    char *cursor = line;
    for (int i = 0; i < LAMPSMART_ADV_LEN; ++i) {
        int count = snprintf(cursor, sizeof(line) - (size_t)(cursor - line),
                             "%02X%s", adv[i],
                             i == LAMPSMART_ADV_LEN - 1 ? "" : " ");
        if (count <= 0) break;
        cursor += count;
    }
    ESP_LOGI(TAG, "%s: %s", label, line);
}

static esp_err_t build_and_send(lampsmart_context_t *ctx, uint16_t f008_command,
                                const uint8_t f008_payload4[4],
                                uint8_t uuid77_command,
                                const uint8_t uuid77_payload2[2],
                                uint8_t uuid77_param4)
{
    if (!ctx || !f008_payload4 || !uuid77_payload2) return ESP_ERR_INVALID_ARG;
    uint8_t f008[LAMPSMART_ADV_LEN];
    uint8_t adv77[LAMPSMART_ADV_LEN];
    esp_err_t err = lampsmart_f008_build_adv31(
        &ctx->profile, ctx->f008_key2++, f008_command, f008_payload4,
        next_rand16(ctx), f008);
    if (err != ESP_OK) return err;
    lampsmart_77f8_build_adv31(&ctx->profile, ctx->uuid77_seq++,
                               uuid77_command, uuid77_payload2,
                               uuid77_param4, next_rand8(), adv77);
    log_hex31("F008", f008);
    if (ctx->send_mode == LAMPSMART_SEND_MODE_COMPAT) log_hex31("77F8", adv77);
    /*
     * 常夜灯は状態指定ではなくトグル命令なので、複数回送信すると
     * 後続受信で元の状態へ戻る可能性がある。常夜灯だけ1周にする。
     */
    const int repeat_count = (f008_command == CMD_NIGHT) ? 1 : 4;
    for (int i = 0; i < repeat_count; ++i) {
        if ((err = lampsmart_sender_advertise_once(f008, 300)) != ESP_OK) return err;
        if (ctx->send_mode == LAMPSMART_SEND_MODE_COMPAT &&
            (err = lampsmart_sender_advertise_once(adv77, 300)) != ESP_OK) return err;
    }
    return lampsmart_sender_advertise_flags_only(0x02, 500);
}

esp_err_t lampsmart_init_from_uuid(lampsmart_context_t *ctx,
                                   const char *uuid_string)
{
    if (!ctx) return ESP_ERR_INVALID_ARG;
    memset(ctx, 0, sizeof(*ctx));
    esp_err_t err = lampsmart_profile_from_uuid(&ctx->profile, uuid_string);
    if (err != ESP_OK) return err;
    ctx->f008_key2 = 0x20;
    ctx->uuid77_seq = 0x0001;
    ctx->f008_rand16_default = LAMPSMART_F008_RAND16_DEFAULT;
    ctx->send_mode = LAMPSMART_SEND_MODE_COMPAT;
    return lampsmart_sender_init();
}

void lampsmart_init_default(lampsmart_context_t *ctx)
{
    (void)lampsmart_init_from_uuid(ctx,
                                  "00000000-9317-5b6d-0000-12c000000000");
}

void lampsmart_set_send_mode(lampsmart_context_t *ctx,
                             lampsmart_send_mode_t mode)
{
    if (ctx) ctx->send_mode = mode;
}

esp_err_t lampsmart_power_on(lampsmart_context_t *ctx)
{
    const uint8_t p4[4] = {0}; const uint8_t p2[2] = {0};
    return build_and_send(ctx, CMD_POWER_ON, p4, CMD_POWER_ON, p2,
                          LAMPSMART_77F8_PARAM4_NORMAL);
}

esp_err_t lampsmart_power_off(lampsmart_context_t *ctx)
{
    const uint8_t p4[4] = {0}; const uint8_t p2[2] = {0};
    return build_and_send(ctx, CMD_POWER_OFF, p4, CMD_POWER_OFF, p2,
                          LAMPSMART_77F8_PARAM4_NORMAL);
}

esp_err_t lampsmart_night(lampsmart_context_t *ctx)
{
    const uint8_t p4[4] = {0}; const uint8_t p2[2] = {0};
    return build_and_send(ctx, CMD_NIGHT, p4, CMD_NIGHT, p2,
                          LAMPSMART_77F8_PARAM4_NORMAL);
}

esp_err_t lampsmart_brightness(lampsmart_context_t *ctx, float brightness,
                               bool app_style)
{
    brightness = clamp01(brightness);
    float scale = app_style ? brightness * 0.9f + 0.1f : brightness;
    if (!app_style && scale < REMOTE_BRIGHTNESS_MIN) scale = REMOTE_BRIGHTNESS_MIN;
    uint8_t channel_a = channel_from_float(scale);
    const uint8_t p4[4] = {0, 0, channel_a, 0};
    const uint8_t p2[2] = {channel_a, 0};
    return build_and_send(ctx, CMD_DIM_COLOR, p4, CMD_DIM_COLOR, p2,
                          LAMPSMART_77F8_PARAM4_NORMAL);
}

esp_err_t lampsmart_color_temp(lampsmart_context_t *ctx, float brightness,
                               float color_temp, bool invert_flag)
{
    brightness = clamp01(brightness);
    color_temp = clamp01(color_temp);
    if (invert_flag) color_temp = 1.0f - color_temp;
    float scale = brightness * 0.9f + 0.1f;
    int position = (int)(color_temp * 100.0f);
    uint8_t channel_a, channel_b;
    if (position > 50) {
        channel_a = channel_from_float(scale);
        channel_b = channel_from_float(
            (1.0f - (((float)position - 50.0f) / 50.0f)) * scale);
    } else {
        channel_a = channel_from_float(((float)position / 50.0f) * scale);
        channel_b = channel_from_float(scale);
    }
    const uint8_t p4[4] = {0, 0, channel_a, channel_b};
    const uint8_t p2[2] = {channel_a, channel_b};
    return build_and_send(ctx, CMD_DIM_COLOR, p4, CMD_DIM_COLOR, p2,
                          LAMPSMART_77F8_PARAM4_NORMAL);
}

esp_err_t lampsmart_sleep_timer(lampsmart_context_t *ctx, uint16_t minutes)
{
    const uint8_t p4[4] = {0, (uint8_t)minutes,
                           (uint8_t)(minutes >> 8), 0};
    const uint8_t p2[2] = {(uint8_t)minutes, 0};
    return build_and_send(ctx, CMD_TIMER, p4, CMD_TIMER, p2,
                          LAMPSMART_77F8_PARAM4_NORMAL);
}

esp_err_t lampsmart_pairing(lampsmart_context_t *ctx)
{
    if (!ctx) return ESP_ERR_INVALID_ARG;
    const uint8_t p4[4] = {0};
    const uint8_t p2[2] = {ctx->profile.uuid77_address2[0],
                           ctx->profile.uuid77_address2[1]};
    return build_and_send(ctx, CMD_PAIRING, p4, CMD_PAIRING, p2,
                          LAMPSMART_77F8_PARAM4_PAIRING);
}

esp_err_t lampsmart_suspend(void) { return lampsmart_sender_suspend(); }
esp_err_t lampsmart_resume(void) { return lampsmart_sender_resume(); }

