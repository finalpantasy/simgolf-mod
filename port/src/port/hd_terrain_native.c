// Course terrain at the screen's own resolution for the native presentation
// (simgolf mod, task-3), the native twin of the browser build's terrain layer
// (hd.c, hd_web.js), drawn with SDL_RenderGeometry instead of WebGL.
//
// Game thread (hdt_capture, from hd_present): compares the finished 800x600
// frame with golf's clean terrain cache (the surface at *(0x4C1574) that the
// per-frame driver FUN_004498a0 keeps, docs/terrain.md) to find the pixels
// that still show bare terrain (or terrain in shadow: golf halves each
// channel). When golf redrew any terrain, terrain_hd.cpp re-records the
// visible terrain as lit, projected triangles in 800x600 window coordinates;
// those and any texture not sent before go to the render thread.
//
// Render thread (hdt_render): replays the triangles into a texture the size
// of the frame on screen. hd_native.c lays the frame over it with the bare
// terrain pixels transparent, so the course comes out sharp at 4K while
// trees, buildings, golfers and the UI stay the game's own pixels.
//
// golf.ini terrain=0 turns this layer off (the scaled frame only).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/hd_art.h"
#include "port/hd_native.h"
#include "port/runtime.h"
#include "mods/mods.h"
#include "port/water_native.h"
#include "platform/terrain_lighting.h"
#include "port/course_relief.h"

extern int terrain_hd_build(void);
extern void terrain_hd_scroll_padding(int on);
extern const float *terrain_hd_vertices(void);
extern int terrain_hd_batch_count(void);
extern int terrain_hd_texture_path_style(int i);
extern const int32_t *terrain_hd_batches(void);
extern const uint8_t *terrain_hd_texture(int i, int32_t *out);
extern void terrain_hd_target(int32_t *out);
extern const char *terrain_hd_texture_src(int i);
extern void terrain_hd_invalidate(void);
extern const float *terrain_hd_world(void);
extern const float *terrain_hd_normals(void);
extern void terrain_hd_light_model(TerrainLightingModel *out);
extern void terrain_hd_view(float out[6]);
extern float terrain_hd_height_pixels(void);
extern int terrain_water_tiles(uint8_t *code, float *height, int cap);
extern int terrain_grid_size(int *w, int *h);

#define GOLF_TERRAIN_CACHE 0x4C1574u     // Surface wrapper or Surface*, golf.exe v1.02
#define GRAPHSY_SURFACE_VTBL 0x1011d0b0u
enum { kBlend = 1, kTextured = 2, kWater = 8 };

// ------------------------------------------------------------------ game thread

typedef struct { const uint16_t *bits; int pitch, w, h; } Surf;

static int readable(uint32_t p, uint32_t n) {
  if (p < 0x10000 || p + n < p) return 0;
  MEMORY_BASIC_INFORMATION mi;
  for (uint32_t a = p; a < p + n;) {
    if (!VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi) || mi.State != MEM_COMMIT ||
        (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
      return 0;
    a = (uint32_t)((uintptr_t)mi.BaseAddress + mi.RegionSize);
  }
  return 1;
}

// graphsy surface layout: port/docs/graphsy-surface.md.
static int surface_at(uint32_t p, Surf *s) {
  if (!readable(p, 0x4d4) || *(uint32_t *)(uintptr_t)p != GRAPHSY_SURFACE_VTBL) return 0;
  const char *o = (const char *)(uintptr_t)p;
  if (*(int32_t *)(o + 0x24) != 16) return 0;
  s->w = *(int32_t *)(o + 0x38);
  s->h = *(int32_t *)(o + 0x3c);
  s->pitch = *(int32_t *)(o + 0x40);
  uint32_t bits = *(uint32_t *)(o + 0x4c0);
  if (s->pitch <= 0 || s->h <= 0 || !readable(bits, (uint32_t)s->pitch * s->h * 2)) return 0;
  s->bits = (const uint16_t *)(uintptr_t)bits;
  return 1;
}

static int terrain_cache(Surf *s) {
  uint32_t p = *(uint32_t *)(uintptr_t)GOLF_TERRAIN_CACHE;
  if (surface_at(p, s)) return 1;
  return readable(p, 8) && surface_at(*(uint32_t *)(uintptr_t)(p + 4), s);
}

// 255: the clean terrain pixel; 128: that pixel in golf's shadow (each
// channel halved, (t >> 1) & 0x3def); 0: anything else (see hd.c).
static uint8_t shade(uint16_t s, uint16_t t) {
  s &= 0x7fff;
  t &= 0x7fff;
  if (s == t) return 255;
  if (s == ((t >> 1) & 0x3def)) return 128;
  return 0;
}

// The hand-over: a recorded scene and the textures it needs that the render
// thread hasn't had yet.
typedef struct { uint32_t serial; int w, h; uint8_t *rgba; int water; } NewTex;
typedef struct {
  float *verts;
  int nverts, cap_verts;
  float *world;              // task-78: world x, y, z per vertex
  int cap_world;
  float *normals;
  int cap_normals;
  TerrainLightingModel light_model;
  float height_pixels;
  uint8_t *materials;int cap_materials;
  float view[6];             // toward the viewer, toward the light (world)
  uint8_t *wcode;            // task-78: the whole map's water tiles (terrain_water_tiles)
  float *wheight;
  int map_w, map_h, cap_map;
  LONG map_serial;
  int32_t *batches;
  int nbatches, cap_batches;
  int tw, th;
  NewTex *tex;
  int ntex, cap_tex;
  LONG serial;
  long frame;   // the presenter frame serial that carries this scene (task-68)
} Scene;

static CRITICAL_SECTION g_tlock;
static Scene g_pending;                 // under g_tlock
static uint32_t *g_sent;                // texture serials already handed over (game thread)
static int g_nsent, g_cap_sent;
static int g_on = 1;

void hdt_init(int on) {
  InitializeCriticalSection(&g_tlock);
  g_on = on;
}

// The art pack's terrain look changed: send every texture again.
void hdt_resend(void) {
  g_nsent = 0;
  terrain_hd_invalidate();
}

static int sent(uint32_t serial) {
  for (int i = 0; i < g_nsent; i++)
    if (g_sent[i] == serial) return 1;
  if (g_nsent == g_cap_sent) g_sent = realloc(g_sent, (g_cap_sent = g_cap_sent ? g_cap_sent * 2 : 256) * 4);
  g_sent[g_nsent++] = serial;
  return 0;
}

#define GROW(p, cap, n, sz) \
  do { if ((n) > (cap)) { (cap) = (n) + (n) / 2; (p) = realloc((p), (size_t)(cap) * (sz)); } } while (0)

// Decode the first new course's independent ground images before taking the
// hand-over lock. SDL textures are still created only on the render thread.
static NewTex *prepare_course_textures(int nb, const int32_t *b, int *count) {
  NewTex *ready = calloc((size_t)nb, sizeof *ready);
  HdTerrainJob *jobs = calloc((size_t)nb, sizeof *jobs);
  if (!ready || !jobs) { free(ready); free(jobs); return NULL; }
  int n = 0;
  for (int i = 0; i < nb; i++) {
    if (!(b[i * 5 + 4] & kTextured) || sent((uint32_t)b[i * 5 + 3])) continue;
    int32_t size[2];
    HdTerrainJob *j = jobs + n;
    j->rgba = terrain_hd_texture(b[i * 5 + 2], size);
    j->src = terrain_hd_texture_src(b[i * 5 + 2]);
    j->path_style = terrain_hd_texture_path_style(b[i * 5 + 2]);
    j->w = size[0]; j->h = size[1];
    ready[n].serial = (uint32_t)b[i * 5 + 3];
    ready[n].water = (b[i * 5 + 4] & kWater) != 0;
    n++;
  }
  hd_art_terrain_jobs(jobs, n);
  for (int i = 0; i < n; i++) {
    HdTerrainJob *j = jobs + i;
    ready[i].w = j->up ? j->uw : j->w;
    ready[i].h = j->up ? j->uh : j->h;
    ready[i].rgba = j->up;
    if (!j->up) {
      size_t bytes = (size_t)j->w * j->h * 4;
      ready[i].rgba = malloc(bytes);
      if (!ready[i].rgba) port_abort("terrain texture allocation failed");
      memcpy(ready[i].rgba, j->rgba, bytes);
    }
  }
  free(jobs);
  *count = n;
  return ready;
}

int hdt_capture(const uint16_t *screen, uint8_t *mask, long frame) {
  Surf c;
  if (!terrain_cache(&c) || c.w != HD_FRAME_W || c.h != HD_FRAME_H) return 0;
  int same = 0;
  for (int y = 0; y < HD_FRAME_H; y++) {
    const uint16_t *a = screen + y * HD_FRAME_W, *b = c.bits + y * c.pitch;
    uint8_t *m = mask + y * HD_FRAME_W;
    for (int x = 0; x < HD_FRAME_W; x++) same += (m[x] = shade(a[x], b[x])) == 255;
  }
  // Menus and other full-screen pages leave a stale cache behind: only use it
  // when a good part of the frame is terrain.
  if (same <= HD_FRAME_W * HD_FRAME_H / 8) return 0;
  terrain_hd_scroll_padding(mods_get_int("fixes.drag_scroll", 1));
  if (!g_on || !terrain_hd_build()) return 1;   // layer off, or nothing redrawn: the last scene stands

  int nb = terrain_hd_batch_count();
  const int32_t *b = terrain_hd_batches();
  int nverts = nb ? b[(nb - 1) * 5] + b[(nb - 1) * 5 + 1] : 0;
  int32_t t[2];
  terrain_hd_target(t);
  int nready = 0;
  NewTex *ready = port_course_textures_fast() && nb > 0 ? prepare_course_textures(nb, b, &nready) : NULL;
  EnterCriticalSection(&g_tlock);
  Scene *s = &g_pending;
  GROW(s->verts, s->cap_verts, nverts * 8, sizeof(float));
  memcpy(s->verts, terrain_hd_vertices(), (size_t)nverts * 8 * sizeof(float));
  s->nverts = nverts;
  GROW(s->world, s->cap_world, nverts * 3, sizeof(float));
  memcpy(s->world, terrain_hd_world(), (size_t)nverts * 3 * sizeof(float));
  GROW(s->normals, s->cap_normals, nverts * 3, sizeof(float));
  memcpy(s->normals, terrain_hd_normals(), (size_t)nverts * 3 * sizeof(float));
  terrain_hd_light_model(&s->light_model);
  terrain_hd_view(s->view);
  s->height_pixels=terrain_hd_height_pixels();
  GROW(s->materials,s->cap_materials,nb,1);
  for(int i=0;i<nb;i++)s->materials[i]=(b[i*5+4]&kTextured)?(uint8_t)course_relief_material(terrain_hd_texture_src(b[i*5+2])):0;
  int mw = 0, mh = 0;
  if (terrain_grid_size(&mw, &mh) && mw * mh > 0) {   // the map's water tiles, sent on when they changed
    static uint8_t *code;
    static float *height;
    static int cap;
    if (cap < mw * mh) {
      cap = mw * mh;
      code = realloc(code, (size_t)cap);
      height = realloc(height, (size_t)cap * sizeof(float));
    }
    if (terrain_water_tiles(code, height, cap) == mw * mh &&
        (mw != s->map_w || mh != s->map_h || memcmp(code, s->wcode, (size_t)mw * mh) ||
         memcmp(height, s->wheight, (size_t)mw * mh * sizeof(float)))) {
      GROW(s->wcode, s->cap_map, mw * mh, 1);
      s->wheight = realloc(s->wheight, (size_t)s->cap_map * sizeof(float));
      memcpy(s->wcode, code, (size_t)mw * mh);
      memcpy(s->wheight, height, (size_t)mw * mh * sizeof(float));
      s->map_w = mw;
      s->map_h = mh;
      s->map_serial++;
    }
  }
  GROW(s->batches, s->cap_batches, nb * 5, sizeof(int32_t));
  memcpy(s->batches, b, (size_t)nb * 5 * sizeof(int32_t));
  s->nbatches = nb;
  s->tw = t[0];
  s->th = t[1];
  if (ready) {
    if (nready) {
      GROW(s->tex, s->cap_tex, s->ntex + nready, sizeof(NewTex));
      memcpy(s->tex + s->ntex, ready, (size_t)nready * sizeof(NewTex));
      s->ntex += nready;
    }
    free(ready);
  } else for (int i = 0; i < nb; i++) {
    if (!(b[i * 5 + 4] & kTextured) || sent((uint32_t)b[i * 5 + 3])) continue;
    int32_t size[2];
    const uint8_t *rgba = terrain_hd_texture(b[i * 5 + 2], size);
    GROW(s->tex, s->cap_tex, s->ntex + 1, sizeof(NewTex));
    NewTex *n = &s->tex[s->ntex++];
    n->serial = (uint32_t)b[i * 5 + 3];
    n->water = (b[i * 5 + 4] & kWater) != 0;
    // the art pack's terrain look (hd_art.c, task-17): the 4x upscale of the same file
    int uw = 0, uh = 0;
    uint8_t *up = hd_art_terrain_style(terrain_hd_texture_src(b[i * 5 + 2]), rgba, size[0], size[1], &uw, &uh,terrain_hd_texture_path_style(b[i*5+2]));
    if (up) {
      n->w = uw;
      n->h = uh;
      n->rgba = up;
      continue;
    }
    n->w = size[0];
    n->h = size[1];
    n->rgba = malloc((size_t)size[0] * size[1] * 4);
    memcpy(n->rgba, rgba, (size_t)size[0] * size[1] * 4);
  }
  s->frame = frame;
  s->serial++;
  LeaveCriticalSection(&g_tlock);
  port_course_textures_done();
  return 1;
}

// ------------------------------------------------------------------ render thread

typedef struct { uint32_t serial; SDL_Texture *tex; } TexEntry;

typedef struct {
  SDL_Texture *texture;
  int w, h;
  LONG serial;
  unsigned lighting_revision;
  float x, y;
} TerrainTarget;

static struct {
  TexEntry *tex;
  int ntex, cap_tex;
  float *verts;
  int nverts, cap_verts;
  float *world;
  int cap_world;
  float *normals;
  int cap_normals;
  TerrainLightingModel light_model;
  unsigned lighting_revision;
  float height_pixels;
  uint8_t *materials;int cap_materials;
  float view[6];
  int32_t *batches;
  int nbatches, cap_batches;
  int tw, th;
  LONG serial;
  uint8_t *wcode;
  float *wheight;
  int map_w, map_h, cap_map;
  LONG map_serial;
  TerrainTarget targets[2];
  SDL_Vertex *sv;
  int cap_sv;
  SDL_BlendMode keep_alpha;
  int logged;
} R = {.serial = 0};

static SDL_Texture *find_tex(uint32_t serial) {
  for (int i = R.ntex - 1; i >= 0; i--)
    if (R.tex[i].serial == serial) return R.tex[i].tex;
  return NULL;
}

// Takes the newest scene, if there is one and the course frame it belongs
// to is on screen: hdt_capture runs before hd_present hands that frame over,
// and taking it earlier showed the new ground under the old course for one
// presented frame each game frame (task-68).
static void take(SDL_Renderer *r, long shown) {
  if (g_pending.serial == R.serial) return;
  EnterCriticalSection(&g_tlock);
  Scene *s = &g_pending;
  if (s->frame > shown) {
    LeaveCriticalSection(&g_tlock);
    return;
  }
  GROW(R.verts, R.cap_verts, s->nverts * 8, sizeof(float));
  memcpy(R.verts, s->verts, (size_t)s->nverts * 8 * sizeof(float));
  R.nverts = s->nverts;
  GROW(R.world, R.cap_world, s->nverts * 3, sizeof(float));
  memcpy(R.world, s->world, (size_t)s->nverts * 3 * sizeof(float));
  GROW(R.normals, R.cap_normals, s->nverts * 3, sizeof(float));
  memcpy(R.normals, s->normals, (size_t)s->nverts * 3 * sizeof(float));
  R.light_model = s->light_model;
  R.height_pixels=s->height_pixels;
  GROW(R.materials,R.cap_materials,s->nbatches,1);
  memcpy(R.materials,s->materials,(size_t)s->nbatches);
  memcpy(R.view, s->view, sizeof R.view);
  if (s->map_serial != R.map_serial && s->map_w > 0) {
    GROW(R.wcode, R.cap_map, s->map_w * s->map_h, 1);
    R.wheight = realloc(R.wheight, (size_t)R.cap_map * sizeof(float));
    memcpy(R.wcode, s->wcode, (size_t)s->map_w * s->map_h);
    memcpy(R.wheight, s->wheight, (size_t)s->map_w * s->map_h * sizeof(float));
    R.map_w = s->map_w;
    R.map_h = s->map_h;
    R.map_serial = s->map_serial;
  }
  GROW(R.batches, R.cap_batches, s->nbatches * 5, sizeof(int32_t));
  memcpy(R.batches, s->batches, (size_t)s->nbatches * 5 * sizeof(int32_t));
  R.nbatches = s->nbatches;
  R.tw = s->tw;
  R.th = s->th;
  for (int i = 0; i < s->ntex; i++) {
    NewTex *n = &s->tex[i];
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, n->w, n->h);
    if (t) {
      if (SDL_UpdateTexture(t, NULL, n->rgba, n->w * 4) != 0) {
        SDL_DestroyTexture(t);
        t = NULL;
      }
    }
    if (t) {
      SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
      int found = -1;
      if (mods_get_int("fixes.terrain_cache", 1))
        for (int j = R.ntex - 1; j >= 0; j--) if (R.tex[j].serial == n->serial) { found = j; break; }
      if (found >= 0) {
        SDL_DestroyTexture(R.tex[found].tex);
        R.tex[found].tex = t;
      } else {
        GROW(R.tex, R.cap_tex, R.ntex + 1, sizeof(TexEntry));
        R.tex[R.ntex++] = (TexEntry){n->serial, t};
      }
    }
    if (n->water) water_take_texture(n->serial, n->w, n->h, n->rgba);   // task-78: the water pass keeps its own copy
    free(n->rgba);
  }
  s->ntex = 0;
  R.serial = s->serial;
  LeaveCriticalSection(&g_tlock);
}

static SDL_Texture *render_at(SDL_Renderer *r, int w, int h, long shown, float ox, float oy, int guard) {
  take(r, shown);
  TerrainTarget *target = &R.targets[guard];
  if (!R.nbatches || R.tw <= 0 || R.th <= 0) return NULL;
  if (!target->texture || target->w != w || target->h != h) {
    if (target->texture) SDL_DestroyTexture(target->texture);
    target->texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    target->w = w;
    target->h = h;
    target->serial = -1;
    if (!target->texture) return NULL;
    SDL_SetTextureBlendMode(target->texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(target->texture, SDL_ScaleModeLinear);
    // The browser build's blendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ZERO, ONE).
    R.keep_alpha = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                              SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE,
                                              SDL_BLENDOPERATION_ADD);
  }
  unsigned lighting_revision = terrain_lighting_revision();
  if (target->serial == R.serial && target->x == ox && target->y == oy && target->lighting_revision == lighting_revision) return target->texture;
  const float *draw_vertices = R.verts;
  const int32_t *draw_batches = R.batches;
  if (terrain_lighting_rebuild(R.verts, R.world, R.normals, R.nverts, R.batches, R.nbatches,
                               &R.light_model, R.serial)) {
    draw_vertices = terrain_lighting_vertices();
    draw_batches = terrain_lighting_batches();
  }
  SDL_SetRenderTarget(r, target->texture);
  SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_RenderClear(r);
  // The guard texture uses canvas resolution. It is recorded once per game
  // frame, then cropped by the presenter at display speed without replaying
  // thousands of geometry batches for every mouse interpolation step.
  float sx = guard ? 1.0f : (float)w / R.tw, sy = guard ? 1.0f : (float)h / R.th;
  float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
  for (int k = 0; k < R.nbatches; k++) {
    const int32_t *b = draw_batches + k * 5;
    int first = b[0], count = b[1], flags = b[4];
    SDL_Texture *tex = flags & kTextured ? find_tex((uint32_t)b[3]) : NULL;
    if ((flags & kTextured) && !tex) continue;
    GROW(R.sv, R.cap_sv, count, sizeof(SDL_Vertex));
    for (int i = 0; i < count; i++) {
      const float *v = draw_vertices + (size_t)(first + i) * 8;
      SDL_Vertex *o = &R.sv[i];
      o->position.x = v[0] * sx + ox;
      o->position.y = (R.th - v[1]) * sy + oy;
      float c[4] = {v[2], v[3], v[4], flags & kBlend ? v[5] : 1.0f};
      Uint8 *d = &o->color.r;
      for (int j = 0; j < 4; j++) d[j] = (Uint8)(c[j] <= 0 ? 0 : c[j] >= 1 ? 255 : c[j] * 255.0f + 0.5f);
      o->tex_coord.x = v[6];
      o->tex_coord.y = v[7];
      if (tex) {
        if (v[6] < umin) umin = v[6];
        if (v[6] > umax) umax = v[6];
        if (v[7] < vmin) vmin = v[7];
        if (v[7] > vmax) vmax = v[7];
      }
    }
    SDL_BlendMode mode = flags & kBlend ? R.keep_alpha : SDL_BLENDMODE_NONE;
    if (tex) SDL_SetTextureBlendMode(tex, mode);
    else SDL_SetRenderDrawBlendMode(r, mode);
    SDL_RenderGeometry(r, tex, R.sv, count, NULL, 0);
  }
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  if (!R.logged) {
    R.logged = 1;
    fprintf(stderr, "hd: terrain layer %d triangles, %d batches, %d textures, uv %.2f..%.2f x %.2f..%.2f, target %dx%d\n",
            R.nverts / 3, R.nbatches, R.ntex, umin, umax, vmin, vmax, R.tw, R.th);
  }
  target->serial = R.serial;
  target->lighting_revision = lighting_revision;
  target->x = ox; target->y = oy;
  return target->texture;
}

SDL_Texture *hdt_render(SDL_Renderer *r, int w, int h, long shown) {
  return render_at(r, w, h, shown, 0, 0, 0);
}

// One viewport of padding on every side, at the source canvas resolution.
SDL_Texture *hdt_render_guard(SDL_Renderer *r, long shown) {
  take(r, shown);
  if (R.tw <= 0 || R.th <= 0) return NULL;
  return render_at(r, R.tw * 3, R.th * 3, shown, (float)R.tw, (float)R.th, 1);
}

// task-78: the scene the terrain layer shows, for the water pass (render thread).
int hdt_scene(HdtScene *out) {
  if (!R.nbatches || R.tw <= 0 || R.th <= 0) return 0;
  out->verts = R.verts;
  out->world = R.world;
  out->batches = R.batches;
  out->nbatches = R.nbatches;
  out->tw = R.tw;
  out->th = R.th;
  out->serial = R.serial;
  memcpy(out->view, R.view, sizeof out->view);
  out->wcode = R.wcode;
  out->wheight = R.wheight;
  out->map_w = R.map_w;
  out->map_h = R.map_h;
  out->map_serial = R.map_serial;
  out->normals=R.normals;out->materials=R.materials;out->height_pixels=R.height_pixels;
  return 1;
}
