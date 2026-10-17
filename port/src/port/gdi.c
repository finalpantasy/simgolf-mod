// GDI32 subset used by jgld.dll: DIB sections, memory DCs, BitBlt/StretchBlt,
// clip regions, palettes, and TrueType text (stb_truetype) for the game's two
// bundled fonts. The window DC draws into an 800x600 RGB555 buffer that is
// presented through an SDL streaming texture.

#define _GNU_SOURCE
#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include "port/host.h"
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "port/fs.h"
#include "port/runtime.h"
#include "port/ui_font.h"
#include "port/legacy_text.h"
#include "port/win32.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#define P(x) ((void *)(uintptr_t)(x))
#define S(x) ((char *)(uintptr_t)(x))
#define U(x) ((nu)(uintptr_t)(x))
#define I32(p, off) (*(int32_t *)((char *)P(p) + (off)))
#define U32(p, off) (*(uint32_t *)((char *)P(p) + (off)))
#define U16(p, off) (*(uint16_t *)((char *)P(p) + (off)))
#define U8(p, off) (*(uint8_t *)((char *)P(p) + (off)))

typedef struct {
  int w, h, bottom_up, bpp, stride;
  uint8_t *bits;
  int owns_bits;
  uint8_t colors[256][4];   // RGBQUAD: b, g, r, reserved
  int is565;
} Dib;

typedef struct {
  int file;            // index into g_fonts
  float scale;
  int ascent, descent, height, internal_leading, avg_width, max_width;
  int bold, italic, underline;
  // ui.font=plus_jakarta (card task-25), prepared on first use (pj_prepare).
  char face[32];
  int lf_weight, pj_state;     // pj_state: 0 not tried, 1 ready, -1 not available
  int pj_file, pj_dy;          // file in g_fonts; baseline shift (px) against the original face
  float pj_scale;
} Font;

typedef struct {
  nu bitmap, font, palette, brush;
  uint32_t text_color;
  int bk_mode, text_align;
  int has_clip;
  int32_t clip[4];
  int is_window;
} Dc;

typedef struct { int32_t r[4]; } Rgn;
typedef struct { int n; uint8_t e[256][4]; } Pal;

// ------------------------------------------------------------------ screen

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
static Dib g_screen_dib;
static nu g_screen_bitmap, g_window_dc;
static double g_last_present_yield;

uint16_t *win_screen(void) { return (uint16_t *)g_screen_dib.bits; }

unsigned g_presents;

// The original presentation; hd.c replaces it with an upscaled one unless
// that's turned off (Ctrl+F11, ?hd=0) or WebGL2 is missing.
static void present_sdl(void) {
  // RGB555 -> the GL-native RGBA byte order through a table; SDL's own
  // conversion (BlitNtoN) was the biggest cost of a menu frame
  static uint32_t lut[32768], *rgba;
  if (!rgba) {
    rgba = malloc(WIN_SCREEN_W * WIN_SCREEN_H * 4);
    for (int c = 0; c < 32768; c++) {
      uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
      r = r << 3 | r >> 2; g = g << 3 | g >> 2; b = b << 3 | b >> 2;
      lut[c] = 0xff000000u | b << 16 | g << 8 | r;
    }
  }
  const uint16_t *px = (const uint16_t *)g_screen_dib.bits;
  for (int i = 0; i < WIN_SCREEN_W * WIN_SCREEN_H; i++) rgba[i] = lut[px[i] & 0x7fff];
  SDL_UpdateTexture(g_texture, NULL, rgba, WIN_SCREEN_W * 4);
  SDL_RenderClear(g_renderer);
  SDL_RenderCopy(g_renderer, g_texture, NULL, NULL);
  SDL_RenderPresent(g_renderer);
}

int hd_present(const uint16_t *screen);  // hd.c

void win_present(void) {
  g_presents++;
#ifndef __EMSCRIPTEN__
  bridge_on_present();   // frame counting and bridge "step" (task-13)
#endif
  if (!hd_present((const uint16_t *)g_screen_dib.bits)) present_sdl();
  // Modal screens (reports, dialogs) redraw in a tight loop: cap presents at
  // about 30 per second so they don't keep a core busy (the course view
  // itself runs at the game's own ~11 per second).
  static double last_present;
  double now = port_now_ms();
  if (now - last_present < 33) {
    win_yield((int)(33 - (now - last_present)));
    g_last_present_yield = port_now_ms();
  } else if (now - g_last_present_yield > 10) {
    win_yield(0);
    g_last_present_yield = port_now_ms();
  }
  last_present = port_now_ms();
}

nu gdi_window_dc(void) { return g_window_dc; }

// task-26/30: the 16-bit DIB golf last copied whole to the window (its back
// buffer), for the Frosted Mint HUD's save/restore of the old HUD's area.
static Dib *g_backbuf;
// Screen-sized 16-bit DIBs (golf's page buffers), newest last.
#define SCREEN_DIBS 8
static Dib *g_screen_dibs[SCREEN_DIBS];
static void screen_dib_add(Dib *d) {
  for (int i = 0; i < SCREEN_DIBS; i++)
    if (!g_screen_dibs[i]) { g_screen_dibs[i] = d; return; }
  memmove(g_screen_dibs, g_screen_dibs + 1, sizeof g_screen_dibs[0] * (SCREEN_DIBS - 1));
  g_screen_dibs[SCREEN_DIBS - 1] = d;
}
static void screen_dib_forget(Dib *d) {
  for (int i = 0; i < SCREEN_DIBS; i++)
    if (g_screen_dibs[i] == d) g_screen_dibs[i] = NULL;
}
// The i-th screen-sized 16-bit DIB, as gdi_backbuffer describes it; NULL past the end.
uint16_t *gdi_screen_dib(int i, int *w, int *h, int *pitch, int *is565) {
  if (i < 0 || i >= SCREEN_DIBS || !g_screen_dibs[i]) return NULL;
  Dib *d = g_screen_dibs[i];
  *is565 = d->is565;
  *w = d->w;
  *h = d->h;
  int px = d->stride / 2;
  *pitch = d->bottom_up ? -px : px;
  return d->bottom_up ? (uint16_t *)(d->bits + (size_t)(d->h - 1) * d->stride) : (uint16_t *)d->bits;
}

// The screen-sized DIB whose pixels contain address p (a pointer golf or jgld
// keeps to its draw surface), as gdi_backbuffer describes it; NULL if none.
uint16_t *gdi_dib_at(uintptr_t p, int *w, int *h, int *pitch, int *is565) {
  for (int i = 0; i < SCREEN_DIBS; i++) {
    Dib *d = g_screen_dibs[i];
    if (!d || p < (uintptr_t)d->bits || p >= (uintptr_t)d->bits + (size_t)d->stride * d->h) continue;
    *is565 = d->is565;
    *w = d->w;
    *h = d->h;
    int px = d->stride / 2;
    *pitch = d->bottom_up ? -px : px;
    return d->bottom_up ? (uint16_t *)(d->bits + (size_t)(d->h - 1) * d->stride) : (uint16_t *)d->bits;
  }
  return NULL;
}
uint16_t *gdi_backbuffer(int *w, int *h, int *pitch, int *is565) {
  Dib *d = g_backbuf;
  if (!d || d->bpp != 16) return NULL;
  *is565 = d->is565;
  *w = d->w;
  *h = d->h;
  int px = d->stride / 2;
  *pitch = d->bottom_up ? -px : px;
  return d->bottom_up ? (uint16_t *)(d->bits + (size_t)(d->h - 1) * d->stride) : (uint16_t *)d->bits;
}

// Terrain renders straight into the DIB selected into the HDC golf passes to
// Terrain::initSystem (platform/terrain_port.h's TerrainSurface).
typedef struct { uint16_t *pixels; int pitch, width, height; } TerrainSurface;
extern void terrain_set_hdc_resolver(const TerrainSurface *(*resolve)(void *hdc));

#ifndef __EMSCRIPTEN__
void hd_native_prepare(int *w, int *h, unsigned *flags);
SDL_Window *hd_native_start(int w, int h, unsigned flags);
#endif

static const TerrainSurface *terrain_surface(void *hdc) {
  static TerrainSurface s;
  Dc *dc = obj_get((nu)(uintptr_t)hdc, OBJ_DC);
  Dib *d = dc ? obj_get(dc->bitmap, OBJ_BITMAP) : NULL;
  if (!d || d->bpp != 16) {
    fprintf(stderr, "terrain: HDC %p has no 16-bit DIB\n", hdc);
    return NULL;
  }
  int pitch = d->stride / 2;
  s.pixels = d->bottom_up ? (uint16_t *)(d->bits + (size_t)(d->h - 1) * d->stride) : (uint16_t *)d->bits;
  s.pitch = d->bottom_up ? -pitch : pitch;
  s.width = d->w;
  s.height = d->h;
  return &s;
}

void gdi_init(void) {
  int win_w = WIN_SCREEN_W, win_h = WIN_SCREEN_H;
  unsigned win_flags = 0;
#ifndef __EMSCRIPTEN__
  hd_native_prepare(&win_w, &win_h, &win_flags);   // task-3: any size, borderless (hd_native.c)
#endif
#ifndef __EMSCRIPTEN__
  g_window = hd_native_start(win_w, win_h, win_flags);
  if (!g_window)   // original presentation owns its window on the game thread
#endif
  {
    g_window = SDL_CreateWindow("SimGolf", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, win_flags);
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                                  WIN_SCREEN_W, WIN_SCREEN_H);
  }
  g_screen_dib.w = WIN_SCREEN_W;
  g_screen_dib.h = WIN_SCREEN_H;
  g_screen_dib.bpp = 16;
  g_screen_dib.stride = WIN_SCREEN_W * 2;
  g_screen_dib.bits = calloc(WIN_SCREEN_W * WIN_SCREEN_H, 2);
  g_screen_bitmap = obj_new(OBJ_BITMAP, &g_screen_dib);
  Dc *dc = calloc(1, sizeof *dc);
  dc->bitmap = g_screen_bitmap;
  dc->is_window = 1;
  dc->text_align = 0;
  dc->bk_mode = 2;
  g_window_dc = obj_new(OBJ_DC, dc);
  terrain_set_hdc_resolver(terrain_surface);
}

// ------------------------------------------------------------------ DCs and objects

static Dc *dc_get(nu h) { return obj_get(h, OBJ_DC); }
static Dib *dc_dib(Dc *dc) { return dc ? obj_get(dc->bitmap, OBJ_BITMAP) : NULL; }

// task-3 B4 (golf/hd_ui.c): while golf draws its bottom-left UI through a
// moved view of the back buffer, GDI drawing into that DIB (text, blits)
// moves with it. The clip a DC gets during the view is stored in the DIB's
// own coordinates.
static const uint8_t *g_view_bits;
static int g_view_dx, g_view_dy;
void gdi_set_view(const void *bits, int dx, int dy) {
  g_view_bits = bits;
  g_view_dx = dx;
  g_view_dy = dy;
}
static int in_view(const Dib *d) { return d && g_view_bits && d->bits == g_view_bits; }
int gdi_view_offset(int *dx, int *dy) {
  if (!g_view_bits) return 0;
  *dx = g_view_dx;
  *dy = g_view_dy;
  return 1;
}

nu CreateCompatibleDC(nu hdc) {
  API_TRACE();
  (void)hdc;
  Dc *dc = calloc(1, sizeof *dc);
  dc->bk_mode = 2;
  return obj_new(OBJ_DC, dc);
}

nu DeleteDC(nu h) {
  API_TRACE();
  Dc *dc = dc_get(h);
  if (!dc || dc->is_window) return dc != NULL;
  free(dc);
  obj_free(h);
  return 1;
}

static nu g_stock[32];

nu GetStockObject(nu i) {
  API_TRACE();
  if (i >= 32) return 0;
  if (!g_stock[i]) g_stock[i] = obj_new(OBJ_BRUSH, NULL);
  return g_stock[i];
}

nu SelectObject(nu hdc, nu h) {
  API_TRACE();
  Dc *dc = dc_get(hdc);
  if (!dc) return 0;
  nu old = 0;
  if (obj_get(h, OBJ_BITMAP)) { old = dc->bitmap; dc->bitmap = h; }
  else if (obj_get(h, OBJ_FONT)) { old = dc->font; dc->font = h; }
  else if (obj_kind(h) == OBJ_BRUSH || h == 0) { old = dc->brush; dc->brush = h; }   // task-114g: by kind (stock brushes carry no payload)
  else if (obj_get(h, OBJ_RGN)) {
    Rgn *r = obj_get(h, OBJ_RGN);
    dc->has_clip = 1;
    memcpy(dc->clip, r->r, 16);
    return 2;   // SIMPLEREGION
  } else {
    static int warned;
    if (warned++ < 10) {
      fprintf(stderr, "SelectObject(%08x, %08x): unknown object (kind %d)\n", hdc, h, obj_kind(h));
      port_trace_dump(12);
    }
  }
  return old ? old : GetStockObject(0);
}

static void text_forget(Dib *d);

nu DeleteObject(nu h) {
  API_TRACE();
  Dib *d = obj_get(h, OBJ_BITMAP);
  if (d && d != &g_screen_dib) {
    if (d == g_backbuf) g_backbuf = NULL;
    screen_dib_forget(d);
    text_forget(d);
    if (d->owns_bits) free(d->bits);
    free(d);
    obj_free(h);
    return 1;
  }
  if (obj_get(h, OBJ_FONT) || obj_get(h, OBJ_RGN) || obj_get(h, OBJ_PALETTE)) {
    void *p = obj_get(h, OBJ_FONT);
    if (!p) p = obj_get(h, OBJ_RGN);
    if (!p) p = obj_get(h, OBJ_PALETTE);
    free(p);
    obj_free(h);
  }
  return 1;
}

nu CreateDIBSection(nu hdc, nu bmi, nu usage, nu ppbits, nu section, nu offset) {
  API_TRACE();
  (void)section; (void)offset;
  Dib *d = calloc(1, sizeof *d);
  int32_t w = I32(bmi, 4), h = I32(bmi, 8);
  d->w = w;
  d->h = h < 0 ? -h : h;
  d->bottom_up = h > 0;
  d->bpp = U16(bmi, 14);
  d->stride = ((w * d->bpp + 31) / 32) * 4;
  PORT_LOG("CreateDIBSection %dx%d %d bpp\n", w, h, d->bpp);
  if (w <= 0 || w > 4096 || d->h > 4096 || !d->bpp) {
    port_trace_dump(48);
    port_abort("CreateDIBSection: bad size");
  }
  d->bits = calloc((size_t)d->stride * d->h + 4, 1);
  d->owns_bits = 1;
  if (d->bpp == 16 && d->w == WIN_SCREEN_W && d->h == WIN_SCREEN_H) screen_dib_add(d);
  nu hdrsize = U32(bmi, 0);
  nu compression = U32(bmi, 16);
  if (compression == 3) {   // BI_BITFIELDS: masks follow the header
    d->is565 = U32(bmi, hdrsize + 4) == 0x7e0;
  }
  if (d->bpp <= 8) {
    nu n = U32(bmi, 32);
    if (!n) n = 1u << d->bpp;
    if (usage == 0) {
      memcpy(d->colors, (char *)P(bmi) + hdrsize, n * 4);
    } else {
      // DIB_PAL_COLORS: indices into the DC's palette
      Dc *dc = dc_get(hdc);
      Pal *pal = dc ? obj_get(dc->palette, OBJ_PALETTE) : NULL;
      for (nu i = 0; i < n; i++) {
        uint16_t idx = U16(bmi, hdrsize + i * 2);
        if (pal && idx < (nu)pal->n) {
          d->colors[i][0] = pal->e[idx][2];
          d->colors[i][1] = pal->e[idx][1];
          d->colors[i][2] = pal->e[idx][0];
        }
      }
    }
  }
  if (ppbits) U32(ppbits, 0) = U(d->bits);
  return obj_new(OBJ_BITMAP, d);
}

nu SetDIBColorTable(nu hdc, nu start, nu n, nu rgb) {
  API_TRACE();
  Dib *d = dc_dib(dc_get(hdc));
  if (!d) return 0;
  for (nu i = 0; i < n && start + i < 256; i++) memcpy(d->colors[start + i], (char *)P(rgb) + i * 4, 4);
  return n;
}

nu CreatePalette(nu lp) {
  API_TRACE();
  Pal *p = calloc(1, sizeof *p);
  p->n = U16(lp, 2);
  if (p->n > 256) p->n = 256;
  memcpy(p->e, (char *)P(lp) + 4, p->n * 4);
  return obj_new(OBJ_PALETTE, p);
}

nu SelectPalette(nu hdc, nu pal, nu bg) {
  API_TRACE();
  (void)bg;
  Dc *dc = dc_get(hdc);
  if (!dc) return 0;
  nu old = dc->palette;
  dc->palette = pal;
  return old;
}

nu RealizePalette(nu hdc) { API_TRACE(); (void)hdc; return 0; }

nu AnimatePalette(nu pal, nu start, nu n, nu entries) {
  API_TRACE();
  Pal *p = obj_get(pal, OBJ_PALETTE);
  if (!p) return 0;
  for (nu i = 0; i < n && start + i < 256; i++) memcpy(p->e[start + i], (char *)P(entries) + i * 4, 4);
  return 1;
}

nu CreateRectRgnIndirect(nu r) {
  API_TRACE();
  Rgn *g = calloc(1, sizeof *g);
  memcpy(g->r, P(r), 16);
  return obj_new(OBJ_RGN, g);
}

nu SelectClipRgn(nu hdc, nu rgn) {
  API_TRACE();
  Dc *dc = dc_get(hdc);
  if (!dc) return 0;
  Rgn *r = obj_get(rgn, OBJ_RGN);
  if (!r) {
    dc->has_clip = 0;
    return 1;   // NULLREGION
  }
  dc->has_clip = 1;
  memcpy(dc->clip, r->r, 16);
  if (in_view(dc_dib(dc))) {
    dc->clip[0] += g_view_dx;
    dc->clip[1] += g_view_dy;
    dc->clip[2] += g_view_dx;
    dc->clip[3] += g_view_dy;
  }
  return 2;
}

nu SetBkMode(nu hdc, nu mode) {
  API_TRACE();
  Dc *dc = dc_get(hdc);
  if (!dc) return 0;
  nu old = dc->bk_mode;
  dc->bk_mode = mode;
  return old;
}

nu SetTextColor(nu hdc, nu color) {
  API_TRACE();
  Dc *dc = dc_get(hdc);
  if (!dc) return 0;
  nu old = dc->text_color;
  dc->text_color = color;
  return old;
}

nu SetTextAlign(nu hdc, nu align) {
  API_TRACE();
  Dc *dc = dc_get(hdc);
  if (!dc) return 0;
  nu old = dc->text_align;
  dc->text_align = align;
  return old;
}

// ------------------------------------------------------------------ blits

static uint8_t *row_ptr(Dib *d, int y) {
  int ry = d->bottom_up ? d->h - 1 - y : y;
  return d->bits + (size_t)ry * d->stride;
}

static uint16_t to555(const uint8_t *bgr) {
  return (uint16_t)(((bgr[2] >> 3) << 10) | ((bgr[1] >> 3) << 5) | (bgr[0] >> 3));
}

static uint16_t to_dst16(Dib *dst, uint16_t c555) {
  if (!dst->is565) return c555;
  return (uint16_t)(((c555 & 0x7c00) << 1) | ((c555 & 0x3e0) << 1) | (c555 & 0x1f));
}

static uint16_t from_src16(Dib *src, uint16_t c) {
  if (!src->is565) return c;
  return (uint16_t)(((c >> 1) & 0x7c00) | ((c >> 1) & 0x3e0) | (c & 0x1f));
}

static void text_copy(Dib *dst, int dx, int dy, int w, int h, Dib *src, int sx, int sy);

static void blit_pixels(Dib *dst, int dx, int dy, int w, int h, Dib *src, int sx, int sy,
                        int sw, int shh, const int32_t *clip) {
  // dst rect (dx, dy, w, h) maps to src rect (sx, sy, sw, shh); nearest neighbour.
  if (w <= 0 || h <= 0 || sw <= 0 || shh <= 0) return;
  int cl = 0, ct = 0, cr = dst->w, cb = dst->h;
  if (clip) {
    if (clip[0] > cl) cl = clip[0];
    if (clip[1] > ct) ct = clip[1];
    if (clip[2] < cr) cr = clip[2];
    if (clip[3] < cb) cb = clip[3];
  }
  int x0 = dx < cl ? cl : dx, x1 = dx + w > cr ? cr : dx + w;
  int y0 = dy < ct ? ct : dy, y1 = dy + h > cb ? cb : dy + h;
  // 8 -> 16 bit, unscaled (the menus' full-screen backgrounds): palette table
  if (src->bpp == 8 && dst->bpp == 16 && sw == w) {
    uint16_t pal[256];
    for (int i = 0; i < 256; i++) pal[i] = to_dst16(dst, to555(src->colors[i]));
    int a = x0, b = x1, sa = sx + (a - dx);
    if (sa < 0) { a -= sa; sa = 0; }
    if (sa + (b - a) > src->w) b = a + (src->w - sa);
    for (int y = y0; y < y1 && b > a; y++) {
      int syy = sy + (int)((int64_t)(y - dy) * shh / h);
      if (syy < 0 || syy >= src->h) continue;
      uint16_t *drow = (uint16_t *)row_ptr(dst, y) + a;
      const uint8_t *srow = row_ptr(src, syy) + sa;
      for (int k = 0; k < b - a; k++) drow[k] = pal[srow[k]];
    }
    return;
  }
  if (src->bpp == 16 && dst->bpp == 16 && sw == w && shh == h && src->is565 == dst->is565) {
    // Unscaled: the text drawn on src comes along (gdi_hd_text).
    int a = x0, b = x1, c = y0, e = y1;
    if (sx + (a - dx) < 0) a = dx - sx;
    if (sx + (b - dx) > src->w) b = dx - sx + src->w;
    if (sy + (c - dy) < 0) c = dy - sy;
    if (sy + (e - dy) > src->h) e = dy - sy + src->h;
    text_copy(dst, a, c, b - a, e - c, src, sx + (a - dx), sy + (c - dy));
  }
  for (int y = y0; y < y1; y++) {
    int syy = sy + (int)((int64_t)(y - dy) * shh / h);
    if (syy < 0 || syy >= src->h) continue;
    uint8_t *drow = row_ptr(dst, y);
    uint8_t *srow = row_ptr(src, syy);
    if (src->bpp == dst->bpp && sw == w && src->is565 == dst->is565) {
      int a = x0, b = x1;
      int sa = sx + (a - dx);
      if (sa < 0) { a -= sa; sa = 0; }
      if (sa + (b - a) > src->w) b = a + (src->w - sa);
      if (b > a) memmove(drow + a * dst->bpp / 8, srow + sa * src->bpp / 8, (size_t)(b - a) * dst->bpp / 8);
      continue;
    }
    for (int x = x0; x < x1; x++) {
      int sxx = sx + (int)((int64_t)(x - dx) * sw / w);
      if (sxx < 0 || sxx >= src->w) continue;
      uint16_t c555;
      uint8_t idx = 0;
      switch (src->bpp) {
        case 8: idx = srow[sxx]; c555 = to555(src->colors[idx]); break;
        case 16: c555 = from_src16(src, ((uint16_t *)srow)[sxx]); break;
        case 24: c555 = to555(srow + sxx * 3); break;
        case 32: c555 = to555(srow + sxx * 4); break;
        default: c555 = 0;
      }
      switch (dst->bpp) {
        case 16: ((uint16_t *)drow)[x] = to_dst16(dst, c555); break;
        case 8: drow[x] = idx; break;
        case 24: case 32: {
          uint8_t *p = drow + x * (dst->bpp / 8);
          p[0] = (uint8_t)((c555 & 0x1f) << 3);
          p[1] = (uint8_t)(((c555 >> 5) & 0x1f) << 3);
          p[2] = (uint8_t)(((c555 >> 10) & 0x1f) << 3);
          break;
        }
      }
    }
  }
}

nu StretchBlt(nu hdst, nu x, nu y, nu w, nu h, nu hsrc, nu sx, nu sy, nu sw, nu sh, nu rop) {
  API_TRACE();
  (void)rop;
  Dc *ddc = dc_get(hdst), *sdc = dc_get(hsrc);
  Dib *dst = dc_dib(ddc), *src = dc_dib(sdc);
  if (!dst || !src) return 0;
  if (in_view(dst)) x += g_view_dx, y += g_view_dy;
  blit_pixels(dst, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, src, (int32_t)sx, (int32_t)sy,
              (int32_t)sw, (int32_t)sh, ddc->has_clip ? ddc->clip : NULL);
  if (ddc->is_window) {
    if (src->bpp == 16 && src->w == dst->w && src->h == dst->h && (int32_t)w == dst->w && (int32_t)h == dst->h) g_backbuf = src;
    win_present();
  }
  return 1;
}

nu BitBlt(nu hdst, nu x, nu y, nu w, nu h, nu hsrc, nu sx, nu sy, nu rop) {
  API_TRACE();
  return StretchBlt(hdst, x, y, w, h, hsrc, sx, sy, w, h, rop);
}

// ------------------------------------------------------------------ fonts

typedef struct {
  char path[256];
  char family[64];
  unsigned char *data;
  int size;
  stbtt_fontinfo info;
  int tn[10];                  // tabular figure glyphs (tnum), all 0 when the font has none
  int fig_cell;                // widest figure advance, for the fallback
} FontFile;

static FontFile g_fonts[16];
static int g_nfonts;

static int load_font_file(const char *dospath) {
  char real[512];
  if (dospath[0] == '/') snprintf(real, sizeof real, "%s", dospath);
  else if (!fs_resolve(dospath, real, sizeof real, 0)) return -1;
  for (int i = 0; i < g_nfonts; i++)
    if (!strcmp(g_fonts[i].path, real)) return i;
  if (g_nfonts == 16) return -1;
  FILE *f = fopen(port_host_path(real), "rb");
  if (!f) return -1;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  FontFile *ff = &g_fonts[g_nfonts];
  ff->data = malloc(n);
  ff->size = (int)fread(ff->data, 1, n, f);
  fclose(f);
  if (!stbtt_InitFont(&ff->info, ff->data, stbtt_GetFontOffsetForIndex(ff->data, 0))) {
    free(ff->data);
    return -1;
  }
  snprintf(ff->path, sizeof ff->path, "%s", real);
  int len = 0;
  const char *name = stbtt_GetFontNameString(&ff->info, &len, STBTT_PLATFORM_ID_MICROSOFT,
                                             STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, 1);
  // UTF-16BE -> ASCII
  int k = 0;
  for (int i = 1; name && i < len && k < 63; i += 2) ff->family[k++] = name[i];
  ff->family[k] = 0;
  PORT_LOG("font %s: family \"%s\"\n", dospath, ff->family);
  // Tabular figures: Plus Jakarta Sans' tnum glyphs follow the figures by ten
  // glyph ids in every cut. Checked (equal advances, "1" wider than its
  // proportional form) rather than trusted; otherwise figures are centred in
  // the widest figure's cell.
  {
    int ok = 1, adv0 = -1, lsb;
    for (int i = 0; i < 10 && ok; i++) {
      int g = stbtt_FindGlyphIndex(&ff->info, '0' + i), a, pa;
      if (!g) { ok = 0; break; }
      stbtt_GetGlyphHMetrics(&ff->info, g + 10, &a, &lsb);
      stbtt_GetGlyphHMetrics(&ff->info, g, &pa, &lsb);
      if (adv0 < 0) adv0 = a;
      if (a != adv0) ok = 0;
      if (i == 1 && a == pa) ok = 0;
      ff->tn[i] = g + 10;
      if (pa > ff->fig_cell) ff->fig_cell = pa;
    }
    if (!ok) memset(ff->tn, 0, sizeof ff->tn);
    else ff->fig_cell = adv0;
  }
  return g_nfonts++;
}

// jgld registers a TTF by writing a .fot with CreateScalableFontResourceA and
// adding that; the .fot is a stub here, so add the .ttf next to it.
nu AddFontResourceA(nu path) {
  API_TRACE();
  char p[512];
  snprintf(p, sizeof p, "%s", S(path));
  size_t n = strlen(p);
  if (n > 4 && !strcasecmp(p + n - 4, ".fot")) memcpy(p + n - 4, ".ttf", 4);
  int r = load_font_file(p);
  if (r < 0) fprintf(stderr, "AddFontResourceA(%s): can't load %s\n", S(path), p);
  return r >= 0 ? 1 : 0;
}
nu RemoveFontResourceA(nu path) { API_TRACE(); (void)path; return 1; }
nu CreateScalableFontResourceA(nu hidden, nu fot, nu ttf, nu dir) {
  API_TRACE();
  (void)hidden; (void)fot; (void)dir;
  load_font_file(S(ttf));
  return 1;
}

nu CreateFontIndirectA(nu lf) {
  API_TRACE();
  int32_t height = I32(lf, 0);
  int32_t weight = I32(lf, 16);
  const char *face = S(lf) + 28;
  int file = -1;
  for (int i = 0; i < g_nfonts; i++) {
    size_t fl = strlen(g_fonts[i].family);
    // "Manual SSi Bold" is the bold face of the "Manual SSi" file
    if (!strcasecmp(g_fonts[i].family, face) ||
        (fl && !strncasecmp(g_fonts[i].family, face, fl) && face[fl] == ' ' && strncmp(g_fonts[i].path, "/fonts/", 7)))
      file = i;
    if (file == i && strncmp(g_fonts[i].path, "/fonts/", 7)) break;   // prefer the game's own font
  }
  if (file < 0) {
    // Windows system fonts: free metric-compatible stand-ins (third_party/fonts).
    const char *sub = "/fonts/LiberationSans-Regular.ttf";
    int bold = weight >= 600 || strcasestr(face, "bold");
    if (strcasestr(face, "times")) sub = "/fonts/LiberationSerif-Bold.ttf";
    else if (strcasestr(face, "comic")) sub = "/fonts/ComicNeue-Bold.ttf";
    else if (bold) sub = "/fonts/LiberationSans-Bold.ttf";
    file = load_font_file(sub);
    if (file < 0) file = 0;
  }
  if (file >= g_nfonts) return 0;
  Font *f = calloc(1, sizeof *f);
  f->file = file;
  stbtt_fontinfo *info = &g_fonts[file].info;
  if (height < 0) f->scale = stbtt_ScaleForMappingEmToPixels(info, (float)-height);
  else f->scale = stbtt_ScaleForPixelHeight(info, (float)(height ? height : 16));
  int asc, desc, gap;
  stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
  f->ascent = (int)(asc * f->scale + 0.5f);
  f->descent = (int)(-desc * f->scale + 0.5f);
  f->height = f->ascent + f->descent;
  int em = height < 0 ? -height : (int)(f->height);
  f->internal_leading = f->height - em;
  if (f->internal_leading < 0) f->internal_leading = 0;
  int adv, lsb;
  stbtt_GetCodepointHMetrics(info, 'x', &adv, &lsb);
  f->avg_width = (int)(adv * f->scale + 0.5f);
  stbtt_GetCodepointHMetrics(info, 'W', &adv, &lsb);
  f->max_width = (int)(adv * f->scale + 0.5f);
  f->bold = weight >= 600;
  f->italic = U8(lf, 20);
  f->underline = U8(lf, 21);
  snprintf(f->face, sizeof f->face, "%s", face);
  f->lf_weight = weight;
  nu hf = obj_new(OBJ_FONT, f);
  PORT_LOG("CreateFont \"%s\" %d -> %08x\n", face, (int)height, hf);
  return hf;
}

static Font *dc_font(Dc *dc) { return dc ? obj_get(dc->font, OBJ_FONT) : NULL; }

nu GetTextMetricsA(nu hdc, nu tm) {
  API_TRACE();
  Font *f = dc_font(dc_get(hdc));
  memset(P(tm), 0, 56);
  if (!f) return 0;
  I32(tm, 0) = f->height;
  I32(tm, 4) = f->ascent;
  I32(tm, 8) = f->descent;
  I32(tm, 12) = f->internal_leading;
  I32(tm, 20) = f->avg_width;
  I32(tm, 24) = f->max_width;
  I32(tm, 28) = f->bold ? 700 : 400;
  I32(tm, 36) = 96;
  I32(tm, 40) = 96;
  U8(tm, 44) = 0x20;
  U8(tm, 45) = 0xff;
  U8(tm, 46) = 0x1f;
  U8(tm, 47) = 0x20;
  U8(tm, 48) = (uint8_t)f->italic;
  U8(tm, 49) = (uint8_t)f->underline;
  return 1;
}

// ------------------------------------------------------------------ Plus Jakarta Sans (card task-25)
//
// With ui.font=plus_jakarta every Font also has a "view" in Plus Jakarta Sans:
// the weight comes from fonts/plus-jakarta-sans/font_map.txt, the size from
// fitting a sample sentence to the original face's width (so text fits the
// boxes drawn for the original), the baseline from the original face's. The
// game keeps being told the original face's metrics (GetTextMetricsA), so its
// layout does not move; GetTextExtentPoint32A reports the real width of what
// is drawn. TextOutA blends anti-aliased coverage into the 16-bit frame.
// ui.font=original (the default without the ui_font mod) never touches this.

// ------------------------------------------------------------------ text for the HD layer
//
// hd.c redraws the game's text at the display's resolution. Every TextOut into
// a 16-bit DIB leaves a record: the glyphs and where they went, plus each pixel
// it set and the value that pixel had before. Unscaled same-format blits carry
// the records along with the pixels (that's how text on golf's back buffer
// reaches the window), and a record only counts while every pixel it set still
// holds the text colour, so anything drawn over the text, or a surface that has
// been redrawn, drops it. Copies jgld makes itself (its own blitters) don't
// carry records, so that text stays as the game drew it.

typedef struct {
  Dib *dib;
  int x0, y0, x1, y1;           // bounds of the pixels set
  int npix, capacity;
  int32_t *pos;                 // y * 65536 + x
  uint16_t *under;              // pixel values before the text
  uint16_t *snap;               // the bounds as they looked after the text
  uint16_t c16;                 // the text colour as stored in the DIB
  uint32_t rgb;                 // COLORREF
  int file, bold, underline, uw0, uw1, ubase;
  int pj;
  float em;
  int32_t clip[4];
  int nglyph;
  float *glyph;                 // (char, x, baseline) per glyph
} TextRec;

#define TEXT_MAX 1024
static TextRec **g_text;  // TEXT_MAX entries, allocated on first use (see hd.c on static data)
static int g_ntext;

static void text_free(int i) {
  TextRec *r = g_text[i];
  free(r->pos);
  free(r->under);
  free(r->snap);
  free(r->glyph);
  free(r);
  // Keep drawing order: overlapping text is put back newest first.
  memmove(g_text + i, g_text + i + 1, (size_t)(--g_ntext - i) * sizeof *g_text);
}

// Whether nothing has been drawn over the text since: its whole bounding box
// still looks as it did (an aim line through a label, a sprite over it).
static int text_intact(TextRec *r) {
  int w = r->x1 - r->x0;
  for (int y = r->y0; y < r->y1; y++)
    if (memcmp((uint16_t *)row_ptr(r->dib, y) + r->x0, r->snap + (y - r->y0) * w, w * 2)) return 0;
  return 1;
}

// After text `r` went on: snapshot its bounds, and let the older text it
// overlaps (shadows under labels) know those pixels changed legitimately.
static void text_snapshot(TextRec *r) {
  int w = r->x1 - r->x0;
  r->snap = malloc((size_t)w * (r->y1 - r->y0) * 2);
  for (int y = r->y0; y < r->y1; y++) memcpy(r->snap + (y - r->y0) * w, (uint16_t *)row_ptr(r->dib, y) + r->x0, w * 2);
  for (int i = 0; i < g_ntext; i++) {
    TextRec *o = g_text[i];
    if (o == r || o->dib != r->dib || o->x0 >= r->x1 || r->x0 >= o->x1 || o->y0 >= r->y1 || r->y0 >= o->y1) continue;
    int ow = o->x1 - o->x0;
    for (int k = 0; k < r->npix; k++) {
      int x = r->pos[k] & 0xffff, y = r->pos[k] >> 16;
      if (x >= o->x0 && x < o->x1 && y >= o->y0 && y < o->y1) o->snap[(y - o->y0) * ow + (x - o->x0)] = ((uint16_t *)row_ptr(r->dib,y))[x];
    }
  }
}

// Drops the records of `d` that no longer match its pixels.
static void text_prune(Dib *d) {
  for (int i = g_ntext - 1; i >= 0; i--)
    if (g_text[i]->dib == d && !text_intact(g_text[i])) text_free(i);
}

static void text_add(TextRec *r) {
  if (!g_text) g_text = malloc(TEXT_MAX * sizeof *g_text);
  // The same text drawn again in the same place replaces the old record.
  for (int i = g_ntext - 1; i >= 0; i--) {
    TextRec *o = g_text[i];
    if (o->dib == r->dib && o->x0 == r->x0 && o->y0 == r->y0 && o->x1 == r->x1 && o->y1 == r->y1) text_free(i);
  }
  if (g_ntext == TEXT_MAX) text_free(0);
  g_text[g_ntext++] = r;
}

static void text_forget(Dib *d) {
  for (int i = g_ntext - 1; i >= 0; i--)
    if (g_text[i]->dib == d) text_free(i);
}

// After an unscaled copy of (sx, sy, w, h) in src to (dx, dy) in dst, already
// clipped to both surfaces: the destination's records inside the rectangle are
// gone, and the source's records wholly inside it come along.
static void text_copy(Dib *dst, int dx, int dy, int w, int h, Dib *src, int sx, int sy) {
  if (!g_ntext || dst->bpp != 16 || src->bpp != 16 || w <= 0 || h <= 0) return;
  for (int i = g_ntext - 1; i >= 0; i--) {
    TextRec *r = g_text[i];
    if (r->dib == dst && r->x0 >= dx && r->y0 >= dy && r->x1 <= dx + w && r->y1 <= dy + h) text_free(i);
  }
  if (src == dst) return;
  TextRec **copies = malloc(TEXT_MAX * sizeof *copies);
  int ncopies = 0;
  int ox = dx - sx, oy = dy - sy;
  for (int i = 0; i < g_ntext; i++) {
    TextRec *r = g_text[i];
    if (r->dib != src || r->x0 < sx || r->y0 < sy || r->x1 > sx + w || r->y1 > sy + h) continue;
    if (!text_intact(r)) continue;
    TextRec *c = malloc(sizeof *c);
    *c = *r;
    c->dib = dst;
    c->x0 += ox; c->x1 += ox; c->y0 += oy; c->y1 += oy;
    c->clip[0] += ox; c->clip[2] += ox; c->clip[1] += oy; c->clip[3] += oy;
    c->uw0 += ox; c->uw1 += ox; c->ubase += oy;
    c->pos = malloc(r->npix * sizeof *c->pos);
    c->under = malloc(r->npix * sizeof *c->under);
    for (int k = 0; k < r->npix; k++) c->pos[k] = r->pos[k] + oy * 65536 + ox;
    memcpy(c->under, r->under, r->npix * sizeof *c->under);
    size_t area = (size_t)(r->x1 - r->x0) * (r->y1 - r->y0);
    c->snap = malloc(area * 2);
    memcpy(c->snap, r->snap, area * 2);
    c->glyph = malloc(r->nglyph * 3 * sizeof(float));
    for (int k = 0; k < r->nglyph; k++) {
      c->glyph[k * 3] = r->glyph[k * 3];
      c->glyph[k * 3 + 1] = r->glyph[k * 3 + 1] + ox;
      c->glyph[k * 3 + 2] = r->glyph[k * 3 + 2] + oy;
    }
    copies[ncopies++] = c;
  }
  for (int i = 0; i < ncopies; i++) text_add(copies[i]);
  free(copies);
}

// For hd.c. Copies the screen into `frame` with the text that still stands
// erased (put back to what was under it), and lists that text in `out` as
// [file, em, rgb, bold, underline, clip x0 y0 x1 y1, underline x0 x1 y, n,
// (char, x, baseline) * n] per run, up to `cap` floats. Text in a font the
// display can't draw yet (font_ready) stays in the frame. Returns the floats used.
int gdi_hd_text(uint16_t *frame, float *out, int cap, int (*font_ready)(int file, const void *data, int size)) {
  Dib *d = &g_screen_dib;
  memcpy(frame, d->bits, (size_t)d->stride * d->h);
  text_prune(d);
  int used = 0;
  // Later text was drawn over earlier text, so put the pixels back newest
  // first; the runs go out in that order too (hd_web.js draws them reversed).
  for (int i = g_ntext - 1; i >= 0; i--) {
    TextRec *r = g_text[i];
    if (r->dib != d) continue;
    FontFile *ff = &g_fonts[r->file];
    if (!font_ready(r->file, ff->data, ff->size)) continue;
    int need = 13 + r->nglyph * 3;
    if (used + need > cap) break;
    for (int k = 0; k < r->npix; k++) {
      int x = r->pos[k] & 0xffff, y = r->pos[k] >> 16;
      frame[y * d->w + x] = r->under[k];
    }
    float *o = out + used;
    o[0] = (float)r->file; o[1] = r->em; o[2] = (float)r->rgb; o[3] = (float)r->bold; o[4] = (float)r->underline;
    for (int k = 0; k < 4; k++) o[5 + k] = (float)r->clip[k];
    o[9] = (float)r->uw0; o[10] = (float)r->uw1; o[11] = (float)r->ubase; o[12] = (float)r->nglyph;
    memcpy(o + 13, r->glyph, r->nglyph * 3 * sizeof(float));
    used += need;
  }
  return used;
}

static const char *pj_weight_name(int w) {
  return w < 450 ? "Regular" : w < 550 ? "Medium" : w < 650 ? "SemiBold" : w < 750 ? "Bold" : "ExtraBold";
}

// Glyph for a character and its advance in font units. Figures use the
// tabular glyphs (or, without them, are centred in the widest figure's cell:
// *shift is the offset in font units).
static int pj_glyph(FontFile *ff, int ch, int *adv, int *shift) {
  int g = stbtt_FindGlyphIndex(&ff->info, ch), lsb;
  *shift = 0;
  if (uif_tabular() && ch >= '0' && ch <= '9') {
    if (ff->tn[0]) g = ff->tn[ch - '0'];
    if (g) {
      stbtt_GetGlyphHMetrics(&ff->info, g, adv, &lsb);
      if (!ff->tn[0] && ff->fig_cell) { *shift = (ff->fig_cell - *adv) / 2; *adv = ff->fig_cell; }
      return g;
    }
  }
  stbtt_GetGlyphHMetrics(&ff->info, g, adv, &lsb);
  return g;
}

static float pj_width_f(int file, float scale, const char *s, int n) {
  float x = 0;
  for (int i = 0; i < n; i++) {
    int adv, sh;
    pj_glyph(&g_fonts[file], (unsigned char)s[i], &adv, &sh);
    x += adv * scale;
  }
  return x;
}

static float orig_width_f(Font *f, const char *s, int n) {
  stbtt_fontinfo *info = &g_fonts[f->file].info;
  float x = 0;
  for (int i = 0; i < n; i++) {
    int adv, lsb;
    stbtt_GetCodepointHMetrics(info, (unsigned char)s[i], &adv, &lsb);
    x += adv * f->scale;
    if (i + 1 < n) x += stbtt_GetCodepointKernAdvance(info, (unsigned char)s[i], (unsigned char)s[i + 1]) * f->scale;
  }
  return x;
}

// Half the height of lowercase and capitals, in pixels: the band the eye reads.
static float visual_height(stbtt_fontinfo *info, float scale) {
  int x0, y0, x1, y1;
  float xh = 0, cap = 0;
  if (stbtt_GetCodepointBox(info, 'x', &x0, &y0, &x1, &y1)) xh = y1 * scale;
  if (stbtt_GetCodepointBox(info, 'H', &x0, &y0, &x1, &y1)) cap = y1 * scale;
  return 0.5f * (xh + cap);
}

// Picks the Plus Jakarta file, size and baseline shift for `f` once.
static int pj_prepare(Font *f) {
  if (f->pj_state) return f->pj_state > 0;
  f->pj_state = -1;
  int w;
  if (!uif_style(f->face, f->lf_weight, &w)) return 0;
  char path[128];
  snprintf(path, sizeof path, "/fonts/plus-jakarta-sans/PlusJakartaSans-%s.ttf", pj_weight_name(w));
  int file = load_font_file(path);
  if (file < 0) {
    fprintf(stderr, "ui.font: can't load %s, keeping the original look for \"%s\"\n", path, f->face);
    return 0;
  }
  stbtt_fontinfo *oi = &g_fonts[f->file].info, *pi = &g_fonts[file].info;
  float em_o = f->scale / stbtt_ScaleForMappingEmToPixels(oi, 1);
  static const char sample[] = "Where will you build your golf course? Hole 12 par 4, 345 yards, $5,000 Sandbox Mode Continue Saved Game";
  int sn = (int)sizeof sample - 1;
  float wo = orig_width_f(f, sample, sn);
  float wp = pj_width_f(file, stbtt_ScaleForMappingEmToPixels(pi, em_o), sample, sn);
  float r = wp > 0 ? uif_fit() * wo / wp : 1.0f;
  if (r < 0.70f) r = 0.70f;
  if (r > 1.05f) r = 1.05f;
  float em_p = em_o * r;
  float floor_px = em_o < 9.0f ? em_o : 9.0f;
  if (em_p < floor_px) em_p = floor_px;
  float sc = stbtt_ScaleForMappingEmToPixels(pi, em_p);
  float dy = 0.5f * (visual_height(pi, sc) - visual_height(oi, f->scale));
  f->pj_file = file;
  f->pj_scale = sc;
  f->pj_dy = (int)(dy + (dy < 0 ? -0.5f : 0.5f));
  f->pj_state = 1;
  PORT_LOG("ui.font: \"%s\" weight %d -> Plus Jakarta Sans %s, em %.1f -> %.1f px, baseline %+d%s\n", f->face, f->lf_weight,
           pj_weight_name(w), em_o, em_p, f->pj_dy, g_fonts[file].tn[0] ? "" : " (figures centred, no tnum glyphs)");
  return 1;
}

static int use_pj(Font *f) { return f && uif_enabled() && pj_prepare(f); }

static int text_width_pj(Font *f, const char *s, int n) { return (int)(pj_width_f(f->pj_file, f->pj_scale, s, n) + 0.5f); }

// One text pixel: `cov` (0..255) of the text colour over what is there, in
// linear light (gamma 1.8), stored back as the frame's 5-bit channels.
static void aa_pixel(Dib *d, const int32_t *clip, int x, int y, const int *src, uint16_t c16, int cov) {
  if (cov < 3 || x < 0 || y < 0 || x >= d->w || y >= d->h) return;
  if (clip && (x < clip[0] || y < clip[1] || x >= clip[2] || y >= clip[3])) return;
  uint16_t *p = (uint16_t *)row_ptr(d, y) + x;
  if (cov >= 253) { *p = c16; return; }
  uint16_t c = from_src16(d, *p);
  int dr = (c >> 10) & 31, dg = (c >> 5) & 31, db = c & 31;
  dr = (dr << 3) | (dr >> 2); dg = (dg << 3) | (dg >> 2); db = (db << 3) | (db >> 2);
  int r = (uif_blend_chan(dr, src[0], cov) * 31 + 127) / 255;
  int g = (uif_blend_chan(dg, src[1], cov) * 31 + 127) / 255;
  int b = (uif_blend_chan(db, src[2], cov) * 31 + 127) / 255;
  *p = to_dst16(d, (uint16_t)((r << 10) | (g << 5) | b));
}

// SIMGOLF_TEXTFIT=1 logs every string that comes out wider than the original
// face would have drawn it (so it may overflow the box made for the original).
static void pj_fit_log(Font *f, const char *s, int n, int wp) {
  static int on = -1;
  static uint32_t seen[1024];
  if (on < 0) on = getenv("SIMGOLF_TEXTFIT") != NULL;
  if (!on) return;
  int wo = (int)(orig_width_f(f, s, n) + 0.5f) + (f->bold ? 1 : 0);
  if (wp <= wo + 1) return;
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 16777619u;
  h = (h ^ (uint32_t)f->pj_file ^ ((uint32_t)(f->pj_scale * 100) << 8)) * 16777619u;
  if (!h) h = 1;
  for (int i = 0; i < 1024; i++) {
    int k = (int)((h + i) & 1023);
    if (seen[k] == h) return;
    if (!seen[k]) { seen[k] = h; break; }
  }
  fprintf(stderr, "textfit OVER \"%.*s\" original %d px, plus_jakarta %d px (+%d) face \"%s\"\n", n, s, wo, wp, wp - wo, f->face);
}

static TextRec *text_pj_begin(Dc *dc, Dib *d, Font *f, int n, int x, int base, int w) {
  TextRec *r = calloc(1, sizeof *r);
  r->dib=d; r->pj=1; r->file=f->pj_file; r->rgb=dc->text_color & 0xffffff;
  r->em=f->pj_scale/stbtt_ScaleForMappingEmToPixels(&g_fonts[r->file].info,1);
  r->x0=r->y0=1<<30; r->x1=r->y1=-(1<<30);
  r->clip[2]=d->w; r->clip[3]=d->h;
  if(dc->has_clip) for(int k=0;k<4;k++) r->clip[k]=k<2?fmax(r->clip[k],dc->clip[k]):fmin(r->clip[k],dc->clip[k]);
  r->nglyph=n; r->glyph=calloc((size_t)n*3,sizeof(float));
  r->underline=f->underline; r->uw0=x; r->uw1=x+w; r->ubase=base+1;
  return r;
}
static void text_pj_pixel(TextRec *r, Dib *d, const int32_t *clip, int x, int y,
                          const int *src, uint16_t c16, int coverage) {
  if(coverage<3 || x<r->clip[0] || y<r->clip[1] || x>=r->clip[2] || y>=r->clip[3]) return;
  uint16_t before=((uint16_t*)row_ptr(d,y))[x];
  aa_pixel(d,clip,x,y,src,c16,coverage);
  uint16_t after=((uint16_t*)row_ptr(d,y))[x];
  if(before==after) return;
  int n=r->npix++;
  if(r->npix>r->capacity) {
    r->capacity=r->capacity?r->capacity*2:256;
    r->pos=realloc(r->pos,(size_t)r->capacity*sizeof *r->pos);
    r->under=realloc(r->under,(size_t)r->capacity*sizeof *r->under);
  }
  r->pos[n]=y*65536+x; r->under[n]=before;
  r->x0=fmin(r->x0,x); r->y0=fmin(r->y0,y);
  r->x1=fmax(r->x1,x+1); r->y1=fmax(r->y1,y+1);
}
static void text_out_pj(Dc *dc, Dib *d, Font *f, int x, int y, const char *s, int n) {
  FontFile *ff = &g_fonts[f->pj_file];
  stbtt_fontinfo *info = &ff->info;
  // task-59: text drawn through a moved view of the back buffer (the bottom-anchored
  // trays on a tall canvas) moves with it, as the original-face path below does and as
  // the DC's clip already does. Without this the trays' tooltips landed 120 px too high,
  // outside the area the new HUD puts back, and stayed on the course ("Tropical bush").
  if (in_view(d)) x += g_view_dx, y += g_view_dy;
  int px = x, base = y;
  int align = dc->text_align;
  if ((align & 24) == 0) base += f->ascent;          // TA_TOP, with the original face's metrics
  else if ((align & 24) == 8) base -= f->descent;    // TA_BOTTOM
  base += f->pj_dy;
  float wf = pj_width_f(f->pj_file, f->pj_scale, s, n);
  int w = (int)(wf + 0.5f);
  pj_fit_log(f, s, n, w);
  if ((align & 6) == 6) px -= w / 2;                 // TA_CENTER
  else if ((align & 6) == 2) px -= w;                // TA_RIGHT
  uint32_t cr = dc->text_color;
  int src[3] = {(int)(cr & 0xff) >> 3, (int)((cr >> 8) & 0xff) >> 3, (int)((cr >> 16) & 0xff) >> 3};
  uint16_t c555 = (uint16_t)((src[0] << 10) | (src[1] << 5) | src[2]);
  uint16_t c16 = to_dst16(d, c555);
  for (int k = 0; k < 3; k++) src[k] = (src[k] << 3) | (src[k] >> 2);
  const int32_t *clip = dc->has_clip ? dc->clip : NULL;
  TextRec *rec=text_pj_begin(dc,d,f,n,px,base,w);
  float fx = (float)px;
  for (int i = 0; i < n; i++) {
    int ch = (unsigned char)s[i], adv, sh;
    int g = pj_glyph(ff, ch, &adv, &sh);
    rec->glyph[i*3]=(float)ch;
    rec->glyph[i*3+1]=fx+sh*f->pj_scale;
    rec->glyph[i*3+2]=(float)base;
    if (ch >= 32 && g) {
      float gx = fx + sh * f->pj_scale;
      int ix = (int)floorf(gx);
      int gw, gh, xoff, yoff;
      unsigned char *bmp = stbtt_GetGlyphBitmapSubpixel(info, f->pj_scale, f->pj_scale, gx - ix, 0, g, &gw, &gh, &xoff, &yoff);
      if (bmp) {
        for (int yy = 0; yy < gh; yy++)
          for (int xx = 0; xx < gw; xx++) text_pj_pixel(rec, d, clip, ix + xoff + xx, base + yoff + yy, src, c16, bmp[yy * gw + xx]);
        stbtt_FreeBitmap(bmp, NULL);
      }
    }
    fx += adv * f->pj_scale;
  }
  if (f->underline)
    for (int xx = px; xx < px + w; xx++) text_pj_pixel(rec, d, clip, xx, base + 1, src, c16, 255);
  if(rec->npix) { text_add(rec); text_snapshot(rec); }
  else { free(rec->glyph); free(rec); }
}


// JGLD also copies DIB pixels directly, bypassing BitBlt. Validate against the
// actual presented pixels, rather than requiring a GDI copy to the window DC.
// Exact rectangle matches prove position, clipping and occlusion; no text is
// removed if a later sprite, caret, HUD erasure or dialog changed that region.
void gdi_legacy_capture(const uint16_t *pixels,uint16_t *clean,int w,int h,LegacyText *text) {
  text->count=text->glyphs=0;
  memcpy(clean,pixels,(size_t)w*h*2);
  for(int i=g_ntext-1;i>=0;i--) {
    TextRec *r=g_text[i];
    if(r->x0<0 || r->y0<0 || r->x1>w || r->y1>h ||
       text->count==LEGACY_RUNS || r->nglyph>LEGACY_GLYPHS-text->glyphs)continue;
    int bw=r->x1-r->x0,match=1;
    for(int y=r->y0;match && y<r->y1;y++)for(int x=r->x0;x<r->x1;x++)
      if((pixels[(size_t)y*w+x]&0x7fff)!=from_src16(r->dib,r->snap[(y-r->y0)*bw+x-r->x0])) {match=0;break;}
    if(!match)continue;
    // The same run may have been carried through several registered DIBs.
    int duplicate=0;
    for(int k=0;k<text->count && !duplicate;k++) {
      LegacyRun *o=text->run+k;
      if(o->font!=g_fonts[r->file].data || o->rgb!=r->rgb || o->em!=r->em || o->count!=r->nglyph ||
         o->bold!=r->bold || o->underline!=r->underline || o->bounds[0]!=r->x0 ||
         o->bounds[1]!=r->y0 || o->bounds[2]!=r->x1 || o->bounds[3]!=r->y1)continue;
      duplicate=1;
      for(int j=0;j<o->count;j++) {
        LegacyGlyph *g=text->glyph+o->first+j;
        if(g->x!=r->glyph[j*3+1] || g->y!=r->glyph[j*3+2]) {duplicate=0;break;}
        int advance,shift;
        int index=r->pj?pj_glyph(g_fonts+r->file,(int)r->glyph[j*3],&advance,&shift):
          stbtt_FindGlyphIndex(&g_fonts[r->file].info,(int)r->glyph[j*3]);
        if(g->index!=index) {duplicate=0;break;}
      }
    }
    if(duplicate)continue;
    LegacyRun *o=text->run+text->count++;
    *o=(LegacyRun){.font=g_fonts[r->file].data,.em=r->em,.rgb=r->rgb,.bold=r->bold,
      .underline=r->underline,.uw0=r->uw0,.uw1=r->uw1,.ubase=r->ubase,
      .first=text->glyphs,.count=r->nglyph};
    memcpy(o->clip,r->clip,sizeof o->clip);
    o->bounds[0]=r->x0;o->bounds[1]=r->y0;o->bounds[2]=r->x1;o->bounds[3]=r->y1;
    for(int j=0;j<r->nglyph;j++) {
      int advance,shift;
      int index=r->pj?pj_glyph(g_fonts+r->file,(int)r->glyph[j*3],&advance,&shift):
        stbtt_FindGlyphIndex(&g_fonts[r->file].info,(int)r->glyph[j*3]);
      text->glyph[text->glyphs++]=(LegacyGlyph){index,r->glyph[j*3+1],r->glyph[j*3+2]};
    }
    for(int j=0;j<r->npix;j++) {
      int x=r->pos[j]&0xffff,y=r->pos[j]>>16;
      clean[(size_t)y*w+x]=from_src16(r->dib,r->under[j]);
    }
  }
}

static int text_width(Font *f, const char *s, int n) {
  stbtt_fontinfo *info = &g_fonts[f->file].info;
  float x = 0;
  for (int i = 0; i < n; i++) {
    int adv, lsb;
    stbtt_GetCodepointHMetrics(info, (unsigned char)s[i], &adv, &lsb);
    x += adv * f->scale;
    if (i + 1 < n) x += stbtt_GetCodepointKernAdvance(info, (unsigned char)s[i], (unsigned char)s[i + 1]) * f->scale;
  }
  return (int)(x + 0.5f) + (f->bold ? 1 : 0);
}

nu GetTextExtentPoint32A(nu hdc, nu str, nu n, nu size) {
  API_TRACE();
  Font *f = dc_font(dc_get(hdc));
  if (!f) {
    I32(size, 0) = (int32_t)n * 8;
    I32(size, 4) = 16;
    return 1;
  }
  I32(size, 0) = use_pj(f) ? text_width_pj(f, S(str), (int)n) : text_width(f, S(str), (int)n);
  I32(size, 4) = f->height;
  return 1;
}

static uint8_t nearest_index(Dib *d, uint32_t colorref) {
  int r = colorref & 0xff, g = (colorref >> 8) & 0xff, b = (colorref >> 16) & 0xff;
  int best = 0, bestd = 1 << 30;
  for (int i = 0; i < 256; i++) {
    int dr = d->colors[i][2] - r, dg = d->colors[i][1] - g, db = d->colors[i][0] - b;
    int dd = dr * dr + dg * dg + db * db;
    if (dd < bestd) { bestd = dd; best = i; }
  }
  return (uint8_t)best;
}

static void put_pixel(Dib *d, const int32_t *clip, int x, int y, uint32_t c16, uint8_t c8) {
  if (x < 0 || y < 0 || x >= d->w || y >= d->h) return;
  if (clip && (x < clip[0] || y < clip[1] || x >= clip[2] || y >= clip[3])) return;
  uint8_t *row = row_ptr(d, y);
  if (d->bpp == 16) ((uint16_t *)row)[x] = (uint16_t)c16;
  else if (d->bpp == 8) row[x] = c8;
}

nu TextOutA(nu hdc, nu x, nu y, nu str, nu n) {
  API_TRACE();
  Dc *dc = dc_get(hdc);
  Dib *d = dc_dib(dc);
  Font *f = dc_font(dc);
  if (!d || !f) {
    static int warned;
    if (warned++ < 5)
      fprintf(stderr, "TextOutA \"%.*s\": DC %08x has %s%s\n", (int)n, S(str), hdc,
              d ? "" : "no bitmap ", f ? "" : "no font");
    return 0;
  }
  const char *s = S(str);
  if (n > 0 && d->bpp == 16 && use_pj(f)) {
    text_out_pj(dc, d, f, (int32_t)x, (int32_t)y, s, (int)n);
    if (dc->is_window) win_present();
    return 1;
  }
  stbtt_fontinfo *info = &g_fonts[f->file].info;
  if (in_view(d)) x += g_view_dx, y += g_view_dy;
  int px = (int32_t)x, base = (int32_t)y;
  int align = dc->text_align;
  if ((align & 24) == 0) base += f->ascent;          // TA_TOP
  else if ((align & 24) == 8) base -= f->descent;    // TA_BOTTOM
  int w = text_width(f, s, (int)n);
  if ((align & 6) == 6) px -= w / 2;                 // TA_CENTER
  else if ((align & 6) == 2) px -= w;                // TA_RIGHT
  uint32_t cr = dc->text_color;
  {
    static int logged;
    if (logged++ < 40)
      PORT_LOG("TextOut \"%.*s\" at %d,%d color %06x align %d dib %dx%d bpp %d font h%d clip %d\n",
              (int)n, s, (int)x, (int)y, cr, dc->text_align, d->w, d->h, d->bpp, f->height, dc->has_clip);
  }
  uint16_t c555 = (uint16_t)((((cr & 0xff) >> 3) << 10) | ((((cr >> 8) & 0xff) >> 3) << 5) | (((cr >> 16) & 0xff) >> 3));
  uint16_t c16 = to_dst16(d, c555);
  uint8_t c8 = d->bpp == 8 ? nearest_index(d, cr) : 0;
  const int32_t *clip = dc->has_clip ? dc->clip : NULL;
  TextRec *rec = NULL;
  if (d->bpp == 16 && n > 0) {
    rec = calloc(1, sizeof *rec);
    rec->dib = d;
    rec->x0 = rec->y0 = 1 << 30;
    rec->x1 = rec->y1 = -(1 << 30);
    rec->c16 = c16;
    rec->rgb = cr & 0xffffff;
    rec->file = f->file;
    rec->em = f->scale / stbtt_ScaleForMappingEmToPixels(info, 1);
    rec->bold = f->bold;
    rec->underline = f->underline;
    rec->uw0 = px;
    rec->uw1 = px + w;
    rec->ubase = base + 1;
    int32_t c[4] = {0, 0, d->w, d->h};
    if (clip) for (int k = 0; k < 4; k++) c[k] = k < 2 ? (clip[k] > c[k] ? clip[k] : c[k]) : (clip[k] < c[k] ? clip[k] : c[k]);
    memcpy(rec->clip, c, sizeof c);
    rec->nglyph = (int)n;
    rec->glyph = malloc(n * 3 * sizeof(float));
  }
  int cappix = 0;
  // Sets one pixel, noting it (and what it was) in the record.
#define TEXT_PIXEL(X, Y)                                                                      \
  do {                                                                                        \
    int X_ = (X), Y_ = (Y);                                                                   \
    if (rec && X_ >= rec->clip[0] && Y_ >= rec->clip[1] && X_ < rec->clip[2] && Y_ < rec->clip[3]) { \
      uint16_t *p_ = (uint16_t *)row_ptr(d, Y_) + X_;                                         \
      if (rec->npix == cappix) {                                                              \
        cappix = cappix ? cappix * 2 : 256;                                                   \
        rec->pos = realloc(rec->pos, cappix * sizeof *rec->pos);                              \
        rec->under = realloc(rec->under, cappix * sizeof *rec->under);                        \
      }                                                                                       \
      if (*p_ != c16) { /* already the colour: nothing to put back */                         \
        rec->pos[rec->npix] = Y_ * 65536 + X_;                                                \
        rec->under[rec->npix++] = *p_;                                                        \
        if (X_ < rec->x0) rec->x0 = X_;                                                       \
        if (Y_ < rec->y0) rec->y0 = Y_;                                                       \
        if (X_ >= rec->x1) rec->x1 = X_ + 1;                                                  \
        if (Y_ >= rec->y1) rec->y1 = Y_ + 1;                                                  \
      }                                                                                       \
    }                                                                                         \
    put_pixel(d, clip, X_, Y_, c16, c8);                                                      \
  } while (0)
  float fx = (float)px;
  for (int i = 0; i < (int)n; i++) {
    int ch = (unsigned char)s[i];
    int adv, lsb;
    stbtt_GetCodepointHMetrics(info, ch, &adv, &lsb);
    if (rec) {
      rec->glyph[i * 3] = (float)ch;
      rec->glyph[i * 3 + 1] = fx;
      rec->glyph[i * 3 + 2] = (float)base;
    }
    int gw, gh, xoff, yoff;
    unsigned char *bmp = stbtt_GetCodepointBitmapSubpixel(info, f->scale, f->scale, fx - (int)fx, 0, ch,
                                                         &gw, &gh, &xoff, &yoff);
    if (bmp) {
      for (int yy = 0; yy < gh; yy++)
        for (int xx = 0; xx < gw; xx++)
          if (bmp[yy * gw + xx] >= 128) {
            int ox = (int)fx + xoff + xx, oy = base + yoff + yy;
            TEXT_PIXEL(ox, oy);
            if (f->bold) TEXT_PIXEL(ox + 1, oy);
          }
      stbtt_FreeBitmap(bmp, NULL);
    }
    fx += adv * f->scale;
    if (i + 1 < (int)n) fx += stbtt_GetCodepointKernAdvance(info, ch, (unsigned char)s[i + 1]) * f->scale;
  }
  if (f->underline)
    for (int xx = px; xx < px + w; xx++) TEXT_PIXEL(xx, base + 1);
#undef TEXT_PIXEL
  if (rec) {
    if (rec->npix) {
      text_add(rec);
      text_snapshot(rec);
    } else {
      free(rec->glyph);
      free(rec);
    }
  }
  if (dc->is_window) win_present();
  return 1;
}
