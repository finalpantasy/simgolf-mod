// Frosted Mint front end, render side (simgolf mod, task-32), see
// frontend_frosted.h. Native build only. Registers a UI layer with the
// presenter (hd_native.h) that paints the screen's background over the
// original picture, draws the screen (ui/ui_frontend.c) from the latest
// snapshot the game thread published, takes the pointer, and queues what the
// player clicked for frontend_game.c.
#include "port/frontend_frosted.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mods/mods.h"
#include "port/dialogs_frosted.h"
#include "port/hd_native.h"
#include "port/host.h"
#include "port/mods_frosted.h"
#include "port/options_frosted.h"
#include "port/runtime.h"
#include "port/update_check.h"
#include "port/ui/loading_progress.h"

#ifndef SIMGOLF_VERSION
#define SIMGOLF_VERSION "dev"
#endif

#define QMAX 16
#define LOADING_STALE_MS 15000
#define BOOT_MS 30000   // the cover for golf's startup, give up after this
#define STALE_MS 400   // the original screen published nothing this long (a dialog runs its own loop): hide

static SDL_mutex *g_lock;
static FeSnap g_snap;
static double g_snap_t = -1e9;
static FeAction g_q[QMAX];
static int g_qn;
static volatile int g_injecting, g_visible;
static struct {
  uint32_t *px;
  int w, h;
  uint32_t serial;
} g_art[FE_ART_MAX];
static uint32_t g_art_pub;

// task-80: the Updates panel and the "is out" notice (presenter side; the screen reads and changes it)
static UpdState g_upd;
static int g_upd_init;

static void upd_init(void) {
  g_upd_init = 1;
  g_upd.expanded = -1;
  char path[1024], err[160] = "";
  snprintf(path, sizeof path, "%s", port_host_path("/ui/updates.json"));
  if (!upd_load_news(&g_upd, path, err, sizeof err)) fprintf(stderr, "updates: no release list (%s)\n", err);
  // the dot: this build is newer than the last one the player opened the panel on (a first start counts as seen)
  const char *seen = mods_get("updates.seen");
  if (!seen || !*seen) mods_option_set("updates.seen", SIMGOLF_VERSION);
  else g_upd.show_dot = upd_version_cmp(seen, SIMGOLF_VERSION) > 0;
  fprintf(stderr, "updates: %d releases, this build %s, last opened on %s, new dot %s\n", g_upd.n_rel, SIMGOLF_VERSION,
          seen && *seen ? seen : "(first start)", g_upd.show_dot ? "on" : "off");
  upd_check_start(&g_upd);
}

// render thread
static UiCtx *g_ui;
static SDL_Texture *g_tex[FE_ART_MAX];
static uint32_t g_tex_serial[FE_ART_MAX];
static Uint32 g_tex_retry_at[FE_ART_MAX];  // next GPU-upload attempt after a transient failure
static int g_registered, g_failed;
static volatile int g_want, g_game_asked;
static volatile double g_action_t = -1e9;   // when the player last chose something on a native screen
static volatile double g_press_t = -1e9;    // when the game thread pressed the original's button for it

static volatile int g_cover_screen = -1;
int fef_cover_screen(void) { return g_cover_screen; }

void fef_note_press(void) { g_press_t = host_now_ms(); }
static volatile double g_drawn_t = -1e9;
static volatile float g_view_x, g_view_y, g_view_px = 1;

static void lock(void) {
  if (g_lock) SDL_LockMutex(g_lock);
}
static void unlock(void) {
  if (g_lock) SDL_UnlockMutex(g_lock);
}

// ------------------------------------------------------------------ game thread API

static volatile long g_release_at;
void fef_release_cover(void) { g_release_at = hd_native_game_frames() + 3; }

void fef_publish(const FeSnap *s) {
  g_release_at = 0;
  lock();
  g_snap = *s;
  g_snap_t = host_now_ms();
  unlock();
}

void fef_loading_progress(float pct) {
  if (!mods_get_int("ui.smooth_loading", 1)) return;
  if (pct > 99) pct = 99;
  lock();
  if (g_snap.screen == FE_LOADING && g_snap.loading.pct >= 0 && pct > g_snap.loading.pct) {
    g_snap.loading.pct = pct;
    g_snap_t = host_now_ms();
  }
  unlock();
}

int fef_take_action(FeAction *a) {
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

void fef_publish_art(int key, const uint32_t *argb, int w, int h) {
  if (key < 0 || key >= FE_ART_MAX) return;
  lock();
  free(g_art[key].px);
  g_art[key].px = NULL;
  g_art[key].w = g_art[key].h = 0;
  if (argb && w > 0 && h > 0) {
    g_art[key].px = malloc((size_t)w * h * 4);
    if (g_art[key].px) {
      memcpy(g_art[key].px, argb, (size_t)w * h * 4);
      g_art[key].w = w;
      g_art[key].h = h;
    }
  }
  g_art[key].serial = ++g_art_pub;
  unlock();
}

void fef_set_injecting(int on) { g_injecting = on; }

// ------------------------------------------------------------------ render thread

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

static void update_art(SDL_Renderer *r) {
  lock();
  Uint32 now = SDL_GetTicks();
  for (int i = 0; i < FE_ART_MAX; i++) {
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
  unlock();
}

static float front_scale(int w, int h) {
  double s = mods_get_num("ui.scale", 0);
  float base = (s >= 0.5 && s <= 4) ? (float)s : ui_scale_for_height(h);
  float fit = fminf(w / 1920.0f, h / 1080.0f);   // the 1920 x 1080 design always fits
  if (base > fit) base = fit;
  return base < 0.3f ? 0.3f : base;
}

static void refresh_want(void);

static void draw(const HdUiFrame *f, void *user) {
  (void)user;
  g_drawn_t = f->now_ms;
  if (!g_game_asked) refresh_want();   // before the game thread's first fef_enabled()
  g_cover_screen = -1;
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
    g_ui = ui_create(r, &t, fonts); ui_debug_name(g_ui, "frontend");
    if (!g_ui) {
      g_failed = 1;
      return;
    }
    fprintf(stderr, "ui: Frosted Mint front end layer on (fonts %s)\n", fonts);
  }
  int w = 0, h = 0;
  if (f->target) SDL_QueryTexture(f->target, NULL, NULL, &w, &h);
  else SDL_GetRendererOutputSize(r, &w, &h);
  g_view_x = (float)f->view.x;
  g_view_y = (float)f->view.y;
  g_view_px = f->px > 0 ? f->px : 1;
  if (!g_upd_init) upd_init();
  FeSnap *s = malloc(sizeof *s);
  if (!s) return;
  double t0;
  lock();
  memcpy(s, &g_snap, sizeof *s);
  t0 = g_snap_t;
  unlock();
  s->upd = &g_upd;
  // A click was sent to the original and it has not drawn a screen since: it left its loop to build or load
  // a course. The last screen stays (blocked, with a loading pill) instead of the original's loading picture,
  // until the course view shows or the original publishes a screen again (a dialog, the next screen).
  // (the original keeps drawing for a frame or two after the press, so "pending" is: a choice was pressed and
  // the original has not published a screen more than 120 ms after that press)
  double now = host_now_ms();
  int valid = s->magic == FE_MAGIC && s->screen != FE_NONE && w > 0;
  // task-106: from the first presented frame until golf publishes its first screen, our own loading card
  // stands in for whatever the original draws (black frames, its loading picture, its title)
  static int seen_snap;
  static double boot_t0 = -1;
  int boot = 0;
  if (valid) seen_snap = 1;
  if (!valid && !seen_snap && w > 0 && !f->course) {
    const char *lv = mods_get("ui.loading");
    if (!(lv && !strcasecmp(lv, "original"))) {
      if (boot_t0 < 0) boot_t0 = now;
      if (now - boot_t0 < BOOT_MS) {
        boot = 1;
        memset(s, 0, sizeof *s);
        s->magic = FE_MAGIC;
        s->screen = FE_LOADING;
        s->loading.pct = -1;
        snprintf(s->loading.title, sizeof s->loading.title, "Starting SimGolf");
        valid = 1;
      }
    }
  }
  double press = g_press_t;
  int pending = valid && !dlgf_active() && g_action_t > 0 && press > g_action_t && t0 <= press + 120 && now - press < 60000;
  // task-69: golf's loading calls are seconds apart; task-27: a native dialog over the screen (delete a save, a pro's menu) keeps it up
  // task-73: only a dialog that opened over a fresh screen keeps it up (a screen left from the title, a game later, must not
  // come back behind the menus and the golfer profile's name strips)
  static double dlg_start = -1;
  int dlg_now = dlgf_active();
  if (dlg_now && dlg_start < 0) dlg_start = now;
  if (!dlg_now) dlg_start = -1;
  int dlg_over_screen = dlg_now && dlg_start - t0 < 1500;
  int stale = !boot && now - t0 >= (s->screen == FE_LOADING ? LOADING_STALE_MS : STALE_MS) && !dlg_over_screen;
  // in a running course (task-27: the in-game Load game and world map) only a screen that is up now shows;
  // from the title the last screen stays up while the original builds the course (pending)
  int show = valid && (!stale || (pending && !f->course));
  // task-106: Quit: the loading card golf raised for the exit gives way to the closing credits once they are on screen
  if (show && g_release_at && hd_native_game_frames() >= g_release_at) show = 0;
  // task-106: a loading card that went up while no course was shown (title, new game, load, championship) ends when
  // the course view comes (a card over a course already shown, an in-game load, is not touched)
  {
    static uint32_t ld_serial;
    static int ld_nc;
    if (s->screen == FE_LOADING && show) {
      if (s->serial != ld_serial) {
        ld_serial = s->serial;
        ld_nc = 0;
      }
      if (!f->course) ld_nc = 1;
      else if (ld_nc) show = 0;
    }
  }
  int loading = show && pending && now - press > 350;
  float progress_target = s->loading.pct;
  static LoadingProgress progress;
  if (show && s->screen == FE_LOADING && mods_get_int("ui.smooth_loading", 1)) {
    s->loading.pct = loading_progress_step(&progress, s->loading.epoch, s->loading.pct, now);
  } else progress.active = 0;
  // Opt-in motion/timing evidence, no screenshots or waits in normal play.
  static int trace = -1;
  if (trace < 0) trace = getenv("SIMGOLF_LOADING_TRACE") != NULL;
  if (trace) fprintf(stderr, "loading-frame %.3f %d %u %.3f %.3f %d\n", now,
                     show ? s->screen : -1, s->loading.epoch,
                     progress.active ? (float)progress.value : s->loading.pct,
                     progress_target, f->course);
  g_visible = show;
  if (!show) {
    ui_skip_frame(g_ui);
    free(s);
    return;
  }
  g_cover_screen = s->screen;
  update_art(r);
  ui_frontend_background(r, ui_tokens(g_ui), w, h);
  ui_begin(g_ui, f->target, w, h, front_scale(w, h), f->target, now);
  FeAction acts[8];
  int n = ui_frontend_draw(g_ui, s, g_tex, acts, 8);
  if (loading) {
    n = 0;   // nothing can be clicked while the original is busy
    ui_frontend_loading(g_ui, "Loading");
  }
  ui_end(g_ui);
  {   // SIMGOLF_FRONTEND_LOG=1: where the pieces are, in target pixels (tests click them), once a second
    static int log_on = -1;
    static double last;
    if (log_on < 0) log_on = getenv("SIMGOLF_FRONTEND_LOG") != NULL;
    if (log_on && f->now_ms - last > 1000) {
      last = f->now_ms;
      const FeLayout *L = ui_frontend_layout();
      char line[1500];
      int o = snprintf(line, sizeof line, "frontend: layout screen %d target %dx%d", s->screen, w, h);
      if (s->screen == FE_WORLD) {
        o += snprintf(line + o, sizeof line - (size_t)o, " selected=%d state=%d tags=", L->world_selected, L->world_state);
        for (int i = 0; i < FE_TAGS; i++)
          o += snprintf(line + o, sizeof line - (size_t)o, "%s%d", i ? "," : "", L->row_tag[i]);
      }
#define PUT(r, ...) \
  if ((r).w > 0) { \
    o += snprintf(line + o, sizeof line - (size_t)o, " "); \
    o += snprintf(line + o, sizeof line - (size_t)o, __VA_ARGS__); \
    o += snprintf(line + o, sizeof line - (size_t)o, " %.0f,%.0f", (r).x + (r).w / 2, (r).y + (r).h / 2); \
  }
      for (int i = 0; i < 8; i++) PUT(L->btn[i], "btn%d", i);
      PUT(L->back, "back");
      PUT(L->upd_btn, "upd");   // task-80
      PUT(L->upd_now, "updnow");
      PUT(L->upd_later, "updlater");
      PUT(L->upd_close, "updclose");
      for (int i = 0; i < 8; i++) PUT(L->upd_row[i], "updrow%d", i);
      PUT(L->ok, "ok");
      PUT(L->del, "del");
      for (int i = 0; i < FE_ROWS + FE_TAGS && o < 1300; i++) PUT(L->row[i], "row%d", i);
#undef PUT
      fprintf(stderr, "%s\n", line);
    }
  }
  free(s);
  for (int i = 0; i < n; i++)   // the Options, Mods and Updates pills are the presenter's own: tell the original nothing
    if (acts[i].kind == FE_A_OPTIONS || acts[i].kind == FE_A_MODS || acts[i].kind == FE_A_UPDATES) {
      if (acts[i].kind == FE_A_MODS) modsp_open();
      else if (acts[i].kind == FE_A_OPTIONS) opt_open();
      else if (acts[i].arg == 0) {   // the panel was opened: this build's news is seen
        g_upd.show_dot = 0;
        mods_option_set("updates.seen", SIMGOLF_VERSION);
        fprintf(stderr, "updates: panel opened\n");
      } else if (acts[i].arg == 1) {   // Later: hidden until the next version
        mods_option_set("updates.later", g_upd.latest);
        g_upd.avail = 0;
        fprintf(stderr, "updates: Later pressed for %s\n", g_upd.latest);
      } else if (acts[i].arg == 2) {
        fprintf(stderr, "updates: Update now pressed for %s\n", g_upd.latest);
        upd_install_start(&g_upd);
      }
      memmove(&acts[i], &acts[i + 1], sizeof acts[0] * (size_t)(n - i - 1));
      n--;
      i--;
    }
  if (n) {
    static int log_on = -1;
    if (log_on < 0) log_on = getenv("SIMGOLF_FRONTEND_LOG") != NULL;
    if (log_on)
      for (int i = 0; i < n; i++) fprintf(stderr, "frontend: native action %d arg %d\n", acts[i].kind, acts[i].arg);
    for (int i = 0; i < n; i++)   // a choice (not a row, hover or scroll): the original may leave its screen for a while
      if (acts[i].kind == FE_A_TAG || acts[i].kind == FE_A_OK || acts[i].kind == FE_A_DIFFICULTY ||
          (acts[i].kind == FE_A_TITLE && acts[i].arg != 5) || acts[i].kind == FE_A_BACK)   // (Quit shows golf's credits)
        g_action_t = now;
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
      u.kind = UI_EV_WHEEL;
      u.wheel = (float)e->wheel.y;
      break;
    case SDL_WINDOWEVENT:
      if (e->window.event == SDL_WINDOWEVENT_LEAVE || e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) ui_input_reset(g_ui);
      return 0;
    case SDL_KEYDOWN:
      if (e->key.keysym.sym == SDLK_ESCAPE && g_upd.open) {   // the Updates panel takes Esc first
        g_upd.close_req = 1;
        return 1;
      }
      return 0;
    default:
      return 0;   // keys stay the game's (Esc goes back, as in the original)
  }
  if (u.kind != UI_EV_WHEEL) {   // the picture is 800x600 in the middle of the window: use the unclamped position
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
  return 1;   // a front-end screen takes every real pointer event; the original sees only our injected clicks
}

// ------------------------------------------------------------------ switch

int fef_loading_enabled(void) {
  if (!fef_enabled()) return 0;
  const char *v = mods_get("ui.loading");
  return !(v && !strcasecmp(v, "original"));
}

// ui.frontend / SIMGOLF_FRONTEND: whether the new front end is wanted at all
static void refresh_want(void) {
  const char *v = mods_get("ui.frontend");
  g_want = v && (!strcasecmp(v, "frosted_mint") || !strcasecmp(v, "frosted-mint"));
  // SIMGOLF_FRONTEND=original: scripted tests (tools/ghost_run.ps1 sets it) that drive the original
  // title and menus by window clicks keep the original screens
  const char *e = getenv("SIMGOLF_FRONTEND");
  if (e && !strcasecmp(e, "original")) g_want = 0;
}

// task-106: the layer is registered when the presenter starts, before golf draws its first picture, so
// not one frame (the black first frames, the original's loading screen, the original title) goes out
// uncovered while the game thread has not yet asked fef_enabled().
void fef_early(void) {
  if (!g_lock) g_lock = SDL_CreateMutex();
  if (g_registered) return;
  g_registered = hd_ui_add_layer(draw, NULL) && hd_ui_add_input(input, NULL);
  if (!g_registered) g_failed = 1;
}

int fef_enabled(void) {
  static unsigned seen = ~0u;
  unsigned ser = mods_serial();
  if (ser != seen) {
    seen = ser;
    refresh_want();
    g_game_asked = 1;
  }
  if (g_want) fef_early();   // registered once; with ui.frontend switched off later the layer draws nothing
  // Only once the presenter has run the layer: with hd=0 (the original 800x600
  // window) nothing would draw the new screens, so the original ones stay.
  return g_want && g_registered && !g_failed && host_now_ms() - g_drawn_t < 2000;
}

// Render thread: published CPU pictures survive a device reset.
void fef_device_reset(void) {
  for (int i=0;i<FE_ART_MAX;i++) {
    if (g_tex[i]) SDL_DestroyTexture(g_tex[i]);
    g_tex[i]=NULL;
    g_tex_serial[i]=~0u;
    g_tex_retry_at[i]=0;
  }
}
