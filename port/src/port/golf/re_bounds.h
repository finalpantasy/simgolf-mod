#ifndef SIMGOLF_RE_BOUNDS_H
#define SIMGOLF_RE_BOUNDS_H

#include <stdint.h>
#include <limits.h>

/* Original v1.02 storage limits, not gameplay tuning values. */
enum {
  SG_RE_SPRITE_TABLES = 590,
  SG_RE_SPRITE_CELLS = 56640,
  SG_RE_PROFILES = 84,
  SG_RE_STOCK_HEADS = 19,
  SG_RE_ANIMATION_CURVE = 20,
  SG_RE_FILE_SLOTS = 100,
  SG_RE_FILE_BYTES = 100
};

int fix_re_bounds_on(void);
int fix_table_bounds_on(void);
int re_animation_height(const void *curve, uint32_t frame);
void re_shot_cache_context(int rolling);
int re_course_slot_ids_valid(void);
int re_file_text_append_fits(const char *name);

typedef struct sg_re_shot_context {
  int initialized;
  int rolling;
  unsigned friction_shift;
} sg_re_shot_context;

/* Airborne range does not depend on the rolling friction table. */
static inline int sg_re_shot_context_changed(sg_re_shot_context *context,
                                             int rolling, unsigned friction) {
  rolling = rolling != 0;
  friction = rolling ? friction & 31u : 0u;
  int changed = !context->initialized || context->rolling != rolling ||
                context->friction_shift != friction;
  context->initialized = 1;
  context->rolling = rolling;
  context->friction_shift = friction;
  return changed;
}

static inline int sg_re_sprite_table_valid(int table) {
  return (unsigned)table < SG_RE_SPRITE_TABLES;
}

static inline int sg_re_profile_valid(int profile) {
  return (unsigned)profile < SG_RE_PROFILES;
}

static inline int sg_re_stock_head_valid(int head) {
  return (unsigned)head < SG_RE_STOCK_HEADS;
}

static inline int sg_re_string_fits(const char *text, unsigned capacity) {
  if (!text) return 0;
  for (unsigned i = 0; i < capacity; ++i)
    if (!text[i]) return 1;
  return 0;
}

static inline int sg_re_cut_axis_fits(int origin, int extent, int source) {
  return origin >= 0 && extent > 0 && source > 0 && origin <= source &&
         extent <= source - origin;
}

static inline int sg_re_cut_area_fits(int width, int height) {
  return width > 0 && height > 0 && (int64_t)width * height <= INT_MAX;
}

/* The original world selector maps sixteen distinct property IDs to slots. */
static inline int sg_re_course_ids_valid(const uint8_t *ids, unsigned stride) {
  if (!ids || !stride || stride > UINT_MAX / 15u) return 0;
  uint32_t seen = 0;
  for (unsigned i = 0; i < 16; ++i) {
    unsigned id = ids[i * stride];
    if (id >= 16 || (seen & (1u << id))) return 0;
    seen |= 1u << id;
  }
  return seen == 0xffffu;
}

static inline unsigned sg_re_bounded_length(const char *text, unsigned limit) {
  if (!text) return limit;
  unsigned length = 0;
  while (length < limit && text[length]) ++length;
  return length;
}

static inline int sg_re_append_fits(const char *text, unsigned boundary,
                                    const char *name, unsigned name_limit,
                                    const char *separator, unsigned separator_limit) {
  unsigned text_bytes = sg_re_bounded_length(text, boundary);
  unsigned name_bytes = sg_re_bounded_length(name, name_limit);
  unsigned separator_bytes = sg_re_bounded_length(separator, separator_limit);
  if (text_bytes == boundary || name_bytes == name_limit ||
      separator_bytes == separator_limit) return 0;
  return (uint64_t)text_bytes + name_bytes + separator_bytes + 1 <= boundary;
}

/* Validate dimensions before any cell pointer is formed. */
static inline int sg_re_sprite_index(int base, int frames, int views,
                                     int frame, int view, int capacity) {
  if (capacity <= 0 || base < 0 || base >= capacity || frames <= 0 ||
      views <= 0 || frame < 0 || frame >= frames || view < 0 || view >= views)
    return -1;
  int64_t span = (int64_t)frames * views;
  if (span > (int64_t)capacity - base) return -1;
  return (int)((int64_t)base + (int64_t)view * frames + frame);
}

static inline int sg_re_animation_height(const void *curve, uint32_t frame) {
  if (!curve || frame >= SG_RE_ANIMATION_CURVE) return 0;
  return ((const int8_t *)curve)[frame];
}

#endif
