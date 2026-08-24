/* main/app_main.c の x86_64 Linux移植版。
 *
 * オリジナルとの差分:
 *   - initialize_nvs() (ESP32のflash NVS初期化) を削除。設定値の永続化は
 *     settings_store.c 側でファイルベースに置き換え済みのため、明示的な
 *     初期化は不要。
 *   - それ以外の起動シーケンス (status_led -> app_resources -> aircon_ir ->
 *     device_worker -> room_temperature -> wifi_manager -> system_loop ->
 *     mqtt_task) はオリジナルと完全に同一の順序・ロジック。
 */
#include "app_resources.h"
#include "device_worker.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_task.h"
#include "room_temperature.h"
#include "status_led.h"
#include "system_loop.h"
#include "wifi_manager.h"

static const char *TAG = "app_main";

static bool initialize_temperature_sensor(void)
{
    for (int attempt = 1; attempt <= 3; ++attempt) {
        esp_err_t ret = room_temperature_init();
        if (ret == ESP_OK) return true;
        ESP_LOGW(TAG, "DS18B20 initialization %d/3 failed: %s", attempt,
                 esp_err_to_name(ret));
        if (attempt < 3) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGE(TAG, "DS18B20 disabled (not available on this platform)");
    return false;
}

void app_main(void)
{
    ESP_ERROR_CHECK(status_led_init());
    status_led_show_startup();
    ESP_ERROR_CHECK(app_resources_init());

    ESP_ERROR_CHECK(aircon_ir_init());
    ESP_ERROR_CHECK(device_worker_initialize());
    bool temperature_enabled = initialize_temperature_sensor();
    device_worker_set_temperature_sensor_enabled(temperature_enabled);

    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(device_worker_start());
    ESP_ERROR_CHECK(system_loop_start());
    ESP_ERROR_CHECK(mqtt_task_start());
    ESP_LOGI(TAG, "Alexa-ESP32 (x86_64 test build) tasks started");
}

int main(void)
{
    app_main();
    /* FreeRTOSシムの各タスクはpthreadとして動き続けるため、
     * メインスレッドはここで待機してプロセスを維持する。 */
    for (;;) vTaskDelay(pdMS_TO_TICKS(60000));
    return 0;
}
