#include "app_resources.h"

#include "esp_log.h"

static const char *TAG = "app_resources";
static EventGroupHandle_t s_events;
static QueueHandle_t s_device_queue;
static QueueHandle_t s_publish_queue;

esp_err_t app_resources_init(void)
{
    s_events = xEventGroupCreate();
    s_device_queue = xQueueCreate(APP_DEVICE_QUEUE_LENGTH, sizeof(device_job_t));
    s_publish_queue = xQueueCreate(APP_PUBLISH_QUEUE_LENGTH, sizeof(publish_request_t));
    if (!s_events || !s_device_queue || !s_publish_queue) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

EventGroupHandle_t app_event_group(void) { return s_events; }
QueueHandle_t app_device_queue(void) { return s_device_queue; }
QueueHandle_t app_publish_queue(void) { return s_publish_queue; }

bool app_enqueue_device_job(const device_job_t *job)
{
    if (!job || !s_device_queue) return false;
    return xQueueSend(s_device_queue, job, 0) == pdTRUE;
}

bool app_enqueue_publish(const publish_request_t *request)
{
    if (!request || !s_publish_queue) return false;
    if (xQueueSend(s_publish_queue, request, 0) != pdTRUE) {
        ESP_LOGW(TAG, "publish queue full; request discarded");
        return false;
    }
    return true;
}

void app_reset_device_queue(void)
{
    if (s_device_queue) xQueueReset(s_device_queue);
}

void app_reset_publish_queue(void)
{
    if (s_publish_queue) xQueueReset(s_publish_queue);
}

