// ps4_storage_stubs_esp.c - ESP32-S3 stubs for Pico-only PS4 storage modules
//
// Shared CDC commands (src/usb/usbd/cdc/cdc_commands.c) and the USB device
// init (src/usb/usbd/usbd.c) unconditionally reference ps4_auth_flash_* and
// ps4_event_log_* APIs. Their Pico implementations
// (src/core/services/storage/{ps4_auth_flash.c, ps4_event_log.c}) depend on
// hardware/flash.h, pico/flash.h, FLASH_SECTOR_SIZE and
// __no_inline_not_in_flash_func — none of which exist in ESP-IDF.
//
// On ESP the PS4 auth key material is provisioned via EMBED_TXTFILES
// (key.pem, serial.txt, sig.bin) at build time, not via the PS4AUTH.SET CDC
// command. These stubs satisfy the linker so the shared sources compile;
// the actual signing path is ps4_local_auth_esp.c, which reads the embedded
// files directly and does not touch flash.
//
// SPDX-License-Identifier: Apache-2.0

#include "core/services/storage/ps4_auth_flash.h"
#include "core/services/storage/ps4_event_log.h"

#include <stdio.h>
#include <string.h>

// ============================================================================
// ps4_auth_flash stubs
// ============================================================================

bool ps4_auth_flash_load(ps4_auth_data_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    return false;
}

void ps4_auth_flash_save(const ps4_auth_data_t *data)
{
    (void)data;
    printf("[ps4_auth] PS4AUTH.SET ignored on ESP — provision via "
           "esp/main/{key.pem,serial.txt,sig.bin} and rebuild\n");
}

void ps4_auth_flash_erase(void)
{
    printf("[ps4_auth] PS4AUTH.CLEAR ignored on ESP — clear "
           "esp/main/{key.pem,serial.txt,sig.bin} and rebuild\n");
}

bool ps4_auth_flash_is_valid(const ps4_auth_data_t *data)
{
    (void)data;
    return false;
}

// ============================================================================
// ps4_event_log stubs (no flash-backed log on ESP)
// ============================================================================

void ps4_event_log_init(void) { }

void ps4_event_log_write(const char *msg) { (void)msg; }

void ps4_event_log_clear(void) { }

int ps4_event_log_dump(char *out, size_t maxlen)
{
    if (out && maxlen > 0) out[0] = '\0';
    return 0;
}

uint8_t ps4_event_log_count(void) { return 0; }
