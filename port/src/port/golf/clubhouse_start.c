// Optional manual Career placement. Preserve the generator's terrain before
// its temporary clubhouse, then use the original demolition/placement state.
#include <stdint.h>
#include <stdio.h>
#include "port/grid.h"
#include "port/clubhouse_start.h"
#include "port/sandbox_landscape.h"
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif
typedef struct { uint8_t terrain, decoration, building, height; uint16_t flags; } Cell;
static Cell cells[GRID_MAX_SIDE * GRID_MAX_SIDE];
static int enabled, ax, ay, size;
static uint8_t *bytes(uint32_t addr) { return (uint8_t *)grid_translate(addr); }
static void sandbox_start_end(void) {
#ifndef __EMSCRIPTEN__
  if(!(*(uint32_t *)(uintptr_t)0x59e7b8 & 0x1000000u))return;
  int blank=mods_get_int("sandbox.blank",0),flat=mods_get_int("sandbox.flat",0);
  if(!blank&&!flat)return;
  extern uint32_t FUN_0040e400(int);
  extern uint32_t FUN_0042f7a0(void);
  if(blank)for(int k=255;k>=0;k--){
    int16_t *record=(int16_t *)(uintptr_t)(0x58bcb8+k*16);
    if(record[0]>=0)FUN_0040e400(k);
  }
  sandbox_landscape_apply(MAP_W,MAP_H,blank,flat,bytes(0x5722e8),bytes(0x542414),
    bytes(0x53bbac),bytes(0x56988c),(uint16_t *)bytes(0x53caf0),bytes(0x5a4998),
    *(uint8_t *)(uintptr_t)(0x578375+4*0x30));
  FUN_0042f7a0();
  fprintf(stderr,"sandbox: new landscape rough-only=%d flat=%d size=%dx%d\n",!!blank,!!flat,MAP_W,MAP_H);
#endif
}
void clubhouse_start_begin(void) {
  enabled = size = 0;
#ifndef __EMSCRIPTEN__
  const char *v = mods_get("gameplay.manual_clubhouse");
  uint32_t flags = *(uint32_t *)(uintptr_t)0x59e7b8;
  enabled = v && v[0] == '1' && !(flags & (0x1000000u | 0x4000000u));
#endif
}
void clubhouse_start_capture(int x, int y, int side) {
  if (!enabled || size || side < 1 || side > GRID_MAX_SIDE || x < 0 || y < 0 || x + side > MAP_H || y + side > MAP_W) return;
  ax = x; ay = y; size = side;
  for (int dx = 0; dx < side; dx++) for (int dy = 0; dy < side; dy++) {
    int i = (x + dx) * MAP_W + y + dy;
    cells[dx * side + dy] = (Cell){bytes(0x5722e8)[i], bytes(0x53bbac)[i], bytes(0x56988c)[i],
      bytes(0x5a4998)[(x + dx) * (MAP_W + 1) + y + dy], ((uint16_t *)bytes(0x53caf0))[i]};
  }
}
void clubhouse_start_end(void) {
  extern uint32_t FUN_0040e400(int);
  extern int FUN_0042f340(void);
  extern uint32_t FUN_0042f7a0(void);
  sandbox_start_end();
  enabled = 0;
  if (!size) return;
  int found = -1;
  for (int k = 0; k < 256; k++) {
    int16_t *r = (int16_t *)(uintptr_t)(0x58bcb8 + k * 16);
    if (r[0] == 15 && r[1] == ax && r[2] == ay) { found = k; break; }
  }
  if (found < 0) { size = 0; return; }
  FUN_0040e400(found);
  for (int dx = 0; dx < size; dx++) for (int dy = 0; dy < size; dy++) {
    int i = (ax + dx) * MAP_W + ay + dy; Cell c = cells[dx * size + dy];
    bytes(0x5722e8)[i] = c.terrain; bytes(0x53bbac)[i] = c.decoration; bytes(0x56988c)[i] = c.building;
    ((uint16_t *)bytes(0x53caf0))[i] = c.flags;
    bytes(0x5a4998)[(ax + dx) * (MAP_W + 1) + ay + dy] = c.height;
  }
  size = 0;
  FUN_0042f340(); FUN_0042f7a0();
  fprintf(stderr, "clubhouse: manual Career placement ready\n");
}
