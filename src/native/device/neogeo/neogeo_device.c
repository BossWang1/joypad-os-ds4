// neogeo_device.c

#include "neogeo_device.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/iobank0.h"
#include "hardware/structs/padsbank0.h"
#include "hardware/structs/sio.h"

// Early init constructor - runs before main() to set output pins HIGH
// This prevents "all buttons pressed" state during boot
__attribute__((constructor(101)))
static void neogeo_early_gpio_init(void)
{
    // Direct register access for fastest possible init
    // Set output pins as outputs with HIGH value
    
    // Enable outputs and set HIGH
    sio_hw->gpio_oe_set = NEOGEO_GPIO_MASK;
    sio_hw->gpio_set = NEOGEO_GPIO_MASK;
}

#if CFG_TUSB_DEBUG >= 1
#include "hardware/uart.h"
#endif

#include "core/router/router.h"
#include "core/input_event.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/services/profiles/profile_indicator.h"
#include "core/services/codes/codes.h"

// ============================================================================
// PROFILE SYSTEM (Delegates to core profile service)
// ============================================================================

static uint8_t neogeo_get_player_count_for_profile(void) {
    return router_get_player_count(OUTPUT_TARGET_NEOGEO);
}

static uint8_t neogeo_get_profile_count(void) {
    return profile_get_count(OUTPUT_TARGET_NEOGEO);
}

static uint8_t neogeo_get_active_profile(void) {
    return profile_get_active_index(OUTPUT_TARGET_NEOGEO);
}

static void neogeo_set_active_profile(uint8_t index) {
    profile_set_active(OUTPUT_TARGET_NEOGEO, index);
}

static const char* neogeo_get_profile_name(uint8_t index) {
    return profile_get_name(OUTPUT_TARGET_NEOGEO, index);
}

// ============================================================================
// PUSH-BASED OUTPUT VIA ROUTER TAP
// ============================================================================
// GPIO updates happen immediately when input arrives via router tap callback,
// eliminating the one-loop-iteration polling delay. The tap fires from within
// router_submit_input() on the same iteration input is received.

// Last raw button state from tap — used by task loop for combo detection
static uint32_t tap_last_buttons = 0;
static bool tap_has_update = false;

// Tap callback — fires immediately from router_submit_input().
// Must be fast: just apply profile + update GPIO. No printf or blocking.
static void __not_in_flash_func(neogeo_tap_callback)(output_target_t output,
                                                      uint8_t player_index,
                                                      const input_event_t* event)
{
  (void)output;
  (void)player_index;

  // Store raw buttons for combo detection in task loop
  tap_last_buttons = event->buttons;
  tap_has_update = true;

  // Only update GPIO if we have connected players
  if (playersCount == 0) return;

  // Apply profile remapping
  const profile_t* profile = profile_get_active(OUTPUT_TARGET_NEOGEO);
  profile_output_t mapped;
  profile_apply(profile, event->buttons,
                event->analog[ANALOG_LX], event->analog[ANALOG_LY],
                event->analog[ANALOG_RX], event->analog[ANALOG_RY],
                event->analog[ANALOG_L2], event->analog[ANALOG_R2],
                event->analog[ANALOG_RZ],
                &mapped);

  uint32_t neogeo_buttons = 0;

  // Mapping the buttons (active-low: 0 = pressed)
  neogeo_buttons |= (mapped.buttons & JP_BUTTON_S2) ? NEOGEO_S2_PIN : 0;  // Option -> START
  neogeo_buttons |= (mapped.buttons & JP_BUTTON_S1) ? NEOGEO_S1_PIN : 0;  // Share -> SELECT
  neogeo_buttons |= (mapped.buttons & JP_BUTTON_DD) ? NEOGEO_DD_PIN : 0;  // Dpad Down -> D-DOWN
  neogeo_buttons |= (mapped.buttons & JP_BUTTON_DL) ? NEOGEO_DL_PIN : 0;  // Dpad Left -> D-LEFT
  neogeo_buttons |= (mapped.buttons & JP_BUTTON_DU) ? NEOGEO_DU_PIN : 0;  // Dpad Up -> D-UP
  neogeo_buttons |= (mapped.buttons & JP_BUTTON_DR) ? NEOGEO_DR_PIN : 0;  // Dpad Right -> D-RIGHT
  neogeo_buttons |= (mapped.buttons & JP_BUTTON_B3) ? NEOGEO_B1_PIN : 0;  // Square -> B1
  neogeo_buttons |= (mapped.buttons & JP_BUTTON_B4) ? NEOGEO_B2_PIN : 0;  // Triangle -> B2
  neogeo_buttons |= (mapped.buttons & JP_BUTTON_R1) ? NEOGEO_B3_PIN : 0;  // R1 -> B3
  neogeo_buttons |= (mapped.buttons & JP_BUTTON_B1) ? NEOGEO_B4_PIN : 0;  // Cross -> B4
  neogeo_buttons |= (mapped.buttons & JP_BUTTON_B2) ? NEOGEO_B5_PIN : 0;  // Circle -> B5
  neogeo_buttons |= (mapped.buttons & JP_BUTTON_R2) ? NEOGEO_B6_PIN : 0;  // R2 -> B6
  // D-pad from left analog stick (threshold at 64/192 from center 128)
  // HID convention: 0=up, 128=center, 255=down
  neogeo_buttons |= (mapped.left_x < 64)  ? NEOGEO_DL_PIN : 0;  // Dpad Left -> D-LEFT
  neogeo_buttons |= (mapped.left_x > 192) ? NEOGEO_DR_PIN : 0;  // Dpad Right -> D-RIGHT
  neogeo_buttons |= (mapped.left_y < 64)  ? NEOGEO_DU_PIN : 0;  // Dpad Up -> D-UP
  neogeo_buttons |= (mapped.left_y > 192) ? NEOGEO_DD_PIN : 0;  // Dpad Down -> D-DOWN

  gpio_put_masked(NEOGEO_GPIO_MASK, ~neogeo_buttons);
}

// init for NEOGEO communication
void neogeo_init()
{
  // Set output pins HIGH immediately to prevent "all buttons pressed" during boot
  gpio_init_mask(NEOGEO_GPIO_MASK);
  gpio_set_dir_out_masked(NEOGEO_GPIO_MASK);
  gpio_put_masked(NEOGEO_GPIO_MASK, NEOGEO_GPIO_MASK);
  
  profile_indicator_disable_rumble();
  profile_set_player_count_callback(neogeo_get_player_count_for_profile);

  // Register tap for push-based GPIO updates — fires immediately from
  // router_submit_input() instead of waiting for next task loop iteration
  router_set_tap(OUTPUT_TARGET_NEOGEO, neogeo_tap_callback);

  #if CFG_TUSB_DEBUG >= 1
  // Initialize chosen UART
  uart_init(UART_ID, BAUD_RATE);

  // Set the GPIO function for the UART pins
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  #endif
}

// Task loop — handles non-latency-critical work (combo detection, cheat codes).
// GPIO updates are now handled by the tap callback above.
void neogeo_task()
{
  static uint32_t last_buttons = 0;
  bool had_update = false;

  // Pick up raw button state from tap callback
  if (tap_has_update) {
    last_buttons = tap_last_buttons;
    tap_has_update = false;
    had_update = true;
  }

  // Always check profile switching combo with last known state
  // This ensures combo detection works even when controller doesn't send updates while buttons held
  if (playersCount > 0) {
    profile_check_switch_combo(last_buttons);
  }

  // Run cheat code detection when we had new input
  if (had_update && playersCount > 0) {
    codes_task_for_output(OUTPUT_TARGET_NEOGEO);
  }
}

//

//-----------------------------------------------------------------------------
// Core1 Entry Point
//-----------------------------------------------------------------------------

void __not_in_flash_func(core1_task)(void) {
  while (1) {
    sleep_ms(100);
  }
}

// Input flow: USB drivers → router_submit_input() → tap callback → GPIO (immediate)
//             Task loop handles combo detection and cheat codes

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

#include "core/output_interface.h"

const OutputInterface neogeo_output_interface = {
    .name = "NEOGEO",
    .target = OUTPUT_TARGET_NEOGEO,
    .init = neogeo_init,
    .core1_task = NULL,
    .task = neogeo_task,  // NEOGEO needs periodic scan detection task
    .get_rumble = NULL,
    .get_player_led = NULL,
    .get_profile_count = neogeo_get_profile_count,
    .get_active_profile = neogeo_get_active_profile,
    .set_active_profile = neogeo_set_active_profile,
    .get_profile_name = neogeo_get_profile_name,
    .get_trigger_threshold = NULL,
};
