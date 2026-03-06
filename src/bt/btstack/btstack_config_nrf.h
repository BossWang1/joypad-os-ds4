// btstack_config_nrf.h - BTstack configuration for nRF52840 (BLE-only)
//
// nRF52840 has BLE only. Uses Zephyr kernel run loop and NVS-based
// TLV storage. Zephyr provides HCI via raw passthrough mode.

#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// ============================================================================
// PORT FEATURES
// ============================================================================

#define HAVE_ASSERT
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_MALLOC
#define HAVE_PRINTF
#define ENABLE_PRINTF_HEXDUMP

// ============================================================================
// BTSTACK FEATURES
// ============================================================================

// BLE only (nRF52840 controller in BLE-only mode)
#define ENABLE_BLE
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_SECURE_CONNECTIONS

// Classic BT enabled for compilation (drivers compile but are dead code)
// The nRF52840 controller only supports BLE, so Classic connections
// will never succeed at runtime
#define ENABLE_CLASSIC

// Enable logging
#define ENABLE_LOG_ERROR
#define ENABLE_LOG_INFO

// Use software AES (nRF52840 has hardware AES but BTstack uses its own)
#define ENABLE_SOFTWARE_AES128

// Enable micro-ecc for LE Secure Connections P-256
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

// ============================================================================
// BUFFER SIZES
// ============================================================================

// ACL buffer large enough for 512 byte Characteristic
#define HCI_ACL_PAYLOAD_SIZE (512 + 4 + 3)

// Pre-buffer for L2CAP headers
#define HCI_INCOMING_PRE_BUFFER_SIZE 14

// Zephyr HCI transport does not need outgoing pre-buffer
#define HCI_OUTGOING_PRE_BUFFER_SIZE 0

// Host flow control buffer sizes
#define HCI_HOST_ACL_PACKET_LEN HCI_ACL_PAYLOAD_SIZE
#define HCI_HOST_ACL_PACKET_NUM 20
#define HCI_HOST_SCO_PACKET_LEN 0
#define HCI_HOST_SCO_PACKET_NUM 0

// ============================================================================
// MEMORY POOLS
// ============================================================================

#define MAX_NR_HCI_CONNECTIONS 2
#define MAX_NR_L2CAP_CHANNELS 8
#define MAX_NR_L2CAP_SERVICES 3
#define MAX_NR_GATT_CLIENTS 1
#define MAX_NR_WHITELIST_ENTRIES 2
#define MAX_NR_LE_DEVICE_DB_ENTRIES 4
#define NVM_NUM_LINK_KEYS 2
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 4
#define NVM_NUM_DEVICE_DB_ENTRIES 4

// ============================================================================
// HID SUPPORT
// ============================================================================

#define ENABLE_HID_HOST
#define MAX_NR_HID_HOST_CONNECTIONS 2
#define MAX_NR_HIDS_CLIENTS 1
#define MAX_NR_BATTERY_SERVICE_CLIENTS 1

#endif // BTSTACK_CONFIG_H
