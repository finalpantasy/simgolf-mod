#pragma once
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Reserve a complete CPU image without touching the currently published one.
// Only (NULL, 0, 0) is an explicit clear; invalid input is a failed publication.
static inline int ui_art_copy(const uint32_t *argb, int w, int h, uint32_t **out) {
  if (!argb) {
    if (w || h) return 0;
    *out = NULL;
    return 1;
  }
  if (w <= 0 || h <= 0 || w > INT_MAX / 4 ||
      (size_t)w > SIZE_MAX / sizeof *argb / (size_t)h) return 0;
  size_t bytes = (size_t)w * h * sizeof *argb;
  uint32_t *next = malloc(bytes);
  if (!next) return 0;
  memcpy(next, argb, bytes);
  *out = next;
  return 1;
}
