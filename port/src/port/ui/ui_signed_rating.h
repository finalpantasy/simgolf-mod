#ifndef SIMGOLF_UI_SIGNED_RATING_H
#define SIMGOLF_UI_SIGNED_RATING_H
#include <stdint.h>
#include <stdio.h>
/* Preserve the sign even when the whole-number component is zero. */
static void ui_signed_rating(char *out, size_t cap, int value) {
  uint32_t magnitude = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
  snprintf(out, cap, "%s%u.%02u", value < 0 ? "-" : "",
           magnitude / 100, magnitude % 100);
}
#endif
