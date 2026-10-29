#include "golf_protos.h"
#include "port/grid.h"
#include "shot_preview.h"

// Ground sampling also reads adjacent corners. A tile in the spare row can
// make that second read leave the allocation on a rectangular map, because
// the original corner routines use width for both bounds. Sample originals
// only for actual map coordinates. Outside uses their flat-ground convention:
// corner height 3 means world height (3 - 3) * 16 == 0 and zero slope.
// Live flight and all in-map sampling keep their original calls.
static int preview_on_map(int x, int y) {
  return (unsigned)(x >> 10) < (unsigned)MAP_H &&
         (unsigned)(y >> 10) < (unsigned)MAP_W;
}

int shot_preview_height(int x, int y) {
  if (!preview_on_map(x, y)) return 0;
  return FUN_0042fa30(x, y);
}

int shot_preview_slope(int x, int y, unsigned direction) {
  if (!preview_on_map(x, y)) return 0;
  return FUN_0040c450(x, y, direction);
}
