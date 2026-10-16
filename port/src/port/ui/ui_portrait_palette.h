// The defined 0..119 body-color range recovered from composeGolferPalette.
#pragma once
#include <stdint.h>
void ui_portrait_palette(uint8_t out[360], int male, const uint8_t base[360],
    const uint8_t shirt[360], const uint8_t pants[360], const uint8_t skin[360],
    const uint8_t trim[360], const uint8_t hair[360], const uint8_t alternate[360]);

