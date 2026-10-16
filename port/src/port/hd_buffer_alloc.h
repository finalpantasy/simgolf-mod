#ifndef SIMGOLF_HD_BUFFER_ALLOC_H
#define SIMGOLF_HD_BUFFER_ALLOC_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#ifndef HD_BUFFER_CALLOC
#define HD_BUFFER_CALLOC calloc
#endif
#ifndef HD_BUFFER_FREE
#define HD_BUFFER_FREE free
#endif

// Render startup only succeeds when all frame buffers were allocated.
// On failure, restore all output pointers to NULL after freeing partial work.
static inline void hd_free_render_buffers(uint16_t **local, uint16_t **ui, uint8_t **mask) {
  HD_BUFFER_FREE(*local);
  HD_BUFFER_FREE(*ui);
  HD_BUFFER_FREE(*mask);
  *local = NULL;
  *ui = NULL;
  *mask = NULL;
}

static inline int hd_allocate_render_buffers(uint16_t **local, uint16_t **ui,
                                              uint8_t **mask, size_t pixels) {
  if (!local || !ui || !mask) return 0;
  *local = NULL;
  *ui = NULL;
  *mask = NULL;
  if (!pixels || pixels > SIZE_MAX / sizeof(uint16_t)) return 0;
  *local = HD_BUFFER_CALLOC(pixels, sizeof **local);
  *ui = HD_BUFFER_CALLOC(pixels, sizeof **ui);
  *mask = HD_BUFFER_CALLOC(pixels, sizeof **mask);
  if (!*local || !*ui || !*mask) {
    hd_free_render_buffers(local, ui, mask);
    return 0;
  }
  return 1;
}

#endif
