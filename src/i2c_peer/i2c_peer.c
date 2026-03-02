// i2c_peer.c - I2C Peer Input Sharing Implementation
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Both master and slave in one file — unused functions eliminated by -gc-sections.

#include "i2c_peer.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/i2c_slave.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// SHARED
// ============================================================================

static i2c_inst_t* get_i2c_inst(uint8_t inst) {
    return inst == 0 ? i2c0 : i2c1;
}

// Device name for this I2C peer (slave sets from app config, master reads from wire)
static char peer_device_name[32] = "Joypad Controller";

void i2c_peer_slave_set_name(const char* name) {
    strncpy(peer_device_name, name, sizeof(peer_device_name) - 1);
    peer_device_name[sizeof(peer_device_name) - 1] = '\0';
}

const char* i2c_peer_get_device_name(void) {
    return peer_device_name;
}

// ============================================================================
// SLAVE IMPLEMENTATION
// ============================================================================

// ISR state — accessed from interrupt context
static volatile uint8_t slave_reg_addr;
static volatile uint8_t slave_reg_offset;
static volatile bool slave_reg_addr_written;

// Double-buffered event: main context writes, ISR reads
static i2c_peer_event_t slave_event;
static volatile bool slave_has_data;
static volatile uint8_t slave_device_count;

// Master→slave device status (ISR accumulates writes)
static volatile uint8_t slave_write_buf[I2C_PEER_STATUS_SIZE];
static volatile uint8_t slave_write_offset;
static volatile bool slave_has_status;

// Build status register
static inline uint8_t slave_status_byte(void) {
    return (I2C_PEER_PROTOCOL_VER << I2C_PEER_STATUS_VER_SHIFT) |
           (slave_has_data ? I2C_PEER_STATUS_HAS_DATA : 0);
}

// I2C slave ISR handler — must be in RAM for timing
static void __not_in_flash_func(i2c_slave_handler)(i2c_inst_t* i2c, i2c_slave_event_t event) {
    (void)i2c;

    switch (event) {
        case I2C_SLAVE_RECEIVE: {
            // Master is writing — first byte is register address
            uint8_t data = i2c_read_byte_raw(i2c);
            if (!slave_reg_addr_written) {
                slave_reg_addr = data;
                slave_reg_offset = 0;
                slave_reg_addr_written = true;
            } else if (slave_reg_addr == I2C_PEER_REG_STATUS_WRITE) {
                // Accumulate status bytes from master
                if (slave_write_offset < I2C_PEER_STATUS_SIZE) {
                    slave_write_buf[slave_write_offset++] = data;
                }
            }
            break;
        }

        case I2C_SLAVE_REQUEST: {
            // Master is reading — serve data from current register
            uint8_t byte = 0x00;

            switch (slave_reg_addr) {
                case I2C_PEER_REG_STATUS:
                    byte = slave_status_byte();
                    break;

                case I2C_PEER_REG_DEV_COUNT:
                    byte = slave_device_count;
                    break;

                case I2C_PEER_REG_PLAYER0: {
                    // Serve 12 bytes of event data
                    const uint8_t* p = (const uint8_t*)&slave_event;
                    if (slave_reg_offset < I2C_PEER_EVENT_SIZE) {
                        byte = p[slave_reg_offset];
                        slave_reg_offset++;
                    }
                    break;
                }

                case I2C_PEER_REG_NAME: {
                    // Serve 32 bytes of device name
                    if (slave_reg_offset < 32) {
                        byte = (uint8_t)peer_device_name[slave_reg_offset];
                        slave_reg_offset++;
                    }
                    break;
                }

                default:
                    byte = 0xFF;
                    break;
            }

            i2c_write_byte_raw(i2c, byte);
            break;
        }

        case I2C_SLAVE_FINISH:
            // Validate completed status write (full packet received)
            if (slave_reg_addr == I2C_PEER_REG_STATUS_WRITE &&
                slave_write_offset == I2C_PEER_STATUS_SIZE) {
                slave_has_status = true;
            }
            // Reset register addressing
            slave_reg_addr_written = false;
            break;
    }
}

void i2c_peer_slave_init(const i2c_peer_config_t* config) {
    i2c_inst_t* i2c = get_i2c_inst(config->i2c_inst);

    if (!config->skip_i2c_init) {
        i2c_init(i2c, I2C_PEER_BAUDRATE);
        gpio_set_function(config->sda_pin, GPIO_FUNC_I2C);
        gpio_set_function(config->scl_pin, GPIO_FUNC_I2C);
        gpio_pull_up(config->sda_pin);
        gpio_pull_up(config->scl_pin);
    }

    // Initialize state
    memset((void*)&slave_event, 0, sizeof(slave_event));
    slave_has_data = false;
    slave_device_count = 0;
    slave_reg_addr_written = false;
    slave_has_status = false;
    slave_write_offset = 0;

    // Enable I2C slave mode
    uint8_t addr = config->addr ? config->addr : I2C_PEER_DEFAULT_ADDR;
    i2c_slave_init(i2c, addr, &i2c_slave_handler);

    printf("[i2c_peer] Slave initialized at 0x%02X on I2C%d (SDA=%d, SCL=%d)\n",
           addr, config->i2c_inst, config->sda_pin, config->scl_pin);
}

void i2c_peer_slave_tap(output_target_t output, uint8_t player_index,
                         const input_event_t* event) {
    (void)output;

    // Loop prevention: don't re-export events that came from an I2C peer
    // I2C peer range is 0xE0-0xEF; pad devices use 0xF0+ so must not be blocked
    if (event->dev_addr >= I2C_PEER_DEV_ADDR_BASE &&
        event->dev_addr < I2C_PEER_DEV_ADDR_BASE + 16) return;

    // Convert input_event_t → i2c_peer_event_t
    i2c_peer_event_t packed = {
        .player_index = player_index,
        .device_type = (uint8_t)event->type,
        .buttons = event->buttons,
        .analog = {
            event->analog[ANALOG_LX],
            event->analog[ANALOG_LY],
            event->analog[ANALOG_RX],
            event->analog[ANALOG_RY],
            event->analog[ANALOG_L2],
            event->analog[ANALOG_R2],
        },
    };

    // Copy to ISR buffer — brief interrupt disable for 12-byte atomic copy
    uint32_t save = save_and_disable_interrupts();
    slave_event = packed;
    slave_has_data = true;
    slave_device_count = 1;
    restore_interrupts(save);
}

bool i2c_peer_slave_get_status(i2c_peer_status_t* out) {
    if (!slave_has_status) return false;

    uint32_t save = save_and_disable_interrupts();
    memcpy(out, (const void*)slave_write_buf, I2C_PEER_STATUS_SIZE);
    slave_has_status = false;
    restore_interrupts(save);

    return true;
}

// ============================================================================
// MASTER IMPLEMENTATION
// ============================================================================

static i2c_peer_config_t master_config;
static i2c_inst_t* master_i2c;
static bool master_initialized = false;

// Connection state
static bool master_connected = false;
static uint8_t master_fail_count = 0;
static uint32_t master_last_poll_ms = 0;
static uint32_t master_last_retry_ms = 0;

#define MASTER_POLL_INTERVAL_MS     4       // 250Hz polling
#define MASTER_RETRY_INTERVAL_MS    500     // Retry every 500ms when disconnected
#define MASTER_FAIL_THRESHOLD       3       // 3 consecutive NAKs = disconnected
#define MASTER_TIMEOUT_US           1000    // 1ms I2C timeout

// Read a register from slave
static int master_read_reg(uint8_t reg, uint8_t* buf, size_t len) {
    // Write register address
    int ret = i2c_write_timeout_us(master_i2c, master_config.addr, &reg, 1, true, MASTER_TIMEOUT_US);
    if (ret < 0) return ret;

    // Read data
    ret = i2c_read_timeout_us(master_i2c, master_config.addr, buf, len, false, MASTER_TIMEOUT_US);
    return ret;
}

// Write a register + data to slave
static int master_write_reg(uint8_t reg, const uint8_t* buf, size_t len) {
    // Build single buffer: [reg_addr] + [data...]
    uint8_t txbuf[1 + I2C_PEER_STATUS_SIZE];
    if (len > I2C_PEER_STATUS_SIZE) return -1;
    txbuf[0] = reg;
    memcpy(&txbuf[1], buf, len);
    // Longer timeout for multi-byte writes (47 bytes @ 400kHz ≈ 1.1ms)
    return i2c_write_timeout_us(master_i2c, master_config.addr, txbuf, 1 + len, false, 2000);
}

void i2c_peer_master_send_status(const i2c_peer_status_t* status) {
    if (!master_initialized || !master_connected) return;
    master_write_reg(I2C_PEER_REG_STATUS_WRITE, (const uint8_t*)status, I2C_PEER_STATUS_SIZE);
}

// Convert wire event to input_event_t and submit to router
static void submit_peer_event(const i2c_peer_event_t* packed) {
    input_event_t event;
    init_input_event(&event);

    event.dev_addr = I2C_PEER_DEV_ADDR_BASE + packed->player_index;
    event.instance = 0;
    event.type = (input_device_type_t)packed->device_type;
    event.transport = INPUT_TRANSPORT_I2C;
    event.buttons = packed->buttons;

    event.analog[ANALOG_LX] = packed->analog[0];
    event.analog[ANALOG_LY] = packed->analog[1];
    event.analog[ANALOG_RX] = packed->analog[2];
    event.analog[ANALOG_RY] = packed->analog[3];
    event.analog[ANALOG_L2] = packed->analog[4];
    event.analog[ANALOG_R2] = packed->analog[5];

    router_submit_input(&event);
}

void i2c_peer_master_init(const i2c_peer_config_t* config) {
    master_config = *config;
    if (!master_config.addr) {
        master_config.addr = I2C_PEER_DEFAULT_ADDR;
    }
    master_i2c = get_i2c_inst(config->i2c_inst);

    if (!config->skip_i2c_init) {
        i2c_init(master_i2c, I2C_PEER_BAUDRATE);
        gpio_set_function(config->sda_pin, GPIO_FUNC_I2C);
        gpio_set_function(config->scl_pin, GPIO_FUNC_I2C);
        gpio_pull_up(config->sda_pin);
        gpio_pull_up(config->scl_pin);
    }

    master_connected = false;
    master_fail_count = 0;
    master_last_poll_ms = 0;
    master_last_retry_ms = 0;
    master_initialized = true;

    printf("[i2c_peer] Master initialized on I2C%d (SDA=%d, SCL=%d) → slave 0x%02X\n",
           config->i2c_inst, config->sda_pin, config->scl_pin, master_config.addr);
}

void i2c_peer_master_task(void) {
    if (!master_initialized) return;

    uint32_t now = to_ms_since_boot(get_absolute_time());

    // Rate limit: poll at 250Hz when connected, retry at 2Hz when disconnected
    if (master_connected) {
        if (now - master_last_poll_ms < MASTER_POLL_INTERVAL_MS) return;
    } else {
        if (now - master_last_retry_ms < MASTER_RETRY_INTERVAL_MS) return;
        master_last_retry_ms = now;
    }
    master_last_poll_ms = now;

    // Read status register to check connection and data availability
    uint8_t status = 0;
    int ret = master_read_reg(I2C_PEER_REG_STATUS, &status, 1);

    if (ret < 0) {
        // NAK or timeout
        master_fail_count++;
        if (master_connected && master_fail_count >= MASTER_FAIL_THRESHOLD) {
            master_connected = false;
            printf("[i2c_peer] Slave disconnected\n");

            // Send neutral event to clear stale input
            input_event_t event;
            init_input_event(&event);
            event.dev_addr = I2C_PEER_DEV_ADDR_BASE;
            event.type = INPUT_TYPE_GAMEPAD;
            event.transport = INPUT_TRANSPORT_I2C;
            router_submit_input(&event);
        }
        return;
    }

    // Connection established/maintained
    if (!master_connected) {
        master_connected = true;
        uint8_t ver = (status >> I2C_PEER_STATUS_VER_SHIFT) & 0x0F;
        printf("[i2c_peer] Slave connected (protocol v%d)\n", ver);

        // Read device name from slave (one-time on connect)
        char name_buf[32] = {0};
        int name_ret = master_read_reg(I2C_PEER_REG_NAME, (uint8_t*)name_buf, 32);
        if (name_ret == 32 && name_buf[0]) {
            name_buf[31] = '\0';
            strncpy(peer_device_name, name_buf, sizeof(peer_device_name) - 1);
            peer_device_name[sizeof(peer_device_name) - 1] = '\0';
            printf("[i2c_peer] Slave device: %s\n", peer_device_name);
        }
    }
    master_fail_count = 0;

    // Read input event if slave has data
    if (status & I2C_PEER_STATUS_HAS_DATA) {
        i2c_peer_event_t packed;
        ret = master_read_reg(I2C_PEER_REG_PLAYER0, (uint8_t*)&packed, I2C_PEER_EVENT_SIZE);
        if (ret == I2C_PEER_EVENT_SIZE) {
            submit_peer_event(&packed);
        } else {
            printf("[i2c_peer] Read event failed: ret=%d\n", ret);
        }
    } else {
        // Log status periodically (every ~2s) to help debug
        static uint32_t last_status_log = 0;
        if (now - last_status_log > 2000) {
            last_status_log = now;
            printf("[i2c_peer] status=0x%02X (has_data=%d)\n", status, status & I2C_PEER_STATUS_HAS_DATA);
        }
    }
}

// ============================================================================
// INPUT INTERFACE
// ============================================================================

static void i2c_peer_input_init(void) {
    // Init handled by app via i2c_peer_master_init()
}

static void i2c_peer_input_task(void) {
    i2c_peer_master_task();
}

static bool i2c_peer_is_connected(void) {
    return master_connected;
}

static uint8_t i2c_peer_device_count(void) {
    return master_connected ? 1 : 0;
}

const InputInterface i2c_peer_input_interface = {
    .name = "I2C Peer",
    .source = INPUT_SOURCE_I2C_PEER,
    .init = i2c_peer_input_init,
    .task = i2c_peer_input_task,
    .is_connected = i2c_peer_is_connected,
    .get_device_count = i2c_peer_device_count,
};
