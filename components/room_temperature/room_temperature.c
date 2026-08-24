/* components/room_temperature/room_temperature.c の x86_64 Linux移植版。
 *
 * ユーザー指示により、DS18B20 (1-Wire温度センサー) はハードウェアが
 * 存在しないx86_64環境では再現不可能なため除外した。
 *
 * ただし関数シグネチャはオリジナルと完全に同一に保ち、常に
 * ESP_ERR_NOT_SUPPORTED を返すことで、呼び出し側 (app_main.c /
 * device_worker.c) に既に実装されている「センサー初期化失敗時は
 * 3回リトライ後に無効化する」というオリジナルのロジックがそのまま
 * 自然に働き、温度センサー無効状態としてグレースフルに動作する。
 * (この既存のフォールバック経路自体がオリジナルの設計であり、
 * 今回の移植で新たに追加したものではない)
 */
#include "room_temperature.h"

#include "esp_log.h"

static const char *TAG = "room_temperature";

esp_err_t room_temperature_init(void)
{
    ESP_LOGW(TAG, "DS18B20は x86_64版では利用できません(ハードウェア非搭載)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t room_temperature_read(float *temperature_c)
{
    (void)temperature_c;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t room_temperature_reinitialize(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void room_temperature_deinit(void)
{
    /* no-op */
}
