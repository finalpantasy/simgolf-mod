// Map size: storage for the per-tile arrays tools/translate/grid.py relocated.
// map_scale (golf.ini, or SIMGOLF_MAP_SCALE) is the land area as a multiple
// of the original: 1 = 50 x 50 tiles, 2 = 70 x 70, 4 = 100 x 100, 10 = 158 x 158.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/grid.h"
#include "port/native.h"
#include "port/runtime.h"
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#include "port/host.h"
#endif

enum { kOrigSide = 50 };

int g_map_w = kOrigSide, g_map_h = kOrigSide;
const char g_gen_zero[(GRID_MAX_SIDE + 2) * (GRID_MAX_SIDE + 2)] = {0};

static double map_scale(void) {
  const char *env = getenv("SIMGOLF_MAP_SCALE");
  if (env && *env) return atof(env);
#ifndef __EMSCRIPTEN__
  // data/mods.ini [map] scale= (or a mod's setting) beats golf.ini map_scale=
  return mods_get_num("map.scale", host_map_scale());
#else
  return 1;
#endif
}

static int g_default_side = kOrigSide;   // from map_scale: the size of new games

// Fresh storage for every array at w x h; with copy_image the image's
// contents (normally zero) keep their (x, y).
static size_t alloc_grids(int w, int h, int copy_image) {
  size_t total = 0;
  for (int i = 0; i < grid_count; i++) {
    const GridArray *g = &grid_arrays[i];
    int n0 = kOrigSide + g->corner, nw = w + g->corner, nh = h + g->corner;
    size_t bytes = (size_t)nw * nh * g->elem, pad = (size_t)(nw + 2) * g->elem;
    // a spare row each side: the game reads the neighbours of edge tiles
    uint8_t *p = calloc(1, bytes + 2 * pad);
    if (!p) port_abort("grid: out of memory");
    p += pad;
    if (copy_image)
      for (int y = 0; y < n0; y++)
        memcpy(p + (size_t)y * nw * g->elem, (const uint8_t *)(uintptr_t)g->addr + (size_t)y * n0 * g->elem,
               (size_t)n0 * g->elem);
    g_grid[i] = (uintptr_t)p;
    total += bytes;
  }
  g_map_w = w;
  g_map_h = h;
  return total;
}

void grid_init(void) {
  double scale = map_scale();
  int side = kOrigSide;
  if (scale > 1) {
    // Clamp before converting to int: huge or infinite settings otherwise
    // become a negative dimension before the existing upper bound can help.
    double wanted = kOrigSide * sqrt(scale) + 0.5;
    side = wanted >= GRID_MAX_SIDE ? GRID_MAX_SIDE : (int)wanted;
    side &= ~1;   // even, so the map keeps a centre line between tiles
    if (side > GRID_MAX_SIDE) side = GRID_MAX_SIDE;
  }
  g_default_side = side;
  size_t total = alloc_grids(side, side, 1);
  fprintf(stderr, "grid: map %d x %d tiles (map_scale %g), %d arrays, %u KB at %p\n", side, side, scale,
          grid_count, (unsigned)(total / 1024), (void *)g_grid[0]);
}

void grid_resize(int w, int h) {
  // The old storage is kept, not freed: a caller up the stack may still hold
  // a pointer into it (a few hundred KB per resize at most).
  size_t total = alloc_grids(w, h, 0);
  terrain_resize(w, h);
  fprintf(stderr, "grid: map now %d x %d tiles, %u KB\n", w, h, (unsigned)(total / 1024));
}

void grid_resize_default(void) {
  if (g_map_w != g_default_side || g_map_h != g_default_side) grid_resize(g_default_side, g_default_side);
}

uintptr_t grid_translate(uintptr_t a) {
  for (int i = 0; i < grid_count; i++) {
    const GridArray *g = &grid_arrays[i];
    int n0 = kOrigSide + g->corner, n = g_map_w + g->corner;
    uintptr_t size = (uintptr_t)n0 * n0 * g->elem;
    if (a < g->addr || a >= g->addr + size) continue;
    uintptr_t o = a - g->addr, row = (uintptr_t)n0 * g->elem;
    return g_grid[i] + (o / row) * n * g->elem + o % row;
  }
  return a;
}

// Save files: the map size tag (port/src/port/golf/map_save.c).
typedef struct { char magic[4]; int32_t version, w, h; } MapTag;

void grid_save_tag(int fd, int reading) {
  MapTag t;
  if (!reading) {
    if (g_map_w == 50 && g_map_h == 50) return;
    memcpy(t.magic, "SGMP", 4);
    t.version = 1;
    t.w = g_map_w;
    t.h = g_map_h;
    crt_write(fd, (uint32_t)(uintptr_t)&t, sizeof t);
    return;
  }
  int w = 50, h = 50;
  int at = crt_lseek(fd, 0, 1);
  if (crt_read(fd, (uint32_t)(uintptr_t)&t, sizeof t) == (int)sizeof t && !memcmp(t.magic, "SGMP", 4) &&
      t.version == 1) {
    w = t.w;
    h = t.h;
  } else {
    crt_lseek(fd, at, 0);   // an original file: the first field starts here
  }
  if (w < 50 || h < 50 || w > GRID_MAX_SIDE || h > GRID_MAX_SIDE) {
    fprintf(stderr, "map: file says %d x %d, not a size this build can hold\n", w, h);
    w = g_map_w;
    h = g_map_h;
  }
  if (w != g_map_w || h != g_map_h) {
    fprintf(stderr, "map: file is %d x %d, map was %d x %d: resizing\n", w, h, g_map_w, g_map_h);
    grid_resize(w, h);
  }
}
