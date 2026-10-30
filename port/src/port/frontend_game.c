// Frosted Mint front end, game-thread side, native build (simgolf mod,
// task-32). The overview is in port/frontend_frosted.h and the top of
// golf/frontend_hooks.c (which holds the browser build's no-op versions of
// these hooks). This file does not include golf_protos.h (its types clash with
// the C library headers); the few game functions it calls are declared below
// with the same ABI.
//
// The original screens (title menu, difficulty, ...) are modal loops that draw
// into the back buffer and read the pointer. They keep running as they are.
// What each hook does, with ui.frontend=frosted_mint and the presenter up:
//   fe_frame(screen)   once per frame of the original screen's loop (translator
//       patches, tools/translate/golf.frontend.json): reads what the screen
//       shows into an FeSnap and publishes it (the native layer covers the
//       original's picture and draws the Frosted Mint screen), and runs the
//       click injector.
//   fe_base(screen)    right after the original drew its base picture: the
//       pieces of art the native screen reuses are read from the back buffer.
//   fe_context(n)      what the caller was doing (the difficulty screen is
//       used by a new game and by the championship).
// Clicks: the native screen only says what the player asked for (FeAction).
// Here each one becomes a scripted mouse move, press and release (SDL events
// without a window, which the game takes as its pointer) at the ORIGINAL
// control's position, one step per frame, so the original handlers run.
#include <SDL.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mods/mdata.h"
#include "mods/mods.h"
#include "port/dialogs_frosted.h"
#include "port/frontend_frosted.h"
#include "port/hd_art.h"
#include "port/host.h"
#include "port/runtime.h"
#include "port/save_ratings.h"

#define G32(a) (*(volatile int32_t *)(uintptr_t)(a))
#define G16(a) (*(volatile int16_t *)(uintptr_t)(a))
#define G8(a) (*(volatile int8_t *)(uintptr_t)(a))

#define THEME_NAME 0x567328   // the theme a new game uses ("Standard")
#define LOGO_SPRITE 0x53ba1c  // the title's logo oval

// translated game functions (build/gen/golf/golf_protos.h)
int FUN_00473e60(void *sprite, int target, int x, int y, int mode);
int FUN_00404b70(char *s, int x, int y, uint32_t col);
int FUN_00404bc0(char *s, int x, int y, uint32_t col);
int FUN_004049d0(char *s, int x, int y, uint32_t col);
int FUN_00404ad0(char *s, int x, int y, uint32_t col);
int FUN_0045ae70(void);   // a key is waiting
int FUN_0045ae30(void);   // takes the special key (VK code)
int FUN_0045ae50(void);   // takes the character key
int FUN_004724c0(void);   // discovers installed theme directories

#define PROPERTY_TABLE 0x4e3dc0   // the 16 emblem positions of the property screen (short x, short y)
#define SPRITE_EMBLEM 0x53c570    // golf's property emblems: sprite (theme * 4 + state), 0x2c each
#define IN_GAME 0x834170          // nonzero while a course is running (the world map in game)

// gdi.c: the screen-sized DIB whose pixels contain address p
uint16_t *gdi_dib_at(uintptr_t p, int *w, int *h, int *pitch, int *is565);
extern int win_screen_w, win_screen_h;

// ------------------------------------------------------------------ state

static FeSnap g_snap;
static uint32_t g_serial;
static int g_on;            // this frame: the native screens replace the original ones
static int g_log = -1;
static int g_champ;         // the difficulty screen belongs to the championship flow
static unsigned char g_art_done[FE_ART_MAX];
static int g_last_screen;
static int g_world_active;
// Game-thread screen identity also applies with the original front end.
// Pixels outside its 800x600 page can still contain the previous course.
static int g_frontend_page;
int fe_game_page_active(void) { return g_frontend_page; }
void fe_course_done(void) { g_frontend_page = 0; }

// the click injector
static int g_inj, g_inj_x, g_inj_y, g_inj_stage, g_inj_wait, g_inj_click;

static void copy_str(char *dst, int cap, const char *src) {
  int n = 0;
  while (n < cap - 1 && src && src[n]) {
    dst[n] = src[n];
    n++;
  }
  dst[n] = 0;
}

// The back buffer (what golf draws into) as pixels: base pointer, pitch in
// pixels, and the format. NULL when it is not what we expect.
static uint16_t *back_pixels(int *pitch, int *is565, int *w, int *h) {
  uint32_t wrap = (uint32_t)G32(0x4c1570);
  if (wrap < 0x10000) return NULL;
  uint32_t p = (uint32_t)G32(wrap + 4);
  if (p < 0x10000) return NULL;
  uint8_t *s = (uint8_t *)(uintptr_t)p;
  if (*(int32_t *)(s + 0x24) != 16 || *(int32_t *)(s + 0x40) < win_screen_w || !*(uint32_t *)(s + 0x4c0)) return NULL;
  uint32_t bits = *(uint32_t *)(s + 0x4c0);
  int dw, dh, dp;
  if (!gdi_dib_at(bits, &dw, &dh, &dp, is565)) return NULL;
  *pitch = *(int32_t *)(s + 0x40);
  *w = *(int32_t *)(s + 0x38);
  *h = *(int32_t *)(s + 0x3c);
  return (uint16_t *)(uintptr_t)bits;
}

static void rgb_of(uint16_t v, int is565, int *r, int *g, int *b) {
  if (is565) {
    *r = ((v >> 11) & 31) * 255 / 31;
    *g = ((v >> 5) & 63) * 255 / 63;
    *b = (v & 31) * 255 / 31;
  } else {
    *r = ((v >> 10) & 31) * 255 / 31;
    *g = ((v >> 5) & 31) * 255 / 31;
    *b = (v & 31) * 255 / 31;
  }
}

// ------------------------------------------------------------------ art

// Draws something into a scratch box of the back buffer over black and over
// white and turns the two into ARGB with alpha (a = 1 - (white - black)), then
// puts the pixels back. draw(bx, by, user) draws at the box's origin.
typedef void (*DrawFn)(int bx, int by, void *user);
static uint32_t *capture(int BW, int BH, DrawFn draw, void *user, int *ow, int *oh) {
  int pitch, is565, w, h;
  uint16_t *bb = back_pixels(&pitch, &is565, &w, &h);
  if (!bb || BW > w || BH > h) return NULL;
  size_t n = (size_t)BW * BH;
  uint16_t *saved = malloc(n * 2 * 3);
  if (!saved) return NULL;
  uint16_t *shot[2] = {saved + n, saved + 2 * n};
  for (int y = 0; y < BH; y++) memcpy(saved + (size_t)y * BW, bb + (ptrdiff_t)y * pitch, (size_t)BW * 2);
  uint16_t white = is565 ? 0xffff : 0x7fff;
  for (int pass = 0; pass < 2; pass++) {
    uint16_t fillc = pass ? white : 0;
    for (int y = 0; y < BH; y++)
      for (int x = 0; x < BW; x++) bb[(ptrdiff_t)y * pitch + x] = fillc;
    draw(0, 0, user);
    for (int y = 0; y < BH; y++) memcpy(shot[pass] + (size_t)y * BW, bb + (ptrdiff_t)y * pitch, (size_t)BW * 2);
  }
  for (int y = 0; y < BH; y++) memcpy(bb + (ptrdiff_t)y * pitch, saved + (size_t)y * BW, (size_t)BW * 2);
  int x0 = BW, y0 = BH, x1 = -1, y1 = -1;
  for (int y = 0; y < BH; y++)
    for (int x = 0; x < BW; x++)
      if (shot[0][y * BW + x] != 0 || shot[1][y * BW + x] != white) {
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
      }
  if (x1 < x0) {
    free(saved);
    return NULL;
  }
  int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
  uint32_t *out = malloc((size_t)cw * ch * 4);
  if (out) {
    for (int y = 0; y < ch; y++)
      for (int x = 0; x < cw; x++) {
        int br, bg, bl, wr, wg, wb;
        rgb_of(shot[0][(y0 + y) * BW + x0 + x], is565, &br, &bg, &bl);
        rgb_of(shot[1][(y0 + y) * BW + x0 + x], is565, &wr, &wg, &wb);
        int d = ((wr - br) + (wg - bg) + (wb - bl)) / 3;
        int a = 255 - d;
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        uint32_t px = 0;
        if (a > 0) {
          int r = br * 255 / a, g = bg * 255 / a, bb2 = bl * 255 / a;
          r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, bb2 = bb2 > 255 ? 255 : bb2;
          px = (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bb2;
        }
        out[y * cw + x] = px;
      }
    *ow = cw;
    *oh = ch;
  }
  free(saved);
  return out;
}

static void draw_logo(int bx, int by, void *user) {
  (void)user;
  FUN_00473e60((void *)(uintptr_t)LOGO_SPRITE, G32(0x4c1570), bx + 8, by + 8, 0);
}

// The round part of what the original has drawn in the back buffer: centre
// (cx, cy), radius r in canvas pixels, an anti-aliased edge.
static uint32_t *grab_circle(int cx, int cy, int r, int *ow, int *oh) {
  int pitch, is565, w, h;
  uint16_t *bb = back_pixels(&pitch, &is565, &w, &h);
  if (!bb) return NULL;
  int d = 2 * r + 4;
  uint32_t *out = calloc((size_t)d * d, 4);
  if (!out) return NULL;
  for (int y = 0; y < d; y++)
    for (int x = 0; x < d; x++) {
      int sx = cx - r - 2 + x, sy = cy - r - 2 + y;
      if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
      float dist = sqrtf((x + 0.5f - d / 2.0f) * (x + 0.5f - d / 2.0f) + (y + 0.5f - d / 2.0f) * (y + 0.5f - d / 2.0f));
      float a = r + 0.5f - dist;
      if (a <= 0) continue;
      if (a > 1) a = 1;
      int R, Gc, B;
      rgb_of(bb[(ptrdiff_t)sy * pitch + sx], is565, &R, &Gc, &B);
      out[y * d + x] = (uint32_t)(a * 255) << 24 | (uint32_t)R << 16 | (uint32_t)Gc << 8 | (uint32_t)B;
    }
  *ow = *oh = d;
  return out;
}

// ------------------------------------------------------------------ the click injector

// Scripted input, the way the bridge's click does it: SDL mouse events without
// a window id, in the original's 800x600 coordinates. The native layer's input
// handler leaves these to the game.
static void sdl_motion(int x, int y) {
  SDL_Event e = {0};
  e.type = SDL_MOUSEMOTION;
  e.motion.x = x;
  e.motion.y = y;
  SDL_PushEvent(&e);
}

static void sdl_button(int x, int y, int down) {
  SDL_Event e = {0};
  e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
  e.button.button = SDL_BUTTON_LEFT;
  e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
  e.button.clicks = 1;
  e.button.x = x;
  e.button.y = y;
  SDL_PushEvent(&e);
}

static void inj_start(int x, int y, int click) {
  g_inj = 1;
  g_inj_click = click;
  g_inj_x = x;
  g_inj_y = y;
  g_inj_stage = 0;
  g_inj_wait = 0;
  fef_set_injecting(1);
  if (g_log) fprintf(stderr, "frontend: click at the original's %d,%d\n", x, y);
}

// One step per frame: move, press, release.
static void inj_step(void) {
  if (!g_inj) return;
  if (g_inj_wait > 0) {
    g_inj_wait--;
    return;
  }
  switch (g_inj_stage) {
    case 0:
      sdl_motion(g_inj_x, g_inj_y);
      g_inj_wait = 1;
      if (!g_inj_click) g_inj_stage = 2;   // a hover only moves the pointer
      break;
    case 1:
      sdl_button(g_inj_x, g_inj_y, 1);
      fef_note_press();
      g_inj_wait = 2;
      break;
    case 2: sdl_button(g_inj_x, g_inj_y, 0); g_inj_wait = 1; break;
    default:
      g_inj = 0;
      fef_set_injecting(0);
      return;
  }
  g_inj_stage++;
}

// Modal screens can return on DOWN before another heartbeat releases it.
int course_switch_on(void);
int course_switch_available(int tag);
int course_switch_current(int tag);
int course_switch_request(int tag);
void course_switch_refresh(void);
void course_switch_invalidate(void);
void course_switch_before_purchase(int site);
void course_switch_apply(void);

void fe_world_end(int result) {
  if (getenv("SIMGOLF_COURSE_TRACE")) fprintf(stderr, "course-build %.3f\n", host_now_ms());
  if (g_inj && g_inj_click && g_inj_stage == 2) sdl_button(g_inj_x, g_inj_y, 0);
  g_inj = 0;
  fef_set_injecting(0);
  if (g_world_active && g_log) fprintf(stderr, "frontend: world closed\n");
  g_world_active = 0;
  g_on = 0;
  g_last_screen = FE_NONE;
  if (result >= 0) course_switch_before_purchase(result);
  else course_switch_apply();
  course_switch_invalidate();
  // task-106: Build here published its loading card just before the map loop returned; the original builds the
  // course for seconds without presenting, so that card stays up (golf's next loading call or the course view replaces it)
  if (g_snap.screen == FE_LOADING) {
    FeAction q;
    while (fef_take_action(&q)) {}
    return;
  }
  if (G32(IN_GAME)) {
    g_snap.screen = FE_NONE;
    g_snap.magic = FE_MAGIC;
    g_snap.serial = ++g_serial;
    fef_publish(&g_snap); // an in-game map returns directly to the course
  }
  // At the title, retain the native map until the next front-end snapshot.
  // Clearing it here exposed the original map during the return transition.
  FeAction stale;
  while (fef_take_action(&stale)) {}   // a queued map hover/click must not reopen it over a report
}

// Called at the original theme loop's authoritative keyboard poll.
int fe_theme_key_ready(void) {
  int ready = FUN_0045ae70();
  if (!fef_enabled() || !ready) return ready;
  int key = FUN_0045ae30();
  FUN_0045ae50();
  if (key == 0x1b && !g_inj) inj_start(0x267, 0x22b, 1);
  return 0;
}

// ------------------------------------------------------------------ the property screen

// What the original draws while it loops over its 16 property tags (index
// local_4e0 in the loop): the texts, the emblem it picks, the pin it puts on
// the globe. Recorded per frame, read at the end of the frame (fe_frame).
enum { W_TEXTS = 10 };
typedef struct {
  int seen;
  int n;
  struct { int dx, dy, fn; unsigned col; char s[100]; } t[W_TEXTS];
  int state, owned, hover, theme, sprite;
  int pin_seen, pin_x, pin_y, pin_state;
} WRec;
static WRec g_wrec[FE_TAGS];
static struct { int x, y, fn; char s[64]; } g_wglob[8];
static int g_n_wglob, g_in_tags, g_w_hover = -1;

static int emblem_x(int tag) { return G16(PROPERTY_TABLE + 4 * tag); }
static int emblem_y(int tag) { return G16(PROPERTY_TABLE + 4 * tag + 2); }

// The list screens (load game, championship courses, pro pick, theme packs)
// draw once per change, then loop on the pointer: everything the draw pass
// puts on screen is recorded between fe_base and the first fe_frame.
enum { A_MAX = 256, R_MAX = 40 };
static struct { int fn, x, y; unsigned col; char s[112]; } g_all[A_MAX];
static struct { int x, y, w, h; unsigned col; } g_rects[R_MAX];
static int g_n_all, g_n_rects, g_rec_all;
static uint32_t g_all_serial;

int hd_native_shot(const char *path);
// Test aid only, both off unless the environment asks: SIMGOLF_LOADING_HOLD_MS keeps each loading call on
// screen; SIMGOLF_LOADING_SHOT=<prefix> saves the presented frame as <prefix><n>.bmp (the bridge cannot
// answer while the game thread is inside the load).
static void load_test_aid(void) {
  const char *shot = getenv("SIMGOLF_LOADING_SHOT");
  if (shot && *shot) {
    static int n;
    char path[512];
    SDL_Delay(1200);
    snprintf(path, sizeof path, "%s%d.bmp", shot, ++n);
    hd_native_shot(path);
  }
  const char *hold = getenv("SIMGOLF_LOADING_HOLD_MS");
  if (hold && atoi(hold) > 0) SDL_Delay((Uint32)atoi(hold));
}

static void publish(int screen);
static char g_off_line[64];
static void loading_text(const char *text, int y) {   // task-69: the property screen's own loading text (new game)
  if (y == 0x11d && strstr(text, "off to")) {
    copy_str(g_off_line, sizeof g_off_line, text);
  } else if (y == 300 && strstr(text, "one moment")) {
    memset(&g_snap.loading, 0, sizeof g_snap.loading);
    g_snap.loading.pct = -1;
    copy_str(g_snap.loading.title, sizeof g_snap.loading.title, g_off_line);
    fef_publish_art(FE_ART_LOADING, NULL, 0, 0);
    publish(FE_LOADING);
    if (g_log < 0) g_log = getenv("SIMGOLF_FRONTEND_LOG") != NULL;
    if (g_log) fprintf(stderr, "frontend: loading property \"%s\"\n", g_off_line);
    load_test_aid();
  }
}

int fe_text(int fn, int tag, const char *text, int x, int y, unsigned colour) {
  if (fn == 1 && fef_loading_enabled()) loading_text(text, y);
  if (g_on && g_rec_all && g_n_all < A_MAX) {
    g_all[g_n_all].fn = fn;
    g_all[g_n_all].x = x;
    g_all[g_n_all].y = y;
    g_all[g_n_all].col = colour;
    copy_str(g_all[g_n_all].s, sizeof g_all[0].s, text);
    g_n_all++;
  } else if (g_on && g_in_tags && tag >= 0 && tag < FE_TAGS) {
    WRec *w = &g_wrec[tag];
    if (w->n < W_TEXTS) {
      w->t[w->n].dx = x - emblem_x(tag);
      w->t[w->n].dy = y - emblem_y(tag);
      w->t[w->n].fn = fn;
      w->t[w->n].col = colour;
      copy_str(w->t[w->n].s, sizeof w->t[w->n].s, text);
      w->n++;
    }
  } else if (g_on && g_n_wglob < 8) {
    g_wglob[g_n_wglob].x = x;
    g_wglob[g_n_wglob].y = y;
    g_wglob[g_n_wglob].fn = fn;
    copy_str(g_wglob[g_n_wglob].s, sizeof g_wglob[0].s, text);
    g_n_wglob++;
  }
  switch (fn) {
    case 0: return FUN_00404b70((char *)text, x, y, colour);
    case 1: return FUN_00404bc0((char *)text, x, y, colour);
    case 2: return FUN_004049d0((char *)text, x, y, colour);
    default: return FUN_00404ad0((char *)text, x, y, colour);
  }
}

void FUN_00480b00(void *screen, int x, int y, int w, int h, uint32_t col);
void fe_rect(int x, int y, int w, int h, unsigned col) {
  if (g_on && g_rec_all && g_n_rects < R_MAX) {
    g_rects[g_n_rects].x = x;
    g_rects[g_n_rects].y = y;
    g_rects[g_n_rects].w = w;
    g_rects[g_n_rects].h = h;
    g_rects[g_n_rects].col = col;
    g_n_rects++;
  }
  FUN_00480b00((void *)0x519a60, x, y, w, h, col);
}

// golf's sprite draw with a source picture (the course emblem of a save)
int FUN_00473f60(void *this, int p1, int target, uint32_t x, uint32_t y, int p5);
static void *g_emb_this;
static int g_emb_p1;
static void draw_emblem3(int bx, int by, void *user) {
  (void)user;
  FUN_00473f60(g_emb_this, g_emb_p1, G32(0x4c1570), (uint32_t)(bx + 4), (uint32_t)(by + 4), 0);
}
int fe_sprite3(void *this, int p1, int target, uint32_t x, uint32_t y, int p5) {
  if (g_on && g_rec_all && (g_emb_this != this || g_emb_p1 != p1 || !g_art_done[FE_ART_COURSE])) {
    g_emb_this = this;
    g_emb_p1 = p1;
    g_art_done[FE_ART_COURSE] = 0;
    int w = 0, h = 0;
    uint32_t *px = capture(120, 120, draw_emblem3, NULL, &w, &h);
    if (px) {
      g_art_done[FE_ART_COURSE] = fef_publish_art(FE_ART_COURSE, px, w, h);
      free(px);
    }
  }
  return FUN_00473f60(this, p1, target, x, y, p5);
}

void fe_world_loop(int on) { g_in_tags = on; }

void fe_world_tag(int tag, int state, int theme, int owned, int hover, int sprite) {
  if (!g_on || tag < 0 || tag >= FE_TAGS) return;
  WRec *w = &g_wrec[tag];
  w->seen = 1;
  w->state = state;
  w->theme = theme;
  w->owned = owned;
  w->hover = hover;
  w->sprite = sprite;
}

void fe_world_pin(int tag, int x, int y, int state) {
  if (!g_on || tag < 0 || tag >= FE_TAGS) return;
  g_wrec[tag].pin_seen = 1;
  g_wrec[tag].pin_x = x;
  g_wrec[tag].pin_y = y;
  g_wrec[tag].pin_state = state;
}

void fe_world_hover(int tag) { g_w_hover = tag; }

static void draw_emblem(int bx, int by, void *user) {
  FUN_00473e60((void *)(uintptr_t)(SPRITE_EMBLEM + (uintptr_t)(intptr_t)user * 0x2c), G32(0x4c1570), bx + 8, by + 8, 0);
}

// Once per captured source globe, not per frame. The original land mask and projection stay at exactly the same
// pixels, so the game's recorded pin coordinates remain authoritative. Only the globe's material changes.
static void matte_map_globe(uint32_t *px, int w, int h) {
  float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f, rad = fminf(w, h) * 0.5f;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      uint32_t old = px[y * w + x];
      float nx = (x - cx) / rad, ny = (y - cy) / rad, rr = nx * nx + ny * ny;
      if (rr >= 1 || !(old >> 24)) { px[y * w + x] = 0; continue; }
      int r = (old >> 16) & 255, g = (old >> 8) & 255, b = old & 255;
      int land = g > b * 0.88f && g > r * 1.04f && g > 45;
      float nz = sqrtf(1 - rr), light = fmaxf(0, -nx * 0.35f - ny * 0.42f + nz * 0.72f);
      float shade = 0.77f + 0.23f * light;
      // Staggered shallow dimples, softened and compressed near the silhouette. No halo or specular glow.
      int row = (int)floorf(ny / 0.095f + 0.5f);
      float dx = nx / 0.10f - (row & 1) * 0.5f;
      dx = (dx - floorf(dx + 0.5f)) * 0.10f;
      float dy = ny - row * 0.095f;
      float crater = sqrtf(dx * dx + dy * dy) / (0.026f + 0.012f * nz);
      float relief = crater < 1 ? (1 - crater) * (dy < 0 ? -12.0f : 7.0f) * nz : 0;
      int red = (int)((land ? 120 : 247) * shade + relief);
      int green = (int)((land ? 185 : 246) * shade + relief);
      int blue = (int)((land ? 164 : 237) * shade + relief);
      uint32_t alpha = old & 0xff000000u;
      px[y * w + x] = alpha | (uint32_t)red << 16 | (uint32_t)green << 8 | (uint32_t)blue;
    }
}

static void world_begin(void) {
  course_switch_refresh();
  memset(g_wrec, 0, sizeof g_wrec);
  g_n_wglob = 0;
  g_in_tags = 0;
  if (!g_art_done[FE_ART_GLOBE]) {
    // the globe, in the base picture before the pins and tags are drawn on it
    int w = 0, h = 0;
    // The HD Frosted Mint golf-ball globe (the maintainer 2026-10-13, ui/map_globe.png): fitted to golf's land so the pins
    // stay on their countries; it covers golf's 392 px circle plus 16 px on every side (the drawer knows by size).
    // [ui] map_globe=original keeps golf's own globe, map_globe=ball the earlier matte recolour.
    const char *mg = mods_get("ui.map_globe");
    int original = mg && (!strcmp(mg, "original") || !strcmp(mg, "ball"));
    uint32_t *px = original ? NULL : hd_art_load_png(port_host_path("/ui/map_globe.png"), &w, &h);
    fprintf(stderr, "frontend: globe %s (%dx%d)\n", px ? "hd" : "original", w, h);
    if (!px) px = grab_circle(201, 308, 196, &w, &h);
    else original = -1;
    if (px) {
      if (original > 0 && !strcmp(mg, "ball")) matte_map_globe(px, w, h);
      g_art_done[FE_ART_GLOBE] = fef_publish_art(FE_ART_GLOBE, px, w, h);
      free(px);
    }
  }
  if (!g_art_done[FE_ART_EMBLEM]) {
    int ok = 0;
    for (int i = 0; i < 16; i++) {
      int w = 0, h = 0;
      uint32_t *px = capture(120, 120, draw_emblem, (void *)(intptr_t)i, &w, &h);
      if (px) {
        ok += fef_publish_art(FE_ART_EMBLEM + i, px, w, h);
        free(px);
      }
      if (g_log) fprintf(stderr, "frontend: emblem sprite %d -> %s %dx%d\n", i, px ? "ok" : "nothing", w, h);
    }
    g_art_done[FE_ART_EMBLEM] = ok == 16;
  }
  if (!g_art_done[FE_ART_BADGE]) {
    // The painted property badges (task-109, owner approved 2026-10-13, ui/badges/badge-NN.png, San Diego .. Northeast),
    // plus a greyed copy for rows you cannot buy. [ui] map_badges=original keeps golf's emblems.
    const char *mb = mods_get("ui.map_badges");
    int ok = 0, retry = 0;
    for (int i = 0; i < 16 && !(mb && !strcmp(mb, "original")); i++) {
      char path[64];
      snprintf(path, sizeof path, "/ui/badges/badge-%02d.png", i);
      int w = 0, h = 0;
      uint32_t *px = hd_art_load_png(port_host_path(path), &w, &h);
      if (!px) continue;
      if (!fef_publish_art(FE_ART_BADGE + i, px, w, h)) retry = 1;
      for (int k = 0; k < w * h; k++) {
        uint32_t p = px[k];
        int lum = (int)((p >> 16 & 255) * 0.30f + (p >> 8 & 255) * 0.59f + (p & 255) * 0.11f);
        int r = (lum * 65 + 200 * 35) / 100, g = (lum * 65 + 210 * 35) / 100, b = (lum * 65 + 205 * 35) / 100;
        px[k] = (p & 0xff000000u) | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
      }
      if (!fef_publish_art(FE_ART_BADGE_DIM + i, px, w, h)) retry = 1;
      free(px);
      ok++;
    }
    fprintf(stderr, "frontend: property badges %d loaded\n", ok);
    g_art_done[FE_ART_BADGE] = !retry;
  }
}

// the painted badge for a property, by the name golf shows (FeTag.badge), -1 unknown
static int badge_of(const char *name) {
  static const char *names[16] = {"San Diego", "Jamaica", "Scotland", "Carolina", "Rocky Mtn", "Nova Scotia", "Monterey", "Ireland",
                                  "Phoenix", "Las Vegas", "Oahu", "Hawaii", "Wales", "Florida", "Spain", "Northeast"};
  for (int i = 0; i < 16; i++)
    if (!strncasecmp(name, names[i], strlen(names[i]))) return i;
  return -1;
}

static void log_world(void) {
  static unsigned last;
  if (!g_log || SDL_GetTicks() - last < 1500) return;
  last = SDL_GetTicks();
  for (int i = 0; i < g_n_wglob; i++) fprintf(stderr, "frontend: world text fn%d at %d,%d \"%s\"\n", g_wglob[i].fn, g_wglob[i].x, g_wglob[i].y, g_wglob[i].s);
  for (int i = 0; i < FE_TAGS; i++) {
    WRec *w = &g_wrec[i];
    if (!w->seen) continue;
    fprintf(stderr, "frontend: tag %d emblem %d,%d state %d theme %d owned %d hover %d pin %d,%d s%d texts %d\n", i, emblem_x(i), emblem_y(i),
            w->state, w->theme, w->owned, w->hover, w->pin_x, w->pin_y, w->pin_state, w->n);
    for (int k = 0; k < w->n; k++) fprintf(stderr, "frontend:    +%d,+%d fn%d \"%s\"\n", w->t[k].dx, w->t[k].dy, w->t[k].fn, w->t[k].s);
  }
}

// golf's text uses 0xA7 for the dollar sign
static void money_text(char *dst, int cap, const char *src) {
  int n = 0;
  while (n < cap - 1 && src && src[n]) {
    dst[n] = (unsigned char)src[n] == 0xA7 ? '$' : src[n];
    n++;
  }
  dst[n] = 0;
}

// the last number after a dollar sign: "Buy 60 acres ... only $60,000." -> 60000
static int price_in(const char *s) {
  const char *p = NULL;
  for (const char *q = s; *q; q++)
    if ((unsigned char)*q == 0xA7 || *q == '$') p = q + 1;
  if (!p) return 0;
  int v = 0;
  for (; *p; p++) {
    if (*p >= '0' && *p <= '9') v = v * 10 + (*p - '0');
    else if (*p != ',') break;
  }
  return v;
}

static int acres_in(const char *s) {
  for (const char *q = s; *q; q++)
    if (*q >= '0' && *q <= '9') return atoi(q);
  return 0;
}

static void read_world(void) {
  FeWorld *W = &g_snap.world;
  memset(W, 0, sizeof *W);
  W->n = FE_TAGS;
  W->hover = g_w_hover;
  W->sandbox = (G32(0x59e7b8) & 0x1000000) != 0;
  { const char *mi = mods_get("ui.map_icons"); W->medallions = mi && !strcmp(mi, "medallions"); }
  { const char *mb = mods_get("ui.map_badges"); W->badges = !(mb && !strcmp(mb, "original")); }
  int diff = G32(0x822c88);
  W->difficulty = diff >= 0 && diff < 4 ? diff : -1;
  // Refresh rerolls site indices; snapshots are keyed by those indices.
  W->multi_course = G32(IN_GAME) && course_switch_on();
  W->can_refresh = !W->multi_course;
  W->gcx = 201;
  W->gcy = 308;
  W->gr = 196;
  snprintf(W->heading, sizeof W->heading, "Where will you build your golf course?");
  (void)0;
  for (int i = 0; i < g_n_wglob; i++)
    if (g_wglob[i].x > 600 && g_wglob[i].y < 40) money_text(W->budget, sizeof W->budget, g_wglob[i].s);
  if (W->sandbox) snprintf(W->budget, sizeof W->budget, "Unlimited");   // golf draws "Unlimited" and a dollar sign
  // acres and price are only drawn while a tag is not highlighted: remembered by name
  static struct { char name[32]; int acres, price; } mem[FE_TAGS * 2];
  static int n_mem;
  for (int i = 0; i < FE_TAGS; i++) {
    WRec *w = &g_wrec[i];
    FeTag *T = &W->tag[i];
    T->ex = (int16_t)emblem_x(i);
    T->ey = (int16_t)emblem_y(i);
    T->art = -1;
    T->badge = -1;
    if (!w->seen) continue;
    T->theme = (int8_t)w->theme;
    T->state = w->owned ? 2 : (w->state == 1 || w->state == 2) ? 1 : 0;
    T->art = (int8_t)(w->theme * 4 + (T->state == 1 ? 1 : 0));
    T->px = (int16_t)w->pin_x;
    T->py = (int16_t)w->pin_y;
    T->pin = (int8_t)w->pin_state;
    if (G32(IN_GAME) && course_switch_on()) {
      T->current = (int8_t)course_switch_current(i);
      if (T->current) T->state = 2;
      else if (T->state == 2) T->can_switch = (int8_t)course_switch_available(i);
      T->art = (int8_t)(w->theme * 4 + (T->state == 1 ? 1 : 0));
    }
    char info[100] = "";
    for (int k = 0; k < w->n; k++) {
      int dy = w->t[k].dy;
      if (dy == 4) money_text(T->name, sizeof T->name, w->t[k].s);
      else if (dy == 16) money_text(T->feature, sizeof T->feature, w->t[k].s);
      else if (dy >= 30) {
        if (!strncmp(w->t[k].s, "Buy ", 4) || T->n_detail > 0) {
          if (T->n_detail < 3) {
            money_text(T->detail[T->n_detail], sizeof T->detail[0], w->t[k].s);
            T->n_detail++;
          }
        } else {
          money_text(info, sizeof info, w->t[k].s);
        }
      }
    }
    T->badge = (int8_t)badge_of(T->name);
    if (info[0]) {
      copy_str(T->info, sizeof T->info, info);
      T->acres = acres_in(info);
      T->price = price_in(info);
    } else if (T->n_detail > 0) {
      T->acres = acres_in(T->detail[0] + 4);
      char all[240] = "";
      for (int k = 0; k < T->n_detail; k++) {
        strncat(all, T->detail[k], sizeof all - strlen(all) - 1);
        strncat(all, " ", sizeof all - strlen(all) - 1);
      }
      T->price = price_in(all);
    }
    // remember (or recall) by name
    int m = -1;
    for (int k = 0; k < n_mem; k++)
      if (!strcmp(mem[k].name, T->name)) m = k;
    if (T->price && T->acres) {
      if (m < 0 && n_mem < FE_TAGS * 2) m = n_mem++;
      if (m >= 0) {
        copy_str(mem[m].name, sizeof mem[m].name, T->name);
        mem[m].acres = T->acres;
        mem[m].price = T->price;
      }
    } else if (m >= 0) {
      T->acres = mem[m].acres;
      T->price = mem[m].price;
    }
  }
  log_world();
}

// ------------------------------------------------------------------ the load game and championship lists

static int g_lv_count, g_lv_sel, g_lv_p1, g_lv_p2;
static int *g_lv_scroll;
static int *g_lv_selection;
static int g_lv_scroll_val;

void fe_list_notice(void) {
  void file_list_draw_notice(void);
  if (!fef_enabled()) file_list_draw_notice();
}

void fe_list_vars(int count, int *scroll, int *sel, int p1, int p2) {
  g_lv_count = count;
  g_lv_scroll = scroll;
  g_lv_scroll_val = scroll ? *scroll : 0;
  g_lv_selection=sel;
  g_lv_sel = sel?*sel:-1;
  g_lv_p1 = p1;
  g_lv_p2 = p2;
}

void fe_suspend(void) {
  if (dlgf_enabled()) return;   // task-27: the dialog is drawn natively over the native screen, which stays
  g_snap.screen = FE_NONE;
  g_snap.magic = FE_MAGIC;
  g_snap.serial = ++g_serial;
  fef_publish(&g_snap);
}

// text of the draw pass at (x, y) (fn -1: any)
static const char *text_at(int fn, int x, int y) {
  for (int i = 0; i < g_n_all; i++)
    if (g_all[i].x == x && g_all[i].y == y && (fn < 0 || g_all[i].fn == fn)) return g_all[i].s;
  return NULL;
}

static void read_load(void) {
  int file_list_omitted(void);
  FeLoad *L = &g_snap.load;
  memset(L, 0, sizeof *L);
  L->list.omitted = file_list_omitted();
  L->championship = g_lv_p2 != 0;
  L->list.sort_mode=file_save_sort_mode();
  L->list.save_sort=!L->championship&&L->list.sort_mode>=0;
  const char *t = text_at(-1, 0x1f8, 0x2a);
  copy_str(L->list.title, sizeof L->list.title, t ? t : (L->championship ? "Select Championship Course" : "Load Previous Game"));
  L->list.total = g_lv_count;
  L->list.scroll = g_lv_scroll_val;
  L->list.sel = -1;
  for (int i = 0; i < g_n_all; i++) {
    if (g_all[i].x != 0x140) continue;
    if (g_all[i].y >= 0x74 && g_all[i].y < 0x74 + 16 * 16) {
      int r = (g_all[i].y - 0x74) / 16;
      if (!strncmp(g_all[i].s, "(more", 5)) L->list.more = 1;
      else if (r >= 0 && r < FE_ROWS) {
        copy_str(L->list.row[r], sizeof L->list.row[r], g_all[i].s);
        if (r + 1 > L->list.n_rows) L->list.n_rows = r + 1;
      }
    } else if (g_all[i].y >= 0x74 + 16 * 16 && !strncmp(g_all[i].s, "(more", 5)) {
      L->list.more = 1;
    }
  }
  int s = g_lv_sel - g_lv_scroll_val;
  if(L->list.save_sort)for(int row=0;row<L->list.n_rows;row++){
    int index=g_lv_scroll_val+row;SaveRatings rating;
    if(index>=0&&index<100&&file_save_rating((const char *)(uintptr_t)(0x80b130+index*100),&rating))
      snprintf(L->list.detail[row],sizeof L->list.detail[row],"Fun %d  Skill %.2f",rating.fun,(double)rating.skill/100.0);
  }
  L->list.sel = g_lv_sel >= 0 && s >= 0 && s < FE_ROWS ? s : -1;
  L->has_info = g_lv_sel >= 0;
  if (!L->has_info) return;
  copy_str(L->holes, sizeof L->holes, text_at(0, 0x42, 0x92));
  copy_str(L->par, sizeof L->par, text_at(0, 0x7d, 0x92));
  copy_str(L->yards, sizeof L->yards, text_at(0, 0xb8, 0x92));
  money_text(L->cash, sizeof L->cash, text_at(0, 0xa1, 0xa9));
  static const int ys[4] = {0xc0, 0xd2, 0xe4, 0xf6};
  for (int i = 0; i < 4; i++) {
    copy_str(L->stat_label[i], sizeof L->stat_label[i], text_at(2, 0x30, ys[i]));
    copy_str(L->stat_value[i], sizeof L->stat_value[i], text_at(0, 0xb8, ys[i]));
  }
  copy_str(L->line1, sizeof L->line1, text_at(2, 0x184, 0x1ba));
  copy_str(L->line2, sizeof L->line2, text_at(2, 0x184, 0x1ca));
  copy_str(L->line3, sizeof L->line3, text_at(2, 0x184, 0x1da));
  for (int k = 0; k < FE_HOLES; k++) {
    int y = 0x11e + 0x11 * k;
    const char *h = text_at(0, 0x42, y);
    if (!h) break;
    copy_str(L->hole_no[k], sizeof L->hole_no[k], h);
    copy_str(L->hole_par[k], sizeof L->hole_par[k], text_at(0, 0x7d, y));
    copy_str(L->hole_yards[k], sizeof L->hole_yards[k], text_at(0, 0xb8, y));
    L->n_holes = k + 1;
  }
}

// ------------------------------------------------------------------ the pro pick

int FUN_00473cb0(void *this, int target_p, int x, int y, int a, int b, int c, int d);
static void *g_pro_this;
static int g_pro_p1, g_pro_x, g_pro_y;
static void draw_portrait3(int bx, int by, void *user) {
  (void)user;
  FUN_00473cb0(g_pro_this, G32(0x4c1570), bx + 8, by + 8, 1, 1, 1, 0);
}
// golf's portrait sprite draw in the pro screen: read back with alpha when the picture changes
int fe_sprite_b(void *self, int target, int x, int y, int a, int b, int c, int d) {
  if (g_on && g_rec_all && (g_pro_this != self || !g_art_done[FE_ART_PORTRAIT])) {
    g_pro_this = self;
    g_art_done[FE_ART_PORTRAIT] = 0;
    int w = 0, h = 0;
    uint32_t *px = capture(160, 160, draw_portrait3, NULL, &w, &h);
    if (px) {
      g_art_done[FE_ART_PORTRAIT] = fef_publish_art(FE_ART_PORTRAIT, px, w, h);
      free(px);
    }
  }
  return FUN_00473cb0(self, target, x, y, a, b, c, d);
}

static void read_pro(void) {
  int file_list_omitted(void);
  FePro *P = &g_snap.pro;
  memset(P, 0, sizeof *P);
  P->list.omitted = file_list_omitted();
  const char *t = text_at(-1, 0x1f8, 0x2a);
  copy_str(P->list.title, sizeof P->list.title, t ? t : "Pick A Pro");
  P->list.total = g_lv_count;
  P->list.scroll = g_lv_scroll_val;
  P->list.sel = -1;
  for (int i = 0; i < g_n_all; i++) {
    if (g_all[i].x != 0x140 || g_all[i].y < 0x74 || g_all[i].y >= 0x74 + 16 * 16) continue;
    int r = (g_all[i].y - 0x74) / 16;
    if (!strncmp(g_all[i].s, "(more", 5)) P->list.more = 1;
    else if (r >= 0 && r < FE_ROWS) {
      copy_str(P->list.row[r], sizeof P->list.row[r], g_all[i].s);
      if (r + 1 > P->list.n_rows) P->list.n_rows = r + 1;
    }
  }
  int s = g_lv_sel - g_lv_scroll_val;
  P->list.sel = g_lv_sel >= 0 && s >= 0 && s < FE_ROWS ? s : -1;
  P->has_info = g_lv_sel >= 0;
  if (!P->has_info) return;
  const char *nm = text_at(2, 0x38, 0xff);
  copy_str(P->name, sizeof P->name, nm ? nm : "");
  char *ap = strstr(P->name, "'s skills");
  if (ap) *ap = 0;
  copy_str(P->saying_label, sizeof P->saying_label, text_at(2, 0x38, 0x220));
  copy_str(P->saying, sizeof P->saying, text_at(3, 0x24, 0x232));
  for (int k = 0; k < 10 && P->n_skills < 12; k++) {
    int y = 0x117 + 0x18 * k;
    const char *lab = text_at(2, 0x5e, y);
    if (!lab) continue;
    copy_str(P->skill[P->n_skills], sizeof P->skill[0], lab);
    copy_str(P->skill_value[P->n_skills], sizeof P->skill_value[0], text_at(0, 0x3b, y));
    P->n_skills++;
  }
}

// ------------------------------------------------------------------ the theme packs

static int g_th_n, g_th_sel;
static unsigned char g_th_mask[FE_THEMES];

// listThemes (004724c0) has just built 50-byte names and reset THEME_NAME.
// Only restore a directory the actual game loader discovered this session.
void fe_theme_restore(int count) {
  const char *saved = mods_get("ui.active_theme");
  if (!saved || !*saved) return;
  for (int i = 0; i < count && i < FE_THEMES; i++) {
    const char *name = (const char *)(uintptr_t)(0x56e928u + (unsigned)i * 50u);
    if (!strncasecmp(saved, name, 50) && strlen(saved) < 50) {
      copy_str((char *)(uintptr_t)THEME_NAME, 50, name);
      return;
    }
  }
}

void fe_theme_remember(void) {
  const char *name = (const char *)(uintptr_t)THEME_NAME;
  if (*name && strlen(name) < 50) mods_option_set("ui.active_theme", name);
}

void fe_theme_vars(int n, const unsigned char *mask, int sel) {
  g_th_n = n > FE_THEMES ? FE_THEMES : n;
  g_th_sel = sel;
  for (int i = 0; i < g_th_n; i++) g_th_mask[i] = mask ? mask[i] : 0;
}

static void read_theme(void) {
  FeTheme *T = &g_snap.theme;
  memset(T, 0, sizeof *T);
  T->n = g_th_n;
  T->sel = g_th_sel;
  const char *h = text_at(2, 0x4e, 0x22);
  copy_str(T->heading, sizeof T->heading, h ? h : "Select a Theme Pack");
  static const char *const cols[5] = {"Stories", "Characters", "Celebrities", "Pro Golfers", "Courses"};
  for (int i = 0; i < 5; i++) copy_str(T->column[i], sizeof T->column[i], cols[i]);
  for (int i = 0; i < T->n; i++) {
    const char *nm = text_at(2, 0x49, 0x5c + 0x20 * i + 0xb);
    copy_str(T->name[i], sizeof T->name[i], nm ? nm : "");
    T->mask[i] = g_th_mask[i];
  }
}

// where each native action goes on the original screen (original 800x600 pixels)
static int action_point(int screen, const FeAction *a, int *x, int *y) {
  if (screen == FE_TITLE && a->kind == FE_A_TITLE) {
    // the title picks the nearest of six points (FUN_0040acd0 on dx and dy * 4 - 0x18): the text positions
    static const int tx[6] = {182, 573, 631, 415, 141, 735};   // continue, new, theme, championship, sandbox, exit
    static const int ty[6] = {72, 94, 415, 531, 437, 540};
    if (a->arg < 0 || a->arg > 5) return 0;
    *x = tx[a->arg];
    *y = ty[a->arg];
    return 1;
  }
  if (screen == FE_THEME) {
    switch (a->kind) {
      case FE_A_ROW: *x = 150; *y = 0x5c + 0x20 * a->arg + 12; return a->arg >= 0 && a->arg < FE_THEMES;   // the row's name
      // Rows already update the selected theme. Back keeps that selection;
      // the original Cancel disc would silently replace it with Standard.
      case FE_A_OK:
      case FE_A_BACK: *x = 0x267; *y = 0x22b; return 1;
      default: return 0;
    }
  }
  if (screen == FE_LOAD || screen == FE_PRO) {
    switch (a->kind) {
      case FE_A_ROW: *x = 336; *y = 0x74 + 16 * a->arg + 8; return a->arg >= 0 && a->arg < FE_ROWS;
      case FE_A_OK: *x = 0x279; *y = 0x22b; return 1;
      case FE_A_BACK: *x = 0x301; *y = 0x22c; return 1;
      case FE_A_DELETE: *x = 0x2c0; *y = 0x22b; return 1;
      case FE_A_SCROLL: *x = 0x310; *y = a->arg < 0 ? 0x78 : 400; return 1;   // golf's scroll arrows: 4 rows a click
      default: return 0;
    }
  }
  if (screen == FE_WORLD) {
    if ((a->kind == FE_A_TAG || a->kind == FE_A_HOVER_TAG) && a->arg >= 0 && a->arg < FE_TAGS) {
      *x = emblem_x(a->arg) + 25;   // the original's hover test is the distance from the emblem's centre
      *y = emblem_y(a->arg) + 25;
      return 1;
    }
    if (a->kind == FE_A_BACK) {
      *x = 768;
      *y = 557;
      return 1;
    }
    if (a->kind == FE_A_REFRESH) {
      *x = 760;
      *y = 476;
      return 1;
    }
  }
  if (screen == FE_DIFFICULTY) {
    if (a->kind == FE_A_DIFFICULTY && a->arg >= 0 && a->arg < 4) {
      static const int dy[4] = {119, 233, 352, 470};
      *x = 390;
      *y = dy[a->arg];
      return 1;
    }
    if (a->kind == FE_A_BACK) {
      *x = 767;
      *y = 557;
      return 1;
    }
  }
  return 0;
}

static void run_actions(int screen) {
  if (g_inj) return;
  FeAction a;
  if (!fef_take_action(&a)) return;
  if (screen == FE_WORLD && a.kind == FE_A_SWITCH) {
    if (a.arg >= 0 && a.arg < FE_TAGS && g_snap.world.tag[a.arg].state == 2 &&
        course_switch_request(a.arg)) inj_start(768, 557, 1);
    return;
  }
  if(screen==FE_LOAD&&!g_lv_p2&&a.kind==FE_A_SAVE_SORT&&a.arg>=0&&a.arg<=2&&file_save_sort_mode()>=0){
    char value[4];snprintf(value,sizeof value,"%d",a.arg);mods_option_set("ui.save_sort",value);
    if(g_lv_selection)*g_lv_selection=-1;
    if(g_lv_scroll)*g_lv_scroll=0;
    g_lv_sel=-1;g_lv_scroll_val=0;
    // Let the original list rebuild its authoritative filename slots.
    inj_start(0x310,0x78,1);return;
  }
  int x, y;
  if (action_point(screen, &a, &x, &y)) inj_start(x, y, a.kind != FE_A_HOVER_TAG);
}

// ------------------------------------------------------------------ the hooks

static void publish(int screen) {
  g_snap.magic = FE_MAGIC;
  g_snap.serial = ++g_serial;
  g_snap.screen = screen;
  // the load list is told by its caller (1: the title's Continue, 0: the menu in a running game); the flag
  // of a running course is not set yet on the title
  g_snap.in_game = screen == FE_LOAD ? g_lv_p1 == 0 && g_lv_p2 == 0 : G32(IN_GAME) != 0;
  fef_publish(&g_snap);
}

static void read_title(void) {
  copy_str(g_snap.theme_name, sizeof g_snap.theme_name, (const char *)(uintptr_t)THEME_NAME);
  if (!g_art_done[FE_ART_LOGO]) {
    int w = 0, h = 0;
    // task-80: the HD "SimGolf Remastered" logo next to the exe; [ui] logo=original keeps the game's own oval
    const char *v = mods_get("ui.logo");
    uint32_t *px = v && !strcasecmp(v, "original") ? NULL : hd_art_load_png(port_host_path("/ui/logo_remastered.png"), &w, &h);
    g_snap.logo_full = px != NULL;
    fprintf(stderr, "frontend: logo %s (%dx%d)\n", px ? "remastered" : "original", w, h);
    if (!px) px = capture(520, 220, draw_logo, NULL, &w, &h);
    if (px) {
      g_art_done[FE_ART_LOGO] = fef_publish_art(FE_ART_LOGO, px, w, h);
      free(px);
    }
  }
}

static void read_difficulty(void) {
  static int cw[4] = {-1, -1, -1, -1}, cf[4] = {-1, -1, -1, -1};
  static const int wp[4] = {512, 341, 256, 204};   // the game's own wage period, 1024 / (difficulty + 2)
  for (int i = 0; i < 4; i++) {
    int period = mdata_at(&cw[i], "difficulty", i, "wage_period", wp[i]);
    g_snap.wages[i] = period > 0 ? (1024 + period / 2) / period : 0;
    g_snap.fatigue[i] = mdata_at(&cf[i], "difficulty", i, "hole_fatigue", i + 1);
  }
  g_snap.championship = g_champ;
}

// ------------------------------------------------------------------ loading (task-69)

// golf's loading screen (FUN_00406250) draws a picture, a quote and a bar, then flips. The picture is
// read once per load; every call publishes the screen with the new percentage. No change to the load
// order or timing: the hooks only read.
#define LOAD_PIC_W 620
#define LOAD_PIC_H 202
static int g_load_pic_ok;
static void draw_load_pic(int bx, int by, void *user) {
  FUN_00473e60(user, G32(0x4c1570), bx, by, 0);
}
static void *g_load_sprite;

void fe_loading_pic(void *sprite) {
  if (!fef_loading_enabled()) return;
  g_load_sprite = sprite;
}

void fe_loading(int pct, const char *quote) {
  g_frontend_page = 1;
  if (!fef_loading_enabled()) return;
  if (pct <= 0 || g_snap.screen != FE_LOADING) g_snap.loading.epoch++;
  if (pct <= 0) g_load_pic_ok = 0;
  if (!g_load_pic_ok && g_load_sprite) {
    int w = 0, h = 0;
    uint32_t *px = capture(LOAD_PIC_W, LOAD_PIC_H, draw_load_pic, g_load_sprite, &w, &h);
    if (px) {
      g_load_pic_ok = fef_publish_art(FE_ART_LOADING, px, w, h);
      free(px);
    }
  }
  g_snap.loading.pct = pct;
  g_snap.loading.title[0] = 0;
  {   // golf's text is Windows-1252: the kit draws UTF-8
    char *o = g_snap.loading.quote;
    int n = 0, cap = (int)sizeof g_snap.loading.quote;
    for (const unsigned char *q = (const unsigned char *)quote; q && *q && n < cap - 3; q++) {
      if (*q < 0x20) o[n++] = ' ';   // golf's quotes carry a line break before the author
      else if (*q < 0x80) o[n++] = (char)*q;
      else if (*q >= 0xa0) {
        o[n++] = (char)(0xc0 | (*q >> 6));
        o[n++] = (char)(0x80 | (*q & 0x3f));
      }
    }
    o[n] = 0;
  }
  publish(FE_LOADING);
  if (g_log < 0) g_log = getenv("SIMGOLF_FRONTEND_LOG") != NULL;
  if (g_log) fprintf(stderr, "frontend: loading pct %d art %d \"%.40s\"\n", pct, g_load_pic_ok, quote);
  // test aid only: SIMGOLF_LOADING_HOLD_MS keeps each call on screen so a screenshot can catch it
  load_test_aid();
}

void fe_loading_progress(int assets) {
  if (fef_loading_enabled()) fef_loading_progress(assets / 4.0f);
}

void fe_context(int title_choice) {
  g_champ = title_choice == 2;   // the title's pick: 2 is the championship
}

void fe_base(int screen) {
  g_frontend_page = 1;
  if (!fef_enabled()) return;
  g_on = 1;
  if (screen == FE_WORLD) {
    world_begin();
    return;
  }
  if (screen == FE_LOAD || screen == FE_PRO || screen == FE_THEME) {
    g_n_all = g_n_rects = 0;   // the draw pass that follows is recorded until the first heartbeat
    g_rec_all = 1;
    return;
  }
  if (screen == FE_DIFFICULTY && !g_art_done[FE_ART_FACE]) {
    // the four faces, in the picture before any hover highlight is drawn over them
    static const int fc[4][3] = {{243, 84, 46}, {203, 218, 46}, {211, 372, 46}, {252, 507, 46}};
    int ok = 0;
    for (int i = 0; i < 4; i++) {
      int w = 0, h = 0;
      uint32_t *px = grab_circle(fc[i][0], fc[i][1], fc[i][2], &w, &h);
      if (px) {
        ok += fef_publish_art(FE_ART_FACE + i, px, w, h);
        free(px);
      }
    }
    g_art_done[FE_ART_FACE] = ok == 4;
  }
}

void fe_frame(int screen) {
  g_frontend_page = 1;
  static int theme_started;
  if (screen == FE_TITLE && !theme_started) {
    theme_started = 1;
    const char *saved = mods_get("ui.active_theme");
    if (saved && *saved) FUN_004724c0();
  }
  if (g_log < 0) g_log = getenv("SIMGOLF_FRONTEND_LOG") != NULL;
  if (screen == FE_WORLD && !g_world_active) {
    g_world_active = 1;
    if (g_log) fprintf(stderr, "frontend: world opened in_game %d\n", G32(IN_GAME) != 0);
  }
  g_on = fef_enabled();
  dlgf_enabled();   // task-27: registers the dialogs layer (after this one, so it draws over the screen) early, a dialog on a front-end screen needs it up
  if (!g_on) {
    // The in-game map keeps its original presentation, even with the new HUD.
    // Its Escape route must not depend on the title-screen appearance setting.
    if (screen == FE_WORLD && G32(IN_GAME)) {
      if (!g_inj && FUN_0045ae70() && FUN_0045ae30() == 0x1b) inj_start(768, 557, 1);
      inj_step();
    }
    return;
  }
  if (screen != g_last_screen) {
    g_last_screen = screen;
    if (g_log) fprintf(stderr, "frontend: screen %d\n", screen);
  }
  switch (screen) {
    case FE_TITLE: read_title(); break;
    case FE_DIFFICULTY: read_difficulty(); break;
    case FE_WORLD:
      read_world();
      break;
    case FE_LOAD:
      g_rec_all = 0;
      read_load();
      break;
    case FE_THEME:
      g_rec_all = 0;
      read_theme();
      break;
    case FE_PRO:
      g_rec_all = 0;
      read_pro();
      break;
    default: break;
  }
  if (g_log) {   // one line whenever what a screen shows changes (tests read these)
    static char last[256];
    char now[256];
    switch (screen) {
      case FE_TITLE: snprintf(now, sizeof now, "title theme \"%s\"", g_snap.theme_name); break;
      case FE_DIFFICULTY: snprintf(now, sizeof now, "difficulty championship %d wages %d %d %d %d", g_snap.championship, g_snap.wages[0], g_snap.wages[1], g_snap.wages[2], g_snap.wages[3]); break;
      case FE_WORLD: {
        int avail = 0, shown = 0;
        for (int i = 0; i < FE_TAGS; i++) {
          avail += g_snap.world.tag[i].state == 1;
          shown += g_snap.world.tag[i].name[0] != 0;
        }
        snprintf(now, sizeof now, "world tags %d available %d hover %d sandbox %d budget \"%s\"", shown, avail, g_snap.world.hover, g_snap.world.sandbox, g_snap.world.budget);
        break;
      }
      case FE_LOAD: snprintf(now, sizeof now, "list championship %d rows %d total %d scroll %d sel %d holes %d \"%s\"", g_snap.load.championship, g_snap.load.list.n_rows, g_snap.load.list.total, g_snap.load.list.scroll, g_snap.load.list.sel, g_snap.load.n_holes, g_snap.load.line1); break;
      case FE_PRO: snprintf(now, sizeof now, "pro rows %d sel %d skills %d \"%s\"", g_snap.pro.list.n_rows, g_snap.pro.list.sel, g_snap.pro.n_skills, g_snap.pro.name); break;
      case FE_THEME: snprintf(now, sizeof now, "theme rows %d sel %d \"%s\"", g_snap.theme.n, g_snap.theme.sel, g_snap.theme.sel >= 0 ? g_snap.theme.name[g_snap.theme.sel] : ""); break;
      default: now[0] = 0;
    }
    if (strcmp(now, last)) {
      strcpy(last, now);
      fprintf(stderr, "frontend: state %s\n", now);
    }
  }
  publish(screen);
  run_actions(screen);
  // task-27: Esc closes the world map in a running game (the original only knows its back disc)
  if (screen == FE_WORLD && G32(IN_GAME) && !g_inj && FUN_0045ae70() && FUN_0045ae30() == 0x1b) inj_start(768, 557, 1);
  inj_step();
}
