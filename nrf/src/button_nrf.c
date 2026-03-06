// button_nrf.c - Button driver stub for Seeed XIAO nRF52840
//
// Seeed XIAO nRF52840 has no exposed user button. This stub satisfies the linker.

#include "core/services/button/button.h"
#include <stdio.h>

static button_callback_t button_cb = NULL;

void button_init(void)
{
    printf("[button_nrf] No user button on this board (stub)\n");
}

void button_set_callback(button_callback_t cb)
{
    button_cb = cb;
}

button_event_t button_task(void)
{
    // No button to poll
    return BUTTON_EVENT_NONE;
}
