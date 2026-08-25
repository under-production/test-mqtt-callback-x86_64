#include "mqtt_task.h"

#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "app_config.h"
#include "app_resources.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_protocol.h"
#include "mqtt_transport.h"
#include "sdkconfig.h"
#include "status_led.h"
#include <wolfmqtt/mqtt_client.h>

static const char *TAG = "mqtt_task";
static MqttClient s_client;
static byte s_tx_buffer[APP_MQTT_BUFFER_SIZE];
static byte s_rx_buffer[APP_MQTT_BUFFER_SIZE];
static word16 s_packet_id;
static uint32_t s_failure_count;
static bool s_sntp_initialized;
static bool s_time_synchronized;

static word16 next_packet_id(void)
{
    if (s_packet_id >= 0xFFFF) s_packet_id = 0;
    return ++s_packet_id;
}

static int64_t now_ms(void) { return esp_timer_get_time() / 1000LL; }

static bool topic_is_ours(const char *topic)
{
    size_t length = strlen(APP_MY_ID);
    return strncmp(topic, APP_MY_ID, length) == 0 &&
           (topic[length] == '/' || topic[length] == '\0');
}

static int message_callback(MqttClient *client, MqttMessage *message,
                            byte message_new, byte message_done)
{
    static char topic[APP_MQTT_TOPIC_MAX];
    static char payload[APP_MQTT_PAYLOAD_MAX];
    static size_t payload_used;
    static bool topic_overflow;
    static bool payload_overflow;
    (void)client;
    static uint32_t callback_no;

    ESP_LOGI(
        TAG,
        "CALLBACK #%lu time=%lld new=%u done=%u",
        ++callback_no,
        esp_timer_get_time() / 1000,
        (unsigned)message_new,
        (unsigned)message_done
    );

    ESP_LOGI(
        TAG,
        "CALLBACK #%lu msg=%p buf=%p new=%u done=%u",
        ++callback_no,
        (void *)message,
        (void *)message->buffer,
        (unsigned)message_new,
        (unsigned)message_done
    );    

    if (message_new) {
        payload_used = 0;
        topic_overflow = message->topic_name_len >= sizeof(topic);
        payload_overflow = false;
        size_t length = topic_overflow ? sizeof(topic) - 1
                                       : message->topic_name_len;
        memcpy(topic, message->topic_name, length);
        topic[length] = '\0';
        payload[0] = '\0';
    }
    if (message->buffer && message->buffer_len) {
        size_t available = sizeof(payload) - 1 - payload_used;
        size_t length = message->buffer_len;
        if (length > available) {
            length = available;
            payload_overflow = true;
        }
        memcpy(&payload[payload_used], message->buffer, length);
        payload_used += length;
        payload[payload_used] = '\0';
    }
   
    if (!message_done) return MQTT_CODE_SUCCESS;

    if (message_done) {
        ESP_LOGI(
            TAG,
            "PAYLOAD=%s",
            payload
        );
    }    
    publish_request_t response;
    if (topic_overflow) {
        ESP_LOGW(TAG, "received MQTT topic exceeds limit");
        return MQTT_CODE_SUCCESS;
    }
    if (payload_overflow) {
        ESP_LOGW(TAG, "received MQTT payload exceeds limit: topic=%s", topic);
        if (topic_is_ours(topic) &&
            mqtt_protocol_make_result(topic, "error", "invalid_command",
                                      &response)) {
            app_enqueue_publish(&response);
        }
        return MQTT_CODE_SUCCESS;
    }
    device_job_t job;
    mqtt_parse_result_t result = mqtt_protocol_parse(topic, payload, &job);
    if (result == MQTT_PARSE_NOT_FOR_US) return MQTT_CODE_SUCCESS;
    if (result == MQTT_PARSE_INVALID) {
        if (mqtt_protocol_make_result(topic, "error", "invalid_command",
                                      &response)) {
            app_enqueue_publish(&response);
        }
        return MQTT_CODE_SUCCESS;
    }
    if (!app_enqueue_device_job(&job) &&
        mqtt_protocol_make_result(topic, "busy", "device_queue_full",
                                  &response)) {
        app_enqueue_publish(&response);
    }
    return MQTT_CODE_SUCCESS;
}

static esp_err_t ensure_initial_time_sync(void)
{
    if (s_time_synchronized) return ESP_OK;
    if (!s_sntp_initialized) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_err_t ret = esp_netif_sntp_init(&config);
        if (ret != ESP_OK) return ret;
        s_sntp_initialized = true;
    }
    ESP_LOGI(TAG, "waiting for initial SNTP synchronization");
    esp_err_t ret = esp_netif_sntp_sync_wait(
        pdMS_TO_TICKS(APP_SNTP_INITIAL_TIMEOUT_MS));
    if (ret == ESP_OK) {
        s_time_synchronized = true;
        ESP_LOGI(TAG, "initial SNTP synchronization completed");
    } else {
        ESP_LOGE(TAG, "initial SNTP synchronization failed: %s",
                 esp_err_to_name(ret));
    }
    return ret;
}

static int publish_one(const publish_request_t *request)
{
    status_led_notify_publish();
    MqttPublish publish;
    memset(&publish, 0, sizeof(publish));
    publish.qos = request->qos;
    publish.retain = request->retain ? 1 : 0;
    publish.topic_name = request->topic;
    publish.packet_id = request->qos ? next_packet_id() : 0;
    publish.buffer = (byte *)request->payload;
    publish.total_len = (word16)strlen(request->payload);
    int ret = MqttClient_Publish(&s_client, &publish);
    if (ret != MQTT_CODE_SUCCESS) {
        ESP_LOGE(TAG, "publish failed: %d topic=%s", ret, request->topic);
    }
    return ret;
}

static void request_initial_publish(void)
{
    device_job_t job = {
        .source = JOB_SOURCE_SYSTEM,
        .device = JOB_DEVICE_SYSTEM,
        .command = JOB_CMD_PUBLISH_INITIAL_STATE,
    };
    if (app_enqueue_device_job(&job)) {
        EventBits_t bits = xEventGroupGetBits(app_event_group());
        if (!(bits & INITIAL_COMPLETE_BIT)) {
            xEventGroupSetBits(app_event_group(), INITIAL_COMPLETE_BIT);
        }
    } else {
        ESP_LOGW(TAG, "initial publish job could not be queued");
    }
}

static int run_session(bool *connected_once)
{
    int ret;
    bool broker_connected = false;
    mqtt_transport_init();
    memset(&s_client, 0, sizeof(s_client));
    ret = MqttClient_Init(&s_client, mqtt_transport_net(), message_callback,
                          s_tx_buffer, sizeof(s_tx_buffer), s_rx_buffer,
                          sizeof(s_rx_buffer), APP_MQTT_COMMAND_TIMEOUT_MS);
    if (ret != MQTT_CODE_SUCCESS) {
        ESP_LOGE(TAG, "MQTT client initialization failed: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "MQTT DNS/TCP/TLS connection starting");
    ret = MqttClient_NetConnect(&s_client, CONFIG_ALEXA_EMQX_HOST,
                                CONFIG_ALEXA_EMQX_PORT,
                                APP_MQTT_CONNECT_TIMEOUT_MS, 1,
                                mqtt_transport_tls_callback);
    if (ret != MQTT_CODE_SUCCESS) {
        ESP_LOGE(TAG, "MQTT DNS/TCP/TLS connection failed: %d", ret);
        goto exit;
    }

    MqttConnect connect;
    memset(&connect, 0, sizeof(connect));
    connect.keep_alive_sec = APP_MQTT_KEEPALIVE_SEC;
    connect.clean_session = 1;
    connect.client_id = CONFIG_ALEXA_MQTT_CLIENT_ID;
    connect.username = CONFIG_ALEXA_EMQX_USERNAME;
    connect.password = CONFIG_ALEXA_EMQX_PASSWORD;
    ret = MqttClient_Connect(&s_client, &connect);
    if (ret != MQTT_CODE_SUCCESS) {
        ESP_LOGE(TAG, "MQTT CONNECT failed: %d", ret);
        goto exit;
    }
    broker_connected = true;

    char subscribe_topic[32];
    snprintf(subscribe_topic, sizeof(subscribe_topic), "%s/#", APP_MY_ID);
    MqttTopic topic = {.topic_filter = subscribe_topic, .qos = MQTT_QOS_0};
    MqttSubscribe subscribe;
    memset(&subscribe, 0, sizeof(subscribe));
    subscribe.packet_id = next_packet_id();
    subscribe.topic_count = 1;
    subscribe.topics = &topic;
    ret = MqttClient_Subscribe(&s_client, &subscribe);
    if (ret != MQTT_CODE_SUCCESS) {
        ESP_LOGE(TAG, "MQTT SUBSCRIBE failed: %d", ret);
        goto exit;
    }

    *connected_once = true;
    s_failure_count = 0;
    xEventGroupClearBits(app_event_group(), MQTT_ERROR_BIT);
    xEventGroupSetBits(app_event_group(), MQTT_CONNECTED_BIT);
    ESP_LOGI(TAG, "connected and subscribed: %s", subscribe_topic);
    request_initial_publish();

    int64_t last_ping = now_ms();
    while (true) {
        publish_request_t request;
        while (xQueueReceive(app_publish_queue(), &request, 0) == pdTRUE) {
            ret = publish_one(&request);
            if (ret != MQTT_CODE_SUCCESS) goto exit;
        }
        MqttObject object;
        memset(&object, 0, sizeof(object));
        ret = MqttClient_WaitMessage_ex(&s_client, &object, APP_MQTT_WAIT_MS);
        if (ret != MQTT_CODE_SUCCESS && ret != MQTT_CODE_ERROR_TIMEOUT) {
            ESP_LOGE(TAG, "MQTT receive failed: %d", ret);
            goto exit;
        }
        if (now_ms() - last_ping >= (APP_MQTT_KEEPALIVE_SEC * 1000LL) / 2) {
            ESP_LOGI(TAG, "PING BEGIN time=%lld", now_ms());
            ret = MqttClient_Ping(&s_client);
            if (ret != MQTT_CODE_SUCCESS) {
                ESP_LOGE(TAG, "MQTT PING failed: %d", ret);
                goto exit;
            }
            last_ping = now_ms();
        }
    }
exit:
    xEventGroupClearBits(app_event_group(), MQTT_CONNECTED_BIT);
    if (broker_connected) (void)MqttClient_Disconnect(&s_client);
    (void)MqttClient_NetDisconnect(&s_client);
    MqttClient_DeInit(&s_client);
    mqtt_transport_force_close();
    app_reset_publish_queue();
    return ret;
}

static void mqtt_task_main(void *argument)
{
    (void)argument;
    while (true) {
        EventBits_t bits = xEventGroupWaitBits(app_event_group(),
                                   WIFI_CONNECTED_BIT,
                                   pdFALSE, pdFALSE, pdMS_TO_TICKS(500));
        if (!(bits & WIFI_CONNECTED_BIT)) continue;

        bool connected_once = false;
        int ret = ensure_initial_time_sync() == ESP_OK
                      ? run_session(&connected_once)
                      : MQTT_CODE_ERROR_NETWORK;
        xEventGroupSetBits(app_event_group(), MQTT_ERROR_BIT);
        if (!connected_once) {
            s_failure_count++;
            if (s_failure_count < APP_FAST_RETRY_FAILURES) {
                ESP_LOGW(TAG,
                         "MQTT connection failure %lu/%d: %d; retry in 20 seconds",
                         (unsigned long)s_failure_count,
                         APP_FAST_RETRY_FAILURES, ret);
            } else {
                ESP_LOGW(TAG,
                         "MQTT connection failure %lu: %d; long-term retry in 5 minutes",
                         (unsigned long)s_failure_count, ret);
            }
        }
        int64_t retry_ms =
            s_failure_count >= APP_FAST_RETRY_FAILURES
                ? APP_SLOW_RETRY_INTERVAL_MS
                : APP_FAST_RETRY_INTERVAL_MS;
        int64_t retry_until = now_ms() + retry_ms;
        while (now_ms() < retry_until) {
            if (!(xEventGroupGetBits(app_event_group()) &
                  WIFI_CONNECTED_BIT)) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

esp_err_t mqtt_task_start(void)
{
    BaseType_t ret = xTaskCreate(mqtt_task_main, "mqtt_task",
                                 APP_MQTT_TASK_STACK, NULL, 5, NULL);
    return ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

uint32_t mqtt_task_failure_count(void) { return s_failure_count; }


