// ble_nus.c - BLE Nordic UART Service (NUS) bridge for CDC protocol
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Bridges the CDC binary protocol over BLE NUS (Nordic UART Service),
// enabling wireless configuration using the same command set as USB CDC.
// RX: NUS write characteristic → cdc_protocol_rx_byte() → command dispatch
// TX: cdc_protocol_send() → nordic_spp_service_server_send()

#include "ble_nus.h"
#include "usb/usbd/cdc/cdc_protocol.h"
#include "usb/usbd/cdc/cdc_commands.h"

#include "btstack_defines.h"
#include "btstack_event.h"
#include "ble/gatt-service/nordic_spp_service_server.h"

#include <stdio.h>
#include <string.h>

// ============================================================================
// STATE
// ============================================================================

static cdc_protocol_t nus_protocol_ctx;
static hci_con_handle_t nus_con_handle = HCI_CON_HANDLE_INVALID;
static bool nus_connected = false;

// TX queue: NUS can only send one notification at a time (flow-controlled)
#define NUS_TX_BUF_SIZE CDC_MAX_PACKET
static uint8_t nus_tx_buf[NUS_TX_BUF_SIZE];
static uint16_t nus_tx_len = 0;
static bool nus_tx_pending = false;
static btstack_context_callback_registration_t nus_send_request;

// ============================================================================
// NUS TRANSPORT WRITE
// ============================================================================

static uint32_t nus_write(const uint8_t* data, uint16_t len)
{
    if (!nus_connected || nus_con_handle == HCI_CON_HANDLE_INVALID) {
        return 0;
    }

    // Try direct send first
    int result = nordic_spp_service_server_send(nus_con_handle, data, len);
    if (result == 0) {
        return len;
    }

    // Queue for later if direct send fails (busy)
    if (len <= NUS_TX_BUF_SIZE && !nus_tx_pending) {
        memcpy(nus_tx_buf, data, len);
        nus_tx_len = len;
        nus_tx_pending = true;
        nordic_spp_service_server_request_can_send_now(&nus_send_request, nus_con_handle);
        return len;
    }

    printf("[ble_nus] TX overflow, dropping %d bytes\n", len);
    return 0;
}

// ============================================================================
// NUS PACKET HANDLER
// ============================================================================

static void nus_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel;

    if (packet_type == RFCOMM_DATA_PACKET) {
        // RX data from BLE central — feed into protocol parser
        // Swap active protocol so command responses route back via NUS
        cdc_commands_set_active_protocol(&nus_protocol_ctx);
        for (uint16_t i = 0; i < size; i++) {
            cdc_protocol_rx_byte(&nus_protocol_ctx, packet[i]);
        }
        cdc_commands_set_active_protocol(NULL);  // Restore USB CDC
        return;
    }

    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_GATTSERVICE_META:
            switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
                case GATTSERVICE_SUBEVENT_SPP_SERVICE_CONNECTED:
                    nus_con_handle = gattservice_subevent_spp_service_connected_get_con_handle(packet);
                    nus_connected = true;
                    printf("[ble_nus] NUS connected (handle=0x%04x)\n", nus_con_handle);
                    break;

                case GATTSERVICE_SUBEVENT_SPP_SERVICE_DISCONNECTED:
                    printf("[ble_nus] NUS disconnected\n");
                    nus_con_handle = HCI_CON_HANDLE_INVALID;
                    nus_connected = false;
                    nus_tx_pending = false;
                    cdc_protocol_rx_reset(&nus_protocol_ctx);
                    break;
            }
            break;

        case ATT_EVENT_CAN_SEND_NOW:
            if (nus_tx_pending && nus_tx_len > 0) {
                nordic_spp_service_server_send(nus_con_handle, nus_tx_buf, nus_tx_len);
                nus_tx_pending = false;
                nus_tx_len = 0;
            }
            break;

        default:
            break;
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void ble_nus_init(void)
{
    printf("[ble_nus] Initializing NUS service\n");

    // Initialize protocol context with NUS transport
    cdc_protocol_init(&nus_protocol_ctx, cdc_commands_process);
    nus_protocol_ctx.write = nus_write;

    // Initialize Nordic SPP (NUS) service
    nordic_spp_service_server_init(&nus_packet_handler);

    printf("[ble_nus] NUS ready\n");
}

void ble_nus_task(void)
{
    // Currently no periodic work needed — all NUS processing is event-driven
}
