// Frosted Mint reports, render-thread side (simgolf mod, task-28). Overview in reports_frosted.h.
// A native UI layer (hd_ui_add_layer) draws ui_reports_draw from the latest RpSnap; its input hook takes
// every real pointer event while a report is up (the original only sees what reports_game.c injects).
#include "port/reports_frosted.h"

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
#include "port/runtime.h"

#define QMAX 16
#define STALE_MS 4000   // safety net only: rp_done ends the panel (some reports wait 2 s before their first frame)

static SDL_mutex *g_lock;
static RpSnap g_snap;
static double g_snap_t = -1e9;
static volatile int g_ended = 1;
static RpAction g_q[QMAX];
static int g_qn;
static volatile int g_visible, g_frame_gap;
static UiCtx *g_ui;
static int g_registered, g_failed;
static volatile int g_want, g_game_asked;
static volatile double g_drawn_t = -1e9;
static volatile float g_view_x, g_view_y, g_view_px = 1;
static struct {
  uint32_t *px;
  int w, h;
  uint32_t serial;
} g_art[RP_ART];
static uint32_t g_art_pub;
static SDL_Texture *g_tex[RP_ART];
static uint32_t g_tex_serial[RP_ART];
static Uint32 g_tex_retry_at[RP_ART];  // next GPU-upload attempt after a transient failure

static void lock(void) {
  if (g_lock) SDL_LockMutex(g_lock);
}
static void unlock(void) {
  if (g_lock) SDL_UnlockMutex(g_lock);
}

void rpf_publish(const RpSnap *s) {
  lock();
  g_snap = *s;
  g_snap_t = host_now_ms();
  g_ended = 0;
  unlock();
}

void rpf_publish_art(int k, const uint32_t *argb, int w, int h) {
  if (k < 0 || k >= RP_ART) return;
  lock();
  free(g_art[k].px);
  g_art[k].px = NULL;
  g_art[k].w = g_art[k].h = 0;
  if (argb && w > 0 && h > 0 && (g_art[k].px = malloc((size_t)w * h * 4))) {
    memcpy(g_art[k].px, argb, (size_t)w * h * 4);
    g_art[k].w = w, g_art[k].h = h;
  }
  g_art[k].serial = ++g_art_pub;
  unlock();
}

static void update_art(SDL_Renderer *r) {
  lock();
  Uint32 now = SDL_GetTicks();
  for (int i = 0; i < RP_ART; i++) {
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

void rpf_end(void) {
  lock();
  g_ended = 1;
  g_qn = 0;
  unlock();
}

int rpf_take_action(RpAction *a) {
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

// the task-73 pages wait for the player inside nested loops (a name being typed, a face picked), so they stay up until rp_done
static int active(void) {
  double lim = g_snap.kind >= RP_PROFILE ? 1e12 : STALE_MS;
  return g_want && g_registered && !g_ended && host_now_ms() - g_snap_t < lim;
}

int rpf_gap(void) { return g_frame_gap; }   // test aid: gap latched with the report layer's draw for this frame
int rpf_visible(void) { return g_visible; }

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

static float rp_scale(int w, int h) {
  double s = mods_get_num("ui.scale", 0);
  float base = (s >= 0.5 && s <= 4) ? (float)s : ui_scale_for_height(h);
  float fit = fminf(w / 1500.0f, h / 900.0f);   // the panel (up to 1400 wide) still fits
  if (base > fit) base = fit;
  return base < 0.5f ? 0.5f : base;
}

static void refresh_want(void);

static void draw(const HdUiFrame *f, void *user) {
  (void)user;
  g_drawn_t = f->now_ms;
  if (!g_game_asked) refresh_want();   // before the game thread's first rpf_enabled()
  int report_active = active();
  if (g_failed || !g_want) {
    g_visible = 0;
    g_frame_gap = report_active;
    if (g_ui) ui_skip_frame(g_ui);
    return;
  }
  SDL_Renderer *r = f->renderer;
  if (!g_ui) {
    UiTokens t;
    load_tokens(&t);
    char fonts[1024];
    snprintf(fonts, sizeof fonts, "%s", port_host_path("/fonts/plus-jakarta-sans"));
    g_ui = ui_create(r, &t, fonts); ui_debug_name(g_ui, "reports");
    if (!g_ui) {
      g_failed = 1;
      g_visible = 0;
      g_frame_gap = report_active;
      return;
    }
    fprintf(stderr, "ui: Frosted Mint reports layer on (fonts %s)\n", fonts);
  }
  int w = 0, h = 0;
  if (f->target) SDL_QueryTexture(f->target, NULL, NULL, &w, &h);
  else SDL_GetRendererOutputSize(r, &w, &h);
  g_view_x = (float)f->view.x;
  g_view_y = (float)f->view.y;
  g_view_px = f->px > 0 ? f->px : 1;
  // the task-73 pages hide while a native dialog is up over them (a name strip, a menu): the dialogs layer was registered first
  // (at the title) and so draws under this one
  int show = report_active && w > 0 && !(g_snap.kind >= RP_PROFILE && dlgf_active());
  g_visible = show;
  g_frame_gap = report_active && !show;
  if (!show) {
    ui_skip_frame(g_ui);
    return;
  }
  RpSnap *s = malloc(sizeof *s);
  if (!s) {
    g_visible = 0;
    g_frame_gap = report_active;
    return;
  }
  lock();
  memcpy(s, &g_snap, sizeof *s);
  unlock();
  if (s->magic != RP_MAGIC || s->kind == RP_NONE) {
    g_visible = 0;
    g_frame_gap = report_active;
    ui_skip_frame(g_ui);
    free(s);
    return;
  }
  update_art(r);
  if (s->kind >= RP_PROFILE && dlgf_active()) ui_input_reset(g_ui);   // a native dialog over the page (a name, a menu) takes the pointer
  ui_begin(g_ui, f->target, w, h, rp_scale(w, h), f->target, host_now_ms());
  RpAction acts[8];
  int n = ui_reports_draw(g_ui, s, g_tex, acts, 8);
  ui_end(g_ui);
  static int log_layout = -1;
  static uint32_t logged_kind = ~0u, logged_hash;
  static int32_t logged_pair_open = -1;
  if (log_layout < 0) log_layout = getenv("SIMGOLF_REPORTS_LOG") != NULL;
  if (log_layout) {
    RpLayout l;
    if (s->kind >= RP_PROFILE) ui_pages_layout(&l);
    else ui_reports_layout(&l);
    char buf[2048];
    int k = snprintf(buf, sizeof buf, "reports: layout kind %d target %dx%d panel %d,%d,%d,%d close %d,%d", s->kind, w, h, (int)l.panel.x,
                     (int)l.panel.y, (int)l.panel.w, (int)l.panel.h, (int)l.close_x, (int)l.close_y);
    for (int i = 0; i < l.ntabs && k < (int)sizeof buf - 32; i++) k += snprintf(buf + k, sizeof buf - k, " tab%d %d,%d", i, (int)l.tab_x[i], (int)l.tab_y);
    for (int i = 0; i < l.nctl && k < (int)sizeof buf - 40; i++) k += snprintf(buf + k, sizeof buf - k, " %s %d,%d", l.ctl_name[i], (int)l.ctl_x[i], (int)l.ctl_y[i]);
    uint32_t hsh = 2166136261u;
    for (const char *q = buf; *q; q++) hsh = (hsh ^ (uint8_t)*q) * 16777619u;
    if (logged_kind != (uint32_t)s->kind || (s->kind >= RP_PROFILE && logged_hash != hsh) ||
        (s->kind == RP_PAIRING && logged_pair_open != s->ext[5])) {
      logged_kind = (uint32_t)s->kind, logged_hash = hsh;
      if (s->kind == RP_PAIRING) logged_pair_open = s->ext[5];
      fputs(buf, stderr);
      fputc(10, stderr);
      fflush(stderr);
    }
  }
  free(s);
  if (n) {
    static int log_on = -1;
    if (log_on < 0) log_on = getenv("SIMGOLF_REPORTS_LOG") != NULL;
    if (log_on)
      for (int i = 0; i < n; i++) fprintf(stderr, "reports: native action %d arg %d at %u ms\n", acts[i].kind, acts[i].arg, (unsigned)SDL_GetTicks());
    lock();
    for (int i = 0; i < n && g_qn < QMAX; i++) g_q[g_qn++] = acts[i];
    unlock();
  }
}

static int input(SDL_Event *e, int course, void *user) {
  (void)user, (void)course;
  if (!g_ui || !g_want) return 0;
  if (g_snap.kind >= RP_PROFILE && dlgf_active()) return 0;   // the dialog over the page takes the pointer
  UiEvent u = {0};
  int cx = 0, cy = 0;
  switch (e->type) {
    case SDL_MOUSEMOTION:
      if (!e->motion.windowID) return 0;   // scripted input (harness, bridge, our own clicks) is the game's
      u.kind = UI_EV_MOVE;
      break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      if (!e->button.windowID) return 0;
      u.kind = e->type == SDL_MOUSEBUTTONDOWN ? UI_EV_DOWN : UI_EV_UP;
      u.button = e->button.button;
      break;
    case SDL_MOUSEWHEEL:
      if (!e->wheel.windowID) return 0;
      u.kind = UI_EV_WHEEL;
      u.wheel = (float)e->wheel.y;
      break;
    case SDL_WINDOWEVENT:
      if (e->window.event == SDL_WINDOWEVENT_LEAVE || e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) ui_input_reset(g_ui);
      return 0;
    default:
      return 0;   // keys stay the game's: any key closes a report, as in the original
  }
  cx = hd_raw_mouse_x;
  cy = hd_raw_mouse_y;
  u.x = g_view_x + (cx + 0.5f) * g_view_px;
  u.y = g_view_y + (cy + 0.5f) * g_view_px;
  if (!g_visible) {
    ui_input_reset(g_ui);
    return 0;
  }
  ui_route_event(g_ui, &u);
  return 1;   // a report takes every real pointer event
}

// the task-73 pages follow ui.dialogs too (they were left by the dialogs card); the older reports only ui.reports
int rpf_enabled_kind(int kind) {
  if (!rpf_enabled()) return 0;
  if (kind < RP_PROFILE) return 1;
  const char *v = mods_get("ui.dialogs");
  const char *e = getenv("SIMGOLF_DIALOGS");
  if (e && !strcasecmp(e, "original")) return 0;
  return v && (!strcasecmp(v, "frosted_mint") || !strcasecmp(v, "frosted-mint"));
}

// task-106: ui.reports / SIMGOLF_REPORTS: whether the native screens are wanted at all
static void refresh_want(void) {
  const char *v = mods_get("ui.reports");
  g_want = v && (!strcasecmp(v, "frosted_mint") || !strcasecmp(v, "frosted-mint"));
  const char *e = getenv("SIMGOLF_REPORTS");
  if (e && !strcasecmp(e, "original")) g_want = 0;
}

// The layer is registered when the presenter starts, before golf's first screen: the first one of a session is
// covered like every later one, not shown as the original's for the frames before the layer is up.
void rpf_early(void) {
  if (!g_lock) g_lock = SDL_CreateMutex();
  if (g_registered) return;
  g_registered = hd_ui_add_layer(draw, NULL) && hd_ui_add_input(input, NULL);
  if (!g_registered) g_failed = 1;
}

int rpf_enabled(void) {
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
  return g_want && g_registered && !g_failed && host_now_ms() - g_drawn_t < 2000;
}
