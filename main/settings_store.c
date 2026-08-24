/* main/settings_store.c の x86_64 Linux移植版。
 *
 * オリジナルはESP32のNVS (flashベースのkey-valueストア) を使用しているが、
 * Linuxには存在しないため、同じ3つの設定値 (alert_hi/alert_lo/switch_en) を
 * 単純なテキスト形式のローカルファイルに保存する方式に置き換えた。
 * 保存先パスは CONFIG_ALEXA_SETTINGS_PATH (sdkconfig.h) で指定する。
 * 公開関数のシグネチャはオリジナルと完全に同一なので、呼び出し側
 * (device_worker.c) は無変更で動作する。
 */
#include "settings_store.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "settings_store";

#define KEY_ALERT_HIGH "alert_hi"
#define KEY_ALERT_LOW  "alert_lo"
#define KEY_SWITCH_ENABLE "switch_en"

typedef struct {
    char key[32];
    long value;
} kv_entry_t;

static esp_err_t load_all(kv_entry_t *entries, size_t max_entries, size_t *count)
{
    *count = 0;
    FILE *file = fopen(CONFIG_ALEXA_SETTINGS_PATH, "r");
    if (!file) return ESP_OK; /* ファイルが無ければ全てデフォルト値扱い */
    char line[128];
    while (fgets(line, sizeof(line), file) && *count < max_entries) {
        char key[32];
        long value;
        if (sscanf(line, "%31[^=]=%ld", key, &value) == 2) {
            strncpy(entries[*count].key, key, sizeof(entries[*count].key) - 1);
            entries[*count].key[sizeof(entries[*count].key) - 1] = '\0';
            entries[*count].value = value;
            (*count)++;
        }
    }
    fclose(file);
    return ESP_OK;
}

static esp_err_t save_all(const kv_entry_t *entries, size_t count)
{
    FILE *file = fopen(CONFIG_ALEXA_SETTINGS_PATH, "w");
    if (!file) {
        ESP_LOGE(TAG, "failed to open settings file for write: %s",
                 CONFIG_ALEXA_SETTINGS_PATH);
        return ESP_FAIL;
    }
    for (size_t i = 0; i < count; ++i) {
        fprintf(file, "%s=%ld\n", entries[i].key, entries[i].value);
    }
    fclose(file);
    return ESP_OK;
}

static bool find_value(const kv_entry_t *entries, size_t count,
                       const char *key, long *value)
{
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].key, key) == 0) {
            *value = entries[i].value;
            return true;
        }
    }
    return false;
}

static esp_err_t upsert_value(const char *key, long value)
{
    kv_entry_t entries[16];
    size_t count = 0;
    esp_err_t ret = load_all(entries, 16, &count);
    if (ret != ESP_OK) return ret;
    bool replaced = false;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].key, key) == 0) {
            entries[i].value = value;
            replaced = true;
            break;
        }
    }
    if (!replaced && count < 16) {
        strncpy(entries[count].key, key, sizeof(entries[count].key) - 1);
        entries[count].key[sizeof(entries[count].key) - 1] = '\0';
        entries[count].value = value;
        count++;
    }
    return save_all(entries, count);
}

esp_err_t settings_store_load_alerts(int8_t *high, int8_t *low)
{
    if (!high || !low) return ESP_ERR_INVALID_ARG;
    kv_entry_t entries[16];
    size_t count = 0;
    esp_err_t ret = load_all(entries, 16, &count);
    if (ret != ESP_OK) return ret;
    long value;
    *high = find_value(entries, count, KEY_ALERT_HIGH, &value) ? (int8_t)value : 99;
    *low = find_value(entries, count, KEY_ALERT_LOW, &value) ? (int8_t)value : -55;
    return ESP_OK;
}

esp_err_t settings_store_save_high(int8_t value)
{
    return upsert_value(KEY_ALERT_HIGH, value);
}

esp_err_t settings_store_save_low(int8_t value)
{
    return upsert_value(KEY_ALERT_LOW, value);
}

esp_err_t settings_store_load_switch_enabled(bool *enabled)
{
    if (!enabled) return ESP_ERR_INVALID_ARG;
    kv_entry_t entries[16];
    size_t count = 0;
    esp_err_t ret = load_all(entries, 16, &count);
    if (ret != ESP_OK) return ret;
    long value;
    *enabled = find_value(entries, count, KEY_SWITCH_ENABLE, &value) ? (value != 0) : false;
    return ESP_OK;
}

esp_err_t settings_store_save_switch_enabled(bool enabled)
{
    return upsert_value(KEY_SWITCH_ENABLE, enabled ? 1 : 0);
}
