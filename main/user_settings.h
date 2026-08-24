#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

/* x86_64 Linux移植版のwolfSSL/wolfMQTT設定。
 *
 * オリジナル(ESP32)版との差分:
 *   - ESP32ハードウェア暗号アクセラレータ関連の定義(WOLFSSL_ESP32,
 *     NO_WOLFSSL_ESP32_CRYPT_*)を削除。Linux版はソフトウェア実装を使う。
 *   - NO_DEV_RANDOM を削除。Linuxには /dev/random が存在するため、
 *     wolfCryptの標準乱数生成経路をそのまま使う(ESP32では独自の
 *     ハードウェアRNGを使うためこの定義が必要だった)。
 * それ以外のTLS/暗号方式・MQTTの設定はオリジナルと完全に同一。
 */
#define WC_NO_HARDEN
#define WOLFSSL_SMALL_STACK

#ifndef ENABLE_MQTT_TLS
#define ENABLE_MQTT_TLS
#endif

#define WOLFMQTT_NO_STDIO

#define NO_FILESYSTEM
#define NO_WRITEV
#define HAVE_TLS_EXTENSIONS
#define HAVE_SNI
#define WOLFSSL_ALT_CERT_CHAINS
#define HAVE_ECC
#define HAVE_SUPPORTED_CURVES
#define HAVE_AESGCM
#define WOLFSSL_SHA384

#define NO_TLS13
#define NO_DTLS
#define NO_OLD_TLS
#define NO_WOLFSSL_SERVER
#define NO_DSA
#define NO_DH
#define NO_PSK
#define NO_RC4
#define NO_DES3
#define NO_HC128
#define NO_RABBIT
#define NO_MD4
#define NO_MD5
#define NO_PWDBASED
#define NO_PKCS12
#define NO_OCSP
#define NO_CRL
#define NO_CERTS_CACHE
#define NO_SESSION_CACHE
#define NO_ERROR_STRINGS

#endif


