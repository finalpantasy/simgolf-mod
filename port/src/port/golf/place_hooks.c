// Building placement centred on the cursor (simgolf mod, task-115). Golf puts the footprint of a lot with its
// first (top) tile under the cursor, so a 3x3 or bigger lot hangs below and to one side of the pointer and the
// ghost picture, centred on the footprint, looks stuck to a corner. With ui.place_center=1 the tile under the
// cursor is taken as the middle of the footprint instead: the hover frame (preview) and the click that places
// both go through place_center, so the preview, the footprint and the placed building are the same tiles.
// ui.place_center=0 (the default without the ui_place mod, the vanilla profile) leaves golf's own tile alone.
// Patches: tools/translate/golf.place.json (FUN_0040f5c0, the two FUN_00430020 calls that pick the tile).
#include "golf_protos.h"
#include "port/runtime.h"

#define G8(a) (*(volatile int8_t *)(uintptr_t)(a))
#define G32(a) (*(volatile int32_t *)(uintptr_t)(a))

void terrain_set_line_offset_fix(int on);

#ifdef __EMSCRIPTEN__

void place_frame_begin(void) { terrain_set_line_offset_fix(1); }

void place_center(int *tx, int *ty) { (void)tx, (void)ty; }

#else

const char *mods_get(const char *key);   // mods/mods.h (that header is not used here: golf's own types are)
unsigned mods_serial(void);

void place_frame_begin(void) {
  static unsigned seen = ~0u;
  unsigned serial = mods_serial();
  if (serial != seen) {
    seen = serial;
    const char *v = mods_get("fixes.placement_lines");
    terrain_set_line_offset_fix(!v || v[0] != '0');
  }
}

static int on(void) {
  static unsigned seen = ~0u;
  static int v;
  unsigned ser = mods_serial();
  if (ser != seen) {
    seen = ser;
    const char *s = mods_get("ui.place_center");
    v = s && s[0] == '1';
  }
  return v;
}

// the lot being placed (tools 6..15), its footprint in tiles: the table size plus the level (golf_01.c ~22484)
void place_center(int *tx, int *ty) {
  int tool = G32(0x4c2854);
  if (tool < 6 || tool > 15 || !on()) return;
  int size = (int)G8(0x4c26c0 + tool * 0x14) + G32(0x5a8c38 + tool * 4);
  if (size < 2 || size > 8) return;
  int off = size / 2;
  *tx -= off;
  *ty -= off;
  if (*tx < 0) *tx = 0;
  if (*ty < 0) *ty = 0;
}

#endif
