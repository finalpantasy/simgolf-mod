// Frosted Mint cart path help, render side (simgolf mod, task-53), see paths_frosted.h. Native build only.
// Two UI layers of the presenter (hd_native.h): the world layer (path view tint, "No path" markers) goes under
// the HUD, like the golfers' name tags; the top layer (legend, hover card) over it. Both draw the
// PvNote the game thread attached to the frame (second note channel).
#pragma clang diagnostic error "-Wimplicit-function-declaration"

#include "port/paths_frosted.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mods/mods.h"
#include "port/hd_native.h"
#include "port/host.h"
#include "port/hud_frosted.h"
#include "port/runtime.h"
#include "port/ui/ui_paths.h"

static UiCtx *g_ui_w, *g_ui_t;
static int g_world_reg, g_top_reg, g_failed;
static volatile int g_want;
static volatile int g_history_guard = 1;
static SDL_atomic_t g_stats_guard;
static volatile int g_user_on;
static volatile double g_drawn_t = -1e9;
static volatile float g_view_x, g_view_y, g_view_px = 1;
static volatile float g_mouse_cx, g_mouse_cy;
static volatile int g_have_mouse, g_top_visible;
static volatile double g_eat_until;
static volatile int g_eat_char;
static volatile double g_note_t;   // when a note last arrived (the key only works with the course up)

static int str_is(const char *v, const char *a) { return v && !strcasecmp(v, a); }

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

static float ui_scale_now(int h) {
  double s = mods_get_num("ui.scale", 0);
  if (s >= 0.5 && s <= 4) return (float)s;
  return ui_scale_for_height(h);
}

static UiCtx *make_ui(SDL_Renderer *r, const char *what) {
  UiTokens t;
  load_tokens(&t);
  char fonts[1024];
  snprintf(fonts, sizeof fonts, "%s", port_host_path("/fonts/plus-jakarta-sans"));
  UiCtx *c = ui_create(r, &t, fonts); ui_debug_name(c, "paths");
  if (c) fprintf(stderr, "ui: Frosted Mint cart path %s layer on (fonts %s)\n", what, fonts);
  return c;
}

// ui.paths_key: the key that toggles the path view (an SDL key name, default Y, "none" turns it off)
static SDL_Keycode toggle_key(char *name, size_t cap) {
  static unsigned seen = ~0u;
  static SDL_Keycode key = SDLK_y;
  static char nm[12] = "Y";
  unsigned ser = mods_serial();
  if (ser != seen) {
    seen = ser;
    const char *v = mods_get("ui.paths_key");
    key = SDLK_y;
    snprintf(nm, sizeof nm, "Y");
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

static void read_opts(PvOpts *o) {
  o->markers = 1;
  o->opacity = 1;
  const char *v = mods_get("ui.paths_markers");
  if (v && (!strcmp(v, "0") || str_is(v, "off") || str_is(v, "false"))) o->markers = 0;
  double d = mods_get_num("ui.paths_opacity", 1);
  if (d >= 0.2 && d <= 1.5) o->opacity = (float)d;
  o->subtle = mods_get_int("fixes.paths_overlay", 1);
  d = mods_get_num("ui.paths_tint", 0.30);
  o->tint = (float)fmax(0.05, fmin(1.5, d));
}

static void make_map(const HdUiFrame *f, PvMap *m) {
  m->view_x = (float)f->view.x;
  m->view_y = (float)f->view.y;
  m->px = f->px > 0 ? f->px : 1;
  m->slide_x = f->slide_x;
  m->slide_y = f->slide_y;
  m->zoom_on = f->zoom_on;
  m->zoom_s = f->zoom_s;
  m->zoom_x = f->zoom_x;
  m->zoom_y = f->zoom_y;
}

static const PvNote *note_of(const HdUiFrame *f) {
  const PvNote *n = (const PvNote *)f->note2;
  if (n && f->note2_size == (int)sizeof(PvNote) && n->magic == PV_MAGIC) return n;
  return NULL;
}

// the button's place: the right edge under the stats at the top
static UiRect button_rect(UiCtx *c, const HdUiFrame *f, float *pad_out) {
  const UiTokens *t = ui_tokens(c);
  float b = ui_s(c, t->cam_button), pad = ui_s(c, 8), gap = ui_s(c, 8);
  float w = b + 2 * pad;
  *pad_out = pad;
  float right = ui_screen(c).w, y = ui_s(c, 204);
  if (SDL_AtomicGet(&g_stats_guard)) {
    right = (float)f->view.x + (float)f->view.w;
    y = (float)f->view.y + HUD_ORIGINAL_STATS_BOTTOM * f->px + gap;
  }
  UiRect br = ui_rect_round(ui_rect(right - ui_s(c, 24) - w, y, w, w));
  if (getenv("SIMGOLF_PATHS_LOG")) {
    static int old_x = -1, old_y = -1;
    if (old_x != (int)br.x || old_y != (int)br.y) {
      old_x = (int)br.x, old_y = (int)br.y;
      fprintf(stderr, "paths: button %.0f,%.0f size %.0fx%.0f target %.0fx%.0f stats_bottom %.1f\n",
              br.x, br.y, br.w, br.h, ui_screen(c).w, ui_screen(c).h,
              (float)f->view.y + HUD_ORIGINAL_STATS_BOTTOM * f->px);
    }
  }
  return br;
}

// user: NULL the world layer, non-NULL the top layer
static void draw(const HdUiFrame *f, void *user) {
  int top = user != NULL;
  const PvNote *n = note_of(f);
  UiCtx **pc = top ? &g_ui_t : &g_ui_w;
  if (f->course && f->layered && n && !top) {
    g_drawn_t = f->now_ms;
    g_note_t = f->now_ms;
  }
  if (!g_want || g_failed || !f->course || !n) {
    if (top) g_top_visible = 0;
    if (*pc) ui_skip_frame(*pc);
    return;
  }
  SDL_Renderer *r = f->renderer;
  if (!*pc) {
    *pc = make_ui(r, top ? "top" : "world");
    if (!*pc) {
      g_failed = 1;
      return;
    }
  }
  int w = 0, h = 0;
  if (f->target) SDL_QueryTexture(f->target, NULL, NULL, &w, &h);
  else SDL_GetRendererOutputSize(r, &w, &h);
  if (w <= 0) return;
  PvMap m;
  make_map(f, &m);
  PvOpts o;
  read_opts(&o);
  UiCtx *c = *pc;
  ui_begin(c, f->target, w, h, ui_scale_now(h), f->target, host_now_ms());
  float mtx = m.view_x + (g_mouse_cx + 0.5f) * m.px, mty = m.view_y + (g_mouse_cy + 0.5f) * m.px;
  int hov = g_have_mouse ? ui_paths_marker_at(c, n, &m, mtx, mty) : -1;
  if (!top) {
    if (n->view) {
      UiRect vr = ui_rect(m.view_x, m.view_y, (float)f->canvas_w * m.px, (float)f->canvas_h * m.px);
      ui_paths_draw_view(c, n, &m, vr, &o);
    }
    ui_paths_draw_markers(c, n, &m, &o, hov);
    ui_end(c);
    return;
  }
  g_view_x = m.view_x, g_view_y = m.view_y, g_view_px = m.px;
  g_top_visible = 1;
  // The later top layer must leave the visible history and its input unobscured.
  // The world tint and the player's view setting remain active underneath it.
  if (g_history_guard && hudf_history_visible()) {
    ui_end(c);
    return;
  }
  char key[12];
  toggle_key(key, sizeof key);
  // Compatibility switch keeps the previous standalone control available.
  if (!mods_get_int("ui.paths_menu", 1)) {
    // the button
    float pad;
    UiRect br = button_rect(c, f, &pad);
    ui_panel(c, br, ui_s(c, 22), 0);
    char tip[96];
    snprintf(tip, sizeof tip, "%s%s%s", g_user_on ? "Return to automatic cart path view" : "Keep the cart path view on after building", key[0] ? " (" : "", key[0] ? key : "");
    if (key[0]) strncat(tip, ")", sizeof tip - strlen(tip) - 1);
    UiRect ir = ui_rect(br.x + pad, br.y + pad, br.w - 2 * pad, br.h - 2 * pad);
    if (ui_icon_button(c, ui_id("paths.view"), ir, UI_ICON_PATH, g_user_on ? UI_COMPACT | UI_SELECTED : UI_COMPACT, tip)) g_user_on = !g_user_on;
  }
  if (n->view) ui_paths_draw_legend(c, n, n->placing && !g_user_on);
  if (hov >= 0 && !n->not_needed) ui_paths_draw_card(c, n, &m, hov, key);
  ui_end(c);
}

// Game thread, before golf sees the event; mouse positions in canvas pixels.
static int input(SDL_Event *e, int course, void *user) {
  (void)user;
  if (!g_want || g_failed) return 0;
  UiEvent u = {0};
  int cx = 0, cy = 0;
  switch (e->type) {
    case SDL_MOUSEMOTION:
      if (!e->motion.windowID) return 0;   // scripted input (harness, bridge) is the game's
      g_mouse_cx = (float)e->motion.x, g_mouse_cy = (float)e->motion.y;
      g_have_mouse = 1;
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
      if (e->window.event == SDL_WINDOWEVENT_LEAVE || e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        g_have_mouse = 0;
        if (g_ui_t) ui_input_reset(g_ui_t);
      }
      return 0;
    case SDL_KEYDOWN: {
      SDL_Keycode k = toggle_key(NULL, 0);
      if (course && k != SDLK_UNKNOWN && e->key.keysym.sym == k && g_top_visible && host_now_ms() - g_note_t < 400 &&
          !(e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI | KMOD_SHIFT))) {
        if (!e->key.repeat) {
          g_user_on = !g_user_on;
          g_eat_char = k < 128 ? (int)k : 0;
          g_eat_until = host_now_ms() + 250;
        }
        return 1;
      }
      return 0;
    }
    case SDL_TEXTINPUT:   // the typed character of that key must not reach the game
      if (g_eat_char && host_now_ms() < g_eat_until && e->text.text[0] && !e->text.text[1] && (e->text.text[0] | 32) == g_eat_char) {
        g_eat_char = 0;
        return 1;
      }
      return 0;
    default:
      return 0;
  }
  if (!g_ui_t) return 0;
  u.x = g_view_x + (cx + 0.5f) * g_view_px;
  u.y = g_view_y + (cy + 0.5f) * g_view_px;
  if (!g_top_visible || (g_history_guard && hudf_history_visible())) {
    ui_input_reset(g_ui_t);
    return 0;
  }
  return !(ui_route_event(g_ui_t, &u) & UI_ROUTE_GAME);
}

static void refresh_switch(void) {
  static unsigned seen = ~0u;
  unsigned ser = mods_serial();
  if (ser != seen) {
    seen = ser;
    g_history_guard = mods_get_int("fixes.history_paths", 1) != 0;
    SDL_AtomicSet(&g_stats_guard, mods_get_int("fixes.paths_stats", 1) != 0);
    const char *v = mods_get("ui.paths");
    g_want = v && (!strcasecmp(v, "frosted_mint") || !strcasecmp(v, "frosted-mint"));
  }
}

int pvf_view_available(void) { return g_want && !g_failed && mods_get_int("ui.paths_menu", 1); }
int pvf_view_user(void) { return g_user_on; }
void pvf_set_view(int on) { g_user_on = on != 0; }

int pvf_enabled(void) {
  refresh_switch();
  if (g_want && !g_world_reg && !g_failed) {
    g_world_reg = hd_ui_add_layer(draw, NULL);
    if (!g_world_reg) g_failed = 1;
  }
  // the top layer and the input go in after the HUD's (registering it first if it is not up), so they draw over the HUD
  if (g_want && g_world_reg && !g_top_reg && !g_failed && host_now_ms() - g_drawn_t < 2000) {
    hudf_enabled();
    g_top_reg = hd_ui_add_layer(draw, (void *)1) && hd_ui_add_input(input, NULL);
    if (!g_top_reg) g_failed = 1;
  }
  return g_want && g_world_reg && !g_failed;
}
