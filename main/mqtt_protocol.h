#pragma once

#include <stdbool.h>

#include "app_types.h"

typedef enum {
    MQTT_PARSE_NOT_FOR_US = 0,
    MQTT_PARSE_VALID,
    MQTT_PARSE_INVALID,
} mqtt_parse_result_t;

mqtt_parse_result_t mqtt_protocol_parse(const char *topic, const char *payload,
                                        device_job_t *job);
bool mqtt_protocol_make_result(const char *received_topic, const char *result,
                               const char *error,
                               publish_request_t *request);
bool mqtt_protocol_make_aircon_state(system_aircon_state_t *state,
                                     publish_request_t *request);
bool mqtt_protocol_make_light_state(const light_state_t *state,
                                    publish_request_t *request);
bool mqtt_protocol_make_switch_state(const switch_state_t *state,
                                     publish_request_t *request);

