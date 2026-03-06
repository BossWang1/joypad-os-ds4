// btstack_tlv_nrf.c - BTstack TLV backed by Zephyr NVS
//
// Maps 32-bit BTstack TLV tags to 16-bit NVS IDs via XOR hash.
// Shares the NVS partition with flash_nrf.c (app settings use key 1;
// BTstack tags hash to 0x0600+ range, no collision).
//
// Zephyr NVS is internally mutex-protected, so this is safe to call
// from the BTstack thread while the main thread uses the same NVS.

#include "btstack_tlv_nrf.h"
#include <stdio.h>

static struct nvs_fs *tlv_nvs;

// Decode 4-char tag for debug printing
static void tag_to_str(uint32_t tag, char out[5])
{
    out[0] = (char)((tag >> 24) & 0xFF);
    out[1] = (char)((tag >> 16) & 0xFF);
    out[2] = (char)((tag >> 8) & 0xFF);
    out[3] = (char)(tag & 0xFF);
    out[4] = '\0';
    // Replace non-printable chars with '.'
    for (int i = 0; i < 4; i++) {
        if (out[i] < 0x20 || out[i] > 0x7E) out[i] = '.';
    }
}

// Hash 32-bit tag to 16-bit NVS ID.
// BTstack tags are 4-char codes like "BTD\x00" (0x42544400).
// XOR upper and lower 16 bits → unique IDs in 0x0600+ range.
static uint16_t tag_to_nvs_id(uint32_t tag)
{
    return (uint16_t)((tag >> 16) ^ (tag & 0xFFFF));
}

static int tlv_get_tag(void *context, uint32_t tag, uint8_t *buffer, uint32_t buffer_size)
{
    (void)context;
    if (!tlv_nvs) return 0;

    uint16_t id = tag_to_nvs_id(tag);
    ssize_t len = nvs_read(tlv_nvs, id, buffer, buffer_size);
    char ts[5];
    tag_to_str(tag, ts);
    if (len < 0) {
        printf("[TLV_NRF] get '%s' (nvs_id=0x%04x): not found\n", ts, id);
        return 0;
    }
    printf("[TLV_NRF] get '%s' (nvs_id=0x%04x): %zd bytes\n", ts, id, len);
    return (int)len;
}

static int tlv_store_tag(void *context, uint32_t tag, const uint8_t *data, uint32_t data_size)
{
    (void)context;
    if (!tlv_nvs) return -1;

    uint16_t id = tag_to_nvs_id(tag);
    ssize_t len = nvs_write(tlv_nvs, id, data, data_size);
    char ts[5];
    tag_to_str(tag, ts);
    if (len < 0) {
        printf("[TLV_NRF] store '%s' (nvs_id=0x%04x): FAILED %zd\n", ts, id, len);
        return -1;
    }
    // nvs_write returns 0 if data was identical (no write needed), or data_size on write
    printf("[TLV_NRF] store '%s' (nvs_id=0x%04x): %u bytes (nvs_ret=%zd)\n",
           ts, id, (unsigned)data_size, len);
    return 0;
}

static void tlv_delete_tag(void *context, uint32_t tag)
{
    (void)context;
    if (!tlv_nvs) return;

    uint16_t id = tag_to_nvs_id(tag);
    char ts[5];
    tag_to_str(tag, ts);
    printf("[TLV_NRF] delete '%s' (nvs_id=0x%04x)\n", ts, id);
    nvs_delete(tlv_nvs, id);
}

static const btstack_tlv_t btstack_tlv_nrf_impl = {
    .get_tag    = tlv_get_tag,
    .store_tag  = tlv_store_tag,
    .delete_tag = tlv_delete_tag,
};

const btstack_tlv_t * btstack_tlv_nrf_init(struct nvs_fs *nvs)
{
    tlv_nvs = nvs;
    printf("[TLV_NRF] Initialized with NVS\n");
    return &btstack_tlv_nrf_impl;
}
