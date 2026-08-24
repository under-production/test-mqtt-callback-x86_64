#pragma once

#include <stdbool.h>

#include "app_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#define INITIAL_COMPLETE_BIT   BIT0
#define WIFI_ERROR_BIT         BIT1
#define MQTT_ERROR_BIT         BIT2
#define THERMOMETER_ERROR_BIT  BIT3
#define WIFI_CONNECTED_BIT     BIT4
#define MQTT_CONNECTED_BIT     BIT5

esp_err_t app_resources_init(void);
EventGroupHandle_t app_event_group(void);
QueueHandle_t app_device_queue(void);
QueueHandle_t app_publish_queue(void);
bool app_enqueue_device_job(const device_job_t *job);
bool app_enqueue_publish(const publish_request_t *request);
void app_reset_device_queue(void);
void app_reset_publish_queue(void);

