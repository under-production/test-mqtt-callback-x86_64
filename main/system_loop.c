#include "system_loop.h"

#include "app_config.h"
#include "app_resources.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "status_led.h"
#include "wifi_manager.h"

static const char *TAG = "system_loop";

static void enqueue_temperature_job(void)
{
    device_job_t job = {
        .source = JOB_SOURCE_INTERNAL_TIMER,
        .device = JOB_DEVICE_TEMPERATURE_SENSOR,
        .command = JOB_CMD_READ_ROOM_TEMPERATURE,
    };
    if (!app_enqueue_device_job(&job)) {
        ESP_LOGD(TAG, "temperature job skipped");
    }
}

static void system_loop_main(void *argument)
{
    (void)argument;
    int64_t next_temperature_us = esp_timer_get_time() +
        (int64_t)APP_TEMPERATURE_INTERVAL_MS * 1000LL;
    while (true) {
        wifi_manager_process();
        //EventBits_t bits = xEventGroupGetBits(app_event_group());
        //status_led_tick(bits);
        int64_t now = esp_timer_get_time();
        if (now >= next_temperature_us) {
            enqueue_temperature_job();
            next_temperature_us = now +
                (int64_t)APP_TEMPERATURE_INTERVAL_MS * 1000LL;
        }
        vTaskDelay(pdMS_TO_TICKS(APP_SYSTEM_LOOP_MS));
    }
}

esp_err_t system_loop_start(void)
{
    BaseType_t ret = xTaskCreate(system_loop_main, "system_loop_task",
                                 APP_SYSTEM_TASK_STACK, NULL, 3, NULL);
    return ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

