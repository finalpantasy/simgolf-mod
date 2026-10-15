// Per-pixel palette shadow conversion for still art, shared with its test.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  HD_ART_STILL_NORMAL,
  HD_ART_STILL_SHADE,
  HD_ART_STILL_INDEXED_SHADOW,
  HD_ART_STILL_OPTIONAL_INDEXED_SHADOW,
};

// Still sprites in the shadow draw slots are usually shade masks. course1.pcx
// also uses those slots for colored course art, so only its original shadow
// palette inks should be darkened. cliffs01.pcx contains ordinary rock art.
static inline int hd_art_still_effect(int slot, const char *norm, int course_shadow_enabled) {
  if ((slot != 27 && slot != 30) || !norm) return HD_ART_STILL_NORMAL;
  if (strstr(norm, "holemark")) return HD_ART_STILL_OPTIONAL_INDEXED_SHADOW;
  if (strstr(norm, "course1.pcx"))
    return course_shadow_enabled ? HD_ART_STILL_INDEXED_SHADOW : HD_ART_STILL_SHADE;
  if (strstr(norm, "cliffs01")) return HD_ART_STILL_NORMAL;
  return HD_ART_STILL_SHADE;
}

// course1.pcx is drawn by slot 30 here: that blitter treats 0xf8..0xfe as
// seven light/shadow planes. Slot 27 only treats 0xf8 as a LUT operation.
// holemark's established conversion is intentionally limited to 0xf8..0xfb.
static inline int hd_art_still_shadow_max_index(int slot, const char *norm) {
  if ((slot != 27 && slot != 30) || !norm) return 0;
  if (strstr(norm, "holemark")) return 0xfb;
  if (strstr(norm, "course1.pcx")) return slot == 30 ? 0xfe : 0xf8;
  return 0;
}

static inline void hd_art_shadow_indexed_pixel(uint8_t *rgba, uint8_t index, const uint16_t *levels, int max_index) {
  // These original draw slots skip 0xff even if an upscale supplied its own
  // opaque/partial alpha for that palette background. Keep the source mask.
  if (index == 0xff) {
    rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
    return;
  }
  if (max_index < 0xf8 || max_index > 0xfe || index < 0xf8 || index > max_index) return;
  rgba[0] = rgba[1] = rgba[2] = 0;
  rgba[3] = (uint8_t)(rgba[3] * (uint8_t)levels[index] / 255);
}

// The RGBA still may be an integer enlargement of the original indexed sheet.
// Map each output pixel to its source index and change only the indices handled
// as LUT planes by the selected draw slot. Returning 0 leaves the caller free
// to use the original game draw.
static inline int hd_art_shadow_indexed(uint8_t *rgba, int rw, int rh, const uint8_t *indices, int iw, int ih,
                                        int sx, int sy, int sw, int sh, const uint16_t *levels, int max_index, int enabled) {
  if (!enabled) return 1;
  if (!rgba || !indices || !levels || rw <= 0 || rh <= 0 || iw <= 0 || ih <= 0 || sx < 0 || sy < 0 ||
      sw <= 0 || sh <= 0 || sx > iw - sw || sy > ih - sh || max_index < 0xf8 || max_index > 0xfe)
    return 0;
  for (int y = 0; y < rh; y++) {
    int iy = sy + (int)((int64_t)y * sh / rh);
    for (int x = 0; x < rw; x++) {
      int ix = sx + (int)((int64_t)x * sw / rw);
      hd_art_shadow_indexed_pixel(rgba + ((size_t)y * rw + x) * 4, indices[(size_t)iy * iw + ix], levels, max_index);
    }
  }
  return 1;
}
