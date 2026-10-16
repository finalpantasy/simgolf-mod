// Restore the defined golfer body colors lost by Ghidra's stack-argument recovery.
// FUN_00462020 still resolves defaults/pros and writes its original record fields.
// The post-hook supplies the missing typed GetEntries/SetEntries arguments for
// the 0..119 body-color range. Other palette entries and all gameplay are untouched.
#include "golf_protos.h"
#include "port/runtime.h"
#include "port/ui/ui_portrait_palette.h"
#include "re_bounds.h"
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
int portrait_memory_readable(uintptr_t p, unsigned size);
#endif

#define P32(a) (*(volatile uint32_t *)(uintptr_t)(a))

static int palette_readable(uintptr_t p) {
  if (p < 0x10000 || p > UINT32_MAX - 32) return 0;
#ifndef __EMSCRIPTEN__
  return portrait_memory_readable(p, 32);
#else
  return 1; // Pointers come from the original's initialized palette-object table.
#endif
}

static int read_rgb(uint32_t backend, uint8_t rgb[360]) {
  if (!palette_readable(backend)) return 0;
  uint32_t vt = P32(backend);
  if (!palette_readable(vt)) return 0;
  ICALLT3(P32(vt + 0x10), backend, rgb, 0, 120);
  return 1;
}

void golfer_palette_finish(int slot) {
#ifndef __EMSCRIPTEN__
  if (!mods_get_int("fixes.golfer_palette", 1)) return;
#endif
  // Special staff/celebrity slots use a different source. This bounded repair
  // covers ordinary golfers, the player and the editor's virtual slot only.
  if (slot < 0 || slot >= 0x9c) return;
  int rec = *(volatile int16_t *)(uintptr_t)(0x57956e + slot * 0x100);
  if (!roster_profile_valid(rec)) return;
  const volatile uint8_t *r = (const volatile uint8_t *)(uintptr_t)(roster_address(0x4d6088) + rec * 0x230);
  int male = !(r[0x21] & 0x80);
  if (*(volatile uint8_t *)(uintptr_t)(0x5794d0 + slot * 0x100) == 0x20 && roster_identity(rec)<0 && roster_character(rec)<0) male = 1;
  int index[7] = {0, r[0x24], r[0x25], r[0x27], r[0x23] & 15, r[0x28], r[0x26]};
  const int max[7] = {1, 10, 10, 4, 10, 5, 5};
  uint8_t src[7][360], out[360];
  uint32_t working = P32(0x81ca14);
  if (!palette_readable(working)) return;
  for (int i = 0; i < 7; i++) {
    if (index[i] < 0 || index[i] >= max[i]) return;
    int p = index[i] + (male ? 0 : 139);
    if (!read_rgb(P32(0x81ca6c + p * 0x58), src[i])) return;
  }
  ui_portrait_palette(out, male, src[0], src[1], src[2], src[3], src[4], src[5], src[6]);
  uint32_t vt = P32(working);
  if (palette_readable(vt)) ICALLT3(P32(vt + 0x14), working, out, 0, 120);
}

