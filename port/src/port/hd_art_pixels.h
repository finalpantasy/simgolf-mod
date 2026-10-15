// Per-pixel palette shadow conversion for still art, shared with its test.
#pragma once
#include <stddef.h>
#include <stdint.h>

static inline void hd_art_shadow_indexed_pixel(uint8_t *rgba, uint8_t index, const uint16_t *levels) {
  if (index < 0xf8 || index > 0xfb) return;
  rgba[0] = rgba[1] = rgba[2] = 0;
  rgba[3] = (uint8_t)(rgba[3] * (uint8_t)levels[index] / 255);
}

// The RGBA still may be an integer enlargement of the original indexed sheet.
// Map each output pixel to its source index and change only the four shadow
// inks. Returning 0 leaves the caller free to use the original game draw.
static inline int hd_art_shadow_indexed(uint8_t *rgba, int rw, int rh, const uint8_t *indices, int iw, int ih,
                                        int sx, int sy, int sw, int sh, const uint16_t *levels, int enabled) {
  if (!enabled) return 1;
  if (!rgba || !indices || !levels || rw <= 0 || rh <= 0 || iw <= 0 || ih <= 0 || sx < 0 || sy < 0 ||
      sw <= 0 || sh <= 0 || sx > iw - sw || sy > ih - sh)
    return 0;
  for (int y = 0; y < rh; y++) {
    int iy = sy + (int)((int64_t)y * sh / rh);
    for (int x = 0; x < rw; x++) {
      int ix = sx + (int)((int64_t)x * sw / rw);
      hd_art_shadow_indexed_pixel(rgba + ((size_t)y * rw + x) * 4, indices[(size_t)iy * iw + ix], levels);
    }
  }
  return 1;
}
