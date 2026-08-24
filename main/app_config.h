#pragma once

#include "sdkconfig.h"

#define APP_MY_ID                      "ESP32-1"
#define APP_AIRCON_ID                  "aircon-1"
#define APP_LIGHT_ID                   "light-1"
#define APP_SWITCH_ID                  "switch-1"
#define APP_AWS_SENDER                 "aws/alexa"

/* x86_64版では物理GPIOが存在しないため、APP_STATUS_LED_GPIO /
 * APP_AIRCON_IR_GPIO / APP_DS18B20_GPIO の各定義は削除した。
 * (元の値: GPIO_NUM_23 / GPIO_NUM_12 / GPIO_NUM_32) */

#define APP_SYSTEM_LOOP_MS             300
#define APP_TEMPERATURE_INTERVAL_MS    60000
#define APP_FAST_RETRY_INTERVAL_MS     20000
#define APP_SLOW_RETRY_INTERVAL_MS     (5LL * 60LL * 1000LL)
#define APP_FAST_RETRY_FAILURES        10
#define APP_MQTT_WAIT_MS               250
#define APP_MQTT_KEEPALIVE_SEC         60
#define APP_SNTP_INITIAL_TIMEOUT_MS    60000
#define APP_MQTT_CONNECT_TIMEOUT_MS    60000
#define APP_MQTT_COMMAND_TIMEOUT_MS    60000
#define APP_TUYA_RETRY_DELAY_MS        500

#define APP_DEVICE_QUEUE_LENGTH        20
#define APP_PUBLISH_QUEUE_LENGTH       10
#define APP_MQTT_TASK_STACK            12288
#define APP_DEVICE_TASK_STACK          6144
#define APP_SYSTEM_TASK_STACK          3072

#define APP_MQTT_TOPIC_MAX             128
#define APP_MQTT_PAYLOAD_MAX           512
#define APP_MQTT_BUFFER_SIZE           1024
#define APP_SENDER_MAX                 32


