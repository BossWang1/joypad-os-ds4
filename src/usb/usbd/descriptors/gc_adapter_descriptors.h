// gc_adapter_descriptors.h - GameCube Adapter USB descriptors
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Nintendo GameCube Controller Adapter compatible descriptors.
// Emulates Wii U/Switch GameCube Adapter (VID 057E, PID 0337).
// Supports up to 4 controllers via single USB interface.
//
// The real adapter uses HID class with a custom report descriptor.
// Report IDs: 0x11=rumble(5B), 0x13=init(1B), 0x21=input(37B)

#ifndef GC_ADAPTER_DESCRIPTORS_H
#define GC_ADAPTER_DESCRIPTORS_H

#include <stdint.h>
#include "tusb.h"

// ============================================================================
// GC ADAPTER USB IDENTIFIERS
// ============================================================================

// Nintendo GameCube Adapter for Wii U/Switch
#define GC_ADAPTER_VID          0x057E  // Nintendo
#define GC_ADAPTER_PID          0x0337  // GameCube Adapter
#define GC_ADAPTER_BCD_DEVICE   0x0100  // v1.0

// ============================================================================
// GC ADAPTER PROTOCOL CONSTANTS
// ============================================================================

// Report IDs
#define GC_ADAPTER_REPORT_ID_RUMBLE  0x11  // Rumble output command (5 bytes)
#define GC_ADAPTER_REPORT_ID_INIT    0x13  // Init command from host (1 byte)
#define GC_ADAPTER_REPORT_ID_INPUT   0x21  // Controller input report (37 bytes)

// Port connection status (upper nibble of first byte per port)
#define GC_ADAPTER_PORT_NONE         0x00  // No controller
#define GC_ADAPTER_PORT_WIRED        0x10  // Wired controller
#define GC_ADAPTER_PORT_WIRELESS     0x20  // Wireless controller

// Controller type (lower nibble of first byte per port)
#define GC_ADAPTER_TYPE_NONE         0x00  // No controller
#define GC_ADAPTER_TYPE_NORMAL       0x01  // Standard controller
#define GC_ADAPTER_TYPE_WAVEBIRD     0x02  // WaveBird (wireless)

// Report sizes (excluding report ID)
#define GC_ADAPTER_INPUT_SIZE        37    // 1 byte report_id + 36 bytes data
#define GC_ADAPTER_RUMBLE_SIZE       5     // 1 byte report_id + 4 bytes rumble state
#define GC_ADAPTER_INIT_SIZE         1     // 1 byte report_id only

// ============================================================================
// GC ADAPTER REPORT STRUCTURES
// ============================================================================

// Per-port input report (9 bytes)
typedef struct __attribute__((packed)) {
    // Byte 0: Connection status (upper nibble) + Controller type (lower nibble)
    struct {
        uint8_t type : 4;       // 0=none, 1=normal, 2=wavebird
        uint8_t connected : 4;  // 0=none, 1=wired, 2=wireless
    };

    // Byte 1: Buttons (A, B, X, Y, D-pad)
    struct {
        uint8_t a : 1;
        uint8_t b : 1;
        uint8_t x : 1;
        uint8_t y : 1;
        uint8_t dpad_left : 1;
        uint8_t dpad_right : 1;
        uint8_t dpad_down : 1;
        uint8_t dpad_up : 1;
    };

    // Byte 2: Buttons (Start, Z, R, L) + padding
    struct {
        uint8_t start : 1;
        uint8_t z : 1;
        uint8_t r : 1;
        uint8_t l : 1;
        uint8_t : 4;  // Unused padding
    };

    // Bytes 3-8: Analog axes
    uint8_t stick_x;     // Main stick X (0-255, 128=center)
    uint8_t stick_y;     // Main stick Y (0-255, 128=center)
    uint8_t cstick_x;    // C-stick X (0-255, 128=center)
    uint8_t cstick_y;    // C-stick Y (0-255, 128=center)
    uint8_t trigger_l;   // L trigger analog (0-255)
    uint8_t trigger_r;   // R trigger analog (0-255)
} gc_adapter_port_t;

_Static_assert(sizeof(gc_adapter_port_t) == 9, "gc_adapter_port_t must be 9 bytes");

// Full adapter input report (37 bytes)
typedef struct __attribute__((packed)) {
    uint8_t report_id;           // Always 0x21
    gc_adapter_port_t port[4];   // 4 controller ports
} gc_adapter_in_report_t;

_Static_assert(sizeof(gc_adapter_in_report_t) == 37, "gc_adapter_in_report_t must be 37 bytes");

// Rumble output command (5 bytes)
typedef struct __attribute__((packed)) {
    uint8_t report_id;   // Always 0x11
    uint8_t rumble[4];   // Per-port rumble state (0=off, 1=on)
} gc_adapter_out_report_t;

_Static_assert(sizeof(gc_adapter_out_report_t) == 5, "gc_adapter_out_report_t must be 5 bytes");

// ============================================================================
// GC ADAPTER HID REPORT DESCRIPTOR
// ============================================================================

// HID Report Descriptor matching the real WUP-028 adapter
// Based on USB capture: Uses Gaming Controls usage page with vendor-specific reports
static const uint8_t gc_adapter_report_descriptor[] = {
    0x05, 0x05,        // Usage Page (Gaming Controls)
    0x09, 0x00,        // Usage (Undefined - vendor specific)
    0xA1, 0x01,        // Collection (Application)

    // Report ID 0x11: Rumble Output (5 bytes: report_id + 4 data)
    0x85, 0x11,        //   Report ID (17)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x00,  //   Usage Maximum (255)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x05,        //   Report Count (5) - includes report ID byte in HID spec
    0x91, 0x00,        //   Output (Data, Array, Absolute)
    0xC0,              // End Collection

    0xA1, 0x01,        // Collection (Application)
    // Report ID 0x21: Controller Input (37 bytes: report_id + 36 data)
    0x85, 0x21,        //   Report ID (33)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x00,  //   Usage Maximum (255)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x25,        //   Report Count (37) - per real adapter (reports 37, sends 36+ID)
    0x81, 0x00,        //   Input (Data, Array, Absolute)
    0xC0,              // End Collection

    0xA1, 0x01,        // Collection (Application)
    // Report ID 0x13: Init Command Output (1 byte: just report_id)
    0x85, 0x13,        //   Report ID (19)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x00,  //   Usage Maximum (255)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x91, 0x00,        //   Output (Data, Array, Absolute)
    0xC0,              // End Collection
};

// ============================================================================
// GC ADAPTER USB DESCRIPTORS
// ============================================================================

// Device descriptor - HID class
static const tusb_desc_device_t gc_adapter_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  // USB 2.0
    .bDeviceClass       = 0x00,    // Use class from interface
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = GC_ADAPTER_VID,
    .idProduct          = GC_ADAPTER_PID,
    .bcdDevice          = GC_ADAPTER_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x00,
    .bNumConfigurations = 0x01
};

// Configuration descriptor length
// Config (9) + Interface (9) + HID (9) + EP IN (7) + EP OUT (7) = 41
#define GC_ADAPTER_CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

// Configuration descriptor - HID with IN and OUT endpoints
static const uint8_t gc_adapter_config_descriptor[] = {
    // Configuration descriptor (9 bytes)
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, GC_ADAPTER_CONFIG_TOTAL_LEN, 0x80, 500),  // 500mA

    // Interface + HID + Endpoints (using TinyUSB HID INOUT macro)
    TUD_HID_INOUT_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE,
                             sizeof(gc_adapter_report_descriptor),
                             0x81, 0x02, 37, 1),  // EP IN 0x81, EP OUT 0x02, 37 bytes, 1ms
};

// String descriptors
#define GC_ADAPTER_MANUFACTURER  "Nintendo Co., Ltd."
#define GC_ADAPTER_PRODUCT       "WUP-028"  // Wii U GameCube Adapter product code

#endif // GC_ADAPTER_DESCRIPTORS_H
