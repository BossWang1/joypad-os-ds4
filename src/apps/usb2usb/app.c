// app.c - USB2USB App Entry Point
// USB to USB HID gamepad adapter
//
// This file contains app-specific initialization and logic.
// The firmware calls app_init() after core system initialization.

#include "app.h"
#include "core/router/router.h"

// Build info fallbacks (normally set by CMake)
#ifndef GIT_COMMIT
#define GIT_COMMIT "not-defined"
#endif
#ifndef BUILD_TIME
#define BUILD_TIME "not-defined"
#endif
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbh/usbh.h"
#include "usb/usbd/usbd.h"

#include "bt/btstack/btstack_host.h"
#include "bt/transport/bt_transport.h"
#include "core/services/leds/leds.h"

#ifdef I2C_PEER_ENABLED
#include "i2c_peer/i2c_peer.h"
#endif
#include "core/buttons.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#ifdef OLED_I2C_INST
#include "core/services/display/display.h"
#include "hardware/gpio.h"

// Arrow characters for display (1=up, 2=down, 3=left, 4=right)
#define ARROW_UP    "\x01"
#define ARROW_DOWN  "\x02"
#define ARROW_LEFT  "\x03"
#define ARROW_RIGHT "\x04"

typedef struct {
    uint32_t mask;
    const char* name;
} button_name_t;

static const button_name_t button_names[] = {
    { JP_BUTTON_DU, ARROW_UP },
    { JP_BUTTON_DR, ARROW_RIGHT },
    { JP_BUTTON_DD, ARROW_DOWN },
    { JP_BUTTON_DL, ARROW_LEFT },
    { JP_BUTTON_B1, "B1" },
    { JP_BUTTON_B2, "B2" },
    { JP_BUTTON_B3, "B3" },
    { JP_BUTTON_B4, "B4" },
    { JP_BUTTON_L1, "L1" },
    { JP_BUTTON_R1, "R1" },
    { JP_BUTTON_L2, "L2" },
    { JP_BUTTON_R2, "R2" },
    { JP_BUTTON_S1, "S1" },
    { JP_BUTTON_S2, "S2" },
    { JP_BUTTON_L3, "L3" },
    { JP_BUTTON_R3, "R3" },
    { JP_BUTTON_A1, "A1" },
    { JP_BUTTON_A2, "A2" },
    { 0, NULL }
};
#endif

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
            if (bt_is_ready()) {
                printf("[app:usb2usb] Starting BT scan (60s)...\n");
                btstack_host_start_timed_scan(60000);
            } else {
                printf("[app:usb2usb] current mode: %s\n",
                       usbd_get_mode_name(usbd_get_mode()));
            }
            break;

        case BUTTON_EVENT_DOUBLE_CLICK: {
            // Double-click to cycle USB output mode
            printf("[app:usb2usb] Double-click - switching USB output mode...\n");
            tud_task();
            sleep_ms(50);
            tud_task();

            usb_output_mode_t next = usbd_get_next_mode();
            printf("[app:usb2usb] Switching to %s\n", usbd_get_mode_name(next));
            usbd_set_mode(next);
            break;
        }

        case BUTTON_EVENT_TRIPLE_CLICK:
            // Triple-click to reset to default HID mode
            printf("[app:usb2usb] Triple-click - resetting to HID mode...\n");
            if (!usbd_reset_to_hid()) {
                printf("[app:usb2usb] Already in HID mode\n");
            }
            break;

        case BUTTON_EVENT_HOLD:
            // Long press to disconnect all devices and clear all bonds
            if (bt_is_ready()) {
                printf("[app:usb2usb] Disconnecting all devices and clearing bonds...\n");
                btstack_host_disconnect_all_devices();
            }
            btstack_host_delete_all_bonds();
            break;

        default:
            break;
    }
}

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &usbh_input_interface,
#ifdef I2C_PEER_ENABLED
    &i2c_peer_input_interface,
#endif
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

static const OutputInterface* output_interfaces[] = {
    &usbd_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// OLED DISPLAY
// ============================================================================

#ifdef OLED_I2C_INST

static const char* transport_str(input_transport_t t) {
    switch (t) {
        case INPUT_TRANSPORT_USB:        return "USB";
        case INPUT_TRANSPORT_BT_CLASSIC: return "BT";
        case INPUT_TRANSPORT_BT_BLE:     return "BLE";
        case INPUT_TRANSPORT_NATIVE:     return "Native";
        case INPUT_TRANSPORT_I2C:        return "I2C";
        default:                         return "?";
    }
}

static void oled_init(void) {
    display_i2c_config_t cfg = {
        .i2c_inst = OLED_I2C_INST,
        .pin_sda  = OLED_I2C_SDA_PIN,
        .pin_scl  = OLED_I2C_SCL_PIN,
        .addr     = OLED_I2C_ADDR,
    };
    display_init_i2c(&cfg);

    // FeatherWing buttons B (GPIO 6) and C (GPIO 5)
    // Button A (GPIO 9) conflicts with MAX3421E INT — skip
    gpio_init(OLED_BUTTON_B_PIN);
    gpio_set_dir(OLED_BUTTON_B_PIN, GPIO_IN);
    gpio_pull_up(OLED_BUTTON_B_PIN);

    gpio_init(OLED_BUTTON_C_PIN);
    gpio_set_dir(OLED_BUTTON_C_PIN, GPIO_IN);
    gpio_pull_up(OLED_BUTTON_C_PIN);

    printf("[app:usb2usb] OLED FeatherWing initialized (I2C%d, buttons B=%d C=%d)\n",
           OLED_I2C_INST, OLED_BUTTON_B_PIN, OLED_BUTTON_C_PIN);
}

static void oled_handle_buttons(void) {
    static bool last_b = true, last_c = true;  // Active-low (pull-up)
    static uint32_t debounce_b = 0, debounce_c = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    bool b = gpio_get(OLED_BUTTON_B_PIN);
    bool c = gpio_get(OLED_BUTTON_C_PIN);

    // Button B: cycle USB output mode
    if (!b && last_b && (now - debounce_b > 200)) {
        debounce_b = now;
        printf("[app:usb2usb] OLED Button B - switching USB output mode...\n");
        tud_task();
        sleep_ms(50);
        tud_task();
        usb_output_mode_t next = usbd_get_next_mode();
        printf("[app:usb2usb] Switching to %s\n", usbd_get_mode_name(next));
        usbd_set_mode(next);
    }
    last_b = b;

    // Button C: start BT scan
    if (!c && last_c && (now - debounce_c > 200)) {
        debounce_c = now;
        if (bt_is_ready()) {
            printf("[app:usb2usb] OLED Button C - Starting BT scan (60s)...\n");
            btstack_host_start_timed_scan(60000);
        }
    }
    last_c = c;
}

static input_event_t oled_cached_event;
static bool oled_has_event = false;

static void oled_update_display(void) {
    static uint32_t last_update = 0;
    static uint32_t last_buttons = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // Cache latest router output (don't gate on display throttle — always consume)
    if (playersCount > 0 && players[0].dev_addr >= 0) {
        const input_event_t* ev = router_get_output(OUTPUT_TARGET_USB_DEVICE, 0);
        if (ev) {
            oled_cached_event = *ev;
            oled_has_event = true;
        }
    }

    // Feed button presses to marquee (edge detection)
    uint32_t buttons = oled_has_event ? oled_cached_event.buttons : 0;
    uint32_t newly_pressed = ~last_buttons & buttons;
    last_buttons = buttons;
    for (int i = 0; button_names[i].name != NULL; i++) {
        if (newly_pressed & button_names[i].mask) {
            display_marquee_add(button_names[i].name);
        }
    }

    if (now - last_update < 50) return;  // 20fps max
    last_update = now;

    display_clear();

    // Line 1 (large, y=0): USB output mode
    usb_output_mode_t mode = usbd_get_mode();
    display_text_large(0, 0, usbd_get_mode_name(mode));

    // Separator
    display_hline(0, 17, DISPLAY_WIDTH);

    // Lines 2-4: Controller info
    if (playersCount > 0 && players[0].dev_addr >= 0) {
        // Line 2 (y=20): Controller name
        const char* name = get_player_name(0);
        if (name) {
            display_text(0, 20, name);
        }

        // Line 3 (y=30): Transport + device address + player
        char info[22];
        snprintf(info, sizeof(info), "%s dev:%d P%d/%d",
                 transport_str(players[0].transport),
                 players[0].dev_addr,
                 players[0].player_number, playersCount);
        display_text(0, 30, info);

        // Line 4 (y=40): Analog sticks + triggers
        if (oled_has_event) {
            char line[22];
            snprintf(line, sizeof(line), "L:%02X,%02X R:%02X,%02X T:%02X,%02X",
                     oled_cached_event.analog[ANALOG_LX], oled_cached_event.analog[ANALOG_LY],
                     oled_cached_event.analog[ANALOG_RX], oled_cached_event.analog[ANALOG_RY],
                     oled_cached_event.analog[ANALOG_L2], oled_cached_event.analog[ANALOG_R2]);
            display_text(0, 40, line);
        }
    } else {
        display_text(0, 28, "No controller");
    }

    // Bottom (y=52): Button marquee
    display_marquee_tick();
    display_marquee_render(52);

    display_update();
}

#endif // OLED_I2C_INST

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:usb2usb] Initializing USB2USB v%s\n", JOYPAD_VERSION);

    // Initialize button service
    button_init();
    button_set_callback(on_button_event);

    // Configure router for USB2USB
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,  // Merge all USB inputs to single output
        .transform_flags = TRANSFORM_FLAGS,
        // Mouse-to-analog: Map mouse to right stick for camera control
        // Useful for accessibility (mouthpad, head tracker) alongside gamepad
        .mouse_target_x = ANALOG_RY,            // Right stick X
        .mouse_target_y = MOUSE_AXIS_DISABLED,  // Y disabled (X-only for camera pan)
        .mouse_drain_rate = 0,                  // No drain - hold position until head returns
    };
    router_init(&router_cfg);

    // Add default route: USB Host → USB Device
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_USB_DEVICE, 0);

#ifdef I2C_PEER_ENABLED
    // Add I2C peer route and initialize master
    router_add_route(INPUT_SOURCE_I2C_PEER, OUTPUT_TARGET_USB_DEVICE, 0);
    {
        i2c_peer_config_t peer_cfg = {
            .i2c_inst = OLED_I2C_INST,
            .sda_pin = OLED_I2C_SDA_PIN,
            .scl_pin = OLED_I2C_SCL_PIN,
            .addr = I2C_PEER_DEFAULT_ADDR,
            .skip_i2c_init = true,  // OLED already initialized I2C bus
        };
        i2c_peer_master_init(&peer_cfg);
    }
#endif

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Set boot LED color (visible during potentially blocking USB host init)
    leds_set_color(80, 80, 80);

#ifdef OLED_I2C_INST
    oled_init();
#endif

    printf("[app:usb2usb] Initialization complete\n");
    printf("[app:usb2usb]   Routing: USB Host → USB Device (HID Gamepad)\n");
    printf("[app:usb2usb]   Player slots: %d\n", MAX_PLAYER_SLOTS);
    printf("[app:usb2usb]   Double-click button (GPIO7) to switch USB mode\n");
}

// ============================================================================
// APP TASK (Optional - called from main loop)
// ============================================================================

void app_task(void)
{
    // Process button input
    button_task();

    // Update LED color when USB output mode changes
    static usb_output_mode_t last_led_mode = USB_OUTPUT_MODE_COUNT;
    usb_output_mode_t mode = usbd_get_mode();
    if (mode != last_led_mode) {
        uint8_t r, g, b;
        usbd_get_mode_color(mode, &r, &g, &b);
        leds_set_color(r, g, b);
        last_led_mode = mode;
    }

    // Update LED with connected device count (USB HID + BT)
    // This makes LED go solid as soon as a controller is detected,
    // without waiting for button press to assign as player
    int devices = 0;
    for (uint8_t addr = 1; addr < MAX_DEVICES; addr++) {
        if (tuh_mounted(addr) && tuh_hid_instance_count(addr) > 0) {
            devices++;
        }
    }
    if (bt_is_ready()) {
        devices += btstack_classic_get_connection_count();
    }
    leds_set_connected_devices(devices);

    // Route feedback from USB device output to USB host input controllers
    // The output interface receives rumble/LED from the console/host
    // and we forward it to connected controllers via the feedback system
    if (usbd_output_interface.get_feedback) {
        output_feedback_t fb;
        if (usbd_output_interface.get_feedback(&fb)) {
            // Set feedback for all active players
            for (int i = 0; i < playersCount; i++) {
                feedback_set_rumble(i, fb.rumble_left, fb.rumble_right);
                if (fb.led_player > 0) {
                    feedback_set_led_player(i, fb.led_player);
                }
                if (fb.led_r || fb.led_g || fb.led_b) {
                    feedback_set_led_rgb(i, fb.led_r, fb.led_g, fb.led_b);
                }
            }
        }
    }

#ifdef OLED_I2C_INST
    oled_handle_buttons();
    oled_update_display();
#endif

#ifdef I2C_PEER_ENABLED
    // Send device status to I2C peer slave (~10Hz or on change)
    {
        static uint32_t last_status_send = 0;
        static i2c_peer_status_t last_status = {0};
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        i2c_peer_status_t status = {0};

        // Build status from current state
        if (playersCount > 0 && players[0].dev_addr >= 0) {
            status.flags = I2C_PEER_FLAG_CONNECTED;
            status.transport = (uint8_t)players[0].transport;
            status.player_number = (uint8_t)players[0].player_number;
            const char* name = get_player_name(0);
            if (name && name[0]) {
                status.flags |= I2C_PEER_FLAG_NAME_VALID;
                strncpy(status.name, name, sizeof(status.name) - 1);
                status.name[sizeof(status.name) - 1] = '\0';
            }
        }

        // USB output mode + color
        usb_output_mode_t cur_mode = usbd_get_mode();
        status.usb_mode = (uint8_t)cur_mode;
        usbd_get_mode_color(cur_mode, &status.mode_color[0],
                            &status.mode_color[1], &status.mode_color[2]);

        // Feedback (rumble, LEDs)
        if (usbd_output_interface.get_feedback) {
            output_feedback_t fb;
            if (usbd_output_interface.get_feedback(&fb)) {
                status.rumble_left = fb.rumble_left;
                status.rumble_right = fb.rumble_right;
                status.led_player = fb.led_player;
                status.led_color[0] = fb.led_r;
                status.led_color[1] = fb.led_g;
                status.led_color[2] = fb.led_b;
            }
        }

        // Send on change or every 100ms (~10Hz)
        if (memcmp(&status, &last_status, sizeof(status)) != 0 ||
            now_ms - last_status_send >= 100) {
            i2c_peer_master_send_status(&status);
            last_status = status;
            last_status_send = now_ms;
        }
    }
#endif
}
