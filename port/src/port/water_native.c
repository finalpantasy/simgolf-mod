// Realistic water for the native HD presentation (simgolf mod, task-78).
// See port/water_native.h for what it draws; the shaders are data/ui/water.hlsl
// (copied next to golf.exe as ui/water.hlsl, compiled at run time with the
// system's d3dcompiler_47.dll) and the numbers data/ui/water.txt.
//
// Render thread: SDL2's Direct3D 11 renderer draws everything else. The water
// pass flushes SDL's queue, takes the render target SDL has bound, swaps in a
// device context state of its own (ID3D11DeviceContext1::SwapDeviceContextState,
// so none of SDL's cached state changes underneath it), steps the ripple
// simulation, draws golf's water triangles with the water shader, and swaps
// SDL's state back.
#pragma clang diagnostic error "-Wimplicit-function-declaration"

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <d3d11_1.h>

#include <SDL.h>
#include <SDL_system.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mods/mods.h"
#include "port/host.h"
#include "port/runtime.h"
#include "port/water_native.h"

double host_now_ms(void);

#ifndef D3DCOMPILE_OPTIMIZATION_LEVEL3
#define D3DCOMPILE_OPTIMIZATION_LEVEL3 (1 << 15)
#endif

enum { kWaterFlag = 8, kCells = 8, kInfo = 4, kMaxImp = 512, kFloats = 16 };

// ------------------------------------------------------------------ settings

typedef struct {
  float shallow[3], deep[3], refl, glints, sun[3], swell, horizon[3], zenith[3], clouds, caustics;
  float shore_foam, lap, stream_foam, ripple_foam, key[2], key_soft, detail, chop, ripple, drops, fall_rate;
  float wind_dir, wind_speed, wind_ms, max_depth, ripple_speed, calm, lmax;
  float absorb[3], depth_scale, bed[3], glint_bias;
  float uw_tint[3], uw_alpha;   // golf's shoal, rock and coral sprites seen through this water
  float ball_size, ball_life, ball_drops, ball_foam, ball_ring, ball_drop_r;   // a ball landing in the water
} Look;

static const Look k_default[1] = {
  {{0.22f, 0.74f, 0.72f}, {0.02f, 0.20f, 0.34f}, 2.2f, 1.0f, {1, 0.95f, 0.85f}, 1.0f, {0.80f, 0.88f, 0.93f},
   {0.38f, 0.60f, 0.88f}, 0.55f, 0.9f, 0.9f, 0.35f, 0.7f, 0.6f, {0.04f, 0.16f}, 2.2f, 1, 1, 4, 0, 0, 35, 1,
   {0.9f, 0.22f, 0.10f}, 5, {0.85f, 0.80f, 0.62f}, 0.35f, 5, 3, 0.8f, 0.3f, 12,
   {0.62f, 0.85f, 0.88f}, 0.85f, 0.05f, 0.8f, 6, 0.9f, 0.010f, 0.010f},
};
// One curated water (decision 2026-10-12): [water] holds the numbers, the four course themes list only what
// differs (colour and feel taken from that theme's original water art). Index 0 is [water], 1 + course type
// is the theme: 0 Parkland, 1 Desert, 2 Tropical, 3 Links (the game's own numbering).
enum { NLOOKS = 5 };
static const char *const k_looks[NLOOKS] = {"water", "parkland", "desert", "tropical", "links"};
static Look g_look[NLOOKS];
extern int terrain_course_type(void);
// The look for the course on screen (SIMGOLF_WATER_THEME=0..3 forces a theme: tests and evidence).
static int cur_look(void) {
  const char *e = getenv("SIMGOLF_WATER_THEME");
  int t = e && *e ? atoi(e) : terrain_course_type();
  return t >= 0 && t < 4 ? 1 + t : 1;
}

static volatile LONG g_mode = -1;     // 1: display.water=hd (game thread writes, render thread reads)
static volatile LONG g_works;         // 1 once the pass has drawn, -1 when it can't run
static volatile float g_wind_dir = -1000, g_wind_speed = -1;   // task-79 override (-1000: the look's own)

// water.clip: record the next frames to a video (look tests; game paused by the caller)
static char g_clip_path[MAX_PATH];
static volatile LONG g_clip_req, g_clip_frames = 300, g_clip_fps = 30;

static unsigned g_seen_serial = ~0u;
static void splashes(void);

void water_poll(void) {
  if (g_mode > 0 && g_works > 0) splashes();
  unsigned s = mods_serial();
  if (s == g_seen_serial && g_mode >= 0) return;
  g_seen_serial = s;
  const char *env = getenv("SIMGOLF_WATER");
  const char *v = env && *env ? env : mods_get("display.water");
  // Realistic is the default with the HD presentation; scripted runs that keep the plain build's
  // pictures (SIMGOLF_ART_DEFAULT=original) keep golf's water unless they ask for it.
  const char *art = getenv("SIMGOLF_ART_DEFAULT");
  int mode = v && *v ? !strcmp(v, "hd") || !strcmp(v, "realistic") : !(art && !_stricmp(art, "original"));
  if (mode != g_mode) fprintf(stderr, "water: %s\n", mode ? "realistic (display.water=hd)" : "original");
  InterlockedExchange(&g_mode, mode);
  // water.splash=x,y[,strength]: a splash at map tile x, y (tests and evidence)
  static char last_splash[64];
  const char *sp = mods_get("water.splash");
  if (sp && *sp && strcmp(sp, last_splash)) {
    float x = 0, y = 0, st = 0.9f;
    if (sscanf(sp, "%f,%f,%f", &x, &y, &st) >= 2) water_impulse(x, y, -st, 0.18f);
    snprintf(last_splash, sizeof last_splash, "%s", sp);
  }
  // water.ball_splash=x,y: a ball splash at map tile x, y (tests and evidence)
  static char last_ball[64];
  const char *bsp = mods_get("water.ball_splash");
  if (bsp && *bsp && strcmp(bsp, last_ball)) {
    float x = 0, y = 0;
    if (sscanf(bsp, "%f,%f", &x, &y) >= 2) water_ball_splash(x, y);
    snprintf(last_ball, sizeof last_ball, "%s", bsp);
  }
  const char *clip = mods_get("water.clip");
  if (clip && *clip && strcmp(clip, g_clip_path)) {
    snprintf(g_clip_path, sizeof g_clip_path, "%s", clip);
    g_clip_frames = mods_get_int("water.clip_frames", 300);
    g_clip_fps = mods_get_int("water.clip_fps", 30);
    InterlockedExchange(&g_clip_req, 1);
  }
}

int water_on(void) { return g_mode > 0 && g_works > 0; }
int water_hide_golf_ripples(void) { return g_mode > 0 && g_works > 0; }

// Sprites drawn under the surface (shoals, rocks and coral, Flics/Water/UW*): the colour and opacity they
// are drawn with while the water pass runs, so they read as seen through the water. 0: draw as golf does.
int water_underwater_tint(float rgb[3], float *alpha) {
  if (!water_hide_golf_ripples()) return 0;
  const Look *L = &g_look[cur_look()];
  rgb[0] = L->uw_tint[0], rgb[1] = L->uw_tint[1], rgb[2] = L->uw_tint[2];
  *alpha = L->uw_alpha;
  return 1;
}

void water_set_wind(float dir_rad, float speed) {
  g_wind_dir = dir_rad;
  g_wind_speed = speed;
}

// ------------------------------------------------------------------ impulses (any thread)

typedef struct { float x, y, s, r; } Imp;
static CRITICAL_SECTION g_ilock;
static LONG g_ilock_init;
static Imp g_imp[kMaxImp];
static int g_nimp;

static void ilock(void) {
  if (InterlockedCompareExchange(&g_ilock_init, 1, 0) == 0) {
    InitializeCriticalSection(&g_ilock);
    InterlockedExchange(&g_ilock_init, 2);
  }
  while (g_ilock_init != 2) YieldProcessor();
  EnterCriticalSection(&g_ilock);
}

// A ball landing in the water: a few droplets and two small curl rings close to the ball, gone within a
// second (size and life in water.txt). Drawn by the surface shader, nothing goes into the ripple
// simulation, so the surface never wobbles. Picked up by the render thread.
static Imp g_bpend[64];
static int g_nbp;
void water_ball_splash(float tx, float ty) {
  if (getenv("SIMGOLF_WATER_LOG")) fprintf(stderr, "water: ball splash queued at %.2f %.2f\n", tx, ty);
  ilock();
  if (g_nbp < 64) g_bpend[g_nbp++] = (Imp){tx, ty, 0, 0};
  LeaveCriticalSection(&g_ilock);
}

void water_impulse(float tx, float ty, float strength, float radius_tiles) {
  ilock();
  if (g_nimp < kMaxImp) g_imp[g_nimp++] = (Imp){tx, ty, strength, radius_tiles};
  LeaveCriticalSection(&g_ilock);
}

// ------------------------------------------------------------------ ball splashes (game thread)

int terrain_tile_water(int x, int y);   // terrain_dll.cpp

#define BALL_X 0x57958cu   // golfer record + 0xd4, 1/1024 tile (as hdball.c reads them)
#define BALL_Y 0x579590u
#define BALL_Z 0x579594u   // height above the ground
#define BALL_SLOTS 152

// A ball that was in the air over water and is now down (or gone: golf takes
// a ball in a hazard away) makes a splash: rings through the ripple
// simulation. Golf's memory is only read.
static void splashes(void) {
  static int32_t px[BALL_SLOTS], py[BALL_SLOTS], pz[BALL_SLOTS];
  static int primed;
  for (int s = 0; s < BALL_SLOTS; s++) {
    int32_t x = *(volatile int32_t *)(uintptr_t)(BALL_X + s * 0x100u);
    int32_t y = *(volatile int32_t *)(uintptr_t)(BALL_Y + s * 0x100u);
    int32_t z = *(volatile int32_t *)(uintptr_t)(BALL_Z + s * 0x100u);
    if (primed && pz[s] > 0) {
      int64_t dx = (int64_t)x - px[s], dy = (int64_t)y - py[s];
      int landed = z <= 0, moved = dx * dx + dy * dy > (int64_t)(2 * 1024) * (2 * 1024);
      if ((landed || moved) && terrain_tile_water(px[s] >> 10, py[s] >> 10)) {
        water_ball_splash(px[s] / 1024.0f, py[s] / 1024.0f);
        if (getenv("SIMGOLF_WATER_LOG")) fprintf(stderr, "water: splash, ball %d at %.2f %.2f\n", s, px[s] / 1024.0, py[s] / 1024.0);
      }
    }
    px[s] = x, py[s] = y, pz[s] = z;
  }
  primed = 1;
}

// ------------------------------------------------------------------ render thread state

typedef struct { uint32_t serial; ID3D11Texture2D *tex; ID3D11ShaderResourceView *srv; } WTex;
typedef struct { uint32_t serial; int w, h; uint8_t *rgba; } Pend;
typedef struct { int first, count; uint32_t serial; } Draw;

typedef struct {
  float rt[4], dst[4], src[4], map[4], view[4], sun[4], wind[4], sim[4];
  float shallow[4], deep[4], foam[4], sky0[4], sky1[4], key[4], sunc[4], imp[4];
  float abs[4], bed[4];
  float wp[4];
  float waves[32][4];
  float ball[8][4];   // active ball splashes: x, y (map tiles), start time (s), 0
  float bp[4], bq[4];   // size, life, droplets, count; foam, ring width, droplet radius, 0
} CB;

typedef HRESULT(WINAPI *PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR,
                                     UINT, UINT, ID3DBlob **, ID3DBlob **);

static struct {
  int state;   // 0 not tried, 1 ready, -1 failed
  ID3D11Device *dev;
  ID3D11DeviceContext *ctx;
  ID3D11DeviceContext1 *ctx1;
  ID3DDeviceContextState *st;
  PFN_Compile compile;
  ID3D11VertexShader *vs_water, *vs_quad, *vs_imp;
  ID3D11PixelShader *ps_water, *ps_sim, *ps_imp, *ps_fall;
  ID3D11InputLayout *il;
  ID3D11Buffer *cb, *vb;
  int vb_cap;
  ID3D11SamplerState *s_wrap, *s_clamp, *s_point;
  ID3D11BlendState *b_water, *b_add;
  ID3D11RasterizerState *rs;
  ID3D11ShaderResourceView *noise_srv;
  ID3D11Texture2D *info;
  ID3D11ShaderResourceView *info_srv;
  ID3D11Texture2D *sim[2];
  ID3D11ShaderResourceView *sim_srv[2];
  ID3D11RenderTargetView *sim_rtv[2];
  int cur, sim_w, sim_h;
  WTex *tex;
  int ntex, cap_tex;
  Pend *pend;
  int npend, cap_pend;
  float *vtx;
  int nvtx, cap_vtx;
  Draw *draws;
  int ndraws, cap_draws;
  int fall_first, fall_count;   // waterfall triangles, after the water's in the vertex buffer
  long scene_serial, map_serial;
  int map_w, map_h;
  float view[6];
  int *wet;          // water tile indices (ambient drops)
  int nwet;
  int *pools;        // tiles below a drop
  int npools;
  double last_t, sim_acc, drop_acc, fall_acc, t_base;
  float ball[8][3];   // active ball splashes: x, y, start time
  int nball;
  FILETIME hlsl_time, txt_time;
  double next_check;
  uint32_t rng;
  // timing
  ID3D11Query *qd[4], *q0[4], *q1[4];
  int qn;
  double gpu_sum, gpu_max, cpu_sum, cpu_max, stat_t0;
  int stat_n, tw, th, ntri;
  float tile_px;     // window px across one tile (from the geometry)
  // clip
  FILE *clip;
  int clip_n, clip_total, clip_fps, clip_w, clip_h;
  uint8_t *clip_px;
  double clip_t0;
  ULONGLONG clip_wall0;
} W = {.scene_serial = -1, .map_serial = -1, .rng = 12345};

#define REL(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)

static float frand(void) {
  W.rng = W.rng * 1664525u + 1013904223u;
  return (W.rng >> 8) * (1.0f / 16777216.0f);
}

// ------------------------------------------------------------------ tuning file

static void copy3(float *d, const float *s) { d[0] = s[0], d[1] = s[1], d[2] = s[2]; }

static int file_time(const char *path, FILETIME *out) {
  WIN32_FILE_ATTRIBUTE_DATA a;
  if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) return 0;
  *out = a.ftLastWriteTime;
  return 1;
}

static void load_txt(void) {
  for (int k = 0; k < NLOOKS; k++) g_look[k] = k_default[0];
  FILE *f = fopen(port_host_path("/ui/water.txt"), "r");
  if (!f) {
    fprintf(stderr, "water: ui/water.txt not found, built-in numbers\n");
    return;
  }
  char line[256];
  int sec = 0, n = 0;
  while (fgets(line, sizeof line, f)) {
    char *h = strchr(line, '#');
    if (h) *h = 0;
    char key[48];
    float a[4] = {0};
    if (sscanf(line, " [%47[a-z]]", key) == 1) {
      sec = 0;
      for (int k = 0; k < NLOOKS; k++)
        if (!strcmp(key, k_looks[k])) sec = k;
      if (sec) g_look[sec] = g_look[0];   // a theme starts from [water] and changes what it lists
      continue;
    }
    int got = sscanf(line, "%47s %f %f %f %f", key, &a[0], &a[1], &a[2], &a[3]);
    if (got < 2) continue;
    Look *L = &g_look[sec];
    n++;
#define K1(name, field) else if (!strcmp(key, name)) L->field = a[0]
#define K3(name, field) else if (!strcmp(key, name) && got >= 4) copy3(L->field, a)
    if (0) {}
    K3("shallow", shallow); K3("deep", deep); K1("reflection", refl); K1("glints", glints); K3("sun", sun);
    K1("swell", swell); K3("horizon", horizon); K3("zenith", zenith); K1("clouds", clouds); K1("caustics", caustics);
    K1("shore_foam", shore_foam); K1("lap", lap); K1("stream_foam", stream_foam); K1("ripple_foam", ripple_foam);
    K1("key_soft", key_soft); K1("detail", detail); K1("chop", chop); K1("ripple", ripple); K1("drops", drops);
    K1("fall_rate", fall_rate); K1("wind_dir", wind_dir); K1("wind_speed", wind_speed);
    K3("absorb", absorb); K1("depth_scale", depth_scale); K3("bed", bed); K1("glint_bias", glint_bias);
    K1("wind_ms", wind_ms); K1("max_depth", max_depth); K1("ripple_speed", ripple_speed);
    K1("calm", calm); K1("longest_wave", lmax); K3("uw_tint", uw_tint); K1("uw_alpha", uw_alpha);
    K1("ball_size", ball_size); K1("ball_life", ball_life); K1("ball_drops", ball_drops); K1("ball_foam", ball_foam);
    K1("ball_ring", ball_ring); K1("ball_drop_r", ball_drop_r);
    else if (!strcmp(key, "key") && got >= 3) L->key[0] = a[0], L->key[1] = a[1];
    else n--, fprintf(stderr, "water: ui/water.txt: unknown key '%s'\n", key);
#undef K1
#undef K3
  }
  fclose(f);
  fprintf(stderr, "water: ui/water.txt, %d values\n", n);
}

// ------------------------------------------------------------------ shaders

static ID3DBlob *compile(const char *src, size_t n, const char *entry, const char *target) {
  ID3DBlob *code = NULL, *err = NULL;
  HRESULT hr = W.compile(src, n, "water.hlsl", NULL, NULL, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
  if (FAILED(hr)) {
    fprintf(stderr, "water: %s: %s\n", entry, err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "compile failed");
    REL(err);
    REL(code);
    return NULL;
  }
  REL(err);
  return code;
}

// (Re)builds the shaders from ui/water.hlsl; keeps the old ones when it fails.
static int load_shaders(void) {
  FILE *f = fopen(port_host_path("/ui/water.hlsl"), "rb");
  if (!f) {
    fprintf(stderr, "water: ui/water.hlsl not found\n");
    return 0;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *src = malloc((size_t)n + 1);
  n = (long)fread(src, 1, (size_t)n, f);
  fclose(f);
  src[n] = 0;
  ID3DBlob *b[7] = {compile(src, n, "vs_water", "vs_4_0"), compile(src, n, "ps_water", "ps_4_0"),
                    compile(src, n, "vs_quad", "vs_4_0"),  compile(src, n, "ps_sim", "ps_4_0"),
                    compile(src, n, "vs_imp", "vs_4_0"),   compile(src, n, "ps_imp", "ps_4_0"),
                    compile(src, n, "ps_fall", "ps_4_0")};
  free(src);
  int ok = 1;
  for (int i = 0; i < 7; i++) ok &= b[i] != NULL;
  if (ok) {
    ID3D11VertexShader *vw = NULL, *vq = NULL, *vi = NULL;
    ID3D11PixelShader *pw = NULL, *ps = NULL, *pi = NULL, *pf = NULL;
    ID3D11InputLayout *il = NULL;
    ok = SUCCEEDED(ID3D11Device_CreateVertexShader(W.dev, ID3D10Blob_GetBufferPointer(b[0]), ID3D10Blob_GetBufferSize(b[0]), NULL, &vw)) &&
         SUCCEEDED(ID3D11Device_CreatePixelShader(W.dev, ID3D10Blob_GetBufferPointer(b[1]), ID3D10Blob_GetBufferSize(b[1]), NULL, &pw)) &&
         SUCCEEDED(ID3D11Device_CreateVertexShader(W.dev, ID3D10Blob_GetBufferPointer(b[2]), ID3D10Blob_GetBufferSize(b[2]), NULL, &vq)) &&
         SUCCEEDED(ID3D11Device_CreatePixelShader(W.dev, ID3D10Blob_GetBufferPointer(b[3]), ID3D10Blob_GetBufferSize(b[3]), NULL, &ps)) &&
         SUCCEEDED(ID3D11Device_CreateVertexShader(W.dev, ID3D10Blob_GetBufferPointer(b[4]), ID3D10Blob_GetBufferSize(b[4]), NULL, &vi)) &&
         SUCCEEDED(ID3D11Device_CreatePixelShader(W.dev, ID3D10Blob_GetBufferPointer(b[5]), ID3D10Blob_GetBufferSize(b[5]), NULL, &pi)) &&
         SUCCEEDED(ID3D11Device_CreatePixelShader(W.dev, ID3D10Blob_GetBufferPointer(b[6]), ID3D10Blob_GetBufferSize(b[6]), NULL, &pf));
    if (ok && !W.il) {
      D3D11_INPUT_ELEMENT_DESC el[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0},
      };
      ok = SUCCEEDED(ID3D11Device_CreateInputLayout(W.dev, el, 6, ID3D10Blob_GetBufferPointer(b[0]), ID3D10Blob_GetBufferSize(b[0]), &il));
    }
    if (ok) {
      REL(W.vs_water);
      REL(W.ps_water);
      REL(W.vs_quad);
      REL(W.ps_sim);
      REL(W.vs_imp);
      REL(W.ps_imp);
      REL(W.ps_fall);
      W.vs_water = vw, W.ps_water = pw, W.vs_quad = vq, W.ps_sim = ps, W.vs_imp = vi, W.ps_imp = pi, W.ps_fall = pf;
      if (il) W.il = il;
    } else {
      REL(vw);
      REL(pw);
      REL(vq);
      REL(ps);
      REL(vi);
      REL(pi);
      REL(pf);
      REL(il);
    }
  }
  for (int i = 0; i < 7; i++) REL(b[i]);
  fprintf(stderr, "water: shaders %s\n", ok ? "compiled" : "FAILED (keeping the last good ones)");
  return ok;
}

// ------------------------------------------------------------------ textures

static ID3D11ShaderResourceView *make_tex(int w, int h, DXGI_FORMAT fmt, const void *px, int pitch, int mips,
                                          ID3D11Texture2D **out_tex) {
  D3D11_TEXTURE2D_DESC d = {0};
  d.Width = w;
  d.Height = h;
  d.MipLevels = mips ? 0 : 1;
  d.ArraySize = 1;
  d.Format = fmt;
  d.SampleDesc.Count = 1;
  d.Usage = D3D11_USAGE_DEFAULT;
  d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (mips ? D3D11_BIND_RENDER_TARGET : 0);
  d.MiscFlags = mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
  ID3D11Texture2D *t = NULL;
  ID3D11ShaderResourceView *srv = NULL;
  if (FAILED(ID3D11Device_CreateTexture2D(W.dev, &d, NULL, &t))) return NULL;
  if (FAILED(ID3D11Device_CreateShaderResourceView(W.dev, (ID3D11Resource *)t, NULL, &srv))) {
    REL(t);
    return NULL;
  }
  if (px) ID3D11DeviceContext_UpdateSubresource(W.ctx, (ID3D11Resource *)t, 0, NULL, px, pitch, 0);
  if (mips) ID3D11DeviceContext_GenerateMips(W.ctx, srv);
  if (out_tex) *out_tex = t;
  else REL(t);
  return srv;
}

// Tileable detail: slopes of a sum of waves with whole-number wave vectors
// (rg), value noise (b) and cloud noise (a), 256 x 256.
static void make_noise(void) {
  enum { N = 256, NW = 48 };
  float kx[NW], ky[NW], amp[NW], ph[NW];
  uint32_t seed = 777;
#define RND() (seed = seed * 1664525u + 1013904223u, (seed >> 8) * (1.0f / 16777216.0f))
  for (int i = 0; i < NW; i++) {
    float m = 2 + powf(RND(), 1.6f) * 22, a = RND() * 6.2831853f;
    kx[i] = roundf(m * cosf(a));
    ky[i] = roundf(m * sinf(a));
    if (kx[i] == 0 && ky[i] == 0) kx[i] = 3;
    float km = sqrtf(kx[i] * kx[i] + ky[i] * ky[i]);
    amp[i] = powf(km, -1.6f);
    ph[i] = RND() * 6.2831853f;
  }
  float *sx = malloc(N * N * sizeof(float)), *sy = malloc(N * N * sizeof(float));
  float mx = 0;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float u = (float)x / N, v = (float)y / N, gx = 0, gy = 0;
      for (int i = 0; i < NW; i++) {
        float s = sinf(6.2831853f * (kx[i] * u + ky[i] * v) + ph[i]) * amp[i] * 6.2831853f;
        gx -= s * kx[i];
        gy -= s * ky[i];
      }
      sx[y * N + x] = gx;
      sy[y * N + x] = gy;
      if (fabsf(gx) > mx) mx = fabsf(gx);
      if (fabsf(gy) > mx) mx = fabsf(gy);
    }
  // value noise lattices (tileable), octaves summed
  float lat[4][32 * 32];
  for (int o = 0; o < 4; o++)
    for (int i = 0; i < 32 * 32; i++) lat[o][i] = RND();
  uint8_t *px = malloc(N * N * 4);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float vn[2] = {0, 0};
      for (int which = 0; which < 2; which++) {
        float sum = 0, wsum = 0;
        for (int o = 0; o < 4; o++) {
          int per = which ? 4 << o : 8 << o;   // cells across the tile
          if (per > 32) per = 32;
          float fx = (float)x * per / N, fy = (float)y * per / N;
          int x0 = (int)fx, y0 = (int)fy;
          float tx = fx - x0, ty = fy - y0;
          tx = tx * tx * (3 - 2 * tx);
          ty = ty * ty * (3 - 2 * ty);
          const float *L = lat[(o + which * 2) & 3];
#define LAT(i, j) L[((j) % per) * 32 + ((i) % per)]
          float a = LAT(x0, y0) + (LAT(x0 + 1, y0) - LAT(x0, y0)) * tx;
          float b = LAT(x0, y0 + 1) + (LAT(x0 + 1, y0 + 1) - LAT(x0, y0 + 1)) * tx;
#undef LAT
          float wt = 1.0f / (1 << o);
          sum += (a + (b - a) * ty) * wt;
          wsum += wt;
        }
        vn[which] = sum / wsum;
      }
      uint8_t *p = px + (y * N + x) * 4;
      p[0] = (uint8_t)lroundf(127.5f + 127.5f * sx[y * N + x] / mx);
      p[1] = (uint8_t)lroundf(127.5f + 127.5f * sy[y * N + x] / mx);
      float b = (vn[0] - 0.5f) * 1.8f + 0.5f, c = (vn[1] - 0.5f) * 1.6f + 0.5f;
      p[2] = (uint8_t)lroundf(255 * (b < 0 ? 0 : b > 1 ? 1 : b));
      p[3] = (uint8_t)lroundf(255 * (c < 0 ? 0 : c > 1 ? 1 : c));
    }
#undef RND
  W.noise_srv = make_tex(N, N, DXGI_FORMAT_R8G8B8A8_UNORM, px, N * 4, 1, NULL);
  free(px);
  free(sx);
  free(sy);
}

void water_take_texture(uint32_t serial, int w, int h, const uint8_t *rgba) {
  if (w <= 0 || h <= 0 || !rgba) return;
  size_t bytes = (size_t)w * h * 4;
  uint8_t *copy = malloc(bytes);
  if (!copy) return;
  memcpy(copy, rgba, bytes);
  Pend *p = NULL;
  // Original water does not drain this queue. A reset may resubmit the same
  // terrain textures, so retain only their newest pixels until HD is enabled.
  for (int i = 0; i < W.npend; i++)
    if (W.pend[i].serial == serial) { p = W.pend + i; break; }
  if (!p) {
    if (W.npend == W.cap_pend) {
      int cap = W.cap_pend ? W.cap_pend * 2 : 64;
      Pend *grown = realloc(W.pend, (size_t)cap * sizeof(Pend));
      if (!grown) { free(copy); return; }
      W.pend = grown; W.cap_pend = cap;
    }
    p = W.pend + W.npend++;
    memset(p, 0, sizeof *p);
  }
  free(p->rgba);
  *p = (Pend){serial, w, h, copy};
}

static WTex *find_wtex(uint32_t serial) {
  for (int i = 0; i < W.ntex; i++)
    if (W.tex[i].serial == serial) return &W.tex[i];
  return NULL;
}

static void upload_pending(void) {
  for (int i = 0; i < W.npend; i++) {
    Pend *p = &W.pend[i];
    WTex *t = find_wtex(p->serial);
    if (!t) {
      if (W.ntex == W.cap_tex) W.tex = realloc(W.tex, (W.cap_tex = W.cap_tex ? W.cap_tex * 2 : 64) * sizeof(WTex));
      t = &W.tex[W.ntex++];
      memset(t, 0, sizeof *t);
      t->serial = p->serial;
    }
    REL(t->srv);
    REL(t->tex);
    t->srv = make_tex(p->w, p->h, DXGI_FORMAT_R8G8B8A8_UNORM, p->rgba, p->w * 4, 1, &t->tex);
    free(p->rgba);
  }
  W.npend = 0;
}

// ------------------------------------------------------------------ the map: depth, flow, pools

static void build_map(const HdtScene *sc) {
  int w = sc->map_w, h = sc->map_h, n = w * h;
  W.map_w = w;
  W.map_h = h;
  float *phi = malloc(n * sizeof(float)), *tmp = malloc(n * sizeof(float)), *tf = calloc((size_t)n * 3, sizeof(float));
  // water level smoothed along the water (the flow follows its slope through flat reaches)
  for (int i = 0; i < n; i++) phi[i] = sc->wheight[i];
  for (int it = 0; it < 40; it++) {
    for (int i = 0; i < n; i++) {
      if (!sc->wcode[i]) {
        tmp[i] = phi[i];
        continue;
      }
      int x = i % w, y = i / w, m = 0;
      float sum = 0;
      int nb[4] = {x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1, y > 0 ? i - w : -1, y < h - 1 ? i + w : -1};
      for (int j = 0; j < 4; j++)
        if (nb[j] >= 0 && sc->wcode[nb[j]]) sum += phi[nb[j]], m++;
      tmp[i] = m ? 0.35f * sc->wheight[i] + 0.65f * sum / m : phi[i];
    }
    float *sw = phi;
    phi = tmp;
    tmp = sw;
  }
  free(W.wet);
  free(W.pools);
  W.wet = malloc(n * sizeof(int));
  W.pools = malloc(n * sizeof(int));
  W.nwet = W.npools = 0;
  for (int i = 0; i < n; i++) {   // per tile: flow (down the smoothed level, along the water) and turbulence
    if (!sc->wcode[i]) continue;
    W.wet[W.nwet++] = i;
    int x = i % w, y = i / w;
    float gx = 0, gy = 0;
    int l = x > 0 && sc->wcode[i - 1], r = x < w - 1 && sc->wcode[i + 1];
    int u = y > 0 && sc->wcode[i - w], dn = y < h - 1 && sc->wcode[i + w];
    if (l && r) gx = (phi[i + 1] - phi[i - 1]) * 0.5f;
    else if (r) gx = phi[i + 1] - phi[i];
    else if (l) gx = phi[i] - phi[i - 1];
    if (u && dn) gy = (phi[i + w] - phi[i - w]) * 0.5f;
    else if (dn) gy = phi[i + w] - phi[i];
    else if (u) gy = phi[i] - phi[i - w];
    float fx = -gx / 12, fy = -gy / 12, fl = sqrtf(fx * fx + fy * fy);
    if (fl > 1) fx /= fl, fy /= fl;
    // water falls into this tile from a higher water neighbour: a pool below a drop
    float drop = 0;
    int nb[4] = {l ? i - 1 : -1, r ? i + 1 : -1, u ? i - w : -1, dn ? i + w : -1};
    for (int j = 0; j < 4; j++)
      if (nb[j] >= 0 && sc->wheight[nb[j]] - sc->wheight[i] > drop) drop = sc->wheight[nb[j]] - sc->wheight[i];
    float turb = drop >= 10 ? (drop >= 40 ? 1 : 0.4f + drop / 70) : 0;
    if (turb > 0) W.pools[W.npools++] = i;
    tf[i * 3] = fx, tf[i * 3 + 1] = fy, tf[i * 3 + 2] = turb;
  }
  // depth from the distance to the shore, kInfo cells per tile (8-neighbour chamfer; the map edge is open water)
  int cw = w * kInfo, ch = h * kInfo, cn = cw * ch;
  float *dist = malloc((size_t)cn * sizeof(float));
  for (int i = 0; i < cn; i++) dist[i] = sc->wcode[(i / cw / kInfo) * w + (i % cw) / kInfo] ? 1e6f : 0;
  for (int pass = 0; pass < 2; pass++)
    for (int k = 0; k < cn; k++) {
      int i = pass ? cn - 1 - k : k, x = i % cw, y = i / cw, sg = pass ? 1 : -1;
      if (!dist[i]) continue;
      float d = dist[i];
      int nx[4] = {x + sg, x, x + sg, x - sg}, ny[4] = {y, y + sg, y + sg, y + sg};
      float c[4] = {1, 1, 1.4142f, 1.4142f};
      for (int j = 0; j < 4; j++)
        if (nx[j] >= 0 && ny[j] >= 0 && nx[j] < cw && ny[j] < ch && dist[ny[j] * cw + nx[j]] + c[j] < d)
          d = dist[ny[j] * cw + nx[j]] + c[j];
      dist[i] = d;
    }
  uint8_t *px = calloc((size_t)cn, 4);
  for (int i = 0; i < cn; i++) {
    int t = (i / cw / kInfo) * w + (i % cw) / kInfo;
    if (!sc->wcode[t]) continue;
    float d = dist[i] > 1e5f ? 30 : (dist[i] - 0.5f) / kInfo;   // tiles from the water's edge
    float depth = 0.08f + 0.92f * (1 - expf(-d / 1.7f));
    float cls = (float)(sc->wcode[t] - 1) * 0.33f;
    if (depth < cls) depth = cls;
    uint8_t *p = px + (size_t)i * 4;
    p[0] = (uint8_t)lroundf(255 * (depth > 1 ? 1 : depth));
    p[1] = (uint8_t)lroundf(127.5f + 127.5f * tf[t * 3]);
    p[2] = (uint8_t)lroundf(127.5f + 127.5f * tf[t * 3 + 1]);
    p[3] = (uint8_t)lroundf(255 * tf[t * 3 + 2]);
  }
  REL(W.info_srv);
  REL(W.info);
  W.info_srv = make_tex(cw, ch, DXGI_FORMAT_R8G8B8A8_UNORM, px, cw * 4, 0, &W.info);
  free(px);
  free(dist);
  free(phi);
  free(tmp);
  free(tf);
  // the ripple simulation, kCells cells per tile, at rest
  int sw = w * kCells, sh = h * kCells;
  if (sw != W.sim_w || sh != W.sim_h) {
    for (int k = 0; k < 2; k++) {
      REL(W.sim_rtv[k]);
      REL(W.sim_srv[k]);
      REL(W.sim[k]);
      D3D11_TEXTURE2D_DESC d = {0};
      d.Width = sw;
      d.Height = sh;
      d.MipLevels = 1;
      d.ArraySize = 1;
      d.Format = DXGI_FORMAT_R16G16_FLOAT;
      d.SampleDesc.Count = 1;
      d.Usage = D3D11_USAGE_DEFAULT;
      d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
      if (SUCCEEDED(ID3D11Device_CreateTexture2D(W.dev, &d, NULL, &W.sim[k]))) {
        ID3D11Device_CreateShaderResourceView(W.dev, (ID3D11Resource *)W.sim[k], NULL, &W.sim_srv[k]);
        ID3D11Device_CreateRenderTargetView(W.dev, (ID3D11Resource *)W.sim[k], NULL, &W.sim_rtv[k]);
        float zero[4] = {0};
        if (W.sim_rtv[k]) ID3D11DeviceContext_ClearRenderTargetView(W.ctx, W.sim_rtv[k], zero);
      }
    }
    W.sim_w = sw;
    W.sim_h = sh;
  }
  fprintf(stderr, "water: map %dx%d, %d water tiles, %d pools below drops, sim %dx%d\n", w, h, W.nwet, W.npools, sw, sh);
  const char *dump = getenv("SIMGOLF_WATER_DUMP");   // tests: the water map as text (. ground, 1-3 depth class, P pool)
  FILE *f = dump && *dump ? fopen(dump, "w") : NULL;
  if (f) {
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        int i = y * w + x, pool = 0;
        for (int k = 0; k < W.npools; k++) pool |= W.pools[k] == i;
        fputc(!sc->wcode[i] ? '.' : pool ? 'P' : '0' + sc->wcode[i], f);
      }
      fputc('\n', f);
    }
    fclose(f);
  }
}

// ------------------------------------------------------------------ geometry

static void build_geometry(const HdtScene *sc) {
  float tile_px = 0;
  W.nvtx = 0;
  W.ndraws = 0;
  for (int k = 0; k < sc->nbatches; k++) {
    const int32_t *b = sc->batches + k * 5;
    if (!(b[4] & kWaterFlag)) continue;
    int first = b[0], count = b[1];
    if (W.nvtx + count > W.cap_vtx) {
      W.cap_vtx = (W.nvtx + count) * 2;
      W.vtx = realloc(W.vtx, (size_t)W.cap_vtx * kFloats * sizeof(float));
    }
    if (W.ndraws && W.draws[W.ndraws - 1].serial == (uint32_t)b[3] && W.draws[W.ndraws - 1].first + W.draws[W.ndraws - 1].count == W.nvtx) {
      W.draws[W.ndraws - 1].count += count;
    } else {
      if (W.ndraws == W.cap_draws) W.draws = realloc(W.draws, (W.cap_draws = W.cap_draws ? W.cap_draws * 2 : 64) * sizeof(Draw));
      W.draws[W.ndraws++] = (Draw){W.nvtx, count, (uint32_t)b[3]};
    }
    for (int tri = 0; tri + 2 < count; tri += 3) {
      const float *wa = sc->world + (size_t)(first + tri) * 3, *wb = wa + 3, *wc = wa + 6;
      float e1[3] = {wb[0] - wa[0], wb[1] - wa[1], wb[2] - wa[2]}, e2[3] = {wc[0] - wa[0], wc[1] - wa[1], wc[2] - wa[2]};
      float nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2], nz = e1[0] * e2[1] - e1[1] * e2[0];
      float l = sqrtf(nx * nx + ny * ny + nz * nz);
      if (l > 0) nx /= l, ny /= l, nz /= l;
      if (ny < 0) nx = -nx, ny = -ny, nz = -nz;
      if (tri == 0) {   // window px per Terrain unit along this triangle's longest edge
        const float *sa = sc->verts + (size_t)first * 8, *sb = sa + 8;
        float wl = sqrtf(e1[0] * e1[0] + e1[2] * e1[2]), sl = sqrtf((sb[0] - sa[0]) * (sb[0] - sa[0]) + (sb[1] - sa[1]) * (sb[1] - sa[1]));
        if (wl > 1) tile_px = sl / wl * 100 * 1.41421f;   // a tile's diagonal is the diamond's width
      }
      for (int v = 0; v < 3; v++) {
        const float *s = sc->verts + (size_t)(first + tri + v) * 8, *wv = sc->world + (size_t)(first + tri + v) * 3;
        float *o = W.vtx + (size_t)(W.nvtx + tri + v) * kFloats;
        o[0] = s[0], o[1] = s[1];      // window position
        o[2] = s[6], o[3] = s[7];      // uv
        o[4] = s[2], o[5] = s[3], o[6] = s[4], o[7] = s[5];
        o[8] = wv[0], o[9] = wv[1], o[10] = wv[2];
        o[11] = nx, o[12] = nz;
        o[13] = o[14] = o[15] = wv[1];
      }
    }
    W.nvtx += count - count % 3;
    if (count % 3) W.draws[W.ndraws - 1].count -= count % 3;
  }
  // waterfalls: upright ground triangles (cliffs) between two water tiles at different levels
  W.fall_first = W.nvtx;
  for (int k = 0; W.map_w > 0 && k < sc->nbatches; k++) {
    const int32_t *b = sc->batches + k * 5;
    if (b[4] & kWaterFlag) continue;
    for (int tri = 0; tri + 2 < b[1]; tri += 3) {
      const float *wa = sc->world + (size_t)(b[0] + tri) * 3, *wb = wa + 3, *wc = wa + 6;
      float e1[3] = {wb[0] - wa[0], wb[1] - wa[1], wb[2] - wa[2]}, e2[3] = {wc[0] - wa[0], wc[1] - wa[1], wc[2] - wa[2]};
      float nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2], nz = e1[0] * e2[1] - e1[1] * e2[0];
      float l = sqrtf(nx * nx + ny * ny + nz * nz), hl = sqrtf(nx * nx + nz * nz);
      if (l <= 0 || hl < 0.94f * l) continue;   // not upright
      nx /= hl, nz /= hl;
      float cx = (wa[0] + wb[0] + wc[0]) / 3 * 0.01f + W.map_w * 0.5f, cy = (wa[2] + wb[2] + wc[2]) / 3 * 0.01f + W.map_h * 0.5f;
      int ax = (int)floorf(cx - nx * 0.5f), ay = (int)floorf(cy - nz * 0.5f), bx = (int)floorf(cx + nx * 0.5f), by = (int)floorf(cy + nz * 0.5f);
      if (ax < 0 || ay < 0 || bx < 0 || by < 0 || ax >= W.map_w || bx >= W.map_w || ay >= W.map_h || by >= W.map_h) continue;
      int ia = ay * W.map_w + ax, ib = by * W.map_w + bx;
      if (!sc->wcode[ia] || !sc->wcode[ib]) continue;
      float ha = sc->wheight[ia], hb = sc->wheight[ib], top = ha > hb ? ha : hb, bot = ha > hb ? hb : ha;
      if (top - bot < 8) continue;
      {   // a face turned away from the viewer is never seen in golf (back faces are culled): drawing it painted a dark wedge over the cliff
        float fx = ha > hb ? nx : -nx, fz = ha > hb ? nz : -nz, vh = sqrtf(sc->view[0] * sc->view[0] + sc->view[2] * sc->view[2]);
        if (vh > 0 && fx * sc->view[0] + fz * sc->view[2] <= 0.1f * vh) continue;
      }
      if (W.nvtx + 3 > W.cap_vtx) {
        W.cap_vtx = (W.nvtx + 3) * 2;
        W.vtx = realloc(W.vtx, (size_t)W.cap_vtx * kFloats * sizeof(float));
      }
      for (int v = 0; v < 3; v++) {
        const float *s = sc->verts + (size_t)(b[0] + tri + v) * 8, *wv = wa + v * 3;
        float *o = W.vtx + (size_t)(W.nvtx + v) * kFloats;
        o[0] = s[0], o[1] = s[1], o[2] = s[6], o[3] = s[7];
        o[4] = s[2], o[5] = s[3], o[6] = s[4], o[7] = s[5];
        o[8] = wv[0], o[9] = wv[1], o[10] = wv[2];
        o[11] = ha > hb ? nx : -nx, o[12] = ha > hb ? nz : -nz;   // facing the lower water
        o[13] = wv[1], o[14] = top, o[15] = bot;
      }
      W.nvtx += 3;
    }
  }
  W.fall_count = W.nvtx - W.fall_first;
  W.ntri = W.nvtx / 3;

  if (tile_px > 0) W.tile_px = tile_px;
  if (memcmp(W.view, sc->view, sizeof W.view))
    fprintf(stderr, "water: view %.2f %.2f %.2f, light %.2f %.2f %.2f, %.1f window px per tile\n", sc->view[0], sc->view[1],
            sc->view[2], sc->view[3], sc->view[4], sc->view[5], tile_px);
  memcpy(W.view, sc->view, sizeof W.view);
  if (W.nvtx > W.vb_cap) {
    REL(W.vb);
    W.vb_cap = W.nvtx + W.nvtx / 2 + 256;
    D3D11_BUFFER_DESC d = {0};
    d.ByteWidth = (UINT)(W.vb_cap * kFloats * sizeof(float));
    d.Usage = D3D11_USAGE_DYNAMIC;
    d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(ID3D11Device_CreateBuffer(W.dev, &d, NULL, &W.vb))) W.vb_cap = 0;
  }
  if (W.vb && W.nvtx) {
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ID3D11DeviceContext_Map(W.ctx, (ID3D11Resource *)W.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
      memcpy(m.pData, W.vtx, (size_t)W.nvtx * kFloats * sizeof(float));
      ID3D11DeviceContext_Unmap(W.ctx, (ID3D11Resource *)W.vb, 0);
    }
  }
}

// ------------------------------------------------------------------ the wind sea

// 32 waves sampled from the Pierson-Moskowitz spectrum for a wind of u m/s
// blowing toward dir (radians, map axes), wavelengths 0.5 m to lmax, spread
// around the wind (wider for short waves), amplitudes from the spectrum and
// scaled so the mean square slope is Cox and Munk's for that wind (times
// chop). Out: per wave the wave vector in radians per tile (a tile is about
// 10 m), the slope amplitude a k and a phase. Returns the wave count. Built
// again only when the wind changes.
static int spectrum(float out[32][4], float dir, float u, float chop, float lmax) {
  static float cache[32][4], c_dir = -1000, c_u = -1, c_chop = -1, c_lmax = -1;
  if (dir == c_dir && u == c_u && chop == c_chop && lmax == c_lmax) {
    memcpy(out, cache, sizeof cache);
    return 32;
  }
  enum { NWAVES = 32 };
  const float g = 9.81f, lmin = 0.5f;
  if (lmax < 1) lmax = 1;
  float uu = u < 0.5f ? 0.5f : u;
  float wp = 0.877f * g / uu;   // peak angular frequency
  uint32_t seed = 4242;
#define RND() (seed = seed * 1664525u + 1013904223u, (seed >> 8) * (1.0f / 16777216.0f))
  float msq = 0;
  for (int i = 0; i < NWAVES; i++) {
    float f = (i + 0.5f) / NWAVES;
    float lam = lmin * powf(lmax / lmin, f) * (0.9f + 0.2f * RND());
    float k = 6.2831853f / lam, w = sqrtf(g * k);
    // the spectrum's energy in this wave's share of the band (log spacing: dw = w * dln)
    float dln = logf(lmax / lmin) / NWAVES * 0.5f;
    float S = 0.0081f * g * g / powf(w, 5) * expf(-1.25f * powf(wp / w, 4));
    float a = sqrtf(2 * S * w * dln);
    // direction: short waves spread wider around the wind
    float spread = 0.35f + 0.6f * (1 - f);
    float th = dir + (RND() + RND() + RND() - 1.5f) * spread * 1.4f;
    float ak = a * k;
    out[i][0] = cosf(th) * k * 10;   // radians per tile
    out[i][1] = sinf(th) * k * 10;
    out[i][2] = ak;
    out[i][3] = RND() * 6.2831853f;
    msq += ak * ak * 0.5f;
  }
#undef RND
  // Cox and Munk: mean square slope 0.003 + 0.00512 u
  float target = (0.003f + 0.00512f * u) * chop * chop;
  float scale = msq > 0 ? sqrtf(target / msq) : 0;
  for (int i = 0; i < NWAVES; i++) out[i][2] *= scale;
  memcpy(cache, out, sizeof cache);
  c_dir = dir, c_u = u, c_chop = chop, c_lmax = lmax;
  return NWAVES;
}

// ------------------------------------------------------------------ init

static int init(SDL_Renderer *r) {
  W.state = -1;
  W.dev = SDL_RenderGetD3D11Device(r);
  if (!W.dev) {
    fprintf(stderr, "water: the renderer is not Direct3D 11; the original water stays\n");
    return 0;
  }
  ID3D11Device_GetImmediateContext(W.dev, &W.ctx);
  ID3D11Device1 *dev1 = NULL;
  if (FAILED(ID3D11Device_QueryInterface(W.dev, &IID_ID3D11Device1, (void **)&dev1)) ||
      FAILED(ID3D11DeviceContext_QueryInterface(W.ctx, &IID_ID3D11DeviceContext1, (void **)&W.ctx1))) {
    fprintf(stderr, "water: no Direct3D 11.1; the original water stays\n");
    REL(dev1);
    return 0;
  }
  D3D_FEATURE_LEVEL fl = ID3D11Device_GetFeatureLevel(W.dev), got;
  UINT cflags = ID3D11Device_GetCreationFlags(W.dev);
  UINT sflags = cflags & D3D11_CREATE_DEVICE_SINGLETHREADED ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
  HRESULT hr = ID3D11Device1_CreateDeviceContextState(dev1, sflags, &fl, 1, D3D11_SDK_VERSION, &IID_ID3D11Device, &got, &W.st);
  REL(dev1);
  if (FAILED(hr)) {
    fprintf(stderr, "water: CreateDeviceContextState failed (0x%08lx)\n", (unsigned long)hr);
    return 0;
  }
  if(!W.compile) {
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
    W.compile = dc ? (PFN_Compile)(void *)GetProcAddress(dc, "D3DCompile") : NULL;
  }
  if (!W.compile) {
    fprintf(stderr, "water: d3dcompiler_47.dll not found\n");
    return 0;
  }
  // everything below is created inside our own state
  ID3DDeviceContextState *prev = NULL;
  ID3D11DeviceContext1_SwapDeviceContextState(W.ctx1, W.st, &prev);
  int ok = load_shaders();
  file_time(port_host_path("/ui/water.hlsl"), &W.hlsl_time);
  load_txt();
  file_time(port_host_path("/ui/water.txt"), &W.txt_time);
  D3D11_BUFFER_DESC cd = {0};
  cd.ByteWidth = sizeof(CB);
  cd.Usage = D3D11_USAGE_DYNAMIC;
  cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  ok = ok && SUCCEEDED(ID3D11Device_CreateBuffer(W.dev, &cd, NULL, &W.cb));
  D3D11_SAMPLER_DESC sd = {0};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
  ok = ok && SUCCEEDED(ID3D11Device_CreateSamplerState(W.dev, &sd, &W.s_wrap));
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  ok = ok && SUCCEEDED(ID3D11Device_CreateSamplerState(W.dev, &sd, &W.s_clamp));
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  ok = ok && SUCCEEDED(ID3D11Device_CreateSamplerState(W.dev, &sd, &W.s_point));
  D3D11_BLEND_DESC bd = {0};
  bd.RenderTarget[0].BlendEnable = TRUE;
  bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;   // the target keeps its alpha
  bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
  bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  ok = ok && SUCCEEDED(ID3D11Device_CreateBlendState(W.dev, &bd, &W.b_water));
  bd.RenderTarget[0].SrcBlend = bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
  bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  ok = ok && SUCCEEDED(ID3D11Device_CreateBlendState(W.dev, &bd, &W.b_add));
  D3D11_RASTERIZER_DESC rd = {0};
  rd.FillMode = D3D11_FILL_SOLID;
  rd.CullMode = D3D11_CULL_NONE;
  rd.ScissorEnable = TRUE;
  rd.DepthClipEnable = TRUE;
  ok = ok && SUCCEEDED(ID3D11Device_CreateRasterizerState(W.dev, &rd, &W.rs));
  if (ok) make_noise();
  ok = ok && W.noise_srv;
  for (int i = 0; ok && i < 4; i++) {
    D3D11_QUERY_DESC q = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    ID3D11Device_CreateQuery(W.dev, &q, &W.qd[i]);
    q.Query = D3D11_QUERY_TIMESTAMP;
    ID3D11Device_CreateQuery(W.dev, &q, &W.q0[i]);
    ID3D11Device_CreateQuery(W.dev, &q, &W.q1[i]);
  }
  ID3D11DeviceContext1_SwapDeviceContextState(W.ctx1, prev, NULL);
  REL(prev);
  if (!ok) {
    fprintf(stderr, "water: setup failed; the original water stays\n");
    return 0;
  }
  W.state = 1;
  fprintf(stderr, "water: Direct3D 11 water pass ready (feature level 0x%x)\n", (unsigned)fl);
  return 1;
}

// ------------------------------------------------------------------ per frame

static void set_cb(const CB *c) {
  D3D11_MAPPED_SUBRESOURCE m;
  if (SUCCEEDED(ID3D11DeviceContext_Map(W.ctx, (ID3D11Resource *)W.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
    memcpy(m.pData, c, sizeof *c);
    ID3D11DeviceContext_Unmap(W.ctx, (ID3D11Resource *)W.cb, 0);
  }
}

static void viewport(float w, float h) {
  D3D11_VIEWPORT vp = {0, 0, w, h, 0, 1};
  ID3D11DeviceContext_RSSetViewports(W.ctx, 1, &vp);
  D3D11_RECT sc = {0, 0, (LONG)w, (LONG)h};
  ID3D11DeviceContext_RSSetScissorRects(W.ctx, 1, &sc);
}

static void sim_steps(CB *c, int steps, const Imp *imp, int nimp) {
  if (!W.sim_rtv[0] || !W.sim_rtv[1]) return;
  ID3D11ShaderResourceView *null_srv[4] = {0};
  viewport((float)W.sim_w, (float)W.sim_h);
  ID3D11DeviceContext_IASetPrimitiveTopology(W.ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_IASetInputLayout(W.ctx, NULL);
  ID3D11DeviceContext_VSSetShader(W.ctx, W.vs_quad, NULL, 0);
  ID3D11DeviceContext_PSSetShader(W.ctx, W.ps_sim, NULL, 0);
  ID3D11DeviceContext_OMSetBlendState(W.ctx, NULL, NULL, 0xffffffff);
  set_cb(c);
  for (int s = 0; s < steps; s++) {
    ID3D11DeviceContext_PSSetShaderResources(W.ctx, 0, 4, null_srv);
    ID3D11DeviceContext_OMSetRenderTargets(W.ctx, 1, &W.sim_rtv[!W.cur], NULL);
    ID3D11ShaderResourceView *srv[3] = {NULL, W.info_srv, W.sim_srv[W.cur]};
    ID3D11DeviceContext_PSSetShaderResources(W.ctx, 0, 3, srv);
    ID3D11DeviceContext_Draw(W.ctx, 3, 0);
    W.cur = !W.cur;
  }
  if (nimp) {
    ID3D11DeviceContext_PSSetShaderResources(W.ctx, 0, 4, null_srv);
    ID3D11DeviceContext_OMSetRenderTargets(W.ctx, 1, &W.sim_rtv[W.cur], NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(W.ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_VSSetShader(W.ctx, W.vs_imp, NULL, 0);
    ID3D11DeviceContext_PSSetShader(W.ctx, W.ps_imp, NULL, 0);
    ID3D11DeviceContext_OMSetBlendState(W.ctx, W.b_add, NULL, 0xffffffff);
    for (int i = 0; i < nimp; i++) {
      c->imp[0] = imp[i].x, c->imp[1] = imp[i].y, c->imp[2] = imp[i].r, c->imp[3] = imp[i].s;
      set_cb(c);
      ID3D11DeviceContext_Draw(W.ctx, 4, 0);
    }
  }
  ID3D11DeviceContext_PSSetShaderResources(W.ctx, 0, 4, null_srv);
}

static void stats(double cpu_ms, int tw, int th) {
  // the oldest query ring entry, when the GPU is done with it
  int slot = W.qn & 3;
  if (W.qn >= 4) {
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
    UINT64 a, b;
    if (ID3D11DeviceContext_GetData(W.ctx, (ID3D11Asynchronous *)W.qd[slot], &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        ID3D11DeviceContext_GetData(W.ctx, (ID3D11Asynchronous *)W.q0[slot], &a, sizeof a, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        ID3D11DeviceContext_GetData(W.ctx, (ID3D11Asynchronous *)W.q1[slot], &b, sizeof b, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        !dj.Disjoint && dj.Frequency) {
      double ms = (double)(b - a) * 1000.0 / (double)dj.Frequency;
      W.gpu_sum += ms;
      if (ms > W.gpu_max) W.gpu_max = ms;
      W.stat_n++;
    }
  }
  W.cpu_sum += cpu_ms;
  if (cpu_ms > W.cpu_max) W.cpu_max = cpu_ms;
  W.tw = tw;
  W.th = th;
  double now = host_now_ms();
  if (!W.stat_t0) W.stat_t0 = now;
  if (now - W.stat_t0 >= 5000 && W.stat_n) {
    fprintf(stderr, "water: gpu avg %.3f ms max %.3f ms, cpu avg %.3f ms max %.3f ms over %d frames; target %dx%d, %d triangles, sim %dx%d\n",
            W.gpu_sum / W.stat_n, W.gpu_max, W.cpu_sum / W.stat_n, W.cpu_max, W.stat_n, tw, th, W.ntri, W.sim_w, W.sim_h);
    W.gpu_sum = W.gpu_max = W.cpu_sum = W.cpu_max = 0;
    W.stat_n = 0;
    W.stat_t0 = now;
  }
}

static void live_reload(double now) {
  if (now < W.next_check) return;
  W.next_check = now + 1000;
  FILETIME ft;
  if (file_time(port_host_path("/ui/water.txt"), &ft) && CompareFileTime(&ft, &W.txt_time)) {
    W.txt_time = ft;
    load_txt();
  }
  if (file_time(port_host_path("/ui/water.hlsl"), &ft) && CompareFileTime(&ft, &W.hlsl_time)) {
    W.hlsl_time = ft;
    load_shaders();
  }
}

void water_draw(SDL_Renderer *r, int x, int y, int w, int h, double now_ms) {
  if (g_mode <= 0 || W.state < 0 || w <= 0 || h <= 0) return;
  double c0 = host_now_ms();
  if (W.state == 0 && !init(r)) {
    InterlockedExchange(&g_works, -1);
    return;
  }
  HdtScene sc;
  if (!hdt_scene(&sc) || sc.map_w <= 0) return;
  SDL_RenderFlush(r);
  ID3D11RenderTargetView *rtv = NULL;
  ID3D11DeviceContext_OMGetRenderTargets(W.ctx, 1, &rtv, NULL);
  if (!rtv) return;
  ID3D11Resource *res = NULL;
  ID3D11RenderTargetView_GetResource(rtv, &res);
  D3D11_TEXTURE2D_DESC td = {0};
  if (res) {
    ID3D11Texture2D *t2 = NULL;
    if (SUCCEEDED(ID3D11Resource_QueryInterface(res, &IID_ID3D11Texture2D, (void **)&t2))) {
      ID3D11Texture2D_GetDesc(t2, &td);
      REL(t2);
    }
    REL(res);
  }
  float sx = 1, sy = 1;
  SDL_RenderGetScale(r, &sx, &sy);
  SDL_Rect clip = {0, 0, 0, 0};
  int clipped = SDL_RenderIsClipEnabled(r);
  if (clipped) SDL_RenderGetClipRect(r, &clip);

  ID3DDeviceContextState *prev = NULL;
  ID3D11DeviceContext1_SwapDeviceContextState(W.ctx1, W.st, &prev);
  live_reload(c0);
  if (W.npend) upload_pending();
  if (sc.map_serial != W.map_serial) {
    W.map_serial = sc.map_serial;
    build_map(&sc);
  }
  if (sc.serial != W.scene_serial || W.map_w != sc.map_w) {
    W.scene_serial = sc.serial;
    build_geometry(&sc);
  }
  int slot = W.qn & 3;
  ID3D11DeviceContext_Begin(W.ctx, (ID3D11Asynchronous *)W.qd[slot]);
  ID3D11DeviceContext_End(W.ctx, (ID3D11Asynchronous *)W.q0[slot]);

  // time: the picture's own, or the recording's fixed steps
  double t = now_ms / 1000.0;
  if (W.clip) t = W.clip_t0 + (double)W.clip_n / (W.clip_fps > 0 ? W.clip_fps : 30);
  double dt = W.last_t ? t - W.last_t : 0;
  if (dt < 0 || dt > 0.25) dt = 0;
  W.last_t = t;
  if (!W.t_base) W.t_base = t;
  float tt = (float)fmod(t - W.t_base, 3600.0);
  static FILE *flog;   // tests: SIMGOLF_WATER_FRAMELOG, one line per presented frame (its time, the water's time)
  static int flog_tried;
  if (!flog_tried) {
    flog_tried = 1;
    const char *fp = getenv("SIMGOLF_WATER_FRAMELOG");
    if (fp && *fp && (flog = fopen(fp, "w"))) fprintf(flog, "now_ms,water_s\n");
  }
  if (flog) fprintf(flog, "%.3f,%.6f\n", now_ms, t), fflush(flog);

  int look_i = cur_look();
  static int last_look = -1;
  if (look_i != last_look) fprintf(stderr, "water: theme look %s\n", k_looks[look_i]), last_look = look_i;
  const Look *L = &g_look[look_i];
  float wd = g_wind_dir > -999 ? g_wind_dir : L->wind_dir * 0.0174533f;
  float ws = g_wind_speed >= 0 ? g_wind_speed : L->wind_speed;
  CB c = {0};
  c.map[0] = (float)W.map_w, c.map[1] = (float)W.map_h, c.map[2] = 1.0f / W.map_w, c.map[3] = 1.0f / W.map_h;
  c.sim[0] = 1.0f / (W.sim_w ? W.sim_w : 1), c.sim[1] = 1.0f / (W.sim_h ? W.sim_h : 1), c.sim[2] = powf(L->ripple_speed * kCells / 60.0f, 2), c.sim[3] = L->ripple;

  // ripple simulation: fixed 60 steps a second, disturbances queued since the last frame
  Imp imp[kMaxImp];
  int nimp = 0;
  ilock();
  nimp = g_nimp;
  memcpy(imp, g_imp, (size_t)nimp * sizeof(Imp));
  g_nimp = 0;
  LeaveCriticalSection(&g_ilock);
  Imp bnew[64];
  int nbnew;
  ilock();
  nbnew = g_nbp;
  memcpy(bnew, g_bpend, (size_t)nbnew * sizeof(Imp));
  g_nbp = 0;
  LeaveCriticalSection(&g_ilock);
  {
    int keep = 0;
    for (int i = 0; i < W.nball; i++)
      if (tt >= W.ball[i][2] && tt - W.ball[i][2] < L->ball_life) memcpy(W.ball[keep++], W.ball[i], sizeof W.ball[0]);
    W.nball = keep;
    if (nbnew && getenv("SIMGOLF_WATER_LOG")) fprintf(stderr, "water: %d ball splash(es) picked up at t=%.2f, life %.2f, size %.3f\n", nbnew, tt, L->ball_life, L->ball_size);
    for (int j = 0; j < nbnew && W.nball < 8; j++) W.ball[W.nball][0] = bnew[j].x, W.ball[W.nball][1] = bnew[j].y, W.ball[W.nball++][2] = tt;
  }
  W.drop_acc += dt * L->drops * W.nwet / 100.0;
  while (W.drop_acc >= 1 && nimp < kMaxImp) {
    W.drop_acc -= 1;
    int i = W.wet[(int)(frand() * W.nwet) % (W.nwet ? W.nwet : 1)];
    imp[nimp++] = (Imp){i % W.map_w + frand(), i / W.map_w + frand(), (0.15f + 0.25f * frand()) * (frand() < 0.5f ? -1 : 1), 0.10f};
  }
  if (W.npools) {
    W.fall_acc += dt * L->fall_rate * W.npools;
    while (W.fall_acc >= 1 && nimp < kMaxImp) {
      W.fall_acc -= 1;
      int i = W.pools[(int)(frand() * W.npools) % W.npools];
      imp[nimp++] = (Imp){i % W.map_w + 0.2f + 0.6f * frand(), i / W.map_w + 0.2f + 0.6f * frand(), 0.35f * (frand() - 0.5f), 0.14f};
    }
  }
  if (!W.nwet) W.drop_acc = 0;
  W.sim_acc += dt;
  int steps = 0;
  while (W.sim_acc >= 1.0 / 60 && steps < 4) W.sim_acc -= 1.0 / 60, steps++;
  if (W.sim_acc > 0.1) W.sim_acc = 0;
  ID3D11DeviceContext_RSSetState(W.ctx, W.rs);
  if (steps || nimp) sim_steps(&c, steps, imp, nimp);

  // the water surface
  float rw = (float)td.Width, rh = (float)td.Height;
  if (W.nvtx && W.vb && rw > 0) {
    c.rt[0] = 2 / rw, c.rt[1] = 2 / rh, c.rt[2] = rw, c.rt[3] = rh;
    c.dst[0] = x * sx, c.dst[1] = y * sy, c.dst[2] = w * sx, c.dst[3] = h * sy;
    c.src[0] = 1.0f / sc.tw, c.src[1] = 1.0f / sc.th, c.src[2] = (float)sc.th;
    // screen px per tile: the window width of a tile at this zoom (64 canvas px at zoom 4)
    c.src[3] = (W.tile_px > 0 ? W.tile_px : 64) * c.dst[2] / sc.tw;
    c.view[0] = W.view[0], c.view[1] = W.view[1], c.view[2] = W.view[2], c.view[3] = tt;
    c.sun[0] = W.view[3], c.sun[1] = W.view[4], c.sun[2] = W.view[5], c.sun[3] = 0.0f;
    c.wind[0] = cosf(wd), c.wind[1] = sinf(wd), c.wind[2] = ws, c.wind[3] = L->chop;
    copy3(c.shallow, L->shallow), c.shallow[3] = L->refl;
    copy3(c.deep, L->deep), c.deep[3] = L->glints;
    c.foam[0] = L->shore_foam, c.foam[1] = L->lap, c.foam[2] = L->stream_foam, c.foam[3] = L->ripple_foam;
    copy3(c.sky0, L->horizon), c.sky0[3] = L->clouds;
    copy3(c.sky1, L->zenith), c.sky1[3] = L->caustics;
    c.key[0] = L->key[0], c.key[1] = L->key[1], c.key[2] = L->key_soft, c.key[3] = L->detail;
    copy3(c.sunc, L->sun), c.sunc[3] = L->swell;
    copy3(c.abs, L->absorb), c.abs[3] = L->depth_scale;
    copy3(c.bed, L->bed), c.bed[3] = L->glint_bias;
    c.wp[0] = 0.98f;                  // g in tiles per s^2 (a tile is about 10 m)
    c.wp[1] = L->max_depth / 10.0f;   // the deepest water, tiles
    c.wp[2] = (float)spectrum(c.waves, wd, ws * L->wind_ms, L->chop, L->lmax);
    c.wp[3] = L->calm;
    for (int i = 0; i < W.nball; i++) c.ball[i][0] = W.ball[i][0], c.ball[i][1] = W.ball[i][1], c.ball[i][2] = W.ball[i][2];
    c.bp[0] = L->ball_size, c.bp[1] = L->ball_life, c.bp[2] = L->ball_drops, c.bp[3] = (float)W.nball;
    c.bq[0] = L->ball_foam, c.bq[1] = L->ball_ring, c.bq[2] = L->ball_drop_r;
    set_cb(&c);
    D3D11_VIEWPORT vp = {0, 0, rw, rh, 0, 1};
    ID3D11DeviceContext_RSSetViewports(W.ctx, 1, &vp);
    D3D11_RECT scr = {0, 0, (LONG)rw, (LONG)rh};
    if (clipped) {
      scr.left = (LONG)floorf(clip.x * sx), scr.top = (LONG)floorf(clip.y * sy);
      scr.right = (LONG)ceilf((clip.x + clip.w) * sx), scr.bottom = (LONG)ceilf((clip.y + clip.h) * sy);
    }
    ID3D11DeviceContext_RSSetScissorRects(W.ctx, 1, &scr);
    ID3D11DeviceContext_OMSetRenderTargets(W.ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_OMSetBlendState(W.ctx, W.b_water, NULL, 0xffffffff);
    UINT stride = kFloats * sizeof(float), off = 0;
    ID3D11DeviceContext_IASetVertexBuffers(W.ctx, 0, 1, &W.vb, &stride, &off);
    ID3D11DeviceContext_IASetInputLayout(W.ctx, W.il);
    ID3D11DeviceContext_IASetPrimitiveTopology(W.ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(W.ctx, W.vs_water, NULL, 0);
    ID3D11DeviceContext_PSSetShader(W.ctx, W.ps_water, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(W.ctx, 0, 1, &W.cb);
    ID3D11DeviceContext_PSSetConstantBuffers(W.ctx, 0, 1, &W.cb);
    ID3D11SamplerState *smp[3] = {W.s_wrap, W.s_clamp, W.s_point};
    ID3D11DeviceContext_PSSetSamplers(W.ctx, 0, 3, smp);
    ID3D11ShaderResourceView *srv[4] = {NULL, W.info_srv, W.sim_srv[W.cur], W.noise_srv};
    ID3D11DeviceContext_PSSetShaderResources(W.ctx, 1, 3, srv + 1);
    for (int i = 0; i < W.ndraws; i++) {
      WTex *tx = find_wtex(W.draws[i].serial);
      if (!tx || !tx->srv) continue;
      ID3D11DeviceContext_PSSetShaderResources(W.ctx, 0, 1, &tx->srv);
      ID3D11DeviceContext_Draw(W.ctx, W.draws[i].count, W.draws[i].first);
    }
    if (W.fall_count && W.ps_fall) {
      ID3D11DeviceContext_PSSetShader(W.ctx, W.ps_fall, NULL, 0);
      ID3D11DeviceContext_PSSetShaderResources(W.ctx, 0, 1, &W.noise_srv);
      ID3D11DeviceContext_Draw(W.ctx, W.fall_count, W.fall_first);
    }
    ID3D11ShaderResourceView *null_srv[4] = {0};
    ID3D11DeviceContext_PSSetShaderResources(W.ctx, 0, 4, null_srv);
    ID3D11RenderTargetView *null_rtv = NULL;
    ID3D11DeviceContext_OMSetRenderTargets(W.ctx, 1, &null_rtv, NULL);
  }
  ID3D11DeviceContext_End(W.ctx, (ID3D11Asynchronous *)W.q1[slot]);
  ID3D11DeviceContext_End(W.ctx, (ID3D11Asynchronous *)W.qd[slot]);
  W.qn++;
  stats(host_now_ms() - c0, (int)rw, (int)rh);
  ID3D11DeviceContext1_SwapDeviceContextState(W.ctx1, prev, NULL);
  REL(prev);
  REL(rtv);
  if (g_works <= 0) {
    InterlockedExchange(&g_works, 1);
    fprintf(stderr, "water: drawing (%d triangles in %d draws, %d of them waterfalls, target %dx%d)\n", W.ntri, W.ndraws,
            W.fall_count / 3, (int)rw, (int)rh);
  }
}

// ------------------------------------------------------------------ clip recording (look tests)

void water_after_frame(SDL_Renderer *r, int cw, int ch) {
  if (g_clip_req && !W.clip && (g_works > 0 || g_mode == 0)) {   // also with the original water: evidence clips of golf's own
    InterlockedExchange(&g_clip_req, 0);
    char cmd[MAX_PATH * 2];
    const char *ff = getenv("SIMGOLF_FFMPEG");
    snprintf(cmd, sizeof cmd,
             "\"\"%s\" -y -loglevel error -f rawvideo -pix_fmt bgra -s %dx%d -r %d -i - -c:v libx264 -preset slow -crf 16 -pix_fmt yuv420p \"%s\"\"",
             ff && *ff ? ff : "ffmpeg", cw, ch, (int)g_clip_fps, g_clip_path);
    W.clip = _popen(cmd, "wb");
    W.clip_n = 0;
    W.clip_total = g_clip_frames;
    W.clip_fps = g_clip_fps;
    W.clip_w = cw;
    W.clip_h = ch;
    W.clip_t0 = W.last_t;
    W.clip_wall0 = GetTickCount64();
    free(W.clip_px);
    W.clip_px = malloc((size_t)cw * ch * 4);
    fprintf(stderr, "water: recording %d frames at %d fps to %s%s\n", W.clip_total, W.clip_fps, g_clip_path, W.clip ? "" : " (ffmpeg FAILED)");
    return;   // the first frame on the recording's clock is the next one
  }
  if (!W.clip) return;
  if (cw != W.clip_w || ch != W.clip_h || SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, W.clip_px, cw * 4)) {
    fprintf(stderr, "water: recording stopped (size changed or read failed)\n");
    W.clip_total = W.clip_n;
  } else {
    fwrite(W.clip_px, 1, (size_t)cw * ch * 4, W.clip);
    W.clip_n++;
  }
  if (W.clip_n >= W.clip_total) {
    _pclose(W.clip);
    W.clip = NULL;
    fprintf(stderr, "water: recorded %d frames to %s in %llu ms\n", W.clip_n, g_clip_path, (unsigned long long)(GetTickCount64() - W.clip_wall0));
  }
}

void water_device_reset(void) {
  REL(W.vs_water);REL(W.vs_quad);REL(W.vs_imp);
  REL(W.ps_water);REL(W.ps_sim);REL(W.ps_imp);REL(W.ps_fall);
  REL(W.il);REL(W.cb);REL(W.vb);W.vb_cap=0;
  REL(W.s_wrap);REL(W.s_clamp);REL(W.s_point);
  REL(W.b_water);REL(W.b_add);REL(W.rs);REL(W.noise_srv);
  REL(W.info_srv);REL(W.info);
  for(int i=0;i<2;i++){REL(W.sim_srv[i]);REL(W.sim_rtv[i]);REL(W.sim[i]);}
  for(int i=0;i<4;i++){REL(W.qd[i]);REL(W.q0[i]);REL(W.q1[i]);}
  for(int i=0;i<W.ntex;i++){REL(W.tex[i].srv);REL(W.tex[i].tex);}
  W.ntex=0;
  REL(W.st);REL(W.ctx1);REL(W.ctx);REL(W.dev);
  W.state=0;W.scene_serial=W.map_serial=-1;W.sim_w=W.sim_h=0;
  W.qn=W.stat_n=0;W.gpu_sum=W.gpu_max=W.cpu_sum=W.cpu_max=0;
}
