// ws2812_nrf.c - NeoPixel stub for Seeed XIAO nRF52840
//
// Seeed XIAO nRF52840 base board has no NeoPixel. This stub satisfies the linker.

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

void neopixel_init(void)
{
    printf("[neopixel] nRF52840 stub initialized (no LED)\n");
}

void neopixel_task(int pat)
{
    (void)pat;
}

void neopixel_set_override_color(uint8_t r, uint8_t g, uint8_t b)
{
    (void)r; (void)g; (void)b;
}

void neopixel_indicate_profile(uint8_t profile_index)
{
    (void)profile_index;
}

bool neopixel_is_indicating(void)
{
    return false;
}
