/* Fault injection against ui_text.c itself, using the bundled real fonts. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
static Uint32 ticks;
static int cpu_fail, cpu_attempts, gpu_fail, gpu_attempts, live_textures;
static int upload_fail, uploads, draws;
static void *allocations[8192];
static int live_allocations;
static void record(void *p) {
  if (!p) return;
  for (int i = 0; i < 8192; i++) if (!allocations[i]) {
    allocations[i] = p; live_allocations++; return;
  }
  assert(!"allocation tracker full");
}
static void forget(void *p) {
  if (!p) return;
  for (int i = 0; i < 8192; i++) if (allocations[i] == p) {
    allocations[i] = NULL; live_allocations--; return;
  }
  assert(!"untracked allocation freed");
}
static void *text_malloc(size_t size) {
  if (size == 2048u * 2048u) {
    cpu_attempts++;
    if (cpu_fail) { cpu_fail--; return NULL; }
  }
  void *p = malloc(size); record(p); return p;
}
static void *text_calloc(size_t n, size_t size) {
  void *p = calloc(n, size); record(p); return p;
}
static void *text_realloc(void *p, size_t size) {
  /* The production call sites request positive sizes. */
  assert(size > 0);
  forget(p);
  void *next = realloc(p, size);
  record(next ? next : p);
  return next;
}
static void text_free(void *p) { forget(p); free(p); }
#define calloc text_calloc
#define realloc text_realloc
#define free text_free
#define malloc text_malloc
#include "port/ui/ui_text.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
Uint32 SDL_GetTicks(void) { return ticks; }
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 fmt, int access, int w, int h) {
  assert(r && fmt == SDL_PIXELFORMAT_ARGB8888 && access == SDL_TEXTUREACCESS_STATIC);
  assert(w == 2048 && h == 2048);
  gpu_attempts++;
  if (gpu_fail) { gpu_fail--; return NULL; }
  SDL_Texture *t = malloc(sizeof *t); assert(t); live_textures++; return t;
}
void SDL_DestroyTexture(SDL_Texture *t) { assert(t); live_textures--; free(t); }
int SDL_SetTextureBlendMode(SDL_Texture *t, int mode) { assert(t && mode == SDL_BLENDMODE_BLEND); return 0; }
int SDL_SetTextureScaleMode(SDL_Texture *t, int mode) { assert(t && mode == SDL_ScaleModeNearest); return 0; }
int SDL_RenderFlush(SDL_Renderer *r) { assert(r); return 0; }
int SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *px, int pitch) {
  assert(t && rect && px && pitch == rect->w * 4); uploads++;
  if (upload_fail) { upload_fail--; return -1; }
  return 0;
}
int SDL_RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *v, int nv, const int *idx, int ni) {
  assert(r && t && v && idx && nv > 0 && ni > 0); draws++; return 0;
}
static void draw(UiText *t) {
  uit_draw(t, 10, 30, 400, 16, "Career menu", 0, (UiColor){255,255,255,255});
}
static void reset(void) {
  assert(live_textures == 0 && live_allocations == 0);
  ticks = 100; cpu_fail = cpu_attempts = gpu_fail = gpu_attempts = upload_fail = uploads = draws = 0;
}
static void run_case(const char *name, const char *fonts) {
  reset(); SDL_Renderer renderer = {0}; UiText t, healthy;
  if (!strcmp(name, "normal")) {
    assert(uit_init(&t, &renderer, fonts, 1) == 5);
    assert(t.alpha && t.atlas && cpu_attempts == 1);
    draw(&t); assert(draws && uploads && !t.dirty);
    void *alpha = t.alpha, *font = t.f[0].data; SDL_Texture *atlas = t.atlas;
    draw(&t); assert(t.alpha == alpha && t.atlas == atlas && t.f[0].data == font && cpu_attempts == 1);
  } else if (!strcmp(name, "cpu_retry") || !strcmp(name, "cpu_gpu_retry") || !strcmp(name, "tick_wrap") || !strcmp(name, "tick_zero")) {
    if (!strcmp(name, "tick_wrap")) ticks = UINT32_MAX - 499;
    if (!strcmp(name, "tick_zero")) ticks = UINT32_MAX - 999;
    Uint32 start = ticks;
    cpu_fail = 1;
    assert(uit_init(&t, &renderer, fonts, 1) == 5);
    assert(!t.alpha && !t.atlas && cpu_attempts == 1 && gpu_attempts == 0);
    void *font = t.f[0].data, *info = t.f[0].info;
    assert(uit_init(&healthy, &renderer, fonts, 1) == 5);
    draw(&healthy); assert(draws > 0); uit_free(&healthy);
    int before = cpu_attempts, drawn = draws;
    draw(&t); assert(cpu_attempts == before && draws == drawn);
    ticks = start + 999; draw(&t); assert(cpu_attempts == before && draws == drawn);
    cpu_fail = 1; ticks = start + 1000; draw(&t);
    assert(cpu_attempts == before + 1 && !t.alpha);
    for (int i = 0; i < 20; i++) draw(&t);
    assert(cpu_attempts == before + 1);
    ticks = start + 1999; draw(&t); assert(cpu_attempts == before + 1);
    if (!strcmp(name, "cpu_gpu_retry")) gpu_fail = 1;
    ticks = start + 2000; draw(&t);
    assert(t.alpha && t.f[0].data == font && t.f[0].info == info);
    void *alpha = t.alpha;
    if (gpu_fail == 0 && !t.atlas) {
      assert(t.dirty && t.shelf_x == 1 && t.shelf_y == 1);
      int attempts = gpu_attempts;
      ticks = start + 2999; draw(&t); assert(gpu_attempts == attempts);
      ticks = start + 3000; draw(&t);
    }
    assert(t.atlas && draws > drawn && !t.dirty && t.alpha == alpha);
    assert(cpu_attempts == before + 2);
    draw(&t); assert(t.alpha == alpha && t.f[0].data == font && cpu_attempts == before + 2);
  } else if (!strcmp(name, "upload_retry")) {
    assert(uit_init(&t, &renderer, fonts, 1) == 5);
    upload_fail = 1; draw(&t); assert(t.dirty && uploads == 1);
    draw(&t); assert(!t.dirty && uploads == 2);
  } else if (!strcmp(name, "device_reset") || !strcmp(name, "reset_late_uptime")) {
    assert(uit_init(&t, &renderer, fonts, 1) == 5);draw(&t);
    void *alpha=t.alpha,*font=t.f[0].data;
    for(int reset=0;reset<12;reset++) {
      ticks=(!strcmp(name,"reset_late_uptime")?0x90000000u:100u)+(Uint32)reset*4000;
      Uint32 start=ticks;int attempts=gpu_attempts;
      uit_device_reset(&t);assert(!t.atlas && !t.nv && !t.ni && t.alpha==alpha && t.f[0].data==font);
      gpu_fail=1;draw(&t);assert(!t.atlas && gpu_attempts==attempts+1);
      for(int i=0;i<20;i++)draw(&t);assert(gpu_attempts==attempts+1);
      ticks=start+999;draw(&t);assert(gpu_attempts==attempts+1);
      ticks=start+1000;upload_fail=1;draw(&t);assert(t.atlas && t.dirty);
      draw(&t);assert(!t.dirty && t.alpha==alpha && t.f[0].data==font);
    }
  } else if (!strcmp(name, "reset_without_cpu")) {
    cpu_fail=1;assert(uit_init(&t,&renderer,fonts,1)==5);assert(!t.alpha);
    ticks=UINT32_MAX-1;uit_device_reset(&t);draw(&t);
    assert(t.alpha && t.atlas && !t.dirty);
  } else if (!strcmp(name, "independent_contexts")) {
    /* The golfer world and card layers own separate atlases. A card-only
       failure must recover without rebuilding or interrupting world text. */
    assert(uit_init(&healthy, &renderer, fonts, 1) == 5);
    draw(&healthy);
    SDL_Texture *world_atlas = healthy.atlas;
    void *world_alpha = healthy.alpha, *world_font = healthy.f[0].data;
    cpu_fail = 1;
    assert(uit_init(&t, &renderer, fonts, 1) == 5);
    assert(!t.alpha && !t.atlas);
    int before = draws;
    draw(&healthy); draw(&t);
    assert(draws == before + 1 && healthy.atlas == world_atlas);
    ticks += 1000; gpu_fail = 1; draw(&t);
    assert(t.alpha && !t.atlas);
    draw(&healthy); assert(healthy.atlas == world_atlas);
    ticks += 1000; draw(&t); assert(t.atlas && !t.dirty);
    assert(healthy.alpha == world_alpha && healthy.f[0].data == world_font);
    void *card_alpha = t.alpha;
    for (int i = 0; i < 12; ++i) {
      uit_device_reset(&healthy); uit_device_reset(&t);
      assert(!healthy.atlas && !t.atlas);
      gpu_fail = 1; draw(&t); draw(&healthy);
      assert(!t.atlas && healthy.atlas && healthy.alpha == world_alpha);
      ticks += 1000; upload_fail = 1; draw(&t);
      assert(t.atlas && t.dirty); draw(&t); draw(&healthy);
      assert(!t.dirty && !healthy.dirty && t.alpha == card_alpha);
    }
    uit_free(&healthy);
  } else if (!strcmp(name, "cleanup_failed")) {
    cpu_fail = 1; assert(uit_init(&t, &renderer, fonts, 1) == 5);
    assert(!t.alpha && !t.atlas);
  } else { fprintf(stderr, "unknown case %s\n", name); exit(2); }
  uit_free(&t);
  assert(!t.alpha && !t.atlas && !t.f[0].data && !t.f[0].info && live_textures == 0);
  assert(live_allocations == 0);
  printf("PASS %s\n", name);
}
int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--list")) {
    puts("normal\ncpu_retry\ncpu_gpu_retry\ntick_wrap\ntick_zero\nupload_retry\ncleanup_failed\ndevice_reset\nreset_late_uptime\nreset_without_cpu\nindependent_contexts"); return 0;
  }
  assert(argc == 3); run_case(argv[1], argv[2]); return 0;
}
