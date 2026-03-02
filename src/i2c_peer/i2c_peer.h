// i2c_peer.h - I2C Peer Input Sharing via STEMMA QT
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Shares controller inputs between two joypad-os devices over I2C.
// Primary use case: MacroPad (slave) → Feather USB Host (master) via STEMMA QT.
//
// Register-based protocol:
//   0x00: Status (bit0=has_data, upper nibble=version)
//   0x01: Device count
//   0x10: Player 0 input event (12 bytes)
//   0x80: Device status (master→slave, 46 bytes)

#ifndef I2C_PEER_H
#define I2C_PEER_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_event.h"
#include "core/input_interface.h"
#include "core/router/router.h"

// ============================================================================
// I2C PEER CONFIGURATION
// ============================================================================

#define I2C_PEER_DEFAULT_ADDR   0x50    // Clear of OLED (0x3C) and PCA9555 (0x20/0x21)
#define I2C_PEER_PROTOCOL_VER   1       // Protocol version (upper nibble of status)
#define I2C_PEER_BAUDRATE       400000  // 400kHz fast mode

// Device address range for I2C peer devices (distinct from USB, BT, UART)
#define I2C_PEER_DEV_ADDR_BASE  0xE0

// ============================================================================
// REGISTER MAP
// ============================================================================

#define I2C_PEER_REG_STATUS     0x00    // 1 byte: bit0=has_data, [7:4]=version
#define I2C_PEER_REG_DEV_COUNT  0x01    // 1 byte: number of active devices
#define I2C_PEER_REG_PLAYER0    0x10    // 12 bytes: player 0 input event
#define I2C_PEER_REG_NAME       0x20    // 32 bytes: device name (slave→master)
#define I2C_PEER_REG_STATUS_WRITE 0x80  // 46 bytes: device status (master→slave)

// Status register bits
#define I2C_PEER_STATUS_HAS_DATA    (1 << 0)
#define I2C_PEER_STATUS_VER_SHIFT   4

// Device status flag bits (i2c_peer_status_t.flags)
#define I2C_PEER_FLAG_CONNECTED     (1 << 0)
#define I2C_PEER_FLAG_NAME_VALID    (1 << 1)

// ============================================================================
// DEVICE STATUS (46 bytes, master→slave)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t  flags;            // bit0=connected, bit1=name_valid
    uint8_t  transport;        // input_transport_t
    uint8_t  device_type;      // input_device_type_t
    uint8_t  player_number;    // 1-based (0=unassigned)
    uint8_t  usb_mode;         // usb_output_mode_t
    uint8_t  mode_color[3];    // RGB for current USB mode
    uint8_t  rumble_left;      // Heavy motor 0-255
    uint8_t  rumble_right;     // Light motor 0-255
    uint8_t  led_player;       // LED player pattern
    uint8_t  led_color[3];     // LED RGB from host feedback
    char     name[32];         // Device name, null-terminated
} i2c_peer_status_t;

#define I2C_PEER_STATUS_SIZE sizeof(i2c_peer_status_t)  // 46 bytes

// ============================================================================
// WIRE FORMAT (12 bytes packed, matches uart_input_event_t layout)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t  player_index;      // Player slot (0-7)
    uint8_t  device_type;       // INPUT_TYPE_* enum
    uint32_t buttons;           // Button state
    uint8_t  analog[6];         // [0]=LX, [1]=LY, [2]=RX, [3]=RY, [4]=L2, [5]=R2
} i2c_peer_event_t;

#define I2C_PEER_EVENT_SIZE sizeof(i2c_peer_event_t)  // 12 bytes

// ============================================================================
// CONFIGURATION STRUCT
// ============================================================================

typedef struct {
    uint8_t i2c_inst;           // I2C instance (0 or 1)
    uint8_t sda_pin;            // SDA GPIO pin
    uint8_t scl_pin;            // SCL GPIO pin
    uint8_t addr;               // I2C slave address (default: I2C_PEER_DEFAULT_ADDR)
    bool skip_i2c_init;         // true = bus already initialized (OLED sharing)
} i2c_peer_config_t;

// ============================================================================
// SLAVE API (e.g., MacroPad serves its button state)
// ============================================================================

// Initialize I2C slave with config
void i2c_peer_slave_init(const i2c_peer_config_t* config);

// Router tap callback — converts input_event_t to wire format for slave buffer
// Install with router_set_tap() or router_set_tap_exclusive()
void i2c_peer_slave_tap(output_target_t output, uint8_t player_index,
                         const input_event_t* event);

// Check for new device status from master — returns true if new status available
// Copies status with interrupts disabled for atomicity
bool i2c_peer_slave_get_status(i2c_peer_status_t* out);

// ============================================================================
// MASTER API (e.g., Feather polls slave for inputs)
// ============================================================================

// Initialize I2C master with config
void i2c_peer_master_init(const i2c_peer_config_t* config);

// Poll slave and submit inputs to router — call from main loop
void i2c_peer_master_task(void);

// Write device status to slave (call from main loop when state changes)
void i2c_peer_master_send_status(const i2c_peer_status_t* status);

// Set the device name reported to the master (call before slave_init)
// Default: "Joypad Controller"
void i2c_peer_slave_set_name(const char* name);

// Get the device name received from the I2C peer slave
// Called by router's get_device_name() for I2C transport devices
const char* i2c_peer_get_device_name(void);

// Input interface for router integration
extern const InputInterface i2c_peer_input_interface;

#endif // I2C_PEER_H
