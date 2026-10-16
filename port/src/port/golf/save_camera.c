// Original saves do not serialize camera_tile_x/y. Large courses can load
// far from all purchased ground. Center the completed load over its clubhouse.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "port/grid.h"
#include "port/golf/hd_ui.h"
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif

static int on_map(int x, int y) {
  return x > 0 && y > 0 && x < MAP_H && y < MAP_W;
}

void save_camera_focus(void) {
#ifndef __EMSCRIPTEN__
  const char *setting = mods_get("fixes.load_camera");
  if (setting && !atoi(setting)) return;
#endif
  // Placed-object record zero is the clubhouse. Coordinates are signed words.
  int x = *(int16_t *)(uintptr_t)0x58bcba;
  int y = *(int16_t *)(uintptr_t)0x58bcbc;
  if (!on_map(x, y)) {
    x = *(int32_t *)(uintptr_t)(0x575ab8 + 0x208);
    y = *(int32_t *)(uintptr_t)(0x575abc + 0x208);
    if (!on_map(x, y)) { x = MAP_H / 2; y = MAP_W / 2; }
  }
  hd_ui_camera_set(x, y);
  // A different course may have the same camera coordinates. Invalidate both
  // caches so its terrain and tile picking are rebuilt before the first view.
  *(int32_t *)(uintptr_t)0x543d04 = -1;
  *(int32_t *)(uintptr_t)0x4d2048 = x + 1000;
  fprintf(stderr, "save-camera: load focused at %d,%d\n", x, y);
}
