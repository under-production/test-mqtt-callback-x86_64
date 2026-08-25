/* main/status_led.c の x86_64 Linux移植版。
 *
 * オリジナルは物理LED (GPIO) を点滅させて状態を可視化するが、Linux実機には
 * GPIOが無いため、状態が変化したときにログへ出力する方式に置き換えた。
 * 状態遷移のロジック (どのビットでどのパターンにするか) はオリジナルの
 * status_led_tick() のステートマシンをできる限りそのまま踏襲している。
 * 呼び出し側 (system_loop.c, app_main.c) は無変更で動作する。
 */
#include "status_led.h"

#include <stdbool.h>

#include "app_config.h"
#include "app_resources.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "status_led";

typedef enum {
    LED_CONDITION_NONE = 0,
    LED_CONDITION_WIFI,
    LED_CONDITION_MQTT,
    LED_CONDITION_THERMOMETER,
} led_condition_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_publish_requested;
static unsigned s_publish_ticks;
static led_condition_t s_condition;
static unsigned s_condition_phase;
static bool s_led_state;
static bool s_led_state_valid;

static void set_led(bool on)
{
    if (s_led_state_valid && s_led_state == on) return;
    s_led_state = on;
    s_led_state_valid = true;
    ESP_LOGI(TAG, "status LED -> %s", on ? "ON" : "off");
}

esp_err_t status_led_init(void)
{
    s_led_state_valid = false;
    set_led(false);
    return ESP_OK;
}

void status_led_show_startup(void) { set_led(true); }

void status_led_notify_publish(void)
{
    portENTER_CRITICAL(&s_lock);
    s_publish_requested = true;
    portEXIT_CRITICAL(&s_lock);
}

void status_led_tick(EventBits_t bits)
{
    if (!(bits & INITIAL_COMPLETE_BIT)) {
        set_led(true);
        return;
    }

    portENTER_CRITICAL(&s_lock);
    bool publish_requested = s_publish_requested;
    s_publish_requested = false;
    portEXIT_CRITICAL(&s_lock);
    if (s_publish_ticks == 0 && publish_requested) s_publish_ticks = 4;
    if (s_publish_ticks > 0) {
        set_led((s_publish_ticks & 1U) == 0);
        s_publish_ticks--;
        return;
    }

    led_condition_t condition = LED_CONDITION_NONE;
    unsigned half_period_ticks = 0;
    if (bits & WIFI_ERROR_BIT) {
        condition = LED_CONDITION_WIFI;
        half_period_ticks = 1;
    } else if (bits & MQTT_ERROR_BIT) {
        //ESP_LOGI(TAG, "status bits -> %b", bits);
        condition = LED_CONDITION_MQTT;
        half_period_ticks = 3;
    } else if (bits & THERMOMETER_ERROR_BIT) {
        condition = LED_CONDITION_THERMOMETER;
        half_period_ticks = 6;
    }
    if (condition == LED_CONDITION_NONE) {
        s_condition = LED_CONDITION_NONE;
        s_condition_phase = 0;
        set_led(false);
        return;
    }
    if (condition != s_condition) {
        s_condition = condition;
        s_condition_phase = 0;
    }
    set_led(s_condition_phase < half_period_ticks);
    s_condition_phase =
        (s_condition_phase + 1) % (half_period_ticks * 2U);
}
