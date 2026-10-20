// Frosted Mint UI kit: glyph atlas text, see ui_text.h.
#include "port/ui/ui_text.h"

#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// gdi.c has its own static copy; the kit is also built without the game.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

static const char *const k_cuts[UIT_FILES] = {"Regular", "Medium", "SemiBold", "Bold", "ExtraBold"};
static const int k_weights[UIT_FILES] = {400, 500, 600, 700, 800};

#define INFO(t, i) ((stbtt_fontinfo *)(t)->f[i].info)

static unsigned char *read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *d = n > 0 ? malloc((size_t)n) : NULL;
  if (d && fread(d, 1, (size_t)n, f) != (size_t)n) {
    free(d);
    d = NULL;
  }
  fclose(f);
  return d;
}

static void flush_text(UiText *t);

static void atlas_reset(UiText *t) {
  // Vertices already queued in this string still reference the old atlas.
  if (t->ni) {
    flush_text(t);
    SDL_RenderFlush(t->r);
  }
  memset(t->alpha, 0, (size_t)t->aw * t->ah);
  memset(t->cache, 0, sizeof t->cache);
  t->nglyphs = 0;
  t->shelf_x = t->shelf_y = 1;
  t->shelf_h = 0;
  t->dirty = 1;
  t->dx0 = t->dy0 = 0;
  t->dx1 = t->aw;
  t->dy1 = t->ah;
}

static int load_fonts(UiText *t) {
  int loaded = 0;
  for (int i = 0; i < UIT_FILES; i++) {
    if (t->f[i].ok) { loaded++; continue; }
    char path[1024];
    snprintf(path, sizeof path, "%s/PlusJakartaSans-%s.ttf", t->font_dir, k_cuts[i]);
    t->f[i].weight = k_weights[i];
    t->f[i].info = calloc(1, sizeof(stbtt_fontinfo));
    t->f[i].data = read_file(path);
    if (!t->f[i].info || !t->f[i].data || !stbtt_InitFont(INFO(t, i), t->f[i].data, stbtt_GetFontOffsetForIndex(t->f[i].data, 0))) {
      fprintf(stderr, "ui: font %s not loaded\n", path);
      free(t->f[i].info); free(t->f[i].data);
      t->f[i].info = NULL; t->f[i].data = NULL;
      continue;
    }
    t->f[i].ok = 1;
    loaded++;
    // Tabular figures follow the figures by ten glyph ids in every cut (gdi.c checks the same).
    int ok = 1, adv0 = -1, lsb;
    for (int k = 0; k < 10 && ok; k++) {
      int g = stbtt_FindGlyphIndex(INFO(t, i), '0' + k), a, pa;
      if (!g) {
        ok = 0;
        break;
      }
      stbtt_GetGlyphHMetrics(INFO(t, i), g + 10, &a, &lsb);
      stbtt_GetGlyphHMetrics(INFO(t, i), g, &pa, &lsb);
      if (adv0 < 0) adv0 = a;
      if (a != adv0 || (k == 1 && a == pa)) ok = 0;
      t->f[i].tn[k] = g + 10;
    }
    if (!ok) memset(t->f[i].tn, 0, sizeof t->f[i].tn);
  }
  t->font_retry_at = loaded < UIT_FILES ? SDL_GetTicks() + 1000 : 0;
  return loaded;
}

static void retry_fonts(UiText *t) {
  if (t->font_retry_at && SDL_TICKS_PASSED(SDL_GetTicks(), t->font_retry_at)) load_fonts(t);
}

int uit_init(UiText *t, SDL_Renderer *r, const char *font_dir, float gamma) {
  memset(t, 0, sizeof *t);
  t->r = r;
  snprintf(t->font_dir, sizeof t->font_dir, "%s", font_dir ? font_dir : "");
  int loaded = load_fonts(t);
  t->gamma = gamma > 0 ? gamma : 1;
  for (int i = 0; i < 256; i++) t->curve[i] = (uint8_t)(powf(i / 255.0f, 1.0f / t->gamma) * 255.0f + 0.5f);
  t->aw = t->ah = 2048;
  t->alpha = malloc((size_t)t->aw * t->ah);
  if (!t->alpha) {
    fprintf(stderr, "ui: cannot allocate glyph atlas pixels; will retry\n");
    t->pixels_retry_at = SDL_GetTicks() + 1000;
    return loaded;  // callers may still show the UI using icons/art
  }
  if (r) {
    t->atlas = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, t->aw, t->ah);
    if (t->atlas) {
      SDL_SetTextureBlendMode(t->atlas, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(t->atlas, SDL_ScaleModeNearest);
    } else t->atlas_retry_at = SDL_GetTicks() + 1000;
  }
  atlas_reset(t);
  return loaded;
}

void uit_free(UiText *t) {
  for (int i = 0; i < UIT_FILES; i++) {
    free(t->f[i].data);
    free(t->f[i].info);
  }
  if (t->atlas) SDL_DestroyTexture(t->atlas);
  free(t->alpha);
  free(t->v);
  free(t->idx);
  memset(t, 0, sizeof *t);
}

static int pick(UiText *t, int weight) {
  retry_fonts(t);
  int best = -1, bd = 1 << 30;
  for (int i = 0; i < UIT_FILES; i++) {
    if (!t->f[i].ok) continue;
    int dd = abs(t->f[i].weight - weight);
    if (dd < bd) bd = dd, best = i;
  }
  return best;
}

// Next code point; invalid UTF-8 bytes are Windows-1252.
static uint32_t next_cp(const char **ps) {
  static const uint16_t cp1252[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                      0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                      0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
  const unsigned char *s = (const unsigned char *)*ps;
  uint32_t c = s[0];
  int n = c >= 0xF0 && c < 0xF8 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 && c < 0xE0 ? 1 : 0;
  if (c >= 0x80 && n) {
    uint32_t v = c & (0x3F >> n);
    int ok = 1;
    for (int i = 1; i <= n; i++) {
      if ((s[i] & 0xC0) != 0x80) {
        ok = 0;
        break;
      }
      v = v << 6 | (s[i] & 0x3F);
    }
    if (ok) {
      *ps += n + 1;
      return v;
    }
  }
  *ps += 1;
  if (c >= 0x80 && c < 0xA0) return cp1252[c - 0x80] ? cp1252[c - 0x80] : '?';
  return c;
}

static int glyph_index(UiText *t, int file, uint32_t cp) {
  if (cp >= '0' && cp <= '9' && t->f[file].tn[0]) return t->f[file].tn[cp - '0'];
  int g = stbtt_FindGlyphIndex(INFO(t, file), (int)cp);
  if (!g && cp == 0x2009) g = stbtt_FindGlyphIndex(INFO(t, file), ' ');   // thin space
  return g;
}

static UitGlyph *glyph(UiText *t, int file, int px, int g) {
  if (!t->alpha) return NULL;  // no rasterization without CPU atlas storage
  uint32_t key = 0x80000000u | (uint32_t)file << 26 | (uint32_t)(px & 1023) << 16 | (uint32_t)(g & 0xFFFF);
  uint32_t h = (key * 2654435761u) >> 19;   // 13 bits
  for (int probe = 0; probe < UIT_CACHE; probe++) {
    UitGlyph *e = &t->cache[(h + probe) & (UIT_CACHE - 1)];
    if (e->key == key) return e;
    if (e->key) continue;
    if (t->nglyphs > UIT_CACHE * 3 / 4) {
      atlas_reset(t);
      t->resets++;
      return glyph(t, file, px, g);
    }
    stbtt_fontinfo *info = INFO(t, file);
    float scale = stbtt_ScaleForPixelHeight(info, (float)px);
    // ScaleForPixelHeight is ascent-descent; the design sizes are em sizes (CSS font-size).
    scale = stbtt_ScaleForMappingEmToPixels(info, (float)px);
    int adv, lsb, x0, y0, x1, y1;
    stbtt_GetGlyphHMetrics(info, g, &adv, &lsb);
    stbtt_GetGlyphBitmapBox(info, g, scale, scale, &x0, &y0, &x1, &y1);
    int w = x1 - x0, hh = y1 - y0;
    if (w > t->aw - 2 || hh > t->ah - 2) return NULL;
    if (w > 0 && hh > 0) {
      if (t->shelf_x + w + 1 > t->aw) {
        t->shelf_x = 1;
        t->shelf_y += t->shelf_h + 1;
        t->shelf_h = 0;
      }
      if (t->shelf_y + hh + 1 > t->ah) {
        atlas_reset(t);
        t->resets++;
        return glyph(t, file, px, g);
      }
      uint8_t *dst = t->alpha + (size_t)t->shelf_y * t->aw + t->shelf_x;
      stbtt_MakeGlyphBitmap(info, dst, w, hh, t->aw, scale, scale, g);
      if (t->gamma != 1)
        for (int y = 0; y < hh; y++)
          for (int x = 0; x < w; x++) dst[(size_t)y * t->aw + x] = t->curve[dst[(size_t)y * t->aw + x]];
      e->x = (int16_t)t->shelf_x;
      e->y = (int16_t)t->shelf_y;
      if (!t->dirty) {
        t->dirty = 1;
        t->dx0 = t->shelf_x, t->dy0 = t->shelf_y, t->dx1 = t->shelf_x + w, t->dy1 = t->shelf_y + hh;
      } else {
        if (t->shelf_x < t->dx0) t->dx0 = t->shelf_x;
        if (t->shelf_y < t->dy0) t->dy0 = t->shelf_y;
        if (t->shelf_x + w > t->dx1) t->dx1 = t->shelf_x + w;
        if (t->shelf_y + hh > t->dy1) t->dy1 = t->shelf_y + hh;
      }
      t->shelf_x += w + 1;
      if (hh > t->shelf_h) t->shelf_h = hh;
    }
    e->key = key;
    e->w = (int16_t)(w > 0 ? w : 0);
    e->h = (int16_t)(hh > 0 ? hh : 0);
    e->xoff = (int16_t)x0;
    e->yoff = (int16_t)y0;
    e->adv = adv * scale;
    t->nglyphs++;
    return e;
  }
  return NULL;
}

static int upload(UiText *t) {
  if (!t->atlas) return 0;
  if (!t->dirty) return 1;
  int w = t->dx1 - t->dx0, h = t->dy1 - t->dy0;
  if (w > 0 && h > 0) {
    // A full 2048-square upload used a 16 MiB temporary allocation, exactly
    // when a new card or device recovery is already under memory pressure.
    int band = h < 32 ? h : 32;
    uint32_t *px = malloc((size_t)w * band * 4);
    if (px) {
      int uploaded = 1;
      for (int first = 0; first < h && uploaded; first += band) {
        int rows = h - first < band ? h - first : band;
        for (int y = 0; y < rows; y++)
          for (int x = 0; x < w; x++)
            px[(size_t)y * w + x] = (uint32_t)t->alpha[(size_t)(t->dy0 + first + y) * t->aw + t->dx0 + x] << 24 | 0xFFFFFFu;
        SDL_Rect r = {t->dx0, t->dy0 + first, w, rows};
        uploaded = SDL_UpdateTexture(t->atlas, &r, px, w * 4) == 0;
      }
      free(px);
      if (!uploaded) {
        // A single transient upload can succeed on the very next draw. Only
        // repeated failure retires the atlas and throttles its recreation.
        if (++t->upload_errors >= 2) {
          uit_device_reset(t);
          t->atlas_retry_at = SDL_GetTicks() + 1000;
        }
        return 0;
      }
    } else return 0;  // CPU allocation failed: do not mark glyphs uploaded
  }
  t->dirty = 0;
  t->upload_errors = 0;
  return 1;
}

static void flush_text(UiText *t) {
  if (t->ni) {
    if (upload(t) && SDL_RenderGeometry(t->r, t->atlas, t->v, t->nv, t->idx, t->ni) < 0) {
      uit_device_reset(t);
      t->atlas_retry_at = SDL_GetTicks() + 1000;
    }
  }
  t->nv = t->ni = 0;
}

float uit_measure(UiText *t, int weight, float px, const char *s, float tracking) {
  int f = pick(t, weight);
  if (f < 0 || !s) return 0;
  int ipx = (int)(px + 0.5f);
  float x = 0;
  int prev = 0;
  stbtt_fontinfo *info = INFO(t, f);
  float scale = stbtt_ScaleForMappingEmToPixels(info, (float)ipx);
  while (*s) {
    uint32_t cp = next_cp(&s);
    int g = glyph_index(t, f, cp);
    int adv, lsb;
    stbtt_GetGlyphHMetrics(info, g, &adv, &lsb);
    if (prev) x += stbtt_GetGlyphKernAdvance(info, prev, g) * scale;
    x += adv * scale + tracking * ipx;
    prev = g;
  }
  return x;
}

void uit_metrics(UiText *t, int weight, float px, float *ascent, float *descent) {
  int f = pick(t, weight);
  *ascent = px * 0.8f;
  *descent = px * 0.2f;
  if (f < 0) return;
  int a, d, gap;
  stbtt_GetFontVMetrics(INFO(t, f), &a, &d, &gap);
  float scale = stbtt_ScaleForMappingEmToPixels(INFO(t, f), (float)(int)(px + 0.5f));
  *ascent = a * scale;
  *descent = -d * scale;
}

float uit_draw(UiText *t, float x, float y, int weight, float px, const char *s, float tracking, UiColor c) {
  int f = pick(t, weight);
  if (f < 0 || !s || !*s) return x;
  if (!t->alpha) {
    Uint32 now = SDL_GetTicks();
    if (t->pixels_retry_at && !SDL_TICKS_PASSED(now, t->pixels_retry_at)) return x;
    t->alpha = malloc((size_t)t->aw * t->ah);
    if (!t->alpha) { t->pixels_retry_at = now + 1000; return x; }
    t->pixels_retry_at = 0;
    atlas_reset(t);
  }
  if (!t->atlas) {
    if (!t->r || !t->alpha) return x;
    Uint32 now = SDL_GetTicks();
    if (t->atlas_retry_at && !SDL_TICKS_PASSED(now, t->atlas_retry_at)) return x;
    t->atlas = SDL_CreateTexture(t->r, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STATIC, t->aw, t->ah);
    if (!t->atlas) {
      t->atlas_retry_at = now + 1000;
      return x;
    }
    SDL_SetTextureBlendMode(t->atlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t->atlas, SDL_ScaleModeNearest);
    // Alpha glyph pixels were already allocated; upload them on first draw.
    t->dirty = 1;
    t->dx0 = t->dy0 = 0;
    t->dx1 = t->aw;
    t->dy1 = t->ah;
    t->atlas_retry_at = 0;
  }
  int ipx = (int)(px + 0.5f);
  stbtt_fontinfo *info = INFO(t, f);
  float scale = stbtt_ScaleForMappingEmToPixels(info, (float)ipx);
  size_t n = strlen(s);
  if (n > (size_t)(INT_MAX - 64) / 4) return x;
  if ((int)n * 4 > t->cap) {
    int cap = (int)n * 4 + 64;
    SDL_Vertex *v = realloc(t->v, sizeof *t->v * (size_t)cap);
    if (!v) return x;
    t->v = v;
    int *idx = realloc(t->idx, sizeof *t->idx * (size_t)(cap / 4) * 6);
    if (!idx) return x;
    t->idx = idx;
    t->cap = cap;  // publish only after both allocations succeed
  }
  t->nv = t->ni = 0;
  float by = floorf(y + 0.5f), pen = x;
  int prev = 0;
  float iw = 1.0f / t->aw, ih = 1.0f / t->ah;
  SDL_Color col = {c.r, c.g, c.b, c.a};
  while (*s) {
    uint32_t cp = next_cp(&s);
    int g = glyph_index(t, f, cp);
    if (prev) pen += stbtt_GetGlyphKernAdvance(info, prev, g) * scale;
    UitGlyph *e = glyph(t, f, ipx, g);
    if (!e) break;
    if (e->w > 0) {
      float gx = floorf(pen + 0.5f) + e->xoff, gy = by + e->yoff;
      float u0 = e->x * iw, v0 = e->y * ih, u1 = (e->x + e->w) * iw, v1 = (e->y + e->h) * ih;
      SDL_Vertex *v = &t->v[t->nv];
      v[0] = (SDL_Vertex){{gx, gy}, col, {u0, v0}};
      v[1] = (SDL_Vertex){{gx + e->w, gy}, col, {u1, v0}};
      v[2] = (SDL_Vertex){{gx + e->w, gy + e->h}, col, {u1, v1}};
      v[3] = (SDL_Vertex){{gx, gy + e->h}, col, {u0, v1}};
      int *ix = &t->idx[t->ni];
      ix[0] = t->nv, ix[1] = t->nv + 1, ix[2] = t->nv + 2;
      ix[3] = t->nv, ix[4] = t->nv + 2, ix[5] = t->nv + 3;
      t->nv += 4;
      t->ni += 6;
    }
    pen += e->adv + tracking * ipx;
    prev = g;
  }
  flush_text(t);
  return pen;
}

static int fit_prefix(UiText *t, int weight, float px, const char *s, float tracking,
                      float max_w, int max_bytes, float suffix_w, int words) {
  if (!s || max_bytes <= 0) return 0;
  int f = pick(t, weight), ipx = (int)(px + 0.5f);
  stbtt_fontinfo *info = f >= 0 ? INFO(t, f) : NULL;
  float scale = info ? stbtt_ScaleForMappingEmToPixels(info, (float)ipx) : 0;
  float width = 0;
  int prev = 0, best = 0, last_space = -1;
  const char *p = s;
  while (*p && p - s < max_bytes) {
    uint32_t cp = next_cp(&p);
    if (p - s > max_bytes) break;
    if (info) {
      int g = glyph_index(t, f, cp), adv, lsb;
      stbtt_GetGlyphHMetrics(info, g, &adv, &lsb);
      if (prev) width += stbtt_GetGlyphKernAdvance(info, prev, g) * scale;
      width += adv * scale + tracking * ipx;
      prev = g;
    }
    if (width + suffix_w > max_w) {
      if (words) break;
      continue;
    }
    best = (int)(p - s);
    if (*p == ' ' || !*p) last_space = best;
  }
  if (words && s[best] && last_space > 0) return last_space;
  return best;
}

int uit_fit(UiText *t, int weight, float px, const char *s, float tracking, float max_w) {
  return fit_prefix(t, weight, px, s, tracking, max_w, 1023, 0, 1);
}

int uit_prefix(UiText *t, int weight, float px, const char *s, float tracking, float max_w, int max_bytes, float suffix_w) {
  return fit_prefix(t, weight, px, s, tracking, max_w, max_bytes, suffix_w, 0);
}

void uit_device_reset(UiText *t) {
  if(t->atlas)SDL_DestroyTexture(t->atlas);
  t->atlas=NULL;t->atlas_retry_at=0;t->upload_errors=0;t->nv=t->ni=0;
  t->dirty=1;t->dx0=t->dy0=0;t->dx1=t->aw;t->dy1=t->ah;
}
