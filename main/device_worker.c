#include "device_worker.h"

#include <string.h>

#include "app_config.h"
#include "app_resources.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lampsmart.h"
#include "mqtt_protocol.h"
#include "room_temperature.h"
#include "sdkconfig.h"
#include "settings_store.h"
#include "tuya_switch.h"

static const char *TAG = "device_worker";
static const int64_t MINUTE_US = 60LL * 1000000LL;
static system_aircon_state_t s_aircon;
static light_state_t s_light;
static switch_state_t s_switch;
static lampsmart_context_t s_lampsmart;
static unsigned s_temperature_failures;
static unsigned s_switch_failures;
static const tuya_switch_config_t s_tuya_config = {
    .ip = CONFIG_ALEXA_TUYA_IP,
    .port = CONFIG_ALEXA_TUYA_PORT,
    .device_id = CONFIG_ALEXA_TUYA_DEVICE_ID,
    .local_key = CONFIG_ALEXA_TUYA_LOCAL_KEY,
    .timeout_ms = CONFIG_ALEXA_TUYA_TIMEOUT_MS,
};

static void refresh_timer(void)
{
    if (s_aircon.timer_enabled &&
        s_aircon.timer_target_us <= esp_timer_get_time()) {
        s_aircon.timer_enabled = false;
        s_aircon.timer_target_us = 0;
    }
}

static void publish_aircon(void)
{
    publish_request_t request;
    if (mqtt_protocol_make_aircon_state(&s_aircon, &request)) {
        app_enqueue_publish(&request);
    }
}

static void publish_light(void)
{
    publish_request_t request;
    if (mqtt_protocol_make_light_state(&s_light, &request)) {
        app_enqueue_publish(&request);
    }
}

static void publish_switch(void)
{
    publish_request_t request;
    if (mqtt_protocol_make_switch_state(&s_switch, &request)) {
        app_enqueue_publish(&request);
    }
}

static void disable_switch_after_failures(void)
{
    s_switch.enable = false;
    s_switch.power = false;
    s_switch.valid = false;
    esp_err_t ret = settings_store_save_switch_enabled(false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "save switch enable failed: %s", esp_err_to_name(ret));
    }
    ESP_LOGE(TAG, "Tuya switch disabled after %u consecutive failures",
             s_switch_failures);
    publish_switch();
}

static bool record_switch_failure(void)
{
    if (s_switch_failures < 10) s_switch_failures++;
    ESP_LOGW(TAG, "Tuya consecutive failure %u/10", s_switch_failures);
    if (s_switch_failures >= 10) {
        disable_switch_after_failures();
        return true;
    }
    return false;
}

static void record_switch_success(void)
{
    if (s_switch_failures != 0) {
        ESP_LOGI(TAG, "Tuya connection recovered; failure count reset");
    }
    s_switch_failures = 0;
}

static esp_err_t read_switch_with_retry(bool *power)
{
    esp_err_t ret = ESP_FAIL;
    for (int attempt = 1; attempt <= 2; ++attempt) {
        ret = tuya_switch_get_power(&s_tuya_config, power);
        if (ret == ESP_OK) {
            record_switch_success();
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Tuya status attempt %d/2 failed: %s", attempt,
                 esp_err_to_name(ret));
        if (record_switch_failure()) return ret;
        if (attempt == 1) vTaskDelay(pdMS_TO_TICKS(APP_TUYA_RETRY_DELAY_MS));
    }
    return ret;
}

static esp_err_t set_switch_with_retry(bool requested, bool *actual)
{
    esp_err_t ret = ESP_FAIL;
    for (int attempt = 1; attempt <= 2; ++attempt) {
        ret = tuya_switch_set_power_verified(&s_tuya_config, requested, actual);
        if (ret == ESP_OK) {
            record_switch_success();
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Tuya power attempt %d/2 failed: %s", attempt,
                 esp_err_to_name(ret));
        if (record_switch_failure()) return ret;
        if (attempt == 1) vTaskDelay(pdMS_TO_TICKS(APP_TUYA_RETRY_DELAY_MS));
    }
    return ret;
}

static void set_switch_enabled(bool enabled)
{
    bool changed = s_switch.enable != enabled;
    if (changed) {
        esp_err_t ret = settings_store_save_switch_enabled(enabled);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "save switch enable failed: %s",
                     esp_err_to_name(ret));
            return;
        }
    }
    s_switch.enable = enabled;
    s_switch_failures = 0;
    if (!enabled) {
        s_switch.power = false;
        s_switch.valid = false;
    }
    publish_switch();
    if (enabled) {
        bool actual;
        esp_err_t ret = read_switch_with_retry(&actual);
        if (ret == ESP_OK) {
            s_switch.power = actual;
            s_switch.valid = true;
            publish_switch();
        } else {
            ESP_LOGE(TAG, "Tuya switch enable check failed: %s",
                     esp_err_to_name(ret));
        }
    }
}

static void process_switch(const device_job_t *job)
{
    if (job->command == JOB_CMD_SWITCH_ENABLE) {
        set_switch_enabled(job->value.bool_value);
        return;
    }
    if (job->command == JOB_CMD_PUBLISH_SWITCH_STATE && !s_switch.enable) {
        publish_switch();
        return;
    }
    if (!s_switch.enable) {
        s_switch.power = false;
        s_switch.valid = false;
        publish_switch();
        return;
    }
    bool actual = false;
    esp_err_t ret;
    if (job->command == JOB_CMD_PUBLISH_SWITCH_STATE) {
        ret = read_switch_with_retry(&actual);
    } else if (job->command == JOB_CMD_SWITCH_POWER) {
        ret = set_switch_with_retry(job->value.bool_value, &actual);
    } else {
        return;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Tuya switch command failed: %s", esp_err_to_name(ret));
        if (job->command == JOB_CMD_PUBLISH_SWITCH_STATE && s_switch.enable) {
            publish_switch();
        }
        return;
    }
    s_switch.power = actual;
    s_switch.valid = true;
    publish_switch();
}

static int8_t evaluate_alert(float temperature, int8_t previous)
{
    if (temperature >= s_aircon.alert_high_temperature) return 1;
    if (temperature <= s_aircon.alert_low_temperature) return -1;
    if (previous == 1 &&
        temperature > (float)s_aircon.alert_high_temperature - 2.0f) return 1;
    if (previous == -1 &&
        temperature < (float)s_aircon.alert_low_temperature + 2.0f) return -1;
    return 0;
}

static aircon_state_t make_ir_state(const system_aircon_state_t *state)
{
    return (aircon_state_t) {
        .power = state->power,
        .timer_is_on = state->timer_is_on,
        .mode = state->mode,
        .temperature_c = state->set_temperature,
        .fan = state->fan,
        .swing = state->swing,
        .timer_target_us = state->timer_enabled ? state->timer_target_us : 0,
    };
}

static bool is_aircon_ir_command(job_command_t command)
{
    return command >= JOB_CMD_AIRCON_POWER &&
           command <= JOB_CMD_AIRCON_ONTIMER;
}

static void process_aircon(const device_job_t *job)
{
    refresh_timer();
    if (job->command == JOB_CMD_PUBLISH_AIRCON_STATE) {
        publish_aircon();
        return;
    }
    if (job->command == JOB_CMD_AIRCON_ALERT_HIGH ||
        job->command == JOB_CMD_AIRCON_ALERT_LOW) {
        int8_t previous = s_aircon.alert_status;
        esp_err_t ret;
        if (job->command == JOB_CMD_AIRCON_ALERT_HIGH) {
            s_aircon.alert_high_temperature = (int8_t)job->value.int_value;
            ret = settings_store_save_high(s_aircon.alert_high_temperature);
        } else {
            s_aircon.alert_low_temperature = (int8_t)job->value.int_value;
            ret = settings_store_save_low(s_aircon.alert_low_temperature);
        }
        if (ret != ESP_OK) ESP_LOGE(TAG, "save alert setting failed: %s",
                                    esp_err_to_name(ret));
        if (s_aircon.temperature_sensor_enabled) {
            s_aircon.alert_status = evaluate_alert(s_aircon.room_temperature,
                                                   previous);
        }
        if (s_aircon.alert_status != previous) publish_aircon();
        return;
    }
    if (!is_aircon_ir_command(job->command)) return;
    system_aircon_state_t next = s_aircon;
    switch (job->command) {
    case JOB_CMD_AIRCON_POWER: next.power = job->value.bool_value; break;
    case JOB_CMD_AIRCON_MODE: next.mode = job->value.aircon_mode; break;
    case JOB_CMD_AIRCON_SETTEMP:
        next.set_temperature = (uint8_t)job->value.int_value; break;
    case JOB_CMD_AIRCON_FAN: next.fan = job->value.aircon_fan; break;
    case JOB_CMD_AIRCON_SWING: next.swing = job->value.aircon_swing; break;
    case JOB_CMD_AIRCON_OFFTIMER:
    case JOB_CMD_AIRCON_ONTIMER:
        next.timer_is_on = job->command == JOB_CMD_AIRCON_ONTIMER;
        next.timer_enabled = job->value.uint16_value != 0;
        next.timer_target_us = next.timer_enabled
                                   ? esp_timer_get_time() +
                                         (int64_t)job->value.uint16_value * MINUTE_US
                                   : 0;
        break;
    default: return;
    }
    aircon_state_t ir_state = make_ir_state(&next);
    esp_err_t ret = aircon_ir_send_state(&ir_state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "aircon command failed: %s", esp_err_to_name(ret));
        return;
    }
    s_aircon = next;
    publish_aircon();
}

static void process_light(const device_job_t *job)
{
    esp_err_t ret = ESP_ERR_INVALID_ARG;
    switch (job->command) {
    case JOB_CMD_LIGHT_POWER:
        ret = job->value.bool_value ? lampsmart_power_on(&s_lampsmart)
                                    : lampsmart_power_off(&s_lampsmart);
        break;
    case JOB_CMD_LIGHT_NIGHT:
        ret = lampsmart_night(&s_lampsmart);
        break;
    case JOB_CMD_LIGHT_BRIGHTNESS:
        ret = lampsmart_brightness(&s_lampsmart, job->value.float_value, false);
        if (ret == ESP_OK) {
            s_light.brightness = job->value.float_value;
            publish_light();
        }
        break;
    case JOB_CMD_LIGHT_COLORTEMP:
        ret = lampsmart_color_temp(&s_lampsmart, s_light.brightness,
                                    job->value.float_value, true);
        break;
    case JOB_CMD_LIGHT_TIMER:
        ret = lampsmart_sleep_timer(&s_lampsmart, job->value.uint16_value);
        break;
    case JOB_CMD_LIGHT_PAIR:
        ret = lampsmart_pairing(&s_lampsmart);
        break;
    default:
        return;
    }
    if (ret != ESP_OK) ESP_LOGE(TAG, "LampSmart command failed: %s",
                                esp_err_to_name(ret));
}

static bool reinitialize_temperature_sensor(void)
{
    for (int attempt = 1; attempt <= 3; ++attempt) {
        esp_err_t ret = room_temperature_reinitialize();
        if (ret == ESP_OK) {
            s_temperature_failures = 0;
            return true;
        }
        ESP_LOGW(TAG, "DS18B20 reinitialize %d/3 failed: %s", attempt,
                 esp_err_to_name(ret));
        if (attempt < 3) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return false;
}

static void process_temperature(void)
{
    if (!s_aircon.temperature_sensor_enabled) return;
    float temperature;
    esp_err_t ret = room_temperature_read(&temperature);
    if (ret != ESP_OK) {
        s_temperature_failures++;
        ESP_LOGW(TAG, "DS18B20 read failure %u/10: %s",
                 s_temperature_failures, esp_err_to_name(ret));
        if (s_temperature_failures < 10) return;
        if (reinitialize_temperature_sensor()) return;
        s_aircon.temperature_sensor_enabled = false;
        s_aircon.alert_status = 0;
        xEventGroupSetBits(app_event_group(), THERMOMETER_ERROR_BIT);
        room_temperature_deinit();
        publish_aircon();
        return;
    }
    s_temperature_failures = 0;
    s_aircon.room_temperature = temperature;
    int8_t previous = s_aircon.alert_status;
    s_aircon.alert_status = evaluate_alert(temperature, previous);
    if (s_aircon.alert_status != previous) publish_aircon();
}

static void process_job(const device_job_t *job)
{
    refresh_timer();
    switch (job->command) {
    case JOB_CMD_PUBLISH_INITIAL_STATE:
        publish_aircon();
        publish_light();
        process_switch(&(device_job_t) {
            .source = JOB_SOURCE_SYSTEM,
            .device = JOB_DEVICE_SWITCH,
            .command = JOB_CMD_PUBLISH_SWITCH_STATE,
        });
        break;
    case JOB_CMD_READ_ROOM_TEMPERATURE:
        process_temperature();
        break;
    default:
        if (job->device == JOB_DEVICE_AIRCON) process_aircon(job);
        else if (job->device == JOB_DEVICE_LIGHT) process_light(job);
        else if (job->device == JOB_DEVICE_SWITCH) process_switch(job);
        break;
    }
}

static void device_worker_main(void *argument)
{
    (void)argument;
    while (true) {
        device_job_t job;
        if (xQueueReceive(app_device_queue(), &job, pdMS_TO_TICKS(100)) == pdTRUE) {
            process_job(&job);
        }
    }
}

esp_err_t device_worker_initialize(void)
{
    memset(&s_aircon, 0, sizeof(s_aircon));
    s_aircon.mode = AIRCON_MODE_AUTO;
    s_aircon.set_temperature = 22;
    s_aircon.fan = AIRCON_FAN_AUTO;
    s_aircon.swing = AIRCON_SWING_AUTO;
    s_aircon.room_temperature = 22.0f;
    s_aircon.alert_high_temperature = 99;
    s_aircon.alert_low_temperature = -55;
    s_aircon.temperature_sensor_enabled = true;
    s_light.brightness = 1.0f;
    memset(&s_switch, 0, sizeof(s_switch));
    esp_err_t ret =
        settings_store_load_switch_enabled(&s_switch.enable);
    if (ret != ESP_OK) return ret;
    if (s_switch.enable) {
        ret = tuya_switch_validate_config(&s_tuya_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Tuya switch configuration is invalid");
            return ret;
        }
    }
    ret = settings_store_load_alerts(
        &s_aircon.alert_high_temperature, &s_aircon.alert_low_temperature);
    if (ret != ESP_OK) return ret;
    return lampsmart_init_from_uuid(&s_lampsmart,
                                    CONFIG_ALEXA_LAMPSMART_UUID);
}

void device_worker_set_temperature_sensor_enabled(bool enabled)
{
    s_aircon.temperature_sensor_enabled = enabled;
    if (!enabled) {
        s_aircon.alert_status = 0;
        xEventGroupSetBits(app_event_group(), THERMOMETER_ERROR_BIT);
    }
}

esp_err_t device_worker_start(void)
{
    BaseType_t ret = xTaskCreate(device_worker_main, "device_worker_task",
                                 APP_DEVICE_TASK_STACK, NULL, 4, NULL);
    return ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}


