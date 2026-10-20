// Frosted Mint UI kit (task-26): text through a glyph atlas.
//
// Plus Jakarta Sans (the five static cuts in fonts/plus-jakarta-sans/, SIL OFL)
// rasterised by stb_truetype at the exact pixel size it is drawn at, so text
// stays sharp at 4K: no bitmap is ever scaled. Glyphs are packed on first use
// into one alpha atlas (white, coverage in alpha) and drawn as textured quads,
// tinted per draw. Figures use the font's tabular forms (equal widths, so money
// and dates do not jitter). Strings are UTF-8; bytes that are not valid UTF-8
// are taken as Windows-1252 (the game's own strings).
#pragma once

#include <SDL.h>
#include <stdint.h>

#include "port/ui/ui_draw.h"

#define UIT_FILES 5
#define UIT_CACHE 8192

typedef struct {
  uint32_t key;     // 0 = empty
  int16_t x, y, w, h, xoff, yoff;
  float adv;
} UitGlyph;

typedef struct UiText {
  SDL_Renderer *r;
  struct {
    unsigned char *data;
    int weight, ok, tn[10];
    void *info;   // stbtt_fontinfo
  } f[UIT_FILES];
  SDL_Texture *atlas;
  Uint32 atlas_retry_at;  // next attempt if creating GPU glyph atlas failed
  Uint32 font_retry_at, pixels_retry_at;
  int upload_errors;
  char font_dir[1024];
  uint8_t *alpha;
  int aw, ah, shelf_x, shelf_y, shelf_h;
  int dirty, dx0, dy0, dx1, dy1;
  UitGlyph cache[UIT_CACHE];
  int nglyphs;
  float gamma;
  uint8_t curve[256];
  SDL_Vertex *v;
  int *idx;
  int nv, ni, cap;
  int resets;   // times the atlas filled up and started over (tests)
} UiText;

// font_dir holds PlusJakartaSans-{Regular,Medium,SemiBold,Bold,ExtraBold}.ttf.
// Returns the number of cuts loaded (0: no text, everything else still draws).
int uit_init(UiText *t, SDL_Renderer *r, const char *font_dir, float gamma);
void uit_free(UiText *t);

// Width of s at weight and pixel size (tracking in em).
float uit_measure(UiText *t, int weight, float px, const char *s, float tracking);
// Ascent and descent (positive) of the cut at that size.
void uit_metrics(UiText *t, int weight, float px, float *ascent, float *descent);
// Draws s with its baseline at y; returns the pen position after it.
float uit_draw(UiText *t, float x, float y, int weight, float px, const char *s, float tracking, UiColor c);
// The longest prefix of s (in bytes, at a space when there is one) that fits in max_w.
int uit_fit(UiText *t, int weight, float px, const char *s, float tracking, float max_w);
// Longest fitting prefix at a complete character boundary, up to max_bytes.
// Unlike wrapping, scans all eligible prefixes (tracking can be negative).
int uit_prefix(UiText *t, int weight, float px, const char *s, float tracking, float max_w, int max_bytes, float suffix_w);

// Render thread: retain CPU glyphs and fonts, recreate their GPU atlas on next draw.
void uit_device_reset(UiText *t);
