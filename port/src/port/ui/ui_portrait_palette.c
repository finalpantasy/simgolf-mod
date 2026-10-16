#include "port/ui/ui_portrait_palette.h"
#include <string.h>

void ui_portrait_palette(uint8_t out[360], int male, const uint8_t base[360],
    const uint8_t shirt[360], const uint8_t pants[360], const uint8_t skin[360],
    const uint8_t trim[360], const uint8_t hair[360], const uint8_t alternate[360]) {
  memcpy(out, base, 360);
  memcpy(out, shirt, 20 * 3);
  memcpy(out + 20 * 3, pants + 20 * 3, 20 * 3);
  memcpy(out + 40 * 3, skin + 40 * 3, 20 * 3);
  memcpy(out + 60 * 3, trim + 60 * 3, 20 * 3);
  if (male) {
    // Original 0x462440: ordinary male slots use alternate skin at 80..86.
    memcpy(out + 80 * 3, alternate + 80 * 3, 7 * 3);
    // Original 0x462347: the shirt also supplies indices 107..119.
    memcpy(out + 107 * 3, shirt + 107 * 3, 13 * 3);
  } else {
    // Original 0x46265c and 0x4626b3.
    memcpy(out + 80 * 3, hair + 80 * 3, 10 * 3);
    memcpy(out + 90 * 3, alternate + 90 * 3, 7 * 3);
  }
}

