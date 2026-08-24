#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "lampsmart_defs.h"

/* オリジナルの lampsmart_sender_esp32.h と完全に同一のAPI。
 * x86_64版では実装を2種類用意し、CMake側でビルド時に自動選択する:
 *   - lampsmart_sender_linux_bluez.c : BlueZのraw HCIソケットで実際に
 *     BLEアドバタイズパケットを送信する (要 libbluetooth-dev, root権限,
 *     実BLEアダプタ)
 *   - lampsmart_sender_stub.c        : 上記が利用できない環境向けの
 *     ログ出力のみのフォールバック実装
 */
esp_err_t lampsmart_sender_init(void);
esp_err_t lampsmart_sender_advertise_once(
    const uint8_t adv31[LAMPSMART_ADV_LEN], uint32_t duration_ms);
esp_err_t lampsmart_sender_advertise_flags_only(uint8_t flags,
                                                 uint32_t duration_ms);
esp_err_t lampsmart_sender_stop(void);
esp_err_t lampsmart_sender_suspend(void);
esp_err_t lampsmart_sender_resume(void);
