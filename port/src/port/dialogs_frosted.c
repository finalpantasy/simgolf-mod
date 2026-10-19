// Frosted Mint dialogs, render side (simgolf mod, task-27), see dialogs_frosted.h.
// Native build only. Registers a UI layer with the presenter (hd_native.h) that
// draws the dialog from the latest snapshot the game thread published, takes
// the pointer while it is up, and queues what the player chose for
// dialogs_game.c.
#include "port/dialogs_frosted.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mods/mods.h"
#include "port/hd_native.h"
#include "port/host.h"
#include "port/runtime.h"

#define QMAX 16
#define STALE_MS 250   // the original published nothing this long: its loop has ended

static SDL_mutex *g_lock;
static DlgSnap g_snap;
static double g_snap_t = -1e9;
static volatile int g_ended = 1;
static DlgAction g_q[QMAX];
static int g_qn;
static volatile int g_visible;

static struct {
  uint32_t *px;
  int w, h;
  uint32_t serial;
} g_art[DLG_ART_MAX];
static uint32_t g_art_pub;
static SDL_Texture *g_tex[DLG_ART_MAX];
static uint32_t g_tex_serial[DLG_ART_MAX];
static Uint32 g_tex_retry_at[DLG_ART_MAX];  // next GPU-upload attempt after a transient failure

static UiCtx *g_ui;
static int g_registered, g_failed;
static volatile int g_want, g_game_asked;
static volatile double g_drawn_t = -1e9;
static volatile float g_view_x, g_view_y, g_view_px = 1;

static void lock(void) {
  if (g_lock) SDL_LockMutex(g_lock);
}
static void unlock(void) {
  if (g_lock) SDL_UnlockMutex(g_lock);
}

void dlgf_publish(const DlgSnap *s) {
  lock();
  g_snap = *s;
  g_snap_t = host_now_ms();
  g_ended = 0;
  unlock();
}

// Caller owns the publication lock, so face text and images change together.
static void set_art(int key,const uint32_t *argb,int w,int h) {
  free(g_art[key].px);g_art[key].px=NULL;g_art[key].w=g_art[key].h=0;
  if(argb && w>0 && h>0) {
    g_art[key].px=malloc((size_t)w*h*4);
    if(g_art[key].px){memcpy(g_art[key].px,argb,(size_t)w*h*4);g_art[key].w=w;g_art[key].h=h;}
  }
  g_art[key].serial=++g_art_pub;
}
void dlgf_publish_art(int key,const uint32_t *argb,int w,int h) {
  if(key<0 || key>=DLG_ART_MAX)return;
  lock();set_art(key,argb,w,h);unlock();
}
void dlgf_publish_faces(const DlgSnap *s,const DlgPixels *tiles,const DlgPixels *preview) {
  lock();
  if(tiles)for(int i=0;i<DLG_FACE_TILES;i++)set_art(DLG_ART_FACE_FIRST+i,tiles[i].px,tiles[i].w,tiles[i].h);
  if(preview)set_art(DLG_ART_PORTRAIT,preview->px,preview->w,preview->h);
  g_snap=*s;g_snap_t=host_now_ms();g_ended=0;
  unlock();
}

// draw() holds the publication lock across uploads and the matching snapshot.
static void update_art(SDL_Renderer *r) {
  Uint32 now = SDL_GetTicks();
  for (int i = 0; i < DLG_ART_MAX; i++) {
    if (g_art[i].serial == g_tex_serial[i]) continue;
    if (!g_art[i].px) {  // an intentional clear, not an upload failure
      if (g_tex[i]) SDL_DestroyTexture(g_tex[i]);
      g_tex[i] = NULL;
      g_tex_serial[i] = g_art[i].serial;
      g_tex_retry_at[i] = 0;
      continue;
    }
    if (g_tex_retry_at[i] && !SDL_TICKS_PASSED(now, g_tex_retry_at[i])) continue;
    SDL_Texture *next = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STATIC, g_art[i].w, g_art[i].h);
    if (!next || SDL_UpdateTexture(next, NULL, g_art[i].px, g_art[i].w * 4) != 0) {
      // Preserve the last valid image and retry rather than marking it loaded.
      // Throttle retries so low GPU memory cannot spin at monitor refresh rate.
      if (next) SDL_DestroyTexture(next);
      g_tex_retry_at[i] = now + 1000;
      continue;
    }
    SDL_SetTextureBlendMode(next, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(next, SDL_ScaleModeLinear);
    if (g_tex[i]) SDL_DestroyTexture(g_tex[i]);
    g_tex[i] = next;
    g_tex_serial[i] = g_art[i].serial;
    g_tex_retry_at[i] = 0;
  }
}

void dlgf_end(void) {
  lock();
  g_ended = 1;
  g_qn = 0;
  unlock();
}

int dlgf_take_action(DlgAction *a) {
  int ok = 0;
  lock();
  if (g_qn) {
    *a = g_q[0];
    memmove(g_q, g_q + 1, sizeof g_q[0] * (size_t)(--g_qn));
    ok = 1;
  }
  unlock();
  return ok;
}

int dlgf_gap(void);
int dlgf_active(void) { return g_want && g_registered && !g_ended && host_now_ms() - g_snap_t < STALE_MS; }

static void load_tokens(UiTokens *t) {
  char base[1024];
  snprintf(base, sizeof base, "%s", port_host_path("/ui/frosted_mint.toml"));
  static char paths[16][1024];
  const char *ov[16];
  int n = 0;
  for (int i = 0; i < mods_count() && n < 16; i++) {
    const char *dir = mods_dir(mods_id_at(i));
    if (!dir) continue;
    snprintf(paths[n], sizeof paths[n], "%s/ui/frosted_mint.toml", dir);
    FILE *f = fopen(paths[n], "rb");
    if (!f) continue;
    fclose(f);
    ov[n] = paths[n];
    n++;
  }
  ui_tokens_load_all(t, base, ov, n);
}

static float dlg_scale(int w, int h) {
  double s = mods_get_num("ui.scale", 0);
  float base = (s >= 0.5 && s <= 4) ? (float)s : ui_scale_for_height(h);
  float fit = fminf(w / 1100.0f, h / 700.0f);   // the biggest dialog (a long menu) still fits
  if (base > fit) base = fit;
  return base < 0.5f ? 0.5f : base;
}

static volatile int g_frame_gap;
int dlgf_gap(void) { return g_frame_gap; } // latch with this frame's layer draw, like reports

static void refresh_want(void);

static void draw(const HdUiFrame *f, void *user) {
  (void)user;
  g_drawn_t = f->now_ms;
  if (!g_game_asked) refresh_want();   // before the game thread's first dlgf_enabled()
  int expected = dlgf_active();
  g_frame_gap = expected;
  if (g_failed || !g_want) {
    g_visible = 0;
    if (g_ui) ui_skip_frame(g_ui);
    return;
  }
  SDL_Renderer *r = f->renderer;
  if (!g_ui) {
    UiTokens t;
    load_tokens(&t);
    char fonts[1024];
    snprintf(fonts, sizeof fonts, "%s", port_host_path("/fonts/plus-jakarta-sans"));
    g_ui = ui_create(r, &t, fonts); ui_debug_name(g_ui, "dialogs");
    if (!g_ui) {
      g_failed = 1;
      return;
    }
    fprintf(stderr, "ui: Frosted Mint dialogs layer on (fonts %s)\n", fonts);
  }
  int w = 0, h = 0;
  if (f->target) SDL_QueryTexture(f->target, NULL, NULL, &w, &h);
  else SDL_GetRendererOutputSize(r, &w, &h);
  g_view_x = (float)f->view.x;
  g_view_y = (float)f->view.y;
  g_view_px = f->px > 0 ? f->px : 1;
  int show = expected && w > 0;
  g_visible = show;
  g_frame_gap = expected && !show;
  if (!show) {
    ui_skip_frame(g_ui);
    return;
  }
  DlgSnap *s = malloc(sizeof *s);
  if (!s) {
    g_visible = 0;
    g_frame_gap = expected;
    ui_skip_frame(g_ui);
    return;
  }
  SDL_Texture *art[DLG_ART_MAX];
  lock();
  memcpy(s, &g_snap, sizeof *s);
  update_art(r);
  for(int i=0;i<DLG_ART_MAX;i++)
    art[i]=s->kind!=DLG_FACE || g_tex_serial[i]==g_art[i].serial?g_tex[i]:NULL;
  unlock();
  if (s->magic != DLG_MAGIC || s->kind == DLG_NONE) {
    g_visible = 0;
    g_frame_gap = expected;
    ui_skip_frame(g_ui);
    free(s);
    return;
  }
  ui_begin(g_ui, f->target, w, h, dlg_scale(w, h), f->target, host_now_ms());
  DlgAction acts[8];
  int n = ui_dialogs_draw(g_ui, s, art, acts, 8);
  ui_end(g_ui);
  free(s);
  if (n) {
    static int log_on = -1;
    if (log_on < 0) log_on = getenv("SIMGOLF_DIALOGS_LOG") != NULL;
    if (log_on)
      for (int i = 0; i < n; i++) fprintf(stderr, "dialogs: native action %d arg %d\n", acts[i].kind, acts[i].arg);
    lock();
    for (int i = 0; i < n && g_qn < QMAX; i++) g_q[g_qn++] = acts[i];
    unlock();
  }
}

// Game thread, before golf sees the event; mouse positions in canvas pixels.
static int input(SDL_Event *e, int course, void *user) {
  (void)user, (void)course;
  if (!g_ui || !g_want) return 0;
  UiEvent u = {0};
  int cx = 0, cy = 0;
  switch (e->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
      int vk = e->key.keysym.sym == SDLK_HOME ? 0x24 : e->key.keysym.sym == SDLK_END ? 0x23 :
               e->key.keysym.sym == SDLK_LEFT ? 0x25 : e->key.keysym.sym == SDLK_RIGHT ? 0x27 : 0;
      if (vk && dlgf_active()) {
        if (e->type == SDL_KEYDOWN) return dlg_entry_nav(vk, (e->key.keysym.mod & KMOD_SHIFT) != 0);
        if (g_snap.kind == DLG_ENTRY) return 1;
      }
      return 0;
    }
    case SDL_MOUSEMOTION:
      if (!e->motion.windowID) return 0;   // scripted input (harness, bridge, our own clicks) is the game's
      u.kind = UI_EV_MOVE;
      cx = e->motion.x, cy = e->motion.y;
      break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      if (!e->button.windowID) return 0;
      u.kind = e->type == SDL_MOUSEBUTTONDOWN ? UI_EV_DOWN : UI_EV_UP;
      u.button = e->button.button;
      cx = e->button.x, cy = e->button.y;
      break;
    case SDL_MOUSEWHEEL:
      if (!e->wheel.windowID) return 0;
      lock();
      if(g_snap.kind==DLG_FACE && !g_ended && g_visible) {
        if(e->wheel.y && g_qn<QMAX)g_q[g_qn++]=(DlgAction){DLG_A_FACE_PAGE,e->wheel.y<0?1:-1};
        unlock();return 1;
      }
      unlock();
      u.kind = UI_EV_WHEEL;
      u.wheel = (float)e->wheel.y;
      break;
    case SDL_WINDOWEVENT:
      if (e->window.event == SDL_WINDOWEVENT_LEAVE || e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) ui_input_reset(g_ui);
      return 0;
    default:
      return 0;   // keys stay the game's (typing, Esc, Enter, arrows)
  }
  if (u.kind != UI_EV_WHEEL) {
    cx = hd_raw_mouse_x;
    cy = hd_raw_mouse_y;
  }
  u.x = g_view_x + (cx + 0.5f) * g_view_px;
  u.y = g_view_y + (cy + 0.5f) * g_view_px;
  if (!g_visible) {
    ui_input_reset(g_ui);
    return 0;
  }
  ui_route_event(g_ui, &u);
  return 1;   // a dialog takes every real pointer event; the original sees only our injected clicks
}

// task-106: ui.dialogs / SIMGOLF_DIALOGS: whether the native screens are wanted at all
static void refresh_want(void) {
  const char *v = mods_get("ui.dialogs");
  g_want = v && (!strcasecmp(v, "frosted_mint") || !strcasecmp(v, "frosted-mint"));
  // SIMGOLF_DIALOGS=original: scripted tests that read the original dialogs keep them
  const char *e = getenv("SIMGOLF_DIALOGS");
  if (e && !strcasecmp(e, "original")) g_want = 0;
}

// The layer is registered when the presenter starts, before golf's first screen: the first one of a session is
// covered like every later one, not shown as the original's for the frames before the layer is up.
void dlgf_early(void) {
  if (!g_lock) g_lock = SDL_CreateMutex();
  if (g_registered) return;
  g_registered = hd_ui_add_layer(draw, NULL) && hd_ui_add_input(input, NULL);
  if (!g_registered) g_failed = 1;
}

int dlgf_enabled(void) {
  static unsigned seen = ~0u;
  unsigned ser = mods_serial();
  if (ser != seen) {
    seen = ser;
    refresh_want();
    g_game_asked = 1;
  }
  if (!g_lock) g_lock = SDL_CreateMutex();
  if (g_want && !g_registered) {
    g_registered = hd_ui_add_layer(draw, NULL) && hd_ui_add_input(input, NULL);
    if (!g_registered) g_failed = 1;
  }
  // only once the presenter has run the layer: with hd=0 nothing would draw the dialog
  return g_want && g_registered && !g_failed && host_now_ms() - g_drawn_t < 2000;
}

// Render thread: published CPU pictures survive a device reset.
void dlgf_device_reset(void) {
  for (int i=0;i<DLG_ART_MAX;i++) {
    if (g_tex[i]) SDL_DestroyTexture(g_tex[i]);
    g_tex[i]=NULL;
    g_tex_serial[i]=~0u;
    g_tex_retry_at[i]=0;
  }
}
