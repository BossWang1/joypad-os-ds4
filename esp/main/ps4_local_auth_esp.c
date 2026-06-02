// ps4_local_auth_esp.c - ESP32-S3 implementation of PS4 local RSA-PSS auth
//
// Implements the public interface declared in
// src/usb/usbd/modes/ps4_local_auth.h, so ps4_mode.c dispatch points work
// unchanged. The Pico version (src/usb/usbd/modes/ps4_local_auth.c) is
// excluded from the ESP build; this file is the ESP-side parallel.
//
// Differences from the Pico version:
//   - Key material loaded from PEM via mbedtls_pk_parse_key (not raw N/P/Q/E)
//   - Serial.txt is an ASCII hex string converted to 16 raw bytes
//     (right-aligned, zero-padded), matching the patched form of
//     esp32s3_xbox_adapter-master/main/hid_ps4_driver.c
//   - RNG is esp_fill_random (true hardware RNG) — no xorshift fallback
//   - Sign offload is a FreeRTOS task pinned to APP_CPU, kicked via
//     xTaskNotifyGive (mirrors Pico's Core 1 + __sev/__wfe pattern)
//   - No flash event log; logging is a printf gate
//
// SPDX-License-Identifier: Apache-2.0

#include "usb/usbd/modes/ps4_local_auth.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"

// ESP-IDF v6.0 ships Mbed TLS 4.0.0, which moved several legacy headers
// under mbedtls/private/ (TF-PSA-Crypto reorg). We still use the legacy
// rsa/sha256/bignum APIs because the wire format is byte-for-byte fixed by
// the Pico implementation; MBEDTLS_ALLOW_PRIVATE_ACCESS unlocks the
// MBEDTLS_PRIVATE(N/E/padding/hash_id) struct fields the sign path touches.
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "mbedtls/private/rsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/private/pk_private.h"   // mbedtls_pk_rsa() accessor (4.x)
#include "mbedtls/private/sha256.h"
#include "mbedtls/private/bignum.h"
#include "mbedtls/md.h"

#define TAG "ps4_auth"

// ============================================================================
// Embedded key material (ESP-IDF EMBED_TXTFILES)
// ============================================================================

extern const unsigned char key_pem_start[]    asm("_binary_key_pem_start");
extern const unsigned char key_pem_end[]      asm("_binary_key_pem_end");
extern const unsigned char ps4_serial_start[] asm("_binary_serial_txt_start");
extern const unsigned char ps4_serial_end[]   asm("_binary_serial_txt_end");
extern const unsigned char ps4_sig_start[]    asm("_binary_sig_bin_start");
extern const unsigned char ps4_sig_end[]      asm("_binary_sig_bin_end");

// ============================================================================
// Constants (must match Pico wire format)
// ============================================================================

#define NONCE_SIZE          280   // 5 pages × 56 bytes
#define SIG_RESPONSE_SIZE  1064   // 19 pages × 56 bytes
#define PAGE_SIZE            56
#define NONCE_PAGES           5
#define SIG_PAGES            19

#define RSP_RSA_SIG_OFFSET      0
#define RSP_SERIAL_OFFSET     256
#define RSP_N_OFFSET          272
#define RSP_E_OFFSET          528
#define RSP_DEVICE_SIG_OFFSET 784
#define RSP_PAD_OFFSET       1040

// ============================================================================
// State
// ============================================================================

static mbedtls_pk_context s_pk;
static bool               s_rsa_valid = false;

static uint8_t s_serial_binary[16];        // hex-converted from serial.txt

// Nonce accumulation (main task only)
static uint8_t s_nonce[NONCE_SIZE];
static uint8_t s_nonce_pages_received = 0;
static uint8_t s_nonce_id = 0;

// Snapshot handed to sign task (so a new nonce can't corrupt in-flight signing)
static uint8_t s_sign_nonce[NONCE_SIZE];

// Cross-task signaling — volatile because main task and sign task run on
// different CPUs
static volatile bool s_signing_requested = false;
static volatile bool s_signing_inflight  = false;
static volatile bool s_signature_ready   = false;
static volatile int  s_sign_ret          = 0;

// Response (written by sign task, read by main task after s_signature_ready)
static uint8_t s_response[SIG_RESPONSE_SIZE];

// Page cursor (sequential GET_REPORT 0xF1 from console — main task only)
static uint8_t s_page_cursor = 0;

// Sign task handle (created lazily in init)
static TaskHandle_t s_sign_task = NULL;

// Logging toggle (no flash event log on ESP — just gates printf)
static bool s_log_enabled = false;

// Forward decl
static void sign_task_fn(void *arg);

// ============================================================================
// CRC32 (IEEE 802.3, poly 0xEDB88320) — must match Pico wire format byte for byte
// ============================================================================

static uint32_t ps4_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else         crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

// ============================================================================
// RNG
// ============================================================================

static int rng_fn(void *ctx, unsigned char *out, size_t len)
{
    (void)ctx;
    esp_fill_random(out, len);
    return 0;
}

// ============================================================================
// Serial hex string → 16-byte binary (right-aligned, zero-padded front)
// Reproduced from esp32s3_xbox_adapter-master/diff.patch
// ============================================================================

static uint8_t hex_char_to_int(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static void hex_string_to_binary(const unsigned char *hex_str, size_t hex_len,
                                 uint8_t *output, size_t output_size)
{
    size_t output_idx = 0;
    size_t i = 0;

    while (i < hex_len && output_idx < output_size) {
        char c = (char)hex_str[i];
        bool is_hex = (c >= '0' && c <= '9') ||
                      (c >= 'A' && c <= 'F') ||
                      (c >= 'a' && c <= 'f');
        if (!is_hex) { i++; continue; }
        if (i + 1 < hex_len) {
            uint8_t hi = hex_char_to_int(hex_str[i]);
            uint8_t lo = hex_char_to_int(hex_str[i + 1]);
            output[output_idx++] = (hi << 4) | lo;
            i += 2;
        } else {
            i++;
        }
    }

    // Right-align: shift parsed bytes to end of buffer, zero-pad the front.
    if (output_idx < output_size) {
        size_t data_start = output_size - output_idx;
        for (int j = (int)output_idx - 1; j >= 0; j--) {
            output[data_start + j] = output[j];
        }
        for (size_t j = 0; j < data_start; j++) {
            output[j] = 0;
        }
    }
}

// ============================================================================
// Initialization
// ============================================================================

bool ps4_local_auth_init(void)
{
    s_rsa_valid           = false;
    s_signing_requested   = false;
    s_signing_inflight    = false;
    s_signature_ready     = false;
    s_sign_ret            = 0;
    s_nonce_pages_received = 0;
    s_page_cursor         = 0;

    size_t key_len    = (size_t)(key_pem_end    - key_pem_start);
    size_t serial_len = (size_t)(ps4_serial_end - ps4_serial_start);
    size_t sig_len    = (size_t)(ps4_sig_end    - ps4_sig_start);

    // Placeholders ship empty so the build succeeds without real key material.
    // Detect unprovisioned state and bail — ps4_mode.c then falls through to
    // the zero-fill path, controller enumerates but auth fails.
    if (key_len < 100 || serial_len < 2 || sig_len < 256) {
        ESP_LOGW(TAG, "key/serial/sig not provisioned"
                 " (key=%u, serial=%u, sig=%u) — local auth disabled",
                 (unsigned)key_len, (unsigned)serial_len, (unsigned)sig_len);
        return false;
    }

    memset(s_serial_binary, 0, sizeof(s_serial_binary));
    hex_string_to_binary(ps4_serial_start, serial_len,
                         s_serial_binary, sizeof(s_serial_binary));

    // Free any previous context from a reload cycle
    mbedtls_pk_free(&s_pk);
    mbedtls_pk_init(&s_pk);

    // Mbed TLS 4.0 dropped the trailing (f_rng, p_rng) args from
    // mbedtls_pk_parse_key — the parser no longer needs RNG.
    int ret = mbedtls_pk_parse_key(&s_pk,
                                   key_pem_start, key_len,
                                   NULL, 0);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_pk_parse_key failed: -0x%04x", -ret);
        return false;
    }

    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(s_pk);
    if (!rsa) {
        ESP_LOGE(TAG, "mbedtls_pk_rsa returned NULL (not an RSA key?)");
        return false;
    }

    ret = mbedtls_rsa_complete(rsa);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_rsa_complete failed: %d", ret);
        return false;
    }

    rsa->MBEDTLS_PRIVATE(padding) = MBEDTLS_RSA_PKCS_V21;
    rsa->MBEDTLS_PRIVATE(hash_id) = MBEDTLS_MD_SHA256;

    ret = mbedtls_rsa_complete(rsa);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_rsa_complete (2) failed: %d", ret);
        return false;
    }

    ret = mbedtls_rsa_check_privkey(rsa);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_rsa_check_privkey failed: -0x%04x", -ret);
        return false;
    }

    // Spawn the sign task on APP_CPU (Core 1) so the main task on PRO_CPU
    // keeps polling USB/BT during the ~1-2 s RSA sign. Lazy: only created once.
    if (!s_sign_task) {
        BaseType_t ok = xTaskCreatePinnedToCore(
            sign_task_fn, "ps4_sign",
            8192, NULL,
            tskIDLE_PRIORITY + 1,
            &s_sign_task,
            APP_CPU_NUM);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "xTaskCreatePinnedToCore failed");
            s_sign_task = NULL;
            return false;
        }
    }

    s_rsa_valid = true;
    ESP_LOGI(TAG, "RSA key loaded, local auth available");
    return true;
}

bool ps4_local_auth_reload(void)
{
    return ps4_local_auth_init();
}

bool ps4_local_auth_is_available(void)
{
    return s_rsa_valid;
}

bool ps4_local_auth_is_signing(void)
{
    return s_signing_inflight && !s_signature_ready;
}

// ============================================================================
// Nonce reception (called from main task when console sends 0xF0)
// ============================================================================

void ps4_local_auth_send_nonce_page(const uint8_t *data, uint16_t len)
{
    if (!s_rsa_valid || !data || len < 3) return;

    uint8_t nonce_id = data[0];
    uint8_t page     = data[1];

    if (page >= NONCE_PAGES) return;

    if (page == 0) {
        s_nonce_id = nonce_id;
        s_nonce_pages_received = 0;
        s_signing_requested = false;
        s_signature_ready   = false;
        s_page_cursor       = 0;
        memset(s_nonce, 0, sizeof(s_nonce));
        if (s_log_enabled) printf("[ps4_auth] New auth session, nonce_id=%d\n", nonce_id);
    }

    uint16_t data_offset = 3;
    uint16_t avail = (len > data_offset) ? (uint16_t)(len - data_offset) : 0;
    uint16_t copy_len = (avail < PAGE_SIZE) ? avail : PAGE_SIZE;

    memcpy(s_nonce + page * PAGE_SIZE, data + data_offset, copy_len);
    s_nonce_pages_received++;

    if (s_log_enabled) {
        printf("[ps4_auth] Received nonce page %d/%d\n",
               s_nonce_pages_received, NONCE_PAGES);
    }

    if (s_nonce_pages_received >= NONCE_PAGES) {
        if (s_log_enabled) printf("[ps4_auth] All nonce pages received, requesting sign\n");
        s_signing_requested = true;
    }
}

// ============================================================================
// Sign task (runs on APP_CPU)
// ============================================================================

static void sign_task_fn(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!s_rsa_valid) continue;

        uint8_t hash[32];
        mbedtls_sha256(s_sign_nonce, 256, hash, 0);

        uint8_t rsa_sig[256];
        mbedtls_rsa_context *rsa = mbedtls_pk_rsa(s_pk);
        int ret = mbedtls_rsa_rsassa_pss_sign(
            rsa, rng_fn, NULL,
            MBEDTLS_MD_SHA256,
            sizeof(hash), hash,
            rsa_sig);

        if (ret == 0) {
            memset(s_response, 0, sizeof(s_response));
            memcpy(s_response + RSP_RSA_SIG_OFFSET,    rsa_sig,         256);
            memcpy(s_response + RSP_SERIAL_OFFSET,     s_serial_binary,  16);
            mbedtls_mpi_write_binary(&rsa->MBEDTLS_PRIVATE(N),
                                     s_response + RSP_N_OFFSET, 256);
            mbedtls_mpi_write_binary(&rsa->MBEDTLS_PRIVATE(E),
                                     s_response + RSP_E_OFFSET, 256);
            memcpy(s_response + RSP_DEVICE_SIG_OFFSET, ps4_sig_start,   256);
            // [1040..1063] zeros (already memset)
        } else {
            memset(s_response, 0, sizeof(s_response));
            if (s_log_enabled) printf("[ps4_sign] FAIL ret=%d\n", ret);
        }

        s_sign_ret    = ret;
        s_page_cursor = 0;
        __sync_synchronize();          // publish s_response before flag
        s_signature_ready  = true;
        s_signing_inflight = false;
        if (s_log_enabled) printf("[ps4_sign] signature_ready=true (ret=%d)\n", ret);
    }
}

// ============================================================================
// Signing dispatch task (called from main loop)
// ============================================================================

void ps4_local_auth_task(void)
{
    // Sign just completed — log once and reset state machine
    if (s_signing_inflight == false && s_signature_ready) {
        // s_signing_inflight already cleared by sign task; nothing to do here.
        // s_signature_ready stays true until the next nonce arrives or reset.
        return;
    }

    // Wait while sign task is working
    if (s_signing_inflight) return;

    // Dispatch a new sign if one is queued
    if (!s_signing_requested || !s_rsa_valid || !s_sign_task) return;

    s_signing_requested = false;
    s_signature_ready   = false;
    s_sign_ret          = 0;
    s_page_cursor       = 0;

    memcpy(s_sign_nonce, s_nonce, NONCE_SIZE);

    // Set inflight BEFORE notify so is_signing() is immediately true on
    // any status poll that races in.
    s_signing_inflight = true;
    __sync_synchronize();
    xTaskNotifyGive(s_sign_task);
}

// ============================================================================
// Status and signature retrieval (called from main task when console GETs)
// ============================================================================

uint8_t ps4_local_auth_get_status(void)
{
    if (s_signature_ready) return 0x00;
    return 0x10;
}

uint16_t ps4_local_auth_get_status_report(uint8_t *buffer, uint16_t maxlen)
{
    // 15 bytes: [nonce_id, status, 0×9, CRC32]
    // CRC computed over [0xF2, nonce_id, status, 0×9] (12 bytes)
    if (!buffer || maxlen < 15) return 0;

    uint8_t status = ps4_local_auth_get_status();

    uint8_t temp[16];
    memset(temp, 0, sizeof(temp));
    temp[0] = 0xF2;
    temp[1] = s_nonce_id;
    temp[2] = status;
    // temp[3..11] zeros
    uint32_t crc = ps4_crc32(temp, 12);
    memcpy(&temp[12], &crc, 4);

    memcpy(buffer, &temp[1], 15);
    return 15;
}

uint16_t ps4_local_auth_get_next_page(uint8_t *buffer, uint16_t maxlen)
{
    // 63 bytes: [nonce_id, page, 0, data×56, CRC32×4]
    // CRC computed over [0xF1, nonce_id, page, 0, data×56] (60 bytes)
    if (!buffer || maxlen < 63) return 0;

    uint8_t page = (s_page_cursor < SIG_PAGES) ? s_page_cursor : (SIG_PAGES - 1);

    uint8_t temp[64];
    memset(temp, 0, sizeof(temp));
    temp[0] = 0xF1;
    temp[1] = s_nonce_id;
    temp[2] = page;
    temp[3] = 0x00;
    memcpy(&temp[4], s_response + (page * PAGE_SIZE), PAGE_SIZE);
    uint32_t crc = ps4_crc32(temp, 60);
    memcpy(&temp[60], &crc, 4);

    memcpy(buffer, &temp[1], 63);

    if (s_page_cursor < SIG_PAGES) {
        s_page_cursor++;
    }
    return 63;
}

void ps4_local_auth_reset(void)
{
    s_signing_requested    = false;
    s_signing_inflight     = false;
    s_signature_ready      = false;
    s_sign_ret             = 0;
    s_nonce_pages_received = 0;
    s_page_cursor          = 0;
    s_nonce_id             = 0;
    memset(s_nonce,      0, sizeof(s_nonce));
    memset(s_sign_nonce, 0, sizeof(s_sign_nonce));
    memset(s_response,   0, sizeof(s_response));
}

// ============================================================================
// Logging toggle
// ============================================================================

void ps4_local_auth_set_log_enabled(bool enabled) { s_log_enabled = enabled; }
bool ps4_local_auth_get_log_enabled(void)         { return s_log_enabled; }
