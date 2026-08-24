#include "tuya_switch.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include <arpa/inet.h>  /* 元の lwip/inet.h (htonl/ntohl) の代替: 標準POSIXヘッダ */
#include "psa/crypto.h"

#define TUYA_PREFIX                0x000055AAu
#define TUYA_SUFFIX                0x0000AA55u
#define TUYA_CMD_NEG_START         3u
#define TUYA_CMD_NEG_RESP          4u
#define TUYA_CMD_NEG_FINISH        5u
#define TUYA_CMD_CONTROL_NEW       13u
#define TUYA_CMD_DP_QUERY_NEW      16u
#define TUYA_MAX_FRAME             2048u
#define TUYA_HEADER_SIZE           16u
#define TUYA_TRAILER_SIZE          36u
#define TUYA_VERSION_HEADER_SIZE   15u
#define TUYA_MAX_SKIPPED_RESPONSES 8u
#define TUYA_DIAGNOSTIC_VERSION    "diag-v4"

typedef struct {
    int socket_fd;
    uint32_t sequence;
    const tuya_switch_config_t *config;
    uint8_t local_key[TUYA_SWITCH_LOCAL_KEY_SIZE];
    uint8_t session_key[TUYA_SWITCH_LOCAL_KEY_SIZE];
} tuya_client_t;

typedef struct {
    uint32_t command;
    uint32_t return_code;
    uint8_t *payload;
    size_t payload_length;
} tuya_message_t;

static const char *TAG = "tuya_switch";
static const uint8_t VERSION_HEADER[TUYA_VERSION_HEADER_SIZE] = {
    '3', '.', '4', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void put_u32(uint8_t *out, uint32_t value)
{
    value = htonl(value);
    memcpy(out, &value, sizeof(value));
}

static uint32_t get_u32(const uint8_t *in)
{
    uint32_t value;
    memcpy(&value, in, sizeof(value));
    return ntohl(value);
}

static bool secure_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    uint8_t difference = 0;
    for (size_t i = 0; i < size; ++i) difference |= left[i] ^ right[i];
    return difference == 0;
}

static esp_err_t hmac_sha256(const uint8_t key[TUYA_SWITCH_LOCAL_KEY_SIZE],
                             const uint8_t *input, size_t input_size,
                             uint8_t output[32])
{
    if (!input || !output) return ESP_ERR_INVALID_ARG;
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        ESP_LOGW(TAG, "PSA init for HMAC failed: %ld", (long)status);
        return ESP_FAIL;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id = 0;
    const psa_algorithm_t algorithm = PSA_ALG_HMAC(PSA_ALG_SHA_256);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, TUYA_SWITCH_LOCAL_KEY_SIZE * 8);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, algorithm);

    status = psa_import_key(&attributes, key, TUYA_SWITCH_LOCAL_KEY_SIZE,
                            &key_id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        ESP_LOGW(TAG, "PSA HMAC key import failed: %ld", (long)status);
        return ESP_FAIL;
    }

    size_t output_length = 0;
    status = psa_mac_compute(key_id, algorithm, input, input_size, output, 32,
                             &output_length);
    psa_status_t destroy_status = psa_destroy_key(key_id);
    if (status != PSA_SUCCESS || destroy_status != PSA_SUCCESS ||
        output_length != 32) {
        ESP_LOGW(TAG,
                 "PSA HMAC failed: compute=%ld destroy=%ld output=%u",
                 (long)status, (long)destroy_status,
                 (unsigned)output_length);
    }
    return status == PSA_SUCCESS && destroy_status == PSA_SUCCESS &&
                   output_length == 32
               ? ESP_OK
               : ESP_FAIL;
}

static esp_err_t aes_ecb_block(
    psa_key_id_t key_id, const uint8_t input[TUYA_SWITCH_LOCAL_KEY_SIZE],
    bool encrypt, uint8_t output[TUYA_SWITCH_LOCAL_KEY_SIZE])
{
    size_t output_length = 0;
    psa_status_t status =
        encrypt
            ? psa_cipher_encrypt(key_id, PSA_ALG_ECB_NO_PADDING, input,
                                 TUYA_SWITCH_LOCAL_KEY_SIZE, output,
                                 TUYA_SWITCH_LOCAL_KEY_SIZE, &output_length)
            : psa_cipher_decrypt(key_id, PSA_ALG_ECB_NO_PADDING, input,
                                 TUYA_SWITCH_LOCAL_KEY_SIZE, output,
                                 TUYA_SWITCH_LOCAL_KEY_SIZE, &output_length);
    if (status != PSA_SUCCESS ||
        output_length != TUYA_SWITCH_LOCAL_KEY_SIZE) {
        ESP_LOGW(TAG, "PSA AES-%s failed: status=%ld output=%u",
                 encrypt ? "encrypt" : "decrypt", (long)status,
                 (unsigned)output_length);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t aes_ecb(const uint8_t key[TUYA_SWITCH_LOCAL_KEY_SIZE],
                         const uint8_t *input, size_t input_size, bool encrypt,
                         bool padding, uint8_t *output, size_t output_size,
                         size_t *written)
{
    if (!input || !output || !written) return ESP_ERR_INVALID_ARG;
    size_t work_size = input_size;
    uint8_t pad = 0;
    if (encrypt && padding) {
        pad = (uint8_t)(TUYA_SWITCH_LOCAL_KEY_SIZE -
                        input_size % TUYA_SWITCH_LOCAL_KEY_SIZE);
        work_size += pad;
    } else if (input_size % TUYA_SWITCH_LOCAL_KEY_SIZE != 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (output_size < work_size) return ESP_ERR_INVALID_SIZE;

    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        ESP_LOGW(TAG, "PSA init for AES failed: %ld", (long)status);
        return ESP_FAIL;
    }
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id = 0;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, TUYA_SWITCH_LOCAL_KEY_SIZE * 8);
    psa_set_key_usage_flags(&attributes,
                            PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECB_NO_PADDING);
    status = psa_import_key(&attributes, key, TUYA_SWITCH_LOCAL_KEY_SIZE,
                            &key_id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        ESP_LOGW(TAG, "PSA AES key import failed: %ld", (long)status);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    for (size_t offset = 0; offset < work_size;
         offset += TUYA_SWITCH_LOCAL_KEY_SIZE) {
        uint8_t block[TUYA_SWITCH_LOCAL_KEY_SIZE];
        if (offset < input_size) {
            size_t copied = input_size - offset;
            if (copied > sizeof(block)) copied = sizeof(block);
            memcpy(block, input + offset, copied);
            if (copied < sizeof(block)) {
                memset(block + copied, pad, sizeof(block) - copied);
            }
        } else {
            memset(block, pad, sizeof(block));
        }
        ret = aes_ecb_block(key_id, block, encrypt, output + offset);
        if (ret != ESP_OK) break;
    }
    psa_status_t destroy_status = psa_destroy_key(key_id);
    if (ret != ESP_OK || destroy_status != PSA_SUCCESS) {
        ESP_LOGW(TAG, "PSA AES cleanup failed: operation=%s destroy=%ld",
                 esp_err_to_name(ret), (long)destroy_status);
        return ESP_FAIL;
    }

    if (!encrypt && padding) {
        if (work_size == 0) return ESP_ERR_INVALID_SIZE;
        pad = output[work_size - 1];
        if (pad == 0 || pad > TUYA_SWITCH_LOCAL_KEY_SIZE || pad > work_size) {
            return ESP_ERR_INVALID_CRC;
        }
        for (size_t i = 0; i < pad; ++i) {
            if (output[work_size - 1 - i] != pad) return ESP_ERR_INVALID_CRC;
        }
        work_size -= pad;
    }
    *written = work_size;
    return ESP_OK;
}

static esp_err_t send_all(int fd, const uint8_t *data, size_t size)
{
    while (size > 0) {
        ssize_t sent = send(fd, data, size, 0);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return ESP_FAIL;
        data += (size_t)sent;
        size -= (size_t)sent;
    }
    return ESP_OK;
}

static esp_err_t receive_all(int fd, uint8_t *data, size_t size,
                             ssize_t *last_result, int *last_errno)
{
    if (last_result) *last_result = 0;
    if (last_errno) *last_errno = 0;
    while (size > 0) {
        errno = 0;
        ssize_t received = recv(fd, data, size, 0);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) {
            if (last_result) *last_result = received;
            if (last_errno) *last_errno = received < 0 ? errno : 0;
            return ESP_FAIL;
        }
        data += (size_t)received;
        size -= (size_t)received;
    }
    return ESP_OK;
}

static esp_err_t build_frame(uint32_t sequence, uint32_t command,
                             const uint8_t *plain, size_t plain_size,
                             const uint8_t key[TUYA_SWITCH_LOCAL_KEY_SIZE],
                             bool add_version_header, uint8_t **frame_out,
                             size_t *frame_length)
{
    esp_err_t result = ESP_ERR_NO_MEM;
    uint8_t *input = calloc(1, TUYA_MAX_FRAME);
    uint8_t *encrypted = calloc(1, TUYA_MAX_FRAME);
    uint8_t *frame = calloc(1, TUYA_MAX_FRAME);
    if (!input || !encrypted || !frame) goto done;

    size_t input_size = 0;
    if (add_version_header) {
        memcpy(input, VERSION_HEADER, sizeof(VERSION_HEADER));
        input_size = sizeof(VERSION_HEADER);
    }
    if (input_size + plain_size > TUYA_MAX_FRAME -
                                      TUYA_SWITCH_LOCAL_KEY_SIZE) {
        result = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    memcpy(input + input_size, plain, plain_size);
    input_size += plain_size;

    size_t encrypted_size = 0;
    result = aes_ecb(key, input, input_size, true, true, encrypted,
                     TUYA_MAX_FRAME, &encrypted_size);
    if (result != ESP_OK) goto done;
    size_t total = TUYA_HEADER_SIZE + encrypted_size + TUYA_TRAILER_SIZE;
    if (total > TUYA_MAX_FRAME) {
        result = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    put_u32(frame, TUYA_PREFIX);
    put_u32(frame + 4, sequence);
    put_u32(frame + 8, command);
    put_u32(frame + 12, (uint32_t)(encrypted_size + TUYA_TRAILER_SIZE));
    memcpy(frame + TUYA_HEADER_SIZE, encrypted, encrypted_size);
    result = hmac_sha256(key, frame, TUYA_HEADER_SIZE + encrypted_size,
                         frame + TUYA_HEADER_SIZE + encrypted_size);
    if (result != ESP_OK) goto done;
    put_u32(frame + total - 4, TUYA_SUFFIX);
    *frame_out = frame;
    *frame_length = total;
    frame = NULL;
done:
    free(input);
    free(encrypted);
    free(frame);
    return result;
}

static esp_err_t crypto_frame_self_test(void)
{
    static const uint8_t key[TUYA_SWITCH_LOCAL_KEY_SIZE] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    static const uint8_t nonce[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t expected_ciphertext[32] = {
        0x27, 0x9f, 0xb7, 0x4a, 0x75, 0x72, 0x13, 0x5e,
        0x8f, 0x9b, 0x8e, 0xf6, 0xd1, 0xee, 0xe0, 0x03,
        0x00, 0x65, 0x7e, 0xa1, 0x40, 0x65, 0x5a, 0x44,
        0x78, 0x27, 0x47, 0x70, 0x5d, 0x42, 0x2f, 0xad
    };
    static const uint8_t expected_hmac[32] = {
        0x01, 0x91, 0x59, 0xeb, 0x3f, 0x3c, 0x31, 0x93,
        0x82, 0x05, 0xdf, 0x65, 0xa1, 0x07, 0x61, 0x67,
        0x3d, 0x31, 0xae, 0x54, 0xa7, 0x96, 0x21, 0x4f,
        0x31, 0xdf, 0x7c, 0x18, 0x93, 0x6f, 0xea, 0xe5
    };
    uint8_t *frame = NULL;
    size_t frame_size = 0;
    esp_err_t ret = build_frame(1, TUYA_CMD_NEG_START, nonce, sizeof(nonce),
                                key, false, &frame, &frame_size);
    bool valid = ret == ESP_OK && frame_size == 84 &&
                 get_u32(frame) == TUYA_PREFIX &&
                 get_u32(frame + 4) == 1 &&
                 get_u32(frame + 8) == TUYA_CMD_NEG_START &&
                 get_u32(frame + 12) == 68 &&
                 secure_equal(frame + TUYA_HEADER_SIZE,
                              expected_ciphertext,
                              sizeof(expected_ciphertext)) &&
                 secure_equal(frame + TUYA_HEADER_SIZE +
                                  sizeof(expected_ciphertext),
                              expected_hmac, sizeof(expected_hmac)) &&
                 get_u32(frame + frame_size - 4) == TUYA_SUFFIX;
    free(frame);
    return valid ? ESP_OK : ESP_FAIL;
}

static esp_err_t send_encrypted(tuya_client_t *client, uint32_t command,
                                const uint8_t *payload, size_t payload_size,
                                const uint8_t key[TUYA_SWITCH_LOCAL_KEY_SIZE],
                                bool add_version_header)
{
    uint8_t *frame = NULL;
    size_t frame_size = 0;
    esp_err_t ret = build_frame(client->sequence++, command, payload,
                                payload_size, key, add_version_header, &frame,
                                &frame_size);
    if (ret == ESP_OK) ret = send_all(client->socket_fd, frame, frame_size);
    free(frame);
    return ret;
}

static void free_message(tuya_message_t *message)
{
    free(message->payload);
    memset(message, 0, sizeof(*message));
}

static esp_err_t receive_frame(int fd,
                               const uint8_t key[TUYA_SWITCH_LOCAL_KEY_SIZE],
                               tuya_message_t *message)
{
    uint8_t header[TUYA_HEADER_SIZE];
    ssize_t receive_result = 0;
    int receive_errno = 0;
    memset(message, 0, sizeof(*message));
    esp_err_t ret = receive_all(fd, header, sizeof(header), &receive_result,
                                &receive_errno);
    if (ret != ESP_OK) {
        if (receive_result == 0) {
            ESP_LOGW(TAG, "Tuya header receive failed: peer closed connection");
        } else {
            ESP_LOGW(TAG,
                     "Tuya header receive failed: recv=%ld errno=%d (%s)",
                     (long)receive_result, receive_errno,
                     strerror(receive_errno));
        }
        return ret;
    }
    uint32_t prefix = get_u32(header);
    if (prefix != TUYA_PREFIX) {
        ESP_LOGW(TAG, "Tuya prefix mismatch: 0x%08lx",
                 (unsigned long)prefix);
        return ESP_FAIL;
    }
    uint32_t length = get_u32(header + 12);
    size_t total = TUYA_HEADER_SIZE + (size_t)length;
    if (length < TUYA_TRAILER_SIZE || total > TUYA_MAX_FRAME) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *frame = malloc(total);
    if (!frame) return ESP_ERR_NO_MEM;
    memcpy(frame, header, sizeof(header));
    ret = receive_all(fd, frame + TUYA_HEADER_SIZE, length, &receive_result,
                      &receive_errno);
    if (ret != ESP_OK) {
        if (receive_result == 0) {
            ESP_LOGW(TAG,
                     "Tuya body receive failed: length=%lu peer closed connection",
                     (unsigned long)length);
        } else {
            ESP_LOGW(TAG,
                     "Tuya body receive failed: length=%lu recv=%ld errno=%d (%s)",
                     (unsigned long)length, (long)receive_result,
                     receive_errno, strerror(receive_errno));
        }
        free(frame);
        return ret;
    }
    uint32_t suffix = get_u32(frame + total - 4);
    if (suffix != TUYA_SUFFIX) {
        ESP_LOGW(TAG, "Tuya suffix mismatch: 0x%08lx",
                 (unsigned long)suffix);
        free(frame);
        return ESP_FAIL;
    }

    size_t protected_size = total - TUYA_TRAILER_SIZE;
    uint8_t expected[32];
    ret = hmac_sha256(key, frame, protected_size, expected);
    if (ret != ESP_OK ||
        !secure_equal(expected, frame + protected_size, sizeof(expected))) {
        ESP_LOGW(TAG, "Tuya frame HMAC verification failed: crypto=%s",
                 esp_err_to_name(ret));
        free(frame);
        return ESP_ERR_INVALID_CRC;
    }
    size_t payload_size = protected_size - TUYA_HEADER_SIZE;
    if (payload_size < 4) {
        free(frame);
        return ESP_ERR_INVALID_SIZE;
    }
    message->command = get_u32(frame + 8);
    message->return_code = get_u32(frame + TUYA_HEADER_SIZE);
    message->payload_length = payload_size - 4;
    if (message->payload_length) {
        message->payload = malloc(message->payload_length);
        if (!message->payload) {
            free(frame);
            return ESP_ERR_NO_MEM;
        }
        memcpy(message->payload, frame + TUYA_HEADER_SIZE + 4,
               message->payload_length);
    }
    free(frame);
    return ESP_OK;
}

static esp_err_t decrypt_message(const tuya_message_t *message,
                                 const uint8_t key[TUYA_SWITCH_LOCAL_KEY_SIZE],
                                 uint8_t *plain, size_t plain_size,
                                 size_t *plain_length)
{
    if (message->payload_length == 0) {
        *plain_length = 0;
        return ESP_OK;
    }
    esp_err_t ret = aes_ecb(key, message->payload, message->payload_length,
                            false, true, plain, plain_size, plain_length);
    if (ret != ESP_OK) {
        ret = aes_ecb(key, message->payload, message->payload_length, false,
                      false, plain, plain_size, plain_length);
    }
    if (ret != ESP_OK) return ret;
    if (*plain_length >= TUYA_VERSION_HEADER_SIZE &&
        memcmp(plain, VERSION_HEADER, 3) == 0) {
        memmove(plain, plain + TUYA_VERSION_HEADER_SIZE,
                *plain_length - TUYA_VERSION_HEADER_SIZE);
        *plain_length -= TUYA_VERSION_HEADER_SIZE;
    }
    return ESP_OK;
}

static esp_err_t receive_json(tuya_client_t *client, char *json,
                              size_t json_size)
{
    uint8_t *plain = malloc(TUYA_MAX_FRAME);
    if (!plain) return ESP_ERR_NO_MEM;
    esp_err_t ret = ESP_FAIL;
    for (unsigned skipped = 0; skipped < TUYA_MAX_SKIPPED_RESPONSES;
         ++skipped) {
        tuya_message_t response;
        ret = receive_frame(client->socket_fd, client->session_key, &response);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "status frame receive failed: %s",
                     esp_err_to_name(ret));
            break;
        }
        if (response.return_code != 0) {
            ESP_LOGW(TAG, "status response error: cmd=%lu return=%lu",
                     (unsigned long)response.command,
                     (unsigned long)response.return_code);
            free_message(&response);
            ret = ESP_FAIL;
            break;
        }
        size_t plain_size = 0;
        ret = decrypt_message(&response, client->session_key, plain,
                              TUYA_MAX_FRAME, &plain_size);
        uint32_t response_command = response.command;
        free_message(&response);
        if (ret != ESP_OK) break;
        size_t json_offset = 0;
        while (json_offset < plain_size && plain[json_offset] != '{' &&
               plain[json_offset] != '[') {
            json_offset++;
        }
        if (json_offset == plain_size) {
            ESP_LOGW(TAG,
                     "skipping non-JSON response: cmd=%lu plain_size=%u",
                     (unsigned long)response_command, (unsigned)plain_size);
            ret = ESP_ERR_NOT_FOUND;
            continue;
        }
        size_t json_length = plain_size - json_offset;
        if (json_length + 1 > json_size) {
            ret = ESP_ERR_INVALID_SIZE;
            break;
        }
        memcpy(json, plain + json_offset, json_length);
        json[json_length] = '\0';
        ret = ESP_OK;
        break;
    }
    free(plain);
    return ret;
}

static esp_err_t connect_device(tuya_client_t *client)
{
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *addresses = NULL;
    char service[8];
    snprintf(service, sizeof(service), "%u", client->config->port);
    if (getaddrinfo(client->config->ip, service, &hints, &addresses) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    struct timeval timeout = {
        .tv_sec = client->config->timeout_ms / 1000,
        .tv_usec = (client->config->timeout_ms % 1000) * 1000,
    };
    for (struct addrinfo *address = addresses; address;
         address = address->ai_next) {
        client->socket_fd = socket(address->ai_family, address->ai_socktype,
                                   address->ai_protocol);
        if (client->socket_fd < 0) continue;
        (void)setsockopt(client->socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                         sizeof(timeout));
        (void)setsockopt(client->socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                         sizeof(timeout));
        if (connect(client->socket_fd, address->ai_addr,
                    address->ai_addrlen) == 0) {
            break;
        }
        close(client->socket_fd);
        client->socket_fd = -1;
    }
    freeaddrinfo(addresses);
    return client->socket_fd >= 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t negotiate(tuya_client_t *client, const char **failure_stage)
{
    uint8_t local_nonce[16];
    uint8_t remote_nonce[16];
    uint8_t decrypted[64];
    uint8_t expected[32];
    uint8_t finish_hmac[32];
    uint8_t mixed[16];
    *failure_stage = "crypto/frame self-test";
    esp_err_t ret = crypto_frame_self_test();
    ESP_LOGI(TAG, "crypto/frame self-test: %s",
             ret == ESP_OK ? "PASS" : "FAIL");
    if (ret != ESP_OK) return ret;
    esp_fill_random(local_nonce, sizeof(local_nonce));
    *failure_stage = "negotiation start send";
    ret = send_encrypted(client, TUYA_CMD_NEG_START, local_nonce,
                         sizeof(local_nonce), client->local_key, false);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "negotiation start frame sent: sequence=1 bytes=84");
    }
    tuya_message_t response;
    if (ret == ESP_OK) *failure_stage = "negotiation response receive";
    if (ret != ESP_OK ||
        (ret = receive_frame(client->socket_fd, client->local_key,
                             &response)) != ESP_OK) {
        return ret;
    }
    size_t decrypted_size = 0;
    if (response.command != TUYA_CMD_NEG_RESP) {
        *failure_stage = "negotiation response command";
        ESP_LOGW(TAG, "unexpected negotiation command: %lu",
                 (unsigned long)response.command);
        free_message(&response);
        return ESP_FAIL;
    }
    *failure_stage = "remote nonce decrypt";
    ret = aes_ecb(client->local_key, response.payload,
                  response.payload_length, false, true, decrypted,
                  sizeof(decrypted), &decrypted_size);
    ESP_LOGI(TAG, "negotiation response: payload=%u decrypted=%u result=%s",
             (unsigned)response.payload_length, (unsigned)decrypted_size,
             esp_err_to_name(ret));
    free_message(&response);
    if (ret != ESP_OK || decrypted_size < 48) return ESP_FAIL;
    memcpy(remote_nonce, decrypted, sizeof(remote_nonce));
    *failure_stage = "local nonce HMAC verify";
    ret = hmac_sha256(client->local_key, local_nonce, sizeof(local_nonce),
                      expected);
    if (ret != ESP_OK ||
        !secure_equal(expected, decrypted + sizeof(remote_nonce), 32)) {
        return ESP_ERR_INVALID_CRC;
    }
    *failure_stage = "finish HMAC generation";
    ret = hmac_sha256(client->local_key, remote_nonce, sizeof(remote_nonce),
                      finish_hmac);
    if (ret != ESP_OK) return ret;
    *failure_stage = "negotiation finish send";
    ret = send_encrypted(client, TUYA_CMD_NEG_FINISH, finish_hmac,
                         sizeof(finish_hmac), client->local_key, false);
    if (ret != ESP_OK) return ret;
    for (size_t i = 0; i < sizeof(mixed); ++i) {
        mixed[i] = local_nonce[i] ^ remote_nonce[i];
    }
    size_t session_size = 0;
    *failure_stage = "session key derivation";
    ret = aes_ecb(client->local_key, mixed, sizeof(mixed), true, false,
                  client->session_key, sizeof(client->session_key),
                  &session_size);
    return ret == ESP_OK && session_size == sizeof(client->session_key)
               ? ESP_OK
               : ESP_FAIL;
}

static esp_err_t open_client(tuya_client_t *client,
                             const tuya_switch_config_t *config,
                             const char **failure_stage)
{
    memset(client, 0, sizeof(*client));
    client->socket_fd = -1;
    client->sequence = 1;
    client->config = config;
    memcpy(client->local_key, config->local_key, sizeof(client->local_key));
    *failure_stage = "TCP connect";
    esp_err_t ret = connect_device(client);
    if (ret == ESP_OK) ret = negotiate(client, failure_stage);
    if (ret != ESP_OK && client->socket_fd >= 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
    }
    return ret;
}

static void close_client(tuya_client_t *client)
{
    if (client->socket_fd >= 0) close(client->socket_fd);
    client->socket_fd = -1;
}

static esp_err_t get_status_json(tuya_client_t *client, char *json,
                                 size_t json_size)
{
    static const uint8_t query[] = "{}";
    esp_err_t ret = send_encrypted(client, TUYA_CMD_DP_QUERY_NEW, query,
                                   sizeof(query) - 1, client->session_key,
                                   false);
    return ret == ESP_OK ? receive_json(client, json, json_size) : ret;
}

static esp_err_t get_power_open(tuya_client_t *client, bool *power)
{
    char *json = malloc(TUYA_MAX_FRAME);
    if (!json) return ESP_ERR_NO_MEM;
    esp_err_t ret = get_status_json(client, json, TUYA_MAX_FRAME);
    if (ret == ESP_OK) {
        cJSON *root = cJSON_Parse(json);
        cJSON *dps = root ? cJSON_GetObjectItemCaseSensitive(root, "dps")
                          : NULL;
        cJSON *item = dps ? cJSON_GetObjectItemCaseSensitive(dps, "1") : NULL;
        if (!cJSON_IsBool(item)) {
            ret = ESP_ERR_INVALID_RESPONSE;
        } else {
            *power = cJSON_IsTrue(item);
        }
        cJSON_Delete(root);
    }
    free(json);
    return ret;
}

static esp_err_t set_power_open(tuya_client_t *client, bool power)
{
    char payload[256];
    int length = snprintf(
        payload, sizeof(payload),
        "{\"protocol\":5,\"t\":%lld,\"data\":{\"dps\":{\"1\":%s}}}",
        (long long)time(NULL), power ? "true" : "false");
    if (length < 0 || (size_t)length >= sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t ret = send_encrypted(
        client, TUYA_CMD_CONTROL_NEW, (const uint8_t *)payload, (size_t)length,
        client->session_key, true);
    tuya_message_t response;
    if (ret != ESP_OK ||
        (ret = receive_frame(client->socket_fd, client->session_key,
                             &response)) != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "power response: cmd=%lu return=%lu payload=%u",
             (unsigned long)response.command,
             (unsigned long)response.return_code,
             (unsigned)response.payload_length);
    if (response.return_code != 0) ret = ESP_FAIL;
    if (ret == ESP_OK && response.payload_length > 0) {
        uint8_t *ignored = malloc(TUYA_MAX_FRAME);
        if (!ignored) {
            ret = ESP_ERR_NO_MEM;
        } else {
            size_t ignored_size = 0;
            ret = decrypt_message(&response, client->session_key, ignored,
                                  TUYA_MAX_FRAME, &ignored_size);
            free(ignored);
        }
    }
    free_message(&response);
    return ret;
}

esp_err_t tuya_switch_validate_config(const tuya_switch_config_t *config)
{
    if (!config || !config->ip || !config->ip[0] || !config->device_id ||
        !config->device_id[0] || !config->local_key ||
        strlen(config->local_key) != TUYA_SWITCH_LOCAL_KEY_SIZE ||
        config->port == 0 || config->timeout_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t tuya_switch_get_power(const tuya_switch_config_t *config, bool *power)
{
    if (!power || tuya_switch_validate_config(config) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "driver diagnostic version: %s", TUYA_DIAGNOSTIC_VERSION);
    tuya_client_t client;
    const char *failure_stage = "client open";
    esp_err_t ret = open_client(&client, config, &failure_stage);
    if (ret == ESP_OK) {
        failure_stage = "status query/parse";
        ret = get_power_open(&client, power);
    }
    close_client(&client);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "status failed at %s: %s", failure_stage,
                 esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t tuya_switch_set_power_verified(const tuya_switch_config_t *config,
                                         bool requested_power,
                                         bool *actual_power)
{
    if (!actual_power || tuya_switch_validate_config(config) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    tuya_client_t command_client;
    const char *command_failure_stage = "client open";
    esp_err_t command_ret =
        open_client(&command_client, config, &command_failure_stage);
    if (command_ret == ESP_OK) {
        command_failure_stage = "power command/ack";
        command_ret = set_power_open(&command_client, requested_power);
    }
    if (command_ret != ESP_OK) {
        ESP_LOGW(TAG, "power command failed at %s: %s",
                 command_failure_stage, esp_err_to_name(command_ret));
    }
    close_client(&command_client);

    /*
     * Verify on a fresh Tuya 3.4 session.  The control connection can still
     * contain an empty ACK or an asynchronous status notification; reopening
     * prevents those frames from being mistaken for the query response.
     * Explicit ON/OFF is idempotent, so the verified state is authoritative
     * even if parsing the command ACK failed.
     */
    tuya_client_t verify_client;
    const char *verify_failure_stage = "verification client open";
    esp_err_t ret =
        open_client(&verify_client, config, &verify_failure_stage);
    if (ret == ESP_OK) {
        verify_failure_stage = "fresh-session status verification";
        ret = get_power_open(&verify_client, actual_power);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "power verification failed at %s: %s",
                 verify_failure_stage, esp_err_to_name(ret));
    }
    close_client(&verify_client);
    if (ret == ESP_OK && *actual_power != requested_power) {
        ret = ESP_ERR_INVALID_STATE;
    }
    if (ret == ESP_OK && command_ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "command ACK failed, but fresh-session state verification succeeded");
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "power verification failed: %s", esp_err_to_name(ret));
    }
    return ret;
}


