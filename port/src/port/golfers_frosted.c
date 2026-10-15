// Frosted Mint golfers in the world, render side (simgolf mod, task-31), see
// golfers_frosted.h. Native build only. Registers a UI layer with the presenter
// (hd_native.h) that draws the note the game thread attached to the frame
// (name tags, thought bubbles, the golfer card) pinned to the course layer
// as it slides, routes the pointer over the card, and queues what the player
// clicked for golfers_game.c.
#include "port/golfers_frosted.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mods/mods.h"
#include "port/hd_native.h"
#include "port/host.h"
#include "port/hud_frosted.h"
#include "port/reports_frosted.h"
#include "port/runtime.h"

#define QMAX 8

static SDL_mutex *g_lock;
static GwAction g_q[QMAX];
static int g_qn;
static volatile int g_injecting, g_visible;
static struct {
  uint32_t *px;
  int w, h;
  uint32_t serial;
} g_art[GW_ART_MAX];
static uint32_t g_art_pub;

// render thread
static UiCtx *g_ui_w, *g_ui_c;   // the world layer (tags, bubbles: under the HUD) and the card layer (over it)
static SDL_Texture *g_tex[GW_ART_MAX];
static uint32_t g_tex_serial[GW_ART_MAX];
static Uint32 g_tex_retry_at[GW_ART_MAX];  // next GPU-upload attempt after a transient failure
static int g_registered, g_card_registered, g_failed;
static volatile int g_want;
static volatile double g_drawn_t = -1e9, g_card_drawn_t = -1e9;
static volatile float g_view_x, g_view_y, g_view_px = 1;

// task-60: the pointer and the show-all-names key, from a hook that sees every event (the world layer takes no input)
static volatile float g_mouse_cx, g_mouse_cy;
static volatile int g_have_mouse, g_show_all, g_world_up, g_input_registered;
static volatile double g_serial_t;   // when the course last drew a new frame: a modal text box stops the frames, and then N is only a letter
static volatile uint32_t g_serial_seen;
static volatile int g_eat_char;
static volatile double g_eat_until;

static int str_is(const char *v, const char *a) { return v && !strcasecmp(v, a); }

// ui.tags, ui.bubbles, ui.tag_style, ui.tag_scale, ui.tag_opacity (defaults in mods/builtin/ui_golfers/mod.toml)
static void read_opts(GwOpts *o, const HdUiFrame *f, int tw) {
  ui_golfers_opts_default(o);
  const char *v = mods_get("ui.tags");
  if (str_is(v, "always")) o->tags = GW_TAGS_ALWAYS;
  else if (str_is(v, "off")) o->tags = GW_TAGS_OFF;
  v = mods_get("ui.bubbles");
  if (str_is(v, "full")) o->bubbles = GW_BUB_FULL;
  else if (str_is(v, "icons")) o->bubbles = GW_BUB_ICONS;
  else if (str_is(v, "off")) o->bubbles = GW_BUB_OFF;
  v = mods_get("ui.tag_style");
  if (str_is(v, "pill")) o->tag_style = GW_STYLE_PILL;
  o->faces_original = str_is(mods_get("ui.faces"), "original");
  double d = mods_get_num("ui.tag_scale", o->tag_scale);
  if (d >= 0.3 && d <= 2.0) o->tag_scale = (float)d;
  d = mods_get_num("ui.tag_opacity", o->tag_opacity);
  if (d >= 0.0 && d <= 1.5) o->tag_opacity = (float)d;
  d = mods_get_num("ui.bubble_ms", o->brief_ms);
  if (d >= 500 && d <= 60000) o->brief_ms = (int)d;
  o->show_all = g_show_all;
  o->have_mouse = g_have_mouse;
  float px = f->px > 0 ? f->px : 1;
  o->mx = (float)f->view.x + (g_mouse_cx + 0.5f) * px;
  o->my = (float)f->view.y + (g_mouse_cy + 0.5f) * px;
  o->now_ms = f->now_ms;
  (void)tw;
}

// ui.tags_key: the key that shows every name while ui.tags=hover (default N, "none" turns it off)
static SDL_Keycode names_key(void) {
  const char *v = mods_get("ui.tags_key");
  if (!v || !*v) return SDLK_n;
  if (!strcasecmp(v, "none") || !strcasecmp(v, "off")) return SDLK_UNKNOWN;
  SDL_Keycode k = SDL_GetKeyFromName(v);
  return k == SDLK_UNKNOWN ? SDLK_n : k;
}

static int track_input(SDL_Event *e, int course, void *user) {
  (void)user, (void)course;
  switch (e->type) {
    case SDL_MOUSEMOTION:
      if (!e->motion.windowID) return 0;
      g_mouse_cx = (float)e->motion.x, g_mouse_cy = (float)e->motion.y;
      g_have_mouse = 1;
      return 0;
    case SDL_WINDOWEVENT:
      if (e->window.event == SDL_WINDOWEVENT_LEAVE || e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) g_have_mouse = 0;
      return 0;
    case SDL_KEYDOWN: {
      SDL_Keycode nk = names_key();
      // N alone is the game's cancel match key, so the default N key needs Shift (card task-93)
      if (nk != SDLK_UNKNOWN && e->key.keysym.sym == nk && (nk != SDLK_n || (e->key.keysym.mod & KMOD_SHIFT)) && g_world_up && host_now_ms() - g_serial_t < 400 && !(e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) {
        if (!e->key.repeat) {
          g_show_all = !g_show_all;
          g_eat_char = nk < 128 ? (int)nk : 0;
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
}

static void lock(void) {
  if (g_lock) SDL_LockMutex(g_lock);
}
static void unlock(void) {
  if (g_lock) SDL_UnlockMutex(g_lock);
}

// ------------------------------------------------------------------ game thread API

void gwf_publish_art(int key, const uint32_t *argb, int w, int h) {
  if (key < 0 || key >= GW_ART_MAX) return;
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

int gwf_take_action(GwAction *a) {
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

// Escape closes the visible card through its original Close button before the HUD.
int gwf_escape(int repeat) {
  if (!g_want || !g_visible || g_failed) return 0;
  lock();
  if (!repeat) {
    int queued = 0;
    for (int i = 0; i < g_qn; ++i) queued |= g_q[i].kind == GW_ACT_CLOSE;
    if (!queued) {
      if (g_qn == QMAX) --g_qn;   // Escape cancels the last pending card action if the queue is full
      g_q[g_qn++] = (GwAction){GW_ACT_CLOSE, 4};
    }
  }
  unlock();
  return 1;
}

void gwf_set_injecting(int on) { g_injecting = on; }

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
  for (int i = 0; i < GW_ART_MAX; i++) {
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
  UiCtx *c = ui_create(r, &t, fonts); ui_debug_name(c, "golfers");
  if (c) fprintf(stderr, "ui: Frosted Mint golfer %s layer on (fonts %s)\n", what, fonts);
  return c;
}

// user: 0 the world layer (name tags and thought bubbles: they go under the
// HUD, like the golfers), 1 the card layer (over everything).
static void draw(const HdUiFrame *f, void *user) {
  int card_layer = user != NULL;
  if (f->course && f->layered) {   // the presenter pairs notes with layered course frames
    if (card_layer) g_card_drawn_t = f->now_ms;
    else g_drawn_t = f->now_ms;
  }
  UiCtx **pc = card_layer ? &g_ui_c : &g_ui_w;
  const GwNote *n = (const GwNote *)f->note;
  int have = n && f->note_size == (int)sizeof(GwNote) && n->magic == GW_NOTE_MAGIC;
  int report_covers_world = !card_layer && rpf_visible();
  int ok = g_want && !g_failed && f->course && have && !report_covers_world &&
           (card_layer ? n->card.on : (n->n_tags || n->n_bubbles));
  if (!ok) {
    if (card_layer) g_visible = 0;
    else g_world_up = 0;
    if (*pc) ui_skip_frame(*pc);
    return;
  }
  SDL_Renderer *r = f->renderer;
  if (!*pc) {
    *pc = make_ui(r, card_layer ? "card" : "world");
    if (!*pc) {
      g_failed = 1;
      return;
    }
  }
  int w = 0, h = 0;
  if (f->target) SDL_QueryTexture(f->target, NULL, NULL, &w, &h);
  else SDL_GetRendererOutputSize(r, &w, &h);
  if (w <= 0) return;
  update_art(r);
  if (f->zoom_on) ui_golfers_set_zoom(f->zoom_s, f->zoom_x, f->zoom_y);   // task-56: tags follow the zoom glide
  else ui_golfers_set_zoom(1, 0, 0);
  ui_begin(*pc, f->target, w, h, ui_scale_now(h), f->target, host_now_ms());
  if (card_layer) {
    g_view_x = (float)f->view.x;
    g_view_y = (float)f->view.y;
    g_view_px = f->px > 0 ? f->px : 1;
    g_visible = 1;
    GwAction acts[4];
    int na = ui_golfers_draw_card(*pc, n, g_tex, g_view_x, g_view_y, g_view_px, f->slide_x, f->slide_y, acts, 4);
    ui_end(*pc);
    static int log_on = -1;
    if (log_on < 0) log_on = getenv("SIMGOLF_GOLFERS_LOG") != NULL;
    if (log_on) {   // where the card's pieces are, in target pixels (tests click them), when they move
      static float last[8];
      const GwLayout *L = ui_golfers_layout();
      float now[8] = {L->card.x, L->card.y, L->close.x, L->btn[0].x, L->btn[2].x, L->btn[3].x, L->btn[1].x, (float)n->card.layout};
      if (memcmp(now, last, sizeof now)) {
        memcpy(last, now, sizeof now);
        fprintf(stderr, "golfers: card at %.0f,%.0f %.0fx%.0f; close %.0f,%.0f; profile %.0f,%.0f; snapshot %.0f,%.0f; story %.0f,%.0f; eject %.0f,%.0f (target px %dx%d)\n",
                L->card.x, L->card.y, L->card.w, L->card.h, L->close.x + L->close.w / 2, L->close.y + L->close.h / 2,
                L->btn[0].x + L->btn[0].w / 2, L->btn[0].y + L->btn[0].h / 2, L->btn[2].x + L->btn[2].w / 2,
                L->btn[2].y + L->btn[2].h / 2, L->btn[3].x + L->btn[3].w / 2, L->btn[3].y + L->btn[3].h / 2,
                L->btn[1].x + L->btn[1].w / 2, L->btn[1].y + L->btn[1].h / 2, w, h);
      }
    }
    if (na) {
      lock();
      for (int i = 0; i < na && g_qn < QMAX; i++) g_q[g_qn++] = acts[i];
      unlock();
    }
  } else {
    GwOpts o;
    read_opts(&o, f, w);
    ui_golfers_set_opts(&o);
    g_world_up = 1;
    if (n->serial != g_serial_seen) g_serial_seen = n->serial, g_serial_t = f->now_ms;
    ui_golfers_draw_world(*pc, n, g_tex, (float)f->view.x, (float)f->view.y, f->px > 0 ? f->px : 1, f->slide_x, f->slide_y);
    ui_end(*pc);
  }
}

// Game thread, before golf sees the event; mouse positions in canvas pixels.
static int input(SDL_Event *e, int course, void *user) {
  (void)user, (void)course;
  if (!g_ui_c || !g_want) return 0;
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
      if (e->window.event == SDL_WINDOWEVENT_LEAVE || e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) ui_input_reset(g_ui_c);
      return 0;
    default:
      return 0;
  }
  u.x = g_view_x + (cx + 0.5f) * g_view_px;
  u.y = g_view_y + (cy + 0.5f) * g_view_px;
  if (!g_visible) {
    ui_input_reset(g_ui_c);
    return 0;
  }
  int to = ui_route_event(g_ui_c, &u);
  // While one of our clicks is on its way to the game, its mouse state must
  // not change under it: hold the real pointer back for those frames.
  if (g_injecting) return 1;
  return !(to & UI_ROUTE_GAME);
}

// ------------------------------------------------------------------ switch

static void refresh_switch(void) {
  static unsigned seen = ~0u;
  unsigned ser = mods_serial();
  if (ser != seen) {
    seen = ser;
    const char *v = mods_get("ui.golfers");
    g_want = v && (!strcasecmp(v, "frosted_mint") || !strcasecmp(v, "frosted-mint"));
  }
  if (!g_lock) g_lock = SDL_CreateMutex();
}

int gwf_enabled(void) {
  refresh_switch();
  if (g_want && !g_registered) {
    // registered once; with ui.golfers switched off later the layers draw nothing
    g_registered = hd_ui_add_layer(draw, NULL);
    if (!g_registered) g_failed = 1;
    else if (!g_input_registered) g_input_registered = hd_ui_add_input(track_input, NULL);
  }
  // Only once the presenter has run the layer: with hd=0 (the original 800x600
  // window), or without separate course and UI layers, nothing would draw the
  // native versions, so the original ones stay.
  return g_want && g_registered && !g_failed && host_now_ms() - g_drawn_t < 2000;
}

// The card layer goes in after the HUD's, so it draws over the HUD and is asked
// for the pointer after it; registered the first time a card is open.
int gwf_card_enabled(void) {
  refresh_switch();
  if (g_want && g_registered && !g_card_registered && !g_failed) {
    hudf_enabled();
    g_card_registered = hd_ui_add_layer(draw, (void *)1) && hd_ui_add_input(input, NULL);
    if (!g_card_registered) g_failed = 1;
  }
  return g_want && g_card_registered && !g_failed && host_now_ms() - g_card_drawn_t < 2000;
}
