// main.c - Seeed XIAO nRF52840 bt2usb entry point
//
// Zephyr entry point for the bt2usb app on Seeed XIAO nRF52840 (xiao_ble).
// BTstack runs in its own Zephyr thread (created by bt_transport_nrf.c).
// Main thread handles USB device, app logic, LED, and storage.

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/onoff.h>
#include <zephyr/fatal.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <nrfx.h>

#include "tusb.h"
#include "platform/platform.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "core/services/players/manager.h"
#include "core/services/leds/leds.h"
#include "core/services/storage/storage.h"

// App layer
extern void app_init(void);
extern void app_task(void);
extern const OutputInterface** app_get_output_interfaces(uint8_t* count);
extern const InputInterface** app_get_input_interfaces(uint8_t* count);

static const OutputInterface** outputs = NULL;
static uint8_t output_count = 0;
static const InputInterface** inputs = NULL;
static uint8_t input_count = 0;
const OutputInterface* active_output = NULL;

// ============================================================================
// FAULT HANDLER — Zephyr's fault dump goes to UART console automatically.
// We just turn on blue LED as visual indicator and halt.
// ============================================================================
#define FAULT_LED_PIN 6  // Blue LED on XIAO BLE, active low

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    (void)reason; (void)esf;
    NRF_P0->DIRSET = (1U << FAULT_LED_PIN);
    NRF_P0->OUTCLR = (1U << FAULT_LED_PIN);  // LED on
    for (;;) { __WFI(); }
}

// ============================================================================
// BT CONTROLLER ASSERT HANDLER
// ============================================================================

void bt_ctlr_assert_handle(char *file, uint32_t line)
{
    printf("[BT] Controller assert: %s:%u\n", file, (unsigned)line);
}

// ============================================================================
// USB POWER + IRQ SETUP
// ============================================================================

// TinyUSB's dcd_nrf5x.c requires tusb_hal_nrf_power_event() to be called
// with VBUS power events to start the USB peripheral.

extern void dcd_int_handler(uint8_t rhport);
extern void tusb_hal_nrf_power_event(uint32_t event);

// USBD interrupt handler
static void usbd_isr(const void *arg)
{
    (void)arg;
    dcd_int_handler(0);
}

// HFCLK must stay running for USB to work. MPSL (BLE radio stack) manages
// HFCLK and will stop it when the radio is idle, breaking USB. We request
// HFCLK through Zephyr's onoff manager so MPSL keeps it running.
static struct onoff_client hfclk_cli;

static void usb_hfclk_request(void)
{
    struct onoff_manager *mgr =
        z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
    sys_notify_init_spinwait(&hfclk_cli.notify);
    int err = onoff_request(mgr, &hfclk_cli);
    if (err < 0) {
        printf("[usb] HFCLK request failed: %d\n", err);
        return;
    }
    // Wait for HFCLK to stabilize
    int res;
    while (sys_notify_fetch_result(&hfclk_cli.notify, &res) == -EAGAIN) {
        k_yield();
    }
    printf("[usb] HFCLK running\n");
}

// Call after tusb_init() to trigger USB enumeration.
// Uses dynamic interrupt registration and unconditionally fires power
// events (bt2usb is always USB-powered, so VBUS is always present).
static void usb_power_init(void)
{
    // Request HFCLK through Zephyr's clock manager (keeps MPSL aware)
    usb_hfclk_request();

    // Register USBD ISR dynamically (runtime, not via static ISR table)
    irq_connect_dynamic(USBD_IRQn, 2, usbd_isr, NULL, 0);

    // Reset USBD to clean state (bootloader may have left it active)
    if (NRF_USBD->ENABLE) {
        NRF_USBD->USBPULLUP = 0;
        __ISB(); __DSB();
        NVIC_DisableIRQ(USBD_IRQn);
        NRF_USBD->INTENCLR = NRF_USBD->INTEN;
        NRF_USBD->ENABLE = 0;
        __ISB(); __DSB();
    }

    // Log USBREGSTATUS for debugging
    uint32_t usb_reg = NRF_POWER->USBREGSTATUS;
    printf("[usb] USBREGSTATUS=0x%08x VBUS=%d OUTRDY=%d\n",
           (unsigned)usb_reg,
           !!(usb_reg & POWER_USBREGSTATUS_VBUSDETECT_Msk),
           !!(usb_reg & POWER_USBREGSTATUS_OUTPUTRDY_Msk));

    // Always fire both events — bt2usb is USB-powered so VBUS is present.
    // Don't gate on USBREGSTATUS since some boards may not report it.
    printf("[usb] Firing DETECTED event\n");
    tusb_hal_nrf_power_event(0);  // USB_EVT_DETECTED

    printf("[usb] Firing READY event\n");
    tusb_hal_nrf_power_event(2);  // USB_EVT_READY

    // Belt and suspenders: ensure USBD IRQ is enabled
    irq_enable(USBD_IRQn);

    printf("[usb] USBD init complete, pullup=%d\n",
           !!(NRF_USBD->USBPULLUP));
}

// ============================================================================
// MAIN
// ============================================================================

int main(void)
{
    printf("[joypad] Starting bt2usb on Seeed XIAO nRF52840...\n");

    // Initialize shared services
    leds_init();
    storage_init();
    players_init();
    app_init();

    // Get and initialize input interfaces
    inputs = app_get_input_interfaces(&input_count);
    for (uint8_t i = 0; i < input_count; i++) {
        if (inputs[i] && inputs[i]->init) {
            printf("[joypad] Initializing input: %s\n", inputs[i]->name);
            inputs[i]->init();
        }
    }

    // Get and initialize output interfaces
    outputs = app_get_output_interfaces(&output_count);
    if (output_count > 0 && outputs[0]) {
        active_output = outputs[0];
    }
    for (uint8_t i = 0; i < output_count; i++) {
        if (outputs[i] && outputs[i]->init) {
            printf("[joypad] Initializing output: %s\n", outputs[i]->name);
            outputs[i]->init();
        }
    }

    printf("[joypad] tusb_inited=%d\n", tud_inited());

    // Trigger USB enumeration (handles VBUS already present at boot)
    usb_power_init();

    printf("[joypad] Entering main loop\n");

    // Main loop
    while (1) {
        // Poll TinyUSB (non-blocking)
        tud_task_ext(0, false);

        leds_task();
        players_task();
        storage_task();

        // Poll input interfaces
        for (uint8_t i = 0; i < input_count; i++) {
            if (inputs[i] && inputs[i]->task) {
                inputs[i]->task();
            }
        }

        // Run output interface tasks
        for (uint8_t i = 0; i < output_count; i++) {
            if (outputs[i] && outputs[i]->task) {
                outputs[i]->task();
            }
        }

        app_task();

        // Yield to other Zephyr threads (BTstack runs in its own thread)
        k_msleep(1);
    }

    return 0;
}
