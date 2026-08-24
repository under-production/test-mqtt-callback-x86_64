#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

esp_err_t status_led_init(void);
void status_led_show_startup(void);
void status_led_notify_publish(void);
void status_led_tick(EventBits_t bits);
