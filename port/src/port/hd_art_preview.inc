// Private HD-art implementation: native chooser previews reuse the same
// tagged frame, palette, alpha-mask and shadow requests as the course.
#include "port/hd_art_preview_pixels.h"

#define PREVIEW_PARTS 32
#define PREVIEW_BYTES ((size_t)64 * 1024 * 1024)
typedef struct { Entry e; uint16_t pal[256], lev[256]; } PreviewPart;
typedef struct {
  PreviewPart *parts;
  int n, x, y, w, h;
  unsigned serial, epoch;
} PreviewPub;
static CRITICAL_SECTION g_preview_lock;
static volatile LONG g_preview_epoch = 1;
static PreviewPub g_previews[HD_ART_PREVIEW_MAX];
static struct {
  PreviewPart parts[PREVIEW_PARTS];
  int slot, n, active, failed;
  unsigned epoch, generation;
} g_preview_capture;
static unsigned g_preview_serial;
static struct {
  SDL_Texture *texture;
  unsigned serial, epoch;
  uint32_t used;
  Uint32 retry;
  size_t bytes;
} g_preview_tex[HD_ART_PREVIEW_MAX];
static size_t g_preview_bytes;
static uint32_t g_preview_build_tick = ~0u;

static void preview_init(void) { InitializeCriticalSection(&g_preview_lock); }
unsigned hd_art_preview_revision(void) {
  return (unsigned)InterlockedCompareExchange(&g_preview_epoch, 0, 0);
}
static void preview_look_changed(void) { InterlockedIncrement(&g_preview_epoch); }
static void preview_reject(void) {
  if (g_preview_capture.active) g_preview_capture.failed = 1;
}
static void preview_record(const Entry *e, const uint16_t *pal, const uint16_t *lev) {
  if (!g_preview_capture.active) return;
  if (g_preview_capture.n >= PREVIEW_PARTS) { preview_reject(); return; }
  PreviewPart *part = &g_preview_capture.parts[g_preview_capture.n++];
  part->e = *e;
  part->e.hidden = 0;
  if (pal) memcpy(part->pal, pal, sizeof part->pal);
  else memset(part->pal, 0, sizeof part->pal);
  if (lev) memcpy(part->lev, lev, sizeof part->lev);
  else memset(part->lev, 0, sizeof part->lev);
  want(e, 0, part->pal);
  if (e->shadow_file >= 0) want(e, 1, part->lev);
}
void hd_art_preview_begin(int slot) {
  g_preview_capture.slot = slot;
  g_preview_capture.n = g_preview_capture.failed = 0;
  g_preview_capture.epoch = hd_art_preview_revision();
  g_preview_capture.generation = g_rec_generation;
  g_preview_capture.active = g_on && g_any && slot >= 0 && slot < HD_ART_PREVIEW_MAX;
}
void hd_art_preview_pause(void) {
  if (g_preview_capture.generation != g_rec_generation) preview_reject();
  g_preview_capture.active = 0;
}
void hd_art_preview_commit(int slot, int x, int y, int w, int h) {
  if (!g_on || slot < 0 || slot >= HD_ART_PREVIEW_MAX) return;
  int valid = slot == g_preview_capture.slot && !g_preview_capture.active &&
              !g_preview_capture.failed && g_preview_capture.n > 0 &&
              g_preview_capture.epoch == hd_art_preview_revision() &&
              g_preview_capture.generation == g_rec_generation &&
              w > 0 && h > 0 && w <= 512 && h <= 512;
  int n = valid ? g_preview_capture.n : 0;
  PreviewPart *parts = n ? malloc((size_t)n * sizeof *parts) : NULL;
  if (parts) memcpy(parts, g_preview_capture.parts, (size_t)n * sizeof *parts);
  else n = 0;
  EnterCriticalSection(&g_preview_lock);
  PreviewPub *pub = &g_previews[slot];
  free(pub->parts);
  *pub = (PreviewPub){parts, n, x, y, w, h, ++g_preview_serial, g_preview_capture.epoch};
  LeaveCriticalSection(&g_preview_lock);
  if (getenv("SIMGOLF_PREVIEW_TRACE")) {
    fprintf(stderr, "art: preview capture slot %d parts %d rejected %d generation %u/%u box %d,%d %dx%d\n",
            slot, n, g_preview_capture.failed, g_preview_capture.generation, g_rec_generation, x, y, w, h);
    fflush(stderr);
  }
}

static int preview_reserve(int slot, size_t bytes) {
  if (bytes > PREVIEW_BYTES) return 0;
  while (g_preview_bytes - g_preview_tex[slot].bytes + bytes > PREVIEW_BYTES) {
    int oldest = -1;
    for (int i = 0; i < HD_ART_PREVIEW_MAX; i++)
      if (i != slot && g_preview_tex[i].texture && g_preview_tex[i].used != g_tick &&
          (oldest < 0 || g_preview_tex[i].used < g_preview_tex[oldest].used)) oldest = i;
    if (oldest < 0) return 0;
    SDL_DestroyTexture(g_preview_tex[oldest].texture);
    g_preview_bytes -= g_preview_tex[oldest].bytes;
    memset(&g_preview_tex[oldest], 0, sizeof g_preview_tex[oldest]);
  }
  return 1;
}

// Render into transparent black using the ordinary HD textures. Preserve all
// renderer state because the HUD shares the course renderer and target.
static SDL_Texture *preview_build(SDL_Renderer *r, const PreviewPub *pub, const PreviewPart *parts) {
  int w = pub->w * 4, h = pub->h * 4;
  SDL_Texture *target = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
  if (!target) return NULL;
  SDL_Texture *old_target = SDL_GetRenderTarget(r);
  SDL_Rect viewport, clip;
  SDL_RenderGetViewport(r, &viewport);
  SDL_RenderGetClipRect(r, &clip);
  SDL_bool clipped = SDL_RenderIsClipEnabled(r);
  float scale_x, scale_y;
  SDL_RenderGetScale(r, &scale_x, &scale_y);
  int logical_w, logical_h;
  SDL_RenderGetLogicalSize(r, &logical_w, &logical_h);
  SDL_BlendMode blend;
  SDL_GetRenderDrawBlendMode(r, &blend);
  Uint8 red, green, blue, alpha;
  SDL_GetRenderDrawColor(r, &red, &green, &blue, &alpha);
  if (SDL_SetRenderTarget(r, target) != 0) { SDL_DestroyTexture(target); return NULL; }
  SDL_RenderSetLogicalSize(r, 0, 0);
  SDL_RenderSetScale(r, 1, 1);
  SDL_RenderSetViewport(r, NULL);
  SDL_RenderSetClipRect(r, NULL);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
  int ok = SDL_RenderClear(r) == 0;
  for (int i = 0; i < pub->n && ok; i++) {
    const Entry *e = &parts[i].e;
    SDL_FRect d = {(e->x - pub->x) * 4, (e->y - pub->y) * 4, e->w * 4, e->h * 4};
    SDL_RendererFlip flip = (e->flip & 1 ? SDL_FLIP_HORIZONTAL : 0) |
                            (e->flip & 2 ? SDL_FLIP_VERTICAL : 0);
    if (e->shadow_file >= 0) {
      SDL_FRect sd;
      shadow_rect(e, &d, &sd);
      ok = SDL_RenderCopyExF(r, tex_of(e, 1), NULL, &sd, 0, NULL, flip) == 0;
    }
    if (ok) ok = SDL_RenderCopyExF(r, tex_of(e, 0), NULL, &d, 0, NULL, flip) == 0;
  }
  uint32_t *pixels = ok ? malloc((size_t)w * h * sizeof *pixels) : NULL;
  if (!pixels || SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, w * 4) != 0) ok = 0;
  SDL_SetRenderTarget(r, old_target);
  SDL_RenderSetLogicalSize(r, logical_w, logical_h);
  SDL_RenderSetScale(r, scale_x, scale_y);
  SDL_RenderSetViewport(r, &viewport);
  SDL_RenderSetClipRect(r, clipped ? &clip : NULL);
  SDL_SetRenderDrawBlendMode(r, blend);
  SDL_SetRenderDrawColor(r, red, green, blue, alpha);
  SDL_DestroyTexture(target);
  SDL_Texture *out = NULL;
  if (ok) {
    size_t visible = 0;
    for (size_t i = 0, n = (size_t)w * h; i < n; i++) {
      visible += (pixels[i] >> 24) != 0;
      pixels[i] = hd_preview_straight(pixels[i]);
    }
    if (!visible) { free(pixels); return NULL; }
    out = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (out && SDL_UpdateTexture(out, NULL, pixels, w * 4) != 0) { SDL_DestroyTexture(out); out = NULL; }
    if (out) {
      SDL_SetTextureBlendMode(out, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(out, SDL_ScaleModeLinear);
    }
  }
  free(pixels);
  return out;
}

SDL_Texture *hd_art_preview_texture(int slot, SDL_Renderer *r) {
  if (!g_on || !g_any || !r || slot < 0 || slot >= HD_ART_PREVIEW_MAX) return NULL;
  PreviewPub pub;
  PreviewPart parts[PREVIEW_PARTS];
  EnterCriticalSection(&g_preview_lock);
  pub = g_previews[slot];
  if (pub.n > 0 && pub.n <= PREVIEW_PARTS) memcpy(parts, pub.parts, (size_t)pub.n * sizeof *parts);
  LeaveCriticalSection(&g_preview_lock);
  if (!pub.n || pub.n > PREVIEW_PARTS || pub.epoch != hd_art_preview_revision()) return NULL;
  if (g_preview_tex[slot].texture && g_preview_tex[slot].serial == pub.serial &&
      g_preview_tex[slot].epoch == pub.epoch) {
    g_preview_tex[slot].used = g_tick;
    return g_preview_tex[slot].texture;
  }
  Uint32 now = SDL_GetTicks();
  if (g_preview_tex[slot].retry && !SDL_TICKS_PASSED(now, g_preview_tex[slot].retry)) return NULL;
  int ready = 1;
  for (int i = 0; i < pub.n; i++) {
    const Entry *e = &parts[i].e;
    if (!tex_of(e, 0)) { want(e, 0, parts[i].pal); ready = 0; }
    if (e->shadow_file >= 0 && !tex_of(e, 1)) { want(e, 1, parts[i].lev); ready = 0; }
  }
  size_t bytes = (size_t)pub.w * pub.h * 16 * 4;
  if (!ready || g_preview_build_tick == g_tick || !preview_reserve(slot, bytes)) return NULL;
  g_preview_build_tick = g_tick;
  SDL_Texture *next = preview_build(r, &pub, parts);
  if (!next) { g_preview_tex[slot].retry = now + 1000; return NULL; }
  if (g_preview_tex[slot].texture) SDL_DestroyTexture(g_preview_tex[slot].texture);
  g_preview_bytes -= g_preview_tex[slot].bytes;
  g_preview_tex[slot].texture = next;
  g_preview_tex[slot].serial = pub.serial;
  g_preview_tex[slot].epoch = pub.epoch;
  g_preview_tex[slot].bytes = bytes;
  g_preview_tex[slot].used = g_tick;
  g_preview_tex[slot].retry = 0;
  g_preview_bytes += bytes;
  if (getenv("SIMGOLF_PREVIEW_TRACE")) {
    fprintf(stderr, "art: HD preview slot %d source %dx%d output %dx%d parts %d epoch %u\n",
            slot, pub.w, pub.h, pub.w * 4, pub.h * 4, pub.n, pub.epoch);
    fflush(stderr);
  }
  return next;
}
