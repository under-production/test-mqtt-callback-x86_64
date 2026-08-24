/* lampsmart_sender_linux_bluez.c
 *
 * オリジナル(ESP32)版は esp_ble_gap_config_adv_data_raw() /
 * esp_ble_gap_start_advertising() / esp_ble_gap_stop_advertising() で
 * BLEのRAWアドバタイズパケットを送信していた。これはLinuxのBlueZが提供する
 * raw HCIソケット (AF_BLUETOOTH / BTPROTO_HCI) 経由で、同等のHCIコマンド
 * (LE Set Advertising Parameters / LE Set Advertising Data /
 * LE Set Advertise Enable) を直接発行することでほぼ同じ動作を再現できる。
 *
 * 実行には以下が必要:
 *   - libbluetooth-dev (ビルド時。CMakeが自動検出)
 *   - 実行時に root 権限、または cap_net_raw+cap_net_admin capability
 *   - 実際のBluetoothアダプタ (hci0 等)
 * いずれかが欠けている場合は lampsmart_sender_stub.c 側にフォールバックする
 * ようCMakeLists.txt で切り替えている。
 */
#include "lampsmart_sender.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "esp_log.h"

static const char *TAG = "lampsmart_sender(bluez)";
static int s_hci_fd = -1;
static bool s_advertising;
static bool s_suspended;

#define ADV_INTERVAL_MIN 0x00A0
#define ADV_INTERVAL_MAX 0x00A0
#define ADV_TYPE_IND     0x00
#define OWN_ADDR_PUBLIC  0x00
#define ADV_CHANNEL_ALL  0x07
#define ADV_FILTER_ANY   0x00

static esp_err_t send_hci_command(uint16_t ocf, const void *params,
                                  uint8_t plen, const char *what)
{
    if (hci_send_cmd(s_hci_fd, OGF_LE_CTL, ocf, plen, (void *)params) < 0) {
        ESP_LOGW(TAG, "%s failed: %s", what, strerror(errno));
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t set_advertising_enable(bool enable)
{
    uint8_t param = enable ? 0x01 : 0x00;
    esp_err_t ret = send_hci_command(OCF_LE_SET_ADVERTISE_ENABLE, &param,
                                     sizeof(param),
                                     enable ? "LE Set Advertise Enable(1)"
                                            : "LE Set Advertise Enable(0)");
    if (ret == ESP_OK) s_advertising = enable;
    return ret;
}

esp_err_t lampsmart_sender_init(void)
{
    if (s_hci_fd >= 0) return ESP_OK;
    int dev_id = hci_get_route(NULL);
    if (dev_id < 0) {
        ESP_LOGW(TAG, "Bluetoothアダプタが見つかりません。"
                       "以降のBLEアドバタイズはログ出力のみのシミュレーションとして"
                       "継続します(MQTT検証は影響を受けません)");
        return ESP_OK; /* ユーザー指示: 再現できなくても致命的エラーにはしない */
    }
    s_hci_fd = hci_open_dev(dev_id);
    if (s_hci_fd < 0) {
        ESP_LOGW(TAG, "hci_open_dev失敗 (root権限/cap_net_rawが必要な場合があります: %s)。"
                       "以降のBLEアドバタイズはログ出力のみのシミュレーションとして継続します",
                 strerror(errno));
        s_hci_fd = -1;
        return ESP_OK;
    }
    ESP_LOGI(TAG, "BlueZ raw HCI 初期化完了 (hci%d) — 実BLEアドバタイズを送信します",
             dev_id);
    s_suspended = false;
    return ESP_OK;
}

esp_err_t lampsmart_sender_stop(void)
{
    if (s_hci_fd < 0) return ESP_OK;
    if (!s_advertising) return ESP_OK;
    return set_advertising_enable(false);
}

static esp_err_t configure_advertising_params(void)
{
    struct {
        uint16_t interval_min;
        uint16_t interval_max;
        uint8_t adv_type;
        uint8_t own_addr_type;
        uint8_t peer_addr_type;
        uint8_t peer_addr[6];
        uint8_t channel_map;
        uint8_t filter_policy;
    } __attribute__((packed)) params = {
        .interval_min = ADV_INTERVAL_MIN,
        .interval_max = ADV_INTERVAL_MAX,
        .adv_type = ADV_TYPE_IND,
        .own_addr_type = OWN_ADDR_PUBLIC,
        .peer_addr_type = 0,
        .peer_addr = {0, 0, 0, 0, 0, 0},
        .channel_map = ADV_CHANNEL_ALL,
        .filter_policy = ADV_FILTER_ANY,
    };
    return send_hci_command(OCF_LE_SET_ADVERTISING_PARAMETERS, &params,
                            sizeof(params), "LE Set Advertising Parameters");
}

static esp_err_t configure_advertising_data(const uint8_t *data, uint8_t length)
{
    struct {
        uint8_t length;
        uint8_t data[31];
    } __attribute__((packed)) params = {0};
    params.length = length;
    memcpy(params.data, data, length);
    return send_hci_command(OCF_LE_SET_ADVERTISING_DATA, &params,
                            sizeof(params), "LE Set Advertising Data");
}

static esp_err_t advertise_raw(const uint8_t *data, uint8_t length,
                               uint32_t duration_ms)
{
    if (s_hci_fd < 0) {
        /* 実アダプタが無い環境: ログのみのシミュレーションにフォールバック */
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, length, ESP_LOG_INFO);
        ESP_LOGI(TAG, "[SIMULATED BLE ADV] %u byte payload, duration=%ums",
                 (unsigned)length, duration_ms);
        return ESP_OK;
    }
    if (s_suspended) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = lampsmart_sender_stop();
    if (ret != ESP_OK) return ret;
    ret = configure_advertising_params();
    if (ret != ESP_OK) return ret;
    ret = configure_advertising_data(data, length);
    if (ret != ESP_OK) return ret;
    ret = set_advertising_enable(true);
    if (ret != ESP_OK) return ret;
    usleep((useconds_t)duration_ms * 1000);
    return lampsmart_sender_stop();
}

esp_err_t lampsmart_sender_advertise_once(
    const uint8_t adv31[LAMPSMART_ADV_LEN], uint32_t duration_ms)
{
    if (!adv31) return ESP_ERR_INVALID_ARG;
    return advertise_raw(adv31, LAMPSMART_ADV_LEN, duration_ms);
}

esp_err_t lampsmart_sender_advertise_flags_only(uint8_t flags,
                                                 uint32_t duration_ms)
{
    uint8_t adv[3] = {0x02, 0x01, flags};
    return advertise_raw(adv, sizeof(adv), duration_ms);
}

esp_err_t lampsmart_sender_suspend(void)
{
    esp_err_t ret = lampsmart_sender_stop();
    if (ret != ESP_OK) return ret;
    s_suspended = true;
    return ESP_OK;
}

esp_err_t lampsmart_sender_resume(void)
{
    s_suspended = false;
    return ESP_OK;
}
