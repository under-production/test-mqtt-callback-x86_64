/* lampsmart_sender_stub.c
 *
 * BlueZ (libbluetooth-dev) や実BLEアダプタが利用できない環境向けの
 * フォールバック実装。実際の電波送信は行わず、送信内容をログに出力する
 * だけのシミュレーションとする。
 *
 * ユーザー指示: 「Bluetooth部分は再現できれば再現、できなければ動作しなくて
 * 良い」に基づく実装。CMakeLists.txt が libbluetooth-dev を検出できなかった
 * 場合に自動的にこちらがビルドされる。
 */
#include "lampsmart_sender.h"

#include "esp_log.h"

static const char *TAG = "lampsmart_sender(stub)";

esp_err_t lampsmart_sender_init(void)
{
    ESP_LOGW(TAG, "BlueZ/libbluetooth-devが見つからないため、"
                   "BLEアドバタイズはシミュレーション(ログ出力のみ)になります");
    return ESP_OK;
}

esp_err_t lampsmart_sender_advertise_once(
    const uint8_t adv31[LAMPSMART_ADV_LEN], uint32_t duration_ms)
{
    if (!adv31) return ESP_ERR_INVALID_ARG;
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, adv31, LAMPSMART_ADV_LEN, ESP_LOG_INFO);
    ESP_LOGI(TAG, "[SIMULATED BLE ADV] %u byte payload, duration=%ums",
             (unsigned)LAMPSMART_ADV_LEN, duration_ms);
    return ESP_OK;
}

esp_err_t lampsmart_sender_advertise_flags_only(uint8_t flags,
                                                 uint32_t duration_ms)
{
    ESP_LOGI(TAG, "[SIMULATED BLE ADV] flags-only 0x%02x, duration=%ums",
             flags, duration_ms);
    return ESP_OK;
}

esp_err_t lampsmart_sender_stop(void) { return ESP_OK; }
esp_err_t lampsmart_sender_suspend(void) { return ESP_OK; }
esp_err_t lampsmart_sender_resume(void) { return ESP_OK; }
