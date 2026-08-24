#include "mqtt_protocol.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_timer.h"

#define TOPIC_DELIMITER "/<-/"

static bool is_our_prefix(const char *topic)
{
    size_t length = strlen(APP_MY_ID);
    return strncmp(topic, APP_MY_ID, length) == 0 &&
           (topic[length] == '/' || topic[length] == '\0');
}

static bool split_topic(const char *topic, char *receiver, size_t receiver_size,
                        char *sender, size_t sender_size)
{
    const char *delimiter = strstr(topic, TOPIC_DELIMITER);
    if (!delimiter || strstr(delimiter + strlen(TOPIC_DELIMITER),
                             TOPIC_DELIMITER)) return false;
    size_t receiver_len = (size_t)(delimiter - topic);
    const char *sender_start = delimiter + strlen(TOPIC_DELIMITER);
    size_t sender_len = strlen(sender_start);
    if (!receiver_len || !sender_len || receiver_len >= receiver_size ||
        sender_len >= sender_size) return false;
    memcpy(receiver, topic, receiver_len);
    receiver[receiver_len] = '\0';
    memcpy(sender, sender_start, sender_len + 1);
    return true;
}

static bool integer_value(const cJSON *item, int minimum, int maximum,
                          int *value)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    double number = item->valuedouble;
    int integer = (int)number;
    if (number != (double)integer || integer < minimum || integer > maximum) {
        return false;
    }
    *value = integer;
    return true;
}

static bool parse_aircon(const cJSON *item, device_job_t *job)
{
    const char *key = item->string;
    if (!key) return false;
    if (strcmp(key, "power") == 0 && cJSON_IsString(item)) {
        if (strcasecmp(item->valuestring, "on") == 0) job->value.bool_value = true;
        else if (strcasecmp(item->valuestring, "off") == 0) job->value.bool_value = false;
        else return false;
        job->command = JOB_CMD_AIRCON_POWER;
    } else if (strcmp(key, "mode") == 0 && cJSON_IsString(item)) {
        if (!aircon_mode_from_string(item->valuestring, &job->value.aircon_mode)) return false;
        job->command = JOB_CMD_AIRCON_MODE;
    } else if (strcmp(key, "settemp") == 0) {
        int value;
        if (!integer_value(item, 16, 30, &value)) return false;
        job->value.int_value = value;
        job->command = JOB_CMD_AIRCON_SETTEMP;
    } else if (strcmp(key, "fan") == 0 && cJSON_IsString(item)) {
        if (!aircon_fan_from_string(item->valuestring, &job->value.aircon_fan)) return false;
        job->command = JOB_CMD_AIRCON_FAN;
    } else if (strcmp(key, "swing") == 0 && cJSON_IsString(item)) {
        if (!aircon_swing_from_string(item->valuestring, &job->value.aircon_swing)) return false;
        job->command = JOB_CMD_AIRCON_SWING;
    } else if (strcmp(key, "offtimer") == 0 || strcmp(key, "ontimer") == 0) {
        int value;
        if (!integer_value(item, 0, 720, &value)) return false;
        job->value.uint16_value = (uint16_t)value;
        job->command = strcmp(key, "offtimer") == 0
                           ? JOB_CMD_AIRCON_OFFTIMER : JOB_CMD_AIRCON_ONTIMER;
    } else if (strcmp(key, "get") == 0 && cJSON_IsString(item) &&
               strcmp(item->valuestring, "temperature") == 0) {
        job->command = JOB_CMD_PUBLISH_AIRCON_STATE;
    } else if (strcmp(key, "alert+") == 0 || strcmp(key, "alert-") == 0) {
        int value;
        if (!integer_value(item, -55, 125, &value)) return false;
        job->value.int_value = value;
        job->command = strcmp(key, "alert+") == 0
                           ? JOB_CMD_AIRCON_ALERT_HIGH : JOB_CMD_AIRCON_ALERT_LOW;
    } else {
        return false;
    }
    return true;
}

static bool parse_light(const cJSON *item, device_job_t *job)
{
    const char *key = item->string;
    if (!key) return false;
    if (strcmp(key, "power") == 0 && cJSON_IsString(item)) {
        if (strcasecmp(item->valuestring, "on") == 0) job->value.bool_value = true;
        else if (strcasecmp(item->valuestring, "off") == 0) job->value.bool_value = false;
        else return false;
        job->command = JOB_CMD_LIGHT_POWER;
    } else if (strcmp(key, "night") == 0 && cJSON_IsString(item) &&
               strcmp(item->valuestring, "toggle") == 0) {
        job->command = JOB_CMD_LIGHT_NIGHT;
    } else if (strcmp(key, "bright") == 0 && cJSON_IsNumber(item) &&
               isfinite(item->valuedouble) && item->valuedouble >= 0.0 &&
               item->valuedouble <= 1.0) {
        job->value.float_value = (float)item->valuedouble;
        job->command = JOB_CMD_LIGHT_BRIGHTNESS;
    } else if (strcmp(key, "colortmp") == 0 && cJSON_IsNumber(item) &&
               isfinite(item->valuedouble) && item->valuedouble >= 0.0 &&
               item->valuedouble <= 1.0) {
        job->value.float_value = (float)item->valuedouble;
        job->command = JOB_CMD_LIGHT_COLORTEMP;
    } else if (strcmp(key, "timer") == 0) {
        int value;
        if (!integer_value(item, 1, 720, &value)) return false;
        job->value.uint16_value = (uint16_t)value;
        job->command = JOB_CMD_LIGHT_TIMER;
    } else if (strcmp(key, "cmd") == 0 && cJSON_IsString(item) &&
               strcmp(item->valuestring, "pair") == 0) {
        job->command = JOB_CMD_LIGHT_PAIR;
    } else {
        return false;
    }
    return true;
}

static bool parse_switch(const cJSON *item, device_job_t *job)
{
    if (!item->string || !cJSON_IsString(item)) return false;
    if (strcmp(item->string, "power") == 0) {
        if (strcasecmp(item->valuestring, "on") == 0) {
            job->value.bool_value = true;
        } else if (strcasecmp(item->valuestring, "off") == 0) {
            job->value.bool_value = false;
        } else {
            return false;
        }
        job->command = JOB_CMD_SWITCH_POWER;
    } else if (strcmp(item->string, "enable") == 0) {
        if (strcasecmp(item->valuestring, "true") == 0) {
            job->value.bool_value = true;
        } else if (strcasecmp(item->valuestring, "false") == 0) {
            job->value.bool_value = false;
        } else {
            return false;
        }
        job->command = JOB_CMD_SWITCH_ENABLE;
    } else {
        return false;
    }
    return true;
}

mqtt_parse_result_t mqtt_protocol_parse(const char *topic, const char *payload,
                                        device_job_t *job)
{
    if (!topic || !payload || !job) return MQTT_PARSE_INVALID;
    if (!is_our_prefix(topic)) return MQTT_PARSE_NOT_FOR_US;
    char receiver[APP_MQTT_TOPIC_MAX];
    char sender[APP_SENDER_MAX];
    if (!split_topic(topic, receiver, sizeof(receiver), sender, sizeof(sender))) {
        return MQTT_PARSE_INVALID;
    }
    char expected_aircon[64];
    char expected_light[64];
    char expected_switch[64];
    snprintf(expected_aircon, sizeof(expected_aircon), "%s/%s", APP_MY_ID,
             APP_AIRCON_ID);
    snprintf(expected_light, sizeof(expected_light), "%s/%s", APP_MY_ID,
             APP_LIGHT_ID);
    snprintf(expected_switch, sizeof(expected_switch), "%s/%s", APP_MY_ID,
             APP_SWITCH_ID);
    bool aircon = strcmp(receiver, expected_aircon) == 0;
    bool light = strcmp(receiver, expected_light) == 0;
    bool switch_device = strcmp(receiver, expected_switch) == 0;
    if (!aircon && !light && !switch_device) return MQTT_PARSE_INVALID;

    cJSON *root = cJSON_Parse(payload);
    if (!root || !cJSON_IsObject(root) || !root->child || root->child->next) {
        cJSON_Delete(root);
        return MQTT_PARSE_INVALID;
    }
    memset(job, 0, sizeof(*job));
    job->source = JOB_SOURCE_MQTT;
    job->device = aircon ? JOB_DEVICE_AIRCON
                         : light ? JOB_DEVICE_LIGHT : JOB_DEVICE_SWITCH;
    strlcpy(job->sender, sender, sizeof(job->sender));
    bool valid = aircon ? parse_aircon(root->child, job)
                        : light ? parse_light(root->child, job)
                                : parse_switch(root->child, job);
    cJSON_Delete(root);
    return valid ? MQTT_PARSE_VALID : MQTT_PARSE_INVALID;
}

static bool fill_request(publish_request_t *request, const char *topic,
                         const char *payload)
{
    if (!request || !topic || !payload || strlen(topic) >= sizeof(request->topic) ||
        strlen(payload) >= sizeof(request->payload)) return false;
    memset(request, 0, sizeof(*request));
    strlcpy(request->topic, topic, sizeof(request->topic));
    strlcpy(request->payload, payload, sizeof(request->payload));
    request->qos = 1;
    request->retain = false;
    return true;
}

bool mqtt_protocol_make_result(const char *received_topic, const char *result,
                               const char *error,
                               publish_request_t *request)
{
    char receiver[APP_MQTT_TOPIC_MAX];
    char sender[APP_SENDER_MAX];
    char response_topic[APP_MQTT_TOPIC_MAX];
    int response_length;

    if (split_topic(received_topic, receiver, sizeof(receiver), sender,
                    sizeof(sender))) {
        response_length = snprintf(response_topic, sizeof(response_topic),
                                   "%s%s%s", sender, TOPIC_DELIMITER,
                                   receiver);
    } else {
        response_length = snprintf(response_topic, sizeof(response_topic),
                                   "%s%s%s/system", APP_AWS_SENDER,
                                   TOPIC_DELIMITER, APP_MY_ID);
    }

    if (response_length < 0 ||
        (size_t)response_length >= sizeof(response_topic)) {
        return false;
    }

    char payload[160];
    int payload_length = snprintf(
        payload, sizeof(payload),
        "{\"result\":\"%s\",\"error\":\"%s\"}", result, error);

    if (payload_length < 0 ||
        (size_t)payload_length >= sizeof(payload)) {
        return false;
    }

    return fill_request(request, response_topic, payload);
}

bool mqtt_protocol_make_aircon_state(system_aircon_state_t *state,
                                     publish_request_t *request)
{
    int remaining = 0;
    if (state->timer_enabled && state->timer_target_us > 0) {
        int64_t us = state->timer_target_us - esp_timer_get_time();
        if (us > 0) remaining = (int)((us + 60000000LL - 1) / 60000000LL);
        else {
            state->timer_enabled = false;
            state->timer_target_us = 0;
        }
    }
    char topic[APP_MQTT_TOPIC_MAX];
    snprintf(topic, sizeof(topic), "%s%s%s/%s", APP_AWS_SENDER,
             TOPIC_DELIMITER, APP_MY_ID, APP_AIRCON_ID);
    char payload[APP_MQTT_PAYLOAD_MAX];
    int length = snprintf(
        payload, sizeof(payload),
        "{\"power\":\"%s\",\"mode\":\"%s\",\"settemp\":%u,"
        "\"temperature\":%.2f,\"fan\":\"%s\",\"swing\":\"%s\","
        "\"timer\":{\"enabled\":%s,\"type\":\"%s\","
        "\"remaining_minutes\":%d},\"alert\":%d,\"sensor\":\"%s\"}",
        state->power ? "on" : "off", aircon_mode_to_string(state->mode),
        state->set_temperature, state->room_temperature,
        aircon_fan_to_string(state->fan), aircon_swing_to_string(state->swing),
        state->timer_enabled ? "true" : "false",
        state->timer_is_on ? "on" : "off", remaining, state->alert_status,
        state->temperature_sensor_enabled ? "ok" : "disabled");
    return length > 0 && length < (int)sizeof(payload) &&
           fill_request(request, topic, payload);
}

bool mqtt_protocol_make_light_state(const light_state_t *state,
                                    publish_request_t *request)
{
    char topic[APP_MQTT_TOPIC_MAX];
    char payload[64];
    snprintf(topic, sizeof(topic), "%s%s%s/%s", APP_AWS_SENDER,
             TOPIC_DELIMITER, APP_MY_ID, APP_LIGHT_ID);
    snprintf(payload, sizeof(payload), "{\"bright\":%.3f}", state->brightness);
    return fill_request(request, topic, payload);
}

bool mqtt_protocol_make_switch_state(const switch_state_t *state,
                                     publish_request_t *request)
{
    if (!state) return false;
    char topic[APP_MQTT_TOPIC_MAX];
    char payload[64];
    snprintf(topic, sizeof(topic), "%s%s%s/%s", APP_AWS_SENDER,
             TOPIC_DELIMITER, APP_MY_ID, APP_SWITCH_ID);
    snprintf(payload, sizeof(payload),
             "{\"power\":\"%s\",\"enable\":\"%s\"}",
             state->power ? "on" : "off",
             state->enable ? "true" : "false");
    return fill_request(request, topic, payload);
}


