// Frosted Mint HUD, render side (simgolf mod, task-30), see hud_frosted.h.
// Native build only. Registers a UI layer with the presenter (hd_native.h),
// draws the HUD screen (ui/ui_hud.c) from the latest snapshot the game thread
// published, routes the pointer (UI first, the rest to the game), and queues
// what the player clicked for hud_game.c.
#include "port/hud_frosted.h"
#include "port/course_lamps_native.h"
#include "port/path_style_catalog.h"
#include "port/hud_tuck_policy.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mods/mods.h"
#include "port/dialogs_frosted.h"
#include "port/reports_frosted.h"
#include "port/paths_frosted.h"
#include "port/golfers_frosted.h"
#include "port/hd_native.h"
#include "port/hd_art.h"
#include "port/options_frosted.h"
#include "port/host.h"
#include "port/runtime.h"
#include "port/ui/ui_hud.h"

#define QMAX 32
#define STALE_MS 300   // no course frame this long (a dialog runs its own loop): hide

enum { HUD_MODE_OFF, HUD_MODE_FROSTED, HUD_MODE_ORIGINAL_ROTATE };

static SDL_mutex *g_lock;
static HudSnap g_snap;
static int g_weather_show, g_weather_kind, g_weather_night;
static int g_weather_air_valid,g_weather_fahrenheit;
static double g_weather_c,g_weather_rh;
static double g_snap_t = -1e9;
static HudAction g_q[QMAX];
static int g_qn;
static volatile int g_injecting, g_visible;
static SDL_atomic_t g_mode;              // game thread publishes; render/input read
static SDL_atomic_t g_history_visible;   // published by the render thread after drawing the HUD

int hudf_history_visible(void) { return SDL_AtomicGet(&g_history_visible); }
static struct {
  uint32_t *px;
  int w, h;
  uint32_t serial;
} g_art[HUD_ART_MAX];
static uint32_t g_art_pub;

// render thread
static UiCtx *g_ui;
static UiHud g_hud = UI_HUD_INIT;
static volatile int g_esc;    // Esc pressed on the course (task-112): the draw decides what it does
static volatile int g_dock_mode = -1;   // the game's dock mode at the last drawn frame (input reads it)
static volatile int g_tuck;   // a click on the course or Esc: the tray tucks away (jwe: the build bar folds up)
static volatile int g_toggle_hidden;   // the hide key was pressed (task-57)
static double g_eat_until;             // swallow the text event that follows the hide key until then
static int g_eat_char;
static SDL_Texture *g_tex[HUD_ART_MAX];
static uint32_t g_tex_serial[HUD_ART_MAX];
static Uint32 g_tex_retry_at[HUD_ART_MAX];  // next GPU-upload attempt after a transient failure
static int g_registered, g_failed;

static void lock(void) {
  if (g_lock) SDL_LockMutex(g_lock);
}
static void unlock(void) {
  if (g_lock) SDL_UnlockMutex(g_lock);
}

static void refresh_mode(void) {
  static unsigned seen = ~0u;
  unsigned ser = mods_serial();
  if (ser == seen) return;
  seen = ser;
  const char *v = mods_get("ui.hud");
  int mode = HUD_MODE_OFF;
  if (v && (!strcasecmp(v, "frosted_mint") || !strcasecmp(v, "frosted-mint")))
    mode = HUD_MODE_FROSTED;
  else if (v && !strcasecmp(v, "original") && mods_get_int("ui.original_rotate_button", 1))
    mode = HUD_MODE_ORIGINAL_ROTATE;
  SDL_AtomicSet(&g_mode, mode);
}

// ------------------------------------------------------------------ game thread API

void hudf_publish(const HudSnap *s) {
  lock();
  g_snap = *s;
  g_snap.weather_report=g_weather_show;
  g_snap.weather_kind=g_weather_kind;
  g_snap.weather_night=g_weather_night;
  g_snap.weather_air_valid=g_weather_air_valid;g_snap.weather_fahrenheit=g_weather_fahrenheit;
  g_snap.weather_c=g_weather_c;g_snap.weather_rh=g_weather_rh;
  g_snap_t = host_now_ms();
  unlock();
}

void hudf_publish_weather(int show,int kind,int night,int air_valid,double c,double rh,int fahrenheit) {
  lock();
  g_snap.weather_report=g_weather_show=show!=0;
  g_snap.weather_kind=g_weather_kind=kind>=0&&kind<=5?kind:0;
  g_snap.weather_night=g_weather_night=night!=0;
  g_snap.weather_air_valid=g_weather_air_valid=air_valid!=0;
  g_snap.weather_fahrenheit=g_weather_fahrenheit=fahrenheit!=0;
  g_snap.weather_c=g_weather_c=c;g_snap.weather_rh=g_weather_rh=rh;
  unlock();
}

int hudf_take_action(HudAction *a) {
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

void hudf_publish_art(int slot, const uint32_t *argb, int w, int h) {
  if (slot < 0 || slot >= HUD_ART_MAX) return;
  lock();
  free(g_art[slot].px);
  g_art[slot].px = NULL;
  g_art[slot].w = g_art[slot].h = 0;
  if (argb && w > 0 && h > 0) {
    g_art[slot].px = malloc((size_t)w * h * 4);
    if (g_art[slot].px) {
      memcpy(g_art[slot].px, argb, (size_t)w * h * 4);
      g_art[slot].w = w;
      g_art[slot].h = h;
    }
  }
  g_art[slot].serial = ++g_art_pub;
  unlock();
  // SIMGOLF_UI_DUMP_ART=dir: also write art_<slot>.bmp (32-bit with alpha) for
  // the kit gallery (ui_gallery --art dir), so its pages show the game's tiles
  const char *dir = getenv("SIMGOLF_UI_DUMP_ART");
  if (dir && *dir && argb && w > 0 && h > 0) {
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormatFrom((void *)argb, w, h, 32, w * 4, SDL_PIXELFORMAT_ARGB8888);
    char path[1024];
    snprintf(path, sizeof path, "%s/art_%d.bmp", dir, slot);
    if (sf) SDL_SaveBMP(sf, path);
    SDL_FreeSurface(sf);
  }
}

void hudf_set_injecting(int on) { g_injecting = on; }

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
  for (int i = 0; i < HUD_ART_MAX; i++) {
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

// ui.hud_show=0: no HUD (photo mode, clean course pictures)
static int hud_shown(void) {
  static unsigned seen = ~0u;
  static int on = 1;
  unsigned ser = mods_serial();
  if (ser != seen) {
    seen = ser;
    on = mods_get_int("ui.hud_show", 1) != 0;
  }
  return on;
}

// ui.hud_density: jwe (default, the maintainer's pick R5, set by the ui_hud mod), regular, compact, minimal, ...
static int hud_density(void) {
  // Keep the selected Corners layout even when an older options file names an alternative.
  // The legacy parser is retained only for explicit compatibility comparisons.
  if (mods_get_int("fixes.hud_layout", 1)) return HUD_JWE;
  static unsigned seen = ~0u;
  static int d;
  unsigned ser = mods_serial();
  if (ser != seen) {
    seen = ser;
    const char *v = mods_get("ui.hud_density");
    static const char *const names[] = {"regular", "compact", "minimal", "compact_column", "wheel", "fan", "wheel_stats",
                                        "card", "quarter", "lshape", "collapsed", "stock", "card_right",
                                        "bars", "topbar", "ribbon", "bottombar", "jwe"};
    d = v ? 0 : HUD_JWE;   // not set at all: the default layout; an unknown name: regular
    for (int i = 0; v && i < 18; i++)
      if (!strcasecmp(v, names[i])) d = i;
  }
  return d;
}

// SIMGOLF_UI_COVERAGE=1: golf.log gets the share of the screen the HUD covers
// whenever it changes (the density comparison for the maintainer).
static void log_coverage(const HudSnap *s) {
  static int on = -1, frames;
  static float last = -1;
  if (on < 0) on = getenv("SIMGOLF_UI_COVERAGE") != NULL;
  if (!on || ++frames % 15) return;
  float cov = ui_coverage(g_ui, 4);
  if (cov < last - 0.0004f || cov > last + 0.0004f) {
    static const char *const names[18] = {"regular", "compact", "minimal", "compact_column", "wheel", "fan", "wheel_stats",
                                          "card", "quarter", "lshape", "collapsed", "stock", "card_right",
                                          "bars", "topbar", "ribbon", "bottombar", "jwe"};
    int tray = s->dock_mode >= 0 && s->dock_mode <= 4 && !g_hud.tray_hidden && (g_hud.density != HUD_JWE || g_hud.bar_open);
    const char *bar = g_hud.hidden ? "hidden" : g_hud.density != HUD_JWE ? "bar" : g_hud.bar_open ? "bar open" : "bar collapsed";
    fprintf(stderr, "ui: coverage %.1f%% (density %s, %s, tray %s)\n", cov * 100, names[g_hud.density], bar,
            tray ? "open" : "closed");
    last = cov;
  }
}

// ui.hud_hide_key (task-57): the key that hides and shows the whole HUD, an SDL key name.
// Default "u" (H is the game's own "open the hole" key); "none" turns it off.
static SDL_Keycode hide_key(char *name, size_t cap) {
  static unsigned seen = ~0u;
  static SDL_Keycode key = SDLK_u;
  static char nm[12] = "U";
  unsigned ser = mods_serial();
  if (ser != seen) {
    seen = ser;
    const char *v = mods_get("ui.hud_hide_key");
    key = SDLK_u;
    snprintf(nm, sizeof nm, "U");
    if (v && *v) {
      if (!strcasecmp(v, "none") || !strcasecmp(v, "off")) {
        key = SDLK_UNKNOWN;
        nm[0] = 0;
      } else {
        SDL_Keycode k = SDL_GetKeyFromName(v);
        if (k != SDLK_UNKNOWN) {
          key = k;
          snprintf(nm, sizeof nm, "%s", v);
          if (nm[1] == 0 && nm[0] >= 'a' && nm[0] <= 'z') nm[0] = (char)(nm[0] - 32);
        }
      }
    }
  }
  if (name) snprintf(name, cap, "%s", nm);
  return key;
}

static float hud_scale(int h) {
  double s = mods_get_num("ui.scale", 0);
  if (s >= 0.5 && s <= 4) return (float)s;
  return ui_scale_for_height(h);
}

// The canvas -> target mapping of the last frame drawn (for the pointer).
static volatile float g_view_x, g_view_y, g_view_px = 1;
static volatile double g_drawn_t = -1e9;   // the presenter ran the layer (it is up)
static void draw(const HdUiFrame *f, void *user) {
  (void)user;
  g_drawn_t = f->now_ms;
  int mode = SDL_AtomicGet(&g_mode);
  if (g_failed || mode == HUD_MODE_OFF) {
    g_visible = 0;
    SDL_AtomicSet(&g_history_visible, 0);
    if (g_ui) ui_skip_frame(g_ui);
    return;
  }
  SDL_Renderer *r = f->renderer;
  if (!g_ui) {
    UiTokens t;
    load_tokens(&t);
    char fonts[1024];
    snprintf(fonts, sizeof fonts, "%s", port_host_path("/fonts/plus-jakarta-sans"));
    g_ui = ui_create(r, &t, fonts); ui_debug_name(g_ui, "hud");
    if (!g_ui) {
      SDL_AtomicSet(&g_history_visible, 0);
      g_failed = 1;
      return;
    }
    fprintf(stderr, "ui: Frosted Mint HUD layer on (fonts %s)\n", fonts);
  }
  int w = 0, h = 0;
  if (f->target) SDL_QueryTexture(f->target, NULL, NULL, &w, &h);
  else SDL_GetRendererOutputSize(r, &w, &h);
  g_view_x = (float)f->view.x;
  g_view_y = (float)f->view.y;
  g_view_px = f->px > 0 ? f->px : 1;
  HudSnap s;
  double t;
  lock();
  s = g_snap;
  t = g_snap_t;
  unlock();
  double now = host_now_ms();
  // ui.hud_show=0: no HUD at all, old or new (clean course pictures; keys still work)
  int show = f->course && s.valid && s.visible && now - t < STALE_MS && w > 0 && hud_shown();
  g_visible = show;
  if (!show) {
    SDL_AtomicSet(&g_history_visible, 0);
    ui_skip_frame(g_ui);
    return;
  }
  if (mode == HUD_MODE_ORIGINAL_ROTATE) {
    SDL_AtomicSet(&g_history_visible, 0);
    ui_begin(g_ui, f->target, w, h, hud_scale(h), f->target, now);
    HudAction rotate[1];
    int n = ui_hud_draw_original_rotate(g_ui, &s, rotate, 1);
    ui_end(g_ui);
    if (n) {
      lock();
      if (g_qn < QMAX) g_q[g_qn++] = rotate[0];
      unlock();
    }
    return;
  }
  update_art(r);
  g_hud.original_bottom = mods_get_int("ui.original_bottom", 1) != 0;
  g_hud.density = g_hud.original_bottom ? HUD_JWE : hud_density();
  hide_key(g_hud.hide_key, sizeof g_hud.hide_key);
  if (g_toggle_hidden) {
    g_toggle_hidden = 0;
    g_hud.hidden = !g_hud.hidden;
  }
  g_dock_mode = s.dock_mode;
  if (g_hud.original_bottom) g_esc = 0;
  if (g_esc) {   // Esc: close what is open, cancel the tool, and only then the Menu (ui_hud_escape)
    g_esc = 0;
    HudAction ea[2];
    const char *what = "";
    int en = ui_hud_escape(&g_hud, &s, ea, 2, &what);
    static double close_until;   // a close click is on its way: another Esc must not send a second one (it would toggle the tray back)
    if (en && (!strcmp(what, "tool") || !strcmp(what, "close"))) {
      if (now < close_until) en = 0, what = "wait";
      else close_until = now + 1500;
    }
    fprintf(stderr, "hud: esc %s\n", what);
    if (en) {
      lock();
      for (int i = 0; i < en && g_qn < QMAX; i++) g_q[g_qn++] = ea[i];
      unlock();
    }
  }
  if (g_tuck) {
    g_tuck = 0;
    if (g_hud.density) g_hud.tray_hidden = 1;
    if (g_hud.density == HUD_JWE) {
      g_hud.bar_open = 0;   // the build bar folds back to its button
      g_hud.msg_open = 0;   // a message read in full and the history close too (task-62)
      g_hud.hist_open = 0;
    }
  }
  ui_begin(g_ui, f->target, w, h, hud_scale(h), f->target, now);
  s.paths_available = pvf_view_available();
  s.paths_on = pvf_view_user();
  HudAction acts[16];
  // Reports draw before the HUD layer. Hide only the transient message card while a report owns this
  // frame (including a handoff gap); use copies so its open state and unread message return on close.
  UiHud report_hud;
  HudSnap report_snap;
  UiHud *draw_hud = &g_hud;
  HudSnap *draw_snap = &s;
  if (rpf_visible() || rpf_gap()) {
    report_hud = g_hud;
    report_hud.msg_open = 0;
    report_hud.hist_open = 0;
    report_snap = s;
    report_snap.news_on = 0;
    draw_hud = &report_hud;
    draw_snap = &report_snap;
  }
  int history_before = draw_hud->hist_open;
  SDL_Texture *pictures[HUD_ART_MAX];
  memcpy(pictures, g_tex, sizeof pictures);
  // The original amenity strip contains cropped interface symbols. Use the
  // complete object designs for the five modern Extras picture buttons.
  if (mods_get_int("fixes.extras_pictures", 1)) {
    static const int amenity[] = {1, 2, 4, 6, 7};
    static const int design[] = {HUD_ART_BENCH, HUD_ART_FLOWER, HUD_ART_LANDMARK, HUD_ART_TREE, HUD_ART_BRIDGE};
    for (int i = 0; i < 5; i++) {
      SDL_Texture *pic = g_tex[design[i]];
      if (draw_snap->amenities_on && !draw_hud->tray_hidden && mods_get_int("fixes.hd_design_previews", 1)) {
        SDL_Texture *hd = hd_art_preview_texture(design[i], r);
        if (hd) pic = hd;
      }
      pictures[HUD_ART_AMENITY + amenity[i]] = pic;
    }
  }
  if (mods_get_int("fixes.hd_design_previews", 1) && draw_snap->design_n > 0) {
    int tool = draw_snap->build_tool;
    int slot = tool == 1 ? HUD_ART_BENCH : tool == 2 ? HUD_ART_FLOWER :
               tool == 4 ? HUD_ART_LANDMARK : tool == 16 ? HUD_ART_TREE :
               tool == 19 ? HUD_ART_BRIDGE : -1;
    for (int i = 0; slot >= 0 && i < draw_snap->design_n && i < 16 && slot + i < HUD_ART_MAX; i++) {
      SDL_Texture *hd = hd_art_preview_texture(slot + i, r);
      if (hd) pictures[slot + i] = hd;
    }
  }
  if(draw_snap->build_tool==0&&draw_snap->design_n>0) {
    static SDL_Texture *path_pictures[PATH_STYLE_COUNT];
    for(int i=0;i<draw_snap->design_n&&i<PATH_STYLE_COUNT;i++) {
      if(!path_pictures[i]) {
        char file[512];snprintf(file,sizeof file,"%s/ui/path-styles/previews/%d.png",host_exe_dir(),i);
        int w=0,h=0;uint32_t *pixels=hd_art_load_png(file,&w,&h);
        if(pixels) {
          path_pictures[i]=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,w,h);
          if(path_pictures[i])SDL_UpdateTexture(path_pictures[i],NULL,pixels,w*4);
          free(pixels);
        }
      }
      pictures[HUD_ART_PATH+i]=path_pictures[i];
    }
  }
  int n = ui_hud_draw(g_ui, draw_hud, draw_snap, pictures, acts, 16);
  ui_end(g_ui);
  // X and row actions close after drawing the panel. Keep later layers clear
  // through that closing frame; the next frame restores their controls.
  SDL_AtomicSet(&g_history_visible, !draw_hud->hidden && draw_hud->density == HUD_JWE && (history_before || draw_hud->hist_open));
  {   // one line when the tray tucks away or comes back, the build bar opens or folds, the HUD hides (tests read these)
    static int last_tucked = -1, last_bar = -1, last_hidden = -1;
    if (g_hud.tray_hidden != last_tucked) {
      fprintf(stderr, "hud: tray %s\n", g_hud.tray_hidden ? "tucked" : "shown");
      last_tucked = g_hud.tray_hidden;
    }
    if (g_hud.bar_open != last_bar) {
      fprintf(stderr, "hud: bar %s\n", g_hud.bar_open ? "open" : "collapsed");
      last_bar = g_hud.bar_open;
    }
    static int last_msg = -1, last_hist = -1;
    if ((g_hud.msg_open != 0) != last_msg) {
      fprintf(stderr, "hud: message %s\n", g_hud.msg_open ? "open" : "closed");
      last_msg = g_hud.msg_open != 0;
    }
    if (g_hud.hist_open != last_hist) {
      fprintf(stderr, "hud: history %s\n", g_hud.hist_open ? "open" : "closed");
      last_hist = g_hud.hist_open;
    }
    if (g_hud.hidden != last_hidden) {
      fprintf(stderr, "hud: ui %s\n", g_hud.hidden ? "hidden" : "shown");
      last_hidden = g_hud.hidden;
    }
  }
  log_coverage(&s);
  if (n) {
    lock();
    for (int i = 0; i < n; i++) {
      if (acts[i].kind == HUD_ACT_PATH_VIEW) pvf_set_view(!pvf_view_user());
      else if (g_qn < QMAX) g_q[g_qn++] = acts[i];
    }
    unlock();
  }
}

// Game thread, before golf sees the event; mouse positions in canvas pixels.
int hudf_blocks_course_point(int x, int y) {
  if (!g_ui || !g_visible || SDL_AtomicGet(&g_mode) == HUD_MODE_OFF) return 0;
  return ui_blocks_point(g_ui, g_view_x + (x + 0.5f) * g_view_px,
                         g_view_y + (y + 0.5f) * g_view_px);
}

static int input(SDL_Event *e, int course, void *user) {
  (void)user, (void)course;
  int mode = SDL_AtomicGet(&g_mode);
  if (!g_ui || mode == HUD_MODE_OFF) return 0;
  if (mode == HUD_MODE_ORIGINAL_ROTATE &&
      (e->type == SDL_KEYDOWN || e->type == SDL_TEXTINPUT || e->type == SDL_MOUSEWHEEL)) return 0;
  // Original modal loops own Escape too, even when their native skin is off.
  if (dlg_game_active() || rp_game_active()) return 0;
  UiEvent u = {0};
  int cx = 0, cy = 0;
  switch (e->type) {
    case SDL_MOUSEMOTION:
      if (!e->motion.windowID) return 0;   // scripted input (harness, bridge) is the game's
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
    case SDL_KEYDOWN: {
      if(g_visible && e->key.keysym.sym==SDLK_ESCAPE && lamps_game_event(e))return 1;
      if (e->key.keysym.sym == SDLK_ESCAPE && !opt_is_open() && gwf_escape(e->key.repeat)) return 1;
      if (e->key.keysym.sym == SDLK_ESCAPE && mode == HUD_MODE_FROSTED &&
          !opt_is_open() && !dlgf_active() && mods_get_int("ui.original_bottom", 1)) return 0;
      if (e->key.keysym.sym == SDLK_ESCAPE) fprintf(stderr, "hud: esc key (visible %d, options %d, dialog %d)\n", g_visible, opt_is_open(), dlgf_active());
      if (e->key.keysym.sym == SDLK_ESCAPE && g_visible && !opt_is_open() && !dlgf_active()) {
        if (!e->key.repeat) g_esc = 1;   // each Esc is one step (the draw runs ui_hud_escape)
        return 1;
      }
      // the hide key (task-57): only while the course view is up (not in a dialog that takes typing)
      SDL_Keycode hk = hide_key(NULL, 0);
      if (hk != SDLK_UNKNOWN && e->key.keysym.sym == hk && g_visible &&
          !(e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) {
        if (!e->key.repeat) {
          g_toggle_hidden = 1;
          g_eat_char = hk < 128 ? (int)hk : 0;
          g_eat_until = host_now_ms() + 250;
        }
        return 1;
      }
      return 0;   // other keys stay the game's (no text fields in the HUD)
    }
    case SDL_TEXTINPUT:   // the character of the hide key must not reach the game as a typed key
      if (g_eat_char && host_now_ms() < g_eat_until && e->text.text[0] && !e->text.text[1] &&
          (e->text.text[0] | 32) == g_eat_char) {
        g_eat_char = 0;
        return 1;
      }
      return 0;
    default:
      return 0;
  }
  // canvas pixel -> target pixel (its centre)
  u.x = g_view_x + (cx + 0.5f) * g_view_px;
  u.y = g_view_y + (cy + 0.5f) * g_view_px;
  if (!g_visible) {
    ui_input_reset(g_ui);
    return 0;
  }
  int to = ui_route_event(g_ui, &u);
  if((to & UI_ROUTE_GAME) && lamps_game_event(e))return 1;
  if (mode == HUD_MODE_FROSTED && !mods_get_int("ui.original_bottom", 1) &&
      u.kind == UI_EV_DOWN && (to & UI_ROUTE_GAME)) {
    lock();
    int placing = hudf_keep_tray_on_world_click(&g_snap);
    unlock();
    if (!placing) g_tuck = 1;
  }
  // While one of our clicks is on its way to the game, its mouse state must
  // not change under it: hold the real pointer back for those frames.
  if (mode == HUD_MODE_FROSTED && g_injecting) return 1;
  return !(to & UI_ROUTE_GAME);
}

// ------------------------------------------------------------------ switch

void hudf_early(void) {
  if (!g_lock) g_lock = SDL_CreateMutex();
  if (!g_registered) {
    g_registered = hd_ui_add_layer(draw, NULL) && hd_ui_add_input(input, NULL);
    if (!g_registered) g_failed = 1;
  }
}

int hudf_enabled(void) {
  refresh_mode();
  int mode = SDL_AtomicGet(&g_mode);
  if (!g_lock) g_lock = SDL_CreateMutex();
  if (mode != HUD_MODE_OFF && !g_registered) {
    // registered once; with ui.hud switched off later the layer draws nothing
    hudf_early();
  }
  // Only once the presenter has run the layer: with hd=0 (the original 800x600
  // window) nothing would draw the new HUD, so the original one stays.
  return mode == HUD_MODE_FROSTED && g_registered && !g_failed && host_now_ms() - g_drawn_t < 2000;
}

int hudf_original_rotate_enabled(void) {
  refresh_mode();
  int mode = SDL_AtomicGet(&g_mode);
  if (!g_lock) g_lock = SDL_CreateMutex();
  if (mode != HUD_MODE_OFF && !g_registered) {
    g_registered = hd_ui_add_layer(draw, NULL) && hd_ui_add_input(input, NULL);
    if (!g_registered) g_failed = 1;
  }
  return mode == HUD_MODE_ORIGINAL_ROTATE && g_registered && !g_failed;
}
