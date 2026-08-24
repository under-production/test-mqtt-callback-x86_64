/* main/wifi_manager.c の x86_64 Linux移植版。
 *
 * オリジナルはESP32のesp_wifi/esp_netif APIでSTAモードのWi-Fi接続を
 * 管理するが、Linuxホストではネットワーク接続はOS側で既に確立されている
 * ため、ここでは「常に接続済み」として WIFI_CONNECTED_BIT を立てるだけの
 * 簡易実装とする。MQTT検証という目的上、これでmqtt_task.c/system_loop.cの
 * ロジックは無変更のまま動作する。
 *
 * wifi_manager_failure_count() はダミーで常に0を返す。
 */
#include "wifi_manager.h"

#include <stdbool.h>

#include "app_config.h"
#include "app_resources.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "wifi_manager";
static bool s_running;

esp_err_t wifi_manager_init(void)
{
    s_running = true;
    xEventGroupClearBits(app_event_group(), WIFI_ERROR_BIT);
    xEventGroupSetBits(app_event_group(), WIFI_CONNECTED_BIT);
    ESP_LOGI(TAG, "Linux host network assumed already up; "
                   "treating Wi-Fi as connected");
    return ESP_OK;
}

void wifi_manager_process(void)
{
    /* Linuxホストのネットワーク管理はOS/NetworkManager等に委ねるため、
     * ここでは何もしない。接続断の検知が必要な場合は、mqtt_task.c側の
     * ソケットエラーで自然にリトライされる。 */
    (void)s_running;
}

esp_err_t wifi_manager_stop(void)
{
    s_running = false;
    xEventGroupClearBits(app_event_group(), WIFI_CONNECTED_BIT);
    return ESP_OK;
}

esp_err_t wifi_manager_restart(void)
{
    s_running = true;
    xEventGroupSetBits(app_event_group(), WIFI_CONNECTED_BIT);
    return ESP_OK;
}

uint32_t wifi_manager_failure_count(void) { return 0; }
