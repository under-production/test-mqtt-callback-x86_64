#include "mqtt_transport.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_log.h"
#include "sdkconfig.h"
#include <wolfssl/error-ssl.h>
#include <wolfssl/ssl.h>
#include "esp_timer.h"

#define INVALID_SOCKET_FD (-1)

static const char *TAG = "mqtt_transport";
static MqttNet s_net;
static int s_socket = INVALID_SOCKET_FD;

/* オリジナル(ESP32)版は emqx_ca.pem をビルド時にバイナリ埋め込み
 * (_binary_emqx_ca_pem_start/end) していたが、x86_64版では
 * CONFIG_ALEXA_CA_CERT_PATH で指定したファイルを実行時にディスクから
 * 読み込む方式に変更した。証明書の中身自体は変えていない。 */

static void set_timeout(struct timeval *timeout, int timeout_ms)
{
    timeout->tv_sec = timeout_ms / 1000;
    timeout->tv_usec = (timeout_ms % 1000) * 1000;
    if (timeout->tv_sec < 0 ||
        (timeout->tv_sec == 0 && timeout->tv_usec <= 0)) {
        timeout->tv_sec = 0;
        timeout->tv_usec = 1000;
    }
}

static int net_connect(void *context, const char *host, word16 port,
                       int timeout_ms)
{
    int *socket_fd = context;
    if (!socket_fd || !host) return MQTT_CODE_ERROR_BAD_ARG;
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *addresses = NULL;
    char port_text[8];
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);
    int gai = getaddrinfo(host, port_text, &hints, &addresses);
    if (gai != 0 || !addresses) {
        ESP_LOGE(TAG, "getaddrinfo failed: host=%s error=%d", host, gai);
        return MQTT_CODE_ERROR_NETWORK;
    }
    int result = MQTT_CODE_ERROR_NETWORK;
    for (struct addrinfo *address = addresses; address; address = address->ai_next) {
        int fd = socket(address->ai_family, address->ai_socktype,
                        address->ai_protocol);
        if (fd < 0) continue;
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(fd);
            continue;
        }
        struct timeval timeout;
        set_timeout(&timeout, timeout_ms);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        int connect_result = connect(fd, address->ai_addr, address->ai_addrlen);
        if (connect_result < 0 && errno == EINPROGRESS) {
            fd_set write_set;
            FD_ZERO(&write_set);
            FD_SET(fd, &write_set);
            connect_result =
                select(fd + 1, NULL, &write_set, NULL, &timeout);
            if (connect_result > 0) {
                int socket_error = 0;
                socklen_t error_size = sizeof(socket_error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                               &error_size) < 0 ||
                    socket_error != 0) {
                    errno = socket_error ? socket_error : errno;
                    connect_result = -1;
                }
            }
        }
        if (connect_result >= 0 && fcntl(fd, F_SETFL, flags) == 0) {
            *socket_fd = fd;
            result = MQTT_CODE_SUCCESS;
            ESP_LOGI(TAG, "TCP connected: %s:%u", host, (unsigned)port);
            break;
        }
        ESP_LOGW(TAG, "TCP connect failed: host=%s port=%u errno=%d",
                 host, (unsigned)port, errno);
        close(fd);
    }
    freeaddrinfo(addresses);
    return result;
}

static int net_read(void *context, byte *buffer, int buffer_len,
                    int timeout_ms)
{
    int *socket_fd = context;
    if (!socket_fd || *socket_fd < 0 || !buffer) return MQTT_CODE_ERROR_BAD_ARG;
    struct timeval timeout;
    set_timeout(&timeout, timeout_ms);
    setsockopt(*socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    int used = 0;
    while (used < buffer_len) {
        int ret = recv(*socket_fd, &buffer[used], buffer_len - used, 0);
/*
        ESP_LOGI(
            "MQTT_RX",
            "recv ret=%d used=%d request=%d time=%lld",
            ret,
            used,
            buffer_len - used,
            esp_timer_get_time() / 1000
        );                
*/
        if (ret > 0) { used += ret; continue; }
        if (ret == 0) return used ? used : MQTT_CODE_ERROR_NETWORK;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        return MQTT_CODE_ERROR_NETWORK;
    }
    return used ? used : MQTT_CODE_ERROR_TIMEOUT;
}

static int net_write(void *context, const byte *buffer, int buffer_len,
                     int timeout_ms)
{
    int *socket_fd = context;
    if (!socket_fd || *socket_fd < 0 || !buffer) return MQTT_CODE_ERROR_BAD_ARG;
    struct timeval timeout;
    set_timeout(&timeout, timeout_ms);
    setsockopt(*socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    int used = 0;
    while (used < buffer_len) {
        int ret = send(*socket_fd, &buffer[used], buffer_len - used, 0);
        if (ret > 0) { used += ret; continue; }
        if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        return MQTT_CODE_ERROR_NETWORK;
    }
    return used ? used : MQTT_CODE_ERROR_TIMEOUT;
}

static int net_disconnect(void *context)
{
    int *socket_fd = context;
    if (!socket_fd) return MQTT_CODE_ERROR_BAD_ARG;
    if (*socket_fd >= 0) {
        close(*socket_fd);
        *socket_fd = INVALID_SOCKET_FD;
    }
    return MQTT_CODE_SUCCESS;
}

static int tls_verify_callback(int preverify, WOLFSSL_X509_STORE_CTX *store)
{
    if (!preverify) {
        ESP_LOGE(TAG, "TLS verify failed: error=%d", store ? store->error : -1);
    }
    return preverify;
}

static int load_ca_cert(unsigned char **out_buffer, long *out_length)
{
    FILE *file = fopen(CONFIG_ALEXA_CA_CERT_PATH, "rb");
    if (!file) {
        ESP_LOGE(TAG, "CA証明書ファイルを開けません: %s",
                 CONFIG_ALEXA_CA_CERT_PATH);
        return -1;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length <= 0) { fclose(file); return -1; }
    unsigned char *buffer = malloc((size_t)length);
    if (!buffer) { fclose(file); return -1; }
    size_t read_len = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (read_len != (size_t)length) { free(buffer); return -1; }
    *out_buffer = buffer;
    *out_length = length;
    return 0;
}

void mqtt_transport_init(void)
{
    memset(&s_net, 0, sizeof(s_net));
    s_socket = INVALID_SOCKET_FD;
    s_net.connect = net_connect;
    s_net.read = net_read;
    s_net.write = net_write;
    s_net.disconnect = net_disconnect;
    s_net.context = &s_socket;
}

MqttNet *mqtt_transport_net(void) { return &s_net; }

int mqtt_transport_tls_callback(MqttClient *client)
{
    unsigned char *ca_buffer = NULL;
    long ca_length = 0;
    if (load_ca_cert(&ca_buffer, &ca_length) != 0) return WOLFSSL_FAILURE;
    client->tls.ctx = wolfSSL_CTX_new(wolfTLSv1_2_client_method());
    if (!client->tls.ctx) { free(ca_buffer); return WOLFSSL_FAILURE; }
    wolfSSL_CTX_set_verify(client->tls.ctx, SSL_VERIFY_PEER,
                           tls_verify_callback);
    int ret = wolfSSL_CTX_load_verify_buffer(client->tls.ctx,
                                              ca_buffer, (long)ca_length,
                                              WOLFSSL_FILETYPE_PEM);
    free(ca_buffer);
    if (ret != WOLFSSL_SUCCESS) {
        ESP_LOGE(TAG, "CA load failed: %d", ret);
        return ret;
    }
    ret = wolfSSL_CTX_UseSNI(client->tls.ctx, WOLFSSL_SNI_HOST_NAME,
                             CONFIG_ALEXA_EMQX_HOST,
                             (unsigned short)strlen(CONFIG_ALEXA_EMQX_HOST));
    if (ret != WOLFSSL_SUCCESS) ESP_LOGE(TAG, "SNI setup failed: %d", ret);
    return ret;
}

void mqtt_transport_force_close(void)
{
    (void)net_disconnect(&s_socket);
}


