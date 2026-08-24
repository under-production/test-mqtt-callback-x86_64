/* ESP-IDF esp_netif_sntp.h compatibility shim.
 * Linuxホストは通常systemd-timesyncd等でシステム時刻が同期済みのため、
 * ここでは「システム時刻が妥当な範囲か」を確認するだけの簡易実装とする。
 * mqtt_task.c 側は変更なしで利用できる。
 */
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    const char *server;
} esp_sntp_config_t;

#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server_name) \
    (esp_sntp_config_t) { .server = (server_name) }

esp_err_t esp_netif_sntp_init(esp_sntp_config_t *config);
esp_err_t esp_netif_sntp_sync_wait(TickType_t ticks_to_wait);
