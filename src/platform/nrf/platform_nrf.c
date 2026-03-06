// platform_nrf.c - Platform HAL for Seeed XIAO nRF52840 (Zephyr)
//
// Implements platform.h using Zephyr kernel APIs.
// Mirrors platform_esp32.c but for nRF Connect SDK.

#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/hwinfo.h>
#include <nrfx.h>

// ============================================================================
// TIME
// ============================================================================

uint32_t platform_time_ms(void)
{
    return k_uptime_get_32();
}

uint32_t platform_time_us(void)
{
    return (uint32_t)k_ticks_to_us_floor64(k_uptime_ticks());
}

void platform_sleep_ms(uint32_t ms)
{
    k_msleep(ms);
}

// ============================================================================
// IDENTITY
// ============================================================================

void platform_get_serial(char* buf, size_t len)
{
    uint8_t id[8];
    ssize_t id_len = hwinfo_get_device_id(id, sizeof(id));
    if (id_len <= 0) {
        snprintf(buf, len, "000000000000");
        return;
    }

    size_t pos = 0;
    for (int i = 0; i < id_len && pos + 2 < len; i++) {
        pos += snprintf(buf + pos, len - pos, "%02x", id[i]);
    }
}

void platform_get_unique_id(uint8_t* buf, size_t len)
{
    ssize_t id_len = hwinfo_get_device_id(buf, len);
    if (id_len < (ssize_t)len) {
        memset(buf + (id_len > 0 ? id_len : 0), 0, len - (id_len > 0 ? id_len : 0));
    }
}

// ============================================================================
// REBOOT
// ============================================================================

void platform_reboot(void)
{
    sys_reboot(SYS_REBOOT_COLD);
}

void platform_reboot_bootloader(void)
{
    // Adafruit nRF52 UF2 bootloader checks GPREGRET for magic value 0x57
    // on boot. If set, it enters UF2 mass storage mode instead of the app.
    // Nordic DFU bootloader uses 0xB1 instead.
    NRF_POWER->GPREGRET = 0x57;
    sys_reboot(SYS_REBOOT_COLD);
}
