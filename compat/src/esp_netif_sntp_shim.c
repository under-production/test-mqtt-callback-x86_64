#include "esp_netif_sntp.h"
#include "esp_log.h"

#include <time.h>

static const char *TAG = "sntp_shim";

esp_err_t esp_netif_sntp_init(esp_sntp_config_t *config)
{
    ESP_LOGI(TAG,
             "Linux版ではOSのNTPサービス(systemd-timesyncd等)に時刻同期を委譲します"
             " (configured server=%s)",
             config && config->server ? config->server : "?");
    return ESP_OK;
}

esp_err_t esp_netif_sntp_sync_wait(TickType_t ticks_to_wait)
{
    /* ESP32版はSNTPクライアントで同期完了を待つが、Linuxホストは通常
     * 既にOSレベルで時刻同期済みなので、現在時刻が妥当な範囲(2024年以降)かを
     * 確認するだけに簡略化する。TLS証明書検証に必要な最低限のチェック。 */
    (void)ticks_to_wait;
    time_t now = time(NULL);
    const time_t year2024 = 1704067200; /* 2024-01-01T00:00:00Z */
    if (now < year2024) {
        ESP_LOGW(TAG, "システム時刻が異常です(未同期の可能性): now=%lld",
                 (long long)now);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
