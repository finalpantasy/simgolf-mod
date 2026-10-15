#pragma once
#include <stdint.h>

// An RGBA target over transparent black contains premultiplied colour.
// HUD images use straight alpha, so remove that multiplication once.
static inline uint32_t hd_preview_straight(uint32_t p) {
  unsigned a = p >> 24;
  if (!a) return 0;
  unsigned r = (((p >> 16) & 255u) * 255u + a / 2) / a;
  unsigned g = (((p >> 8) & 255u) * 255u + a / 2) / a;
  unsigned b = ((p & 255u) * 255u + a / 2) / a;
  return (a << 24) | ((r > 255 ? 255 : r) << 16) |
         ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
}
