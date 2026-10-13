#include "golf_protos.h"
#include "port/runtime.h"
#include "re_bounds.h"

#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif

int fix_re_bounds_on(void) {
#ifndef __EMSCRIPTEN__
  return mods_get_int("fixes.re_bounds", 1) != 0;
#else
  return 1;
#endif
}

int re_course_slot_ids_valid(void) {
  return sg_re_course_ids_valid((const uint8_t *)(uintptr_t)0x571ff4, 46);
}

int re_file_text_append_fits(const char *name) {
  /* 0x51b068 is a live terrain surface, not a proven text-buffer capacity. */
  return sg_re_append_fits((const char *)(uintptr_t)0x51a068, 0x1000,
                           name, SG_RE_FILE_BYTES,
                           (const char *)(uintptr_t)0x4c8a28, 32);
}

/* 0x4632fd used a stale animation frame before its end-of-animation branch. */
int re_animation_height(const void *curve, uint32_t frame) {
  if (!fix_re_bounds_on()) return ((const int8_t *)curve)[frame];
  return sg_re_animation_height(curve, frame);
}

/* 0x422430 caches distance and vertical speed but omits these dependencies. */
void re_shot_cache_context(int rolling) {
  static sg_re_shot_context context;
  if (!fix_re_bounds_on()) {
    context.initialized = 0;
    return;
  }
  unsigned friction = rolling ? *(const uint8_t *)(uintptr_t)0x5783a1 : 0u;
  uint32_t *next = (uint32_t *)(uintptr_t)0x5a9ce0;
  if (sg_re_shot_context_changed(&context, rolling, friction) || *next >= 10u) {
    int32_t *distances = (int32_t *)(uintptr_t)0x5a47b8;
    int32_t *vertical = (int32_t *)(uintptr_t)0x5685c8;
    /* No vertical speed can match both sentinels for a legal distance >= 0. */
    for (int i = 0; i < 10; ++i) {
      distances[i] = -1;
      vertical[i] = 0;
    }
    *next = 0;
  }
}

// @override FUN_0043d6f0
undefined *FUN_0043d6f0(int table, int frame, int view) {
  const int32_t *bases = (const int32_t *)(uintptr_t)0x5a9370;
  const int32_t *counts = (const int32_t *)(uintptr_t)0x53f3e8;
  const int32_t *views = (const int32_t *)(uintptr_t)0x5a5a2c;
  undefined *empty = (undefined *)(uintptr_t)0x53ba48;
  if (fix_re_bounds_on() || fix_table_bounds_on()) {
    if (!sg_re_sprite_table_valid(table)) return empty;
    int cell = sg_re_sprite_index(bases[table], counts[table], views[table],
                                  frame, view, SG_RE_SPRITE_CELLS);
    if (cell < 0) return empty;
    return (undefined *)(uintptr_t)(0x5aaa30u + (uint32_t)cell * 44u);
  }
  /* Original lookup requires both bounds switches to be disabled. */
  if (bases[table] == -1 || frame >= counts[table]) return empty;
  uint32_t cell = (uint32_t)bases[table] + (uint32_t)frame +
                  (uint32_t)counts[table] * (uint32_t)view;
  return (undefined *)(uintptr_t)(0x5aaa30u + cell * 44u);
}
