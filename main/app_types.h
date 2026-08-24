#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "aircon_ir.h"
#include "app_config.h"

typedef enum {
    JOB_SOURCE_MQTT = 0,
    JOB_SOURCE_INTERNAL_TIMER,
    JOB_SOURCE_SYSTEM,
} job_source_t;

typedef enum {
    JOB_DEVICE_AIRCON = 0,
    JOB_DEVICE_LIGHT,
    JOB_DEVICE_SWITCH,
    JOB_DEVICE_TEMPERATURE_SENSOR,
    JOB_DEVICE_SYSTEM,
} job_device_t;

typedef enum {
    JOB_CMD_NONE = 0,
    JOB_CMD_AIRCON_POWER,
    JOB_CMD_AIRCON_MODE,
    JOB_CMD_AIRCON_SETTEMP,
    JOB_CMD_AIRCON_FAN,
    JOB_CMD_AIRCON_SWING,
    JOB_CMD_AIRCON_OFFTIMER,
    JOB_CMD_AIRCON_ONTIMER,
    JOB_CMD_AIRCON_ALERT_HIGH,
    JOB_CMD_AIRCON_ALERT_LOW,
    JOB_CMD_LIGHT_POWER,
    JOB_CMD_LIGHT_NIGHT,
    JOB_CMD_LIGHT_BRIGHTNESS,
    JOB_CMD_LIGHT_COLORTEMP,
    JOB_CMD_LIGHT_TIMER,
    JOB_CMD_LIGHT_PAIR,
    JOB_CMD_SWITCH_POWER,
    JOB_CMD_SWITCH_ENABLE,
    JOB_CMD_PUBLISH_SWITCH_STATE,
    JOB_CMD_READ_ROOM_TEMPERATURE,
    JOB_CMD_PUBLISH_AIRCON_STATE,
    JOB_CMD_PUBLISH_INITIAL_STATE,
} job_command_t;

typedef union {
    bool bool_value;
    int32_t int_value;
    uint16_t uint16_value;
    float float_value;
    aircon_mode_t aircon_mode;
    aircon_fan_t aircon_fan;
    aircon_swing_t aircon_swing;
} job_value_t;

typedef struct {
    job_source_t source;
    job_device_t device;
    job_command_t command;
    char sender[APP_SENDER_MAX];
    job_value_t value;
} device_job_t;

typedef struct {
    char topic[APP_MQTT_TOPIC_MAX];
    char payload[APP_MQTT_PAYLOAD_MAX];
    uint8_t qos;
    bool retain;
} publish_request_t;

typedef struct {
    bool power;
    aircon_mode_t mode;
    uint8_t set_temperature;
    aircon_fan_t fan;
    aircon_swing_t swing;
    bool timer_enabled;
    bool timer_is_on;
    int64_t timer_target_us;
    float room_temperature;
    int8_t alert_high_temperature;
    int8_t alert_low_temperature;
    int8_t alert_status;
    bool temperature_sensor_enabled;
} system_aircon_state_t;

typedef struct {
    float brightness;
} light_state_t;

typedef struct {
    bool power;
    bool enable;
    bool valid;
} switch_state_t;


