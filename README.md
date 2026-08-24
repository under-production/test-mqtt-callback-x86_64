# ESP32-IR-Bluetooth-Controler — x86_64 Linux 動作検証版 (`x86_64_test`)

このブランチは、[development-program/ESP32-IR-Bluetooth-Controler](https://github.com/development-program/ESP32-IR-Bluetooth-Controler)
(ESP-IDF v6.0.1 / FreeRTOS) を、**MQTT周りの動作検証**を目的として
x86_64 Linux上でビルド・実行できるように移植したものです。

## 移植方針

オリジナルのアーキテクチャ・処理フローをできる限り忠実に再現しつつ、
ESP32固有のハードウェアAPIをLinux/POSIX相当のものに置き換えています。

| 項目 | オリジナル(ESP32) | x86_64版 |
|---|---|---|
| MQTT/TLS | wolfMQTT + wolfSSL | **無変更**(同じライブラリをGitHubから取得してそのままビルド) |
| RTOS/同期プリミティブ | FreeRTOS (task/queue/event_group) | pthreadベースの互換シム (`compat/`)。呼び出し側のロジックは無変更 |
| DS18B20温度センサー | 1-Wire実装 | **除外**。常に初期化失敗を返し、元々あった「3回リトライ後に無効化」というオリジナルのフォールバック経路がそのまま働く |
| IR送信(RMT/GPIO) | Panasonicエアコン用赤外線信号送信 | 状態組み立て・チェックサム計算・enum文字列変換などの**ロジックは無変更のまま流用**。実送信のみログ出力によるシミュレーションに置換 |
| ステータスLED(GPIO) | LED点滅パターン | 状態遷移ロジックは同一のまま、ログ出力に置換 |
| Wi-Fi管理 | esp_wifi/esp_netif | ホストOSのネットワークを「常時接続済み」として扱う簡易実装 |
| 設定値の永続化(NVS) | ESP32 flash NVS | ローカルファイル(`data/settings.dat`)ベースのkey-valueストア |
| BLE電球制御(lampsmart) | esp_ble_gap (ESP-IDF Bluedroid) | **BlueZのraw HCIソケットで実際にBLEアドバタイズパケットを送信**する実装。暗号・プロトコルロジック(`lampsmart*.c`)は無変更。libbluetooth-devや実アダプタが無い環境では自動的にログ出力のみのシミュレーションにフォールバック |
| Tuyaスイッチ(ローカルWi-Fi制御) | BSDソケット + PSA crypto | ほぼ無変更(`lwip/inet.h` → `arpa/inet.h`のみ変更) |
| SNTP時刻同期 | esp_netif_sntp | ホストOSが既に時刻同期済みという前提の簡易チェックに置換 |

## ビルド方法

### 必要パッケージ (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
    libmbedtls-dev libcjson-dev libbluetooth-dev
```

- `libbluetooth-dev` は任意です。無い場合はBLEアドバタイズがログ出力のみの
  シミュレーションになりますが、それ以外は問題なく動作します。
- wolfSSL / wolfMQTT はビルド時にGitHubから自動取得されます
  (`CMakeLists.txt` の `FetchContent`)。ネットワークアクセスが必要です。

### 設定ファイルの用意

オリジナルはESP-IDFの `idf.py menuconfig` (Kconfig) でWi-Fi/MQTT/Tuya等の
認証情報を設定していましたが、今回アップロードいただいたファイル一式には
`Kconfig.projbuild` の実体が含まれていなかったため、x86_64版では
同じマクロ名をプレーンな `#define` として直接編集する方式にしました。

```bash
cp main/sdkconfig.h.example main/sdkconfig.h
# main/sdkconfig.h を編集し、実際のMQTTブローカー/Tuya/LampSmartの
# 値を入力してください。(このファイルは.gitignore対象です)
```

CA証明書(オリジナルの `emqx_ca.pem` に相当)を配置してください:

```bash
cp /path/to/your/broker_ca.pem certs/emqx_ca.pem
```

### ビルド・実行

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./alexa_esp32_x86_64_test
```

## 動作確認済み事項

- 上記の手順でビルドが100%通ることを確認済みです。
- ダミー設定での起動シーケンス確認済み: LED状態遷移 → IR送信シミュレーション
  → BLEアドバタイズ(シミュレーションへの自動フォールバック含む) →
  DS18B20無効化フォールバック → Wi-Fi接続扱い → SNTP代替チェック →
  MQTT接続試行、まで正常に到達することを確認しています。
- 実際のMQTTブローカーへの接続確認は、`main/sdkconfig.h` に実際の
  ブローカー情報とCA証明書を設定した上で行ってください。

## 制限事項

- **DS18B20 / IR送信の実ハードウェア動作は再現していません**(ご要望通り除外)。
- **BLEは実アダプタ・root権限・libbluetooth-devがすべて揃っている場合のみ
  実際に電波を送信します**。それ以外の場合は自動的にログ出力のみの
  シミュレーションとして動作し、プログラム全体は正常に継続します。
- Tuyaスイッチのローカル通信は同一LAN上に実機がある場合のみ実際に疎通します。
