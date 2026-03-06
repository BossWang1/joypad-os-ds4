// btstack_tlv_nrf.h - BTstack TLV backed by Zephyr NVS
//
// Provides persistent key-value storage for BTstack (BLE bond keys,
// SM identity keys, etc.) using the nRF52840's internal flash via NVS.

#ifndef BTSTACK_TLV_NRF_H
#define BTSTACK_TLV_NRF_H

#include "btstack_tlv.h"
#include <zephyr/fs/nvs.h>

// Initialize NVS-backed TLV. Takes a mounted NVS filesystem handle
// (shared with flash_nrf.c). Returns the btstack_tlv_t implementation.
const btstack_tlv_t * btstack_tlv_nrf_init(struct nvs_fs *nvs);

#endif
