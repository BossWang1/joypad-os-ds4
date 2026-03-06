// tusb_config_nrf.h - TinyUSB configuration for Seeed XIAO nRF52840
//
// Device-only configuration for bt2usb on Seeed XIAO nRF52840.
// No USB host (BT input only), same HID/CDC config as ESP32.

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

#define CFG_TUSB_MCU                OPT_MCU_NRF5X
#define CFG_TUSB_OS                 OPT_OS_NONE

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#endif

//--------------------------------------------------------------------
// USB DEVICE CONFIGURATION
//--------------------------------------------------------------------

// Device-only mode (no USB host on nRF52840 bt2usb)
#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_DEVICE

#define CFG_TUD_ENDPOINT0_SIZE      64

// Standard HID gamepad mode
#define CFG_TUD_HID                 4   // Up to 4 HID gamepads

// Xbox Original (XID) mode support
#define CFG_TUD_XID                 1
#define CFG_TUD_XID_EP_BUFSIZE      32

// Xbox 360 (XInput) mode support
#define CFG_TUD_XINPUT              1
#define CFG_TUD_XINPUT_EP_BUFSIZE   32

// GameCube Adapter mode support
#define CFG_TUD_GC_ADAPTER          1
#define CFG_TUD_GC_ADAPTER_EP_BUFSIZE 37

// CDC configuration: single data port
#define CFG_TUD_CDC                 1

#define CFG_TUD_MSC                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

// HID buffer sizes
#define CFG_TUD_HID_EP_BUFSIZE      64

// CDC buffer sizes
#define CFG_TUD_CDC_RX_BUFSIZE      256
#define CFG_TUD_CDC_TX_BUFSIZE      1024
#define CFG_TUD_CDC_EP_BUFSIZE      64

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
