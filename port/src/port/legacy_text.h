// Immutable, frame-bound legacy text sent from the game to the native renderer.
#pragma once
#include <stdint.h>
#define LEGACY_RUNS 256
#define LEGACY_GLYPHS 8192
typedef struct {
  const unsigned char *font; // font files remain allocated for the process lifetime
  float em;
  uint32_t rgb;
  int bold, underline, clip[4], bounds[4], uw0, uw1, ubase, first, count;
} LegacyRun;
typedef struct { int index; float x, y; } LegacyGlyph;
typedef struct {
  int count, glyphs;
  LegacyRun run[LEGACY_RUNS];
  LegacyGlyph glyph[LEGACY_GLYPHS];
} LegacyText;
// Game thread only. Pixels are removed only for intact, fully captured records.
void gdi_legacy_capture(const uint16_t *pixels, uint16_t *clean, int w, int h, LegacyText *text);
