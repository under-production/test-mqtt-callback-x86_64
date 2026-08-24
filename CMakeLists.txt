cmake_minimum_required(VERSION 3.16)
project(alexa_esp32_x86_64_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

include(FetchContent)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# mbedTLS (PSA crypto: tuya_switch のAES/HMAC, lampsmartのAES-ECBに使用)
#   Ubuntu/Debian: sudo apt install libmbedtls-dev
# ---------------------------------------------------------------------------
pkg_check_modules(MBEDTLS REQUIRED mbedtls mbedcrypto mbedx509)

# ---------------------------------------------------------------------------
# cJSON (mqtt_protocol / tuya_switch のJSONパース)
#   Ubuntu/Debian: sudo apt install libcjson-dev
# ---------------------------------------------------------------------------
pkg_check_modules(CJSON REQUIRED libcjson)

# ---------------------------------------------------------------------------
# BlueZ (任意。実BLEアドバタイズ送信に必要。無ければログのみのスタブを使う)
#   Ubuntu/Debian: sudo apt install libbluetooth-dev
# ---------------------------------------------------------------------------
pkg_check_modules(BLUEZ bluez)

# ---------------------------------------------------------------------------
# wolfSSL / wolfMQTT を GitHub から取得してソースツリーとしてビルドする。
# オリジナル(ESP32)版と同じライブラリをそのまま使うことで、TLS/MQTTの
# 挙動を可能な限り忠実に再現する。
# ---------------------------------------------------------------------------
set(WOLFSSL_USER_SETTINGS yes CACHE STRING "" FORCE)
set(WOLFSSL_EXAMPLES no CACHE STRING "" FORCE)
set(WOLFSSL_CRYPT_TESTS no CACHE STRING "" FORCE)
set(WOLFSSL_BENCHMARK no CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    wolfssl
    GIT_REPOSITORY https://github.com/wolfSSL/wolfssl.git
    GIT_TAG        v5.7.6-stable
    GIT_SHALLOW    TRUE
)
# ここでは取得のみ行う(add_subdirectoryはしない)。
# wolfMQTTのCMake側がWITH_WOLFSSL_TREE経由でadd_subdirectoryするため、
# 二重にターゲットを定義しないようFetchContent_Populateのみ使う。
FetchContent_GetProperties(wolfssl)
if(NOT wolfssl_POPULATED)
    FetchContent_Populate(wolfssl)
endif()

FetchContent_Declare(
    wolfmqtt
    GIT_REPOSITORY https://github.com/wolfSSL/wolfMQTT.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
set(WITH_WOLFSSL_TREE ${wolfssl_SOURCE_DIR} CACHE PATH "" FORCE)
set(WOLFMQTT_EXAMPLES no CACHE STRING "" FORCE)
set(WOLFMQTT_TLS yes CACHE STRING "" FORCE)
FetchContent_MakeAvailable(wolfmqtt)
target_include_directories(wolfmqtt PRIVATE ${CMAKE_SOURCE_DIR}/main)
target_compile_definitions(wolfmqtt PRIVATE WOLFSSL_USER_SETTINGS)
target_include_directories(wolfssl PRIVATE ${CMAKE_SOURCE_DIR}/main)

# ---------------------------------------------------------------------------
# 互換シム層 (ESP-IDF/FreeRTOS API -> pthread/POSIX)
# ---------------------------------------------------------------------------
add_library(esp_compat STATIC
    compat/src/esp_shim.c
    compat/src/freertos_shim.c
    compat/src/esp_netif_sntp_shim.c
)
target_include_directories(esp_compat PUBLIC
    compat/include
    ${MBEDTLS_INCLUDE_DIRS}
)
target_link_libraries(esp_compat PUBLIC ${MBEDTLS_LIBRARIES} Threads::Threads)

# ---------------------------------------------------------------------------
# lampsmart (BLE電球制御): プロトコル/暗号ロジックは共通、送信部のみ切替
# ---------------------------------------------------------------------------
add_library(lampsmart STATIC
    components/lampsmart/src/lampsmart.c
    components/lampsmart/src/lampsmart_77f8.c
    components/lampsmart/src/lampsmart_crc.c
    components/lampsmart/src/lampsmart_f008.c
    components/lampsmart/src/lampsmart_profile.c
)
target_include_directories(lampsmart PUBLIC components/lampsmart/include)
target_link_libraries(lampsmart PUBLIC esp_compat)

if(BLUEZ_FOUND)
    message(STATUS "libbluetooth-dev が見つかりました: 実BLEアドバタイズ送信(BlueZ raw HCI)を有効化します")
    target_sources(lampsmart PRIVATE components/lampsmart/src/lampsmart_sender_linux_bluez.c)
    target_include_directories(lampsmart PUBLIC ${BLUEZ_INCLUDE_DIRS})
    target_link_libraries(lampsmart PUBLIC ${BLUEZ_LIBRARIES})
else()
    message(WARNING "libbluetooth-dev が見つかりません: BLEアドバタイズはログ出力のみのシミュレーションになります (sudo apt install libbluetooth-dev で有効化できます)")
    target_sources(lampsmart PRIVATE components/lampsmart/src/lampsmart_sender_stub.c)
endif()

# ---------------------------------------------------------------------------
# aircon_ir (Panasonicエアコン状態ロジック; 実IR送信はシミュレーション)
# ---------------------------------------------------------------------------
add_library(aircon_ir STATIC components/aircon_ir/aircon_ir.c)
target_include_directories(aircon_ir PUBLIC components/aircon_ir/include)
target_link_libraries(aircon_ir PUBLIC esp_compat)

# ---------------------------------------------------------------------------
# room_temperature (DS18B20; x86_64版ではハードウェア非搭載のためスタブ)
# ---------------------------------------------------------------------------
add_library(room_temperature STATIC components/room_temperature/room_temperature.c)
target_include_directories(room_temperature PUBLIC components/room_temperature/include)
target_link_libraries(room_temperature PUBLIC esp_compat)

# ---------------------------------------------------------------------------
# tuya_switch (Wi-Fi/TCPローカル制御)
# ---------------------------------------------------------------------------
add_library(tuya_switch STATIC components/tuya_switch/tuya_switch.c)
target_include_directories(tuya_switch PUBLIC
    components/tuya_switch/include
    ${CJSON_INCLUDE_DIRS}
)
target_link_libraries(tuya_switch PUBLIC esp_compat ${CJSON_LIBRARIES} ${MBEDTLS_LIBRARIES})

# ---------------------------------------------------------------------------
# アプリ本体 (main/)
# ---------------------------------------------------------------------------
add_executable(alexa_esp32_x86_64_test
    main/app_main.c
    main/app_resources.c
    main/device_worker.c
    main/mqtt_protocol.c
    main/mqtt_task.c
    main/mqtt_transport.c
    main/settings_store.c
    main/status_led.c
    main/system_loop.c
    main/wifi_manager.c
)
target_include_directories(alexa_esp32_x86_64_test PRIVATE
    main
    ${CJSON_INCLUDE_DIRS}
)
target_link_libraries(alexa_esp32_x86_64_test PRIVATE
    esp_compat
    aircon_ir
    room_temperature
    lampsmart
    tuya_switch
    wolfmqtt
    wolfssl
    ${CJSON_LIBRARIES}
    Threads::Threads
    m
)
target_compile_definitions(alexa_esp32_x86_64_test PRIVATE WOLFSSL_USER_SETTINGS)

message(STATUS "======================================================")
message(STATUS " alexa-esp32 x86_64 test build (MQTT検証用移植版)")
message(STATUS "  BlueZ (実BLEアドバタイズ): ${BLUEZ_FOUND}")
message(STATUS "  ビルド後は main/sdkconfig.h を編集してから実行してください")
message(STATUS "======================================================")
