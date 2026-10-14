// Frosted Mint HUD, game-thread side, native build (simgolf mod, task-30).
// The overview is at the top of golf/hud_hooks.c (which holds the news strip
// override and the browser build's no-op versions of these hooks). This file
// does not include golf_protos.h (its types clash with the C library headers);
// the few game functions it calls are declared below with the same ABI.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/dialogs_frosted.h"
#include "port/hud_frosted.h"
#include "port/host.h"
#include "mods/mdata.h"
#include "port/grid.h"

#define G32(a) (*(volatile int32_t *)(uintptr_t)(a))
#define G16(a) (*(volatile int16_t *)(uintptr_t)(a))
#define G8(a) (*(volatile int8_t *)(uintptr_t)(a))

#define FLAGS 0x59e7b8
#define DOCK_MODE 0x567afc
#define SCREEN_STATE 0x569498
#define TEXT_BUF 0x51a068
#define NEWS_TEXT 0x5a6d40
#define MOUSE_X 0x822d60
#define MOUSE_Y 0x822d64

// translated game functions (build/gen/golf/golf_protos.h)
uint32_t FUN_004657e0(int x, int y);                                                       // left click entry
int FUN_00473cb0(void *sprite, int target, int x, int y, int sx, int sy, int div, int clip);  // draw sprite
uint32_t FUN_00473f60(void *sprite, int mask, int target, uint32_t x, uint32_t y, int clip);  // draw sprite + alpha
int FUN_00473e60(void *sprite, int target, int x, int y, int clip);                         // draw sprite
int FUN_0040daa0(int what);                                                                // course name -> text_buf
uint32_t FUN_0040d7b0(int tick);                                                           // date -> text_buf
uint32_t FUN_00407280(int hole);                                                           // hole name -> appended to text_buf
int FUN_004065c0(void);                                                                    // the pro's first skill setup (10 points, 3 per accomplishment)
int FUN_0045f0f0(char *title, uint32_t mask, int points, int slot, int xoff);              // the skill dialog
uint32_t FUN_004385d0(int a, int b);                                                       // the golfer profile page
void *FUN_0043d6f0(int group, int last, int flags);                                         // an object's sprite set
uint32_t FUN_004628d0(int x, int y, int z, int sx, int sy, void *set, int rot, uint32_t frame);   // draw an object
int FUN_0040bfa0(int x, int y);                                                            // terrain at a world point
uint32_t FUN_004074a0(int design, int full);                                                // landmark name, appended to text_buf
uint32_t FUN_004676e0(uint32_t slot, int what);                                            // golfer name -> text_buf
uint32_t FUN_00467600(int rec);                                                            // employee name (or its default) -> text_buf
const char *mods_get(const char *key);
unsigned mods_serial(void);


int pv_building_of_text(const char *text);   // paths_game.c (task-53)
void pv_show_building(int idx);
int gw_native_card(void);   // golfers_game.c: the Frosted Mint golfer card replaces the original

// hd_art.c (task-81): the art pack's hi-res copies of what the old HUD drew go with its pixels
void hd_art_hide_begin(void);
void hd_art_hide_rects(const int *xywh, int nrects);

// gdi.c: golf's screen-sized 16-bit page buffers (it may flip between two)
uint16_t *gdi_screen_dib(int i, int *w, int *h, int *pitch, int *is565);
extern int win_screen_w, win_screen_h;

#define PAGES 8
static int g_on;              // this frame: the new HUD replaces the old one
static uint16_t *g_save[PAGES];
static size_t g_save_cap[PAGES];
static int g_target = -1;     // the page the old HUD drew into this frame (found in hud_restore)
typedef struct { int x, y, w, h; } R;
static R g_rects[3];
static int g_nrects;

// injection
static int g_inj, g_inj_x, g_inj_y, g_inj_frames;
static HudAction g_goal;
static int g_goal_on, g_goal_steps;
static int g_staff_tab, g_staff_tab_wait;   // task-112: open the Staff tab once the People tray is up

static HudSnap g_snap;
static uint32_t g_serial, g_art_serial;
static int g_art_theme = -1, g_art_frames;

int hud_on(void) { return g_on; }
int hud_mx(int x) { return g_on ? -10000 : x; }
int hud_my(int y) { return g_on ? -10000 : y; }
int hud_ylim(int y) { return g_on ? 100000 : y; }
int hud_injecting(void) { return g_inj; }

// Scripted clicks (harness, bridge: SDL events without a window, in frame
// pixels) still reach the old wheel and trays, so scripts and tools written
// for the original layout keep working with the new HUD on.
static int g_script_x = -1, g_script_y = -1;
void hud_note_script_click(int x, int y) {
  g_script_x = x;
  g_script_y = y;
}
static int script_click(void) { return G32(MOUSE_X) == g_script_x && G32(MOUSE_Y) == g_script_y; }

int hud_strip_click(void) { return !g_on || (!g_inj && script_click()); }

int hud_old_click(void) {
  if (!g_on) return 1;
  if (!g_inj) return script_click();
  // the click being handled is ours: make sure it carries our point
  G32(MOUSE_X) = g_inj_x;
  G32(MOUSE_Y) = g_inj_y;
  g_inj = 0;
  hudf_set_injecting(0);
  return 1;
}

static void rects(void) {
  int W = win_screen_w, H = win_screen_h;
  g_nrects = 0;
  g_rects[g_nrects++] = (R){0, 0, 240, 84};                      // banner, stars, hearts
  g_rects[g_nrects++] = (R){W - 215, 0, 215, 142};               // money, fun, skill plates
  g_rects[g_nrects++] = (R){0, H - 220, W, 220};                 // wheel, trays, chip strip, tray tooltips
                                                                   // (bottom-anchored on a tall canvas, task-3 B4)
  for (int i = 0; i < g_nrects; i++) {
    R *r = &g_rects[i];
    if (r->x < 0) r->w += r->x, r->x = 0;
    if (r->y < 0) r->h += r->y, r->y = 0;
    if (r->x + r->w > W) r->w = W - r->x;
    if (r->y + r->h > H) r->h = H - r->y;
    if (r->w < 0) r->w = 0;
    if (r->h < 0) r->h = 0;
  }
}

// mode 1: keep page i's pixels under the old HUD; 0: put them back;
// 2: compare (returns 1 when the old HUD drew into page i since the save).
static int copy_rects(int page, int mode) {
  int w, h, pitch, is565;
  uint16_t *bb = gdi_screen_dib(page, &w, &h, &pitch, &is565);
  if (!bb) return 0;
  size_t need = 0;
  for (int i = 0; i < g_nrects; i++) need += (size_t)g_rects[i].w * g_rects[i].h;
  if (need > g_save_cap[page]) {
    g_save[page] = realloc(g_save[page], need * 2);
    g_save_cap[page] = need;
  }
  uint16_t *p = g_save[page];
  for (int i = 0; i < g_nrects; i++) {
    R r = g_rects[i];
    if (r.x + r.w > w) r.w = w - r.x;
    if (r.y + r.h > h) r.h = h - r.y;
    for (int y = 0; y < r.h; y++) {
      uint16_t *row = bb + (ptrdiff_t)(r.y + y) * pitch + r.x;
      if (mode == 1) memcpy(p, row, (size_t)r.w * 2);
      else if (mode == 0) memcpy(row, p, (size_t)r.w * 2);
      else if (memcmp(row, p, (size_t)r.w * 2)) return 1;
      p += g_rects[i].w;
    }
  }
  return 0;
}

void hud_save(void) {
  dlgf_enabled();   // task-27: registers the dialogs layer early (a dialog needs it up before it opens)
  int w, h, pitch, is565;
  g_on = hudf_enabled() && gdi_screen_dib(0, &w, &h, &pitch, &is565) != NULL;
  if (!g_on) return;
  rects();
  for (int i = 0; i < PAGES; i++) copy_rects(i, 1);
  hd_art_hide_begin();
}

static void hide_art(void) { hd_art_hide_rects(&g_rects[0].x, g_nrects); }

// ------------------------------------------------------------------ art

// Draws a game sprite into a scratch box of the back buffer (inside the
// area hud_restore puts back) over black and over white, and turns the two
// into ARGB with alpha: a = 1 - (white - black), colour = black / a.
// kind: 0 FUN_00473cb0 (plain), 1 FUN_00473f60 (with its alpha sprite mask), 2 FUN_00473e60,
// 3 FUN_004628d0 (sprite is an object sprite set; g_obj_* say how)
static int g_obj_scale = 100, g_obj_rot, g_obj_frame;
static uint32_t *snap_sprite(void *sprite, int kind, void *mask, int *ow, int *oh) {
  int w, h, pitch, is565;
  uint16_t *bb = g_target >= 0 ? gdi_screen_dib(g_target, &w, &h, &pitch, &is565) : NULL;
  const int BX = 4, BY = win_screen_h - 210, BW = 196, BH = 200;   // inside the bottom rectangle hud_restore puts back
  if (!bb || BX + BW > w || BY + BH > h) return NULL;
  static uint16_t shot[2][196 * 200];
  for (int pass = 0; pass < 2; pass++) {
    uint16_t fillc = pass ? (is565 ? 0xffff : 0x7fff) : 0;
    for (int y = 0; y < BH; y++)
      for (int x = 0; x < BW; x++) bb[(ptrdiff_t)(BY + y) * pitch + BX + x] = fillc;
    if (kind == 3) {   // an object from its sprite set, as the design strips draw it (FUN_00432200)
      FUN_004628d0(BX + BW / 2, BY + BH - 30, 0, g_obj_scale, g_obj_scale, sprite, g_obj_rot, (uint32_t)g_obj_frame);
      goto drawn;
    }
    int ox = G32((uintptr_t)sprite + 0x20), oy = G32((uintptr_t)sprite + 0x24);
    int dx = BX + 8 - (ox > 0 ? 0 : ox), dy = BY + 8 - (oy > 0 ? 0 : oy);
    if (kind == 1) FUN_00473f60(sprite, (int)(uintptr_t)mask, G32(0x4c1570), dx, dy, 0);
    else if (kind == 2) FUN_00473e60(sprite, G32(0x4c1570), dx, dy, 0);
    else FUN_00473cb0(sprite, G32(0x4c1570), dx, dy, 1, 1, 1, 0);
  drawn:
    for (int y = 0; y < BH; y++)
      for (int x = 0; x < BW; x++) shot[pass][y * BW + x] = bb[(ptrdiff_t)(BY + y) * pitch + BX + x];
  }
  int x0 = BW, y0 = BH, x1 = -1, y1 = -1;
  for (int y = 0; y < BH; y++)
    for (int x = 0; x < BW; x++)
      if (shot[0][y * BW + x] != 0 || shot[1][y * BW + x] != (is565 ? 0xffff : 0x7fff)) {
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
      }
  if (x1 < x0) return NULL;
  int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
  uint32_t *out = malloc((size_t)cw * ch * 4);
  for (int y = 0; y < ch; y++)
    for (int x = 0; x < cw; x++) {
      uint16_t b = shot[0][(y0 + y) * BW + x0 + x], wv = shot[1][(y0 + y) * BW + x0 + x];
      int br, bg, bb2, wr, wg, wb;
      if (is565) {
        br = (b >> 11) & 31, bg = (b >> 6) & 31, bb2 = b & 31;
        wr = (wv >> 11) & 31, wg = (wv >> 6) & 31, wb = wv & 31;
      } else {
        br = (b >> 10) & 31, bg = (b >> 5) & 31, bb2 = b & 31;
        wr = (wv >> 10) & 31, wg = (wv >> 5) & 31, wb = wv & 31;
      }
      int d = ((wr - br) + (wg - bg) + (wb - bb2)) * 255 / (3 * 31);
      int a = 255 - d;
      if (a < 0) a = 0;
      if (a > 255) a = 255;
      uint32_t px = 0;
      if (a > 0) {
        int r = br * 255 / 31 * 255 / a, g = bg * 255 / 31 * 255 / a, bl = bb2 * 255 / 31 * 255 / a;
        r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, bl = bl > 255 ? 255 : bl;
        px = (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bl;
      }
      out[y * cw + x] = px;
    }
  *ow = cw;
  *oh = ch;
  return out;
}

static void publish_sprite(int slot, void *sprite, int kind, void *mask) {
  int w = 0, h = 0;
  uint32_t *px = snap_sprite(sprite, kind, mask, &w, &h);
  hudf_publish_art(slot, px, w, h);
  free(px);
}

// task-95: every design of the five amenity strips (FUN_00434cf0's FUN_00432200 calls), the way the strip
// picks group and frame for design i, drawn at full size for the chooser's tiles.
static void snap_designs(void) {
  static int c_bench = -1, c_land = -1;
  static const struct { int slot, group, step, frame, rot, max; } k[5] = {
      {HUD_ART_BENCH, 0x208, 1, 0xaa, 4, 6},    {HUD_ART_LANDMARK, 0x168, 1, 100, -4, 16},
      {HUD_ART_FLOWER, 0x1a3, 0, 0x2d, -6, 15}, {HUD_ART_TREE, 0x12f, 1, 0xb4, 2, 7},
      {HUD_ART_BRIDGE, 0x226, 1, 0xa9, 4, 8},
  };
  int counts[5] = {mdata_cell(&c_bench, "trays", "amenities", "bench_designs", 5),
                   mdata_cell(&c_land, "trays", "amenities", "landmark_designs", 14), 15, 7, 8};
  for (int f = 0; f < 5; f++) {
    int n = counts[f] > k[f].max ? k[f].max : counts[f];
    int group = k[f].group, base = k[f].frame, off = 0;
    for (int i = 0; i < n; i++) {
      int g = group + off;
      void *set = FUN_0043d6f0(g, G32(0x53f3e8 + g * 4) - 1, 0);
      g_obj_rot = k[f].rot;
      g_obj_frame = i + base;
      int w = 0, h = 0;
      uint32_t *px = set ? snap_sprite(set, 3, NULL, &w, &h) : NULL;
      hudf_publish_art(k[f].slot + i, px, w, h);
      free(px);
      off += k[f].step;
      // the flower strip moves to the next two groups a third and two thirds along (FUN_00432200)
      if (group == 0x1a3 && i + 1 == n / 3) group = 0x1a9, base -= n / 3;
      else if (group == 0x1a9 && i + 1 == n * 2 / 3) group = 0x1af, base += -5;
      else if (group == 0x226) base -= 1;
    }
  }
}

static void snap_art(void) {
  // terrain slots: sprite 0x570cb8 + (state + slot*4) * 0x2c, state 0 = normal
  for (int i = 0; i < 16; i++) publish_sprite(HUD_ART_TERRAIN + i, (void *)(uintptr_t)(0x570cb8 + i * 4 * 0x2c), 0, NULL);
  // the course emblem, as the banner draws it
  int prop = G32(0x59bf90);
  int kind = (int)(signed char)G8(0x571ff4 + prop * 0x2e);
  int idx = (int)(signed char)G8(0x4c1ea8 + kind * 0x82);
  if (idx >= 0 && idx < 64)
    publish_sprite(HUD_ART_EMBLEM, (void *)(uintptr_t)(0x541710 + idx * 0x2c), 1, (void *)(uintptr_t)(0x5791f8 + idx * 0x2c));
  // building lots (FUN_00434350): 0x53df58 + (enabled + (upgraded + lot*2)*2) * 0x2c
  for (int i = 0; i < 9; i++) {
    int up = G32(0x5a8c50 + i * 4) != 0;
    publish_sprite(HUD_ART_BUILDING + i, (void *)(uintptr_t)(0x53df58 + (1 + (up + i * 2) * 2) * 0x2c), 0, NULL);
  }
  // amenities (FUN_00434cf0): the lit look of each control, 0x59ba68 + slot * 0x84, in ui_hud.c's order
  static const int amen_slot[8] = {1, 3, 5, 0, 4, 2, 6, 9};
  for (int i = 0; i < 8; i++)
    publish_sprite(HUD_ART_AMENITY + i, (void *)(uintptr_t)(0x59ba68 + amen_slot[i] * 0x84), 2, NULL);
  // elevation tools (FUN_00433e50): the selected look, 0x571d6c + tool * 0x84
  for (int i = 0; i < 4; i++) publish_sprite(HUD_ART_ELEVATION + i, (void *)(uintptr_t)(0x571d6c + i * 0x84), 2, NULL);
  snap_designs();
  g_art_serial++;
}

// ------------------------------------------------------------------ state

static void copy_str(char *dst, int cap, const char *src) {
  int n = 0;
  while (n < cap - 1 && src[n]) {
    dst[n] = src[n];
    n++;
  }
  dst[n] = 0;
}

// ------------------------------------------------------------------ message history (task-62)
//
// The game's news strip (FUN_0040cb00 posts, FUN_0040d6a0 ticks) keeps one message at a time, in
// NEWS_TEXT (0x5a6d40) with screen_state (0x569498) == 1 while it shows, the subject in 0x4c2e08
// (>= 0: the golfer slot whose portrait the strip shows) and the display timer in 0x5a7144 (set to
// length / 3 + 16 at a post, counted down by the tick). A post is seen here as screen_state going to 1,
// or the timer going up while it shows (a forced message over the old one). Accepted posts also
// capture here immediately, before another post can replace the single news slot. What the message is about: the golfer slot when the game names one;
// else the hole whose name the text starts with (the hole hotkeys then do the camera jump).
static HudMsg g_hist[HUD_MSG_MAX];
static int g_hist_n, g_hist_seq, g_news_seq;
static int g_prev_ss, g_prev_timer;
static char g_prev_text[400];

// A golfer on the course: the record's state byte (+0x21) is neither 0 (unused) nor -1 (the "hire an
// employee" placeholder, which opens that dialog when selected).
static int golfer_ok(int slot) {
  if (slot < 0 || slot >= 152) return 0;
  int b = G8(0x5794d9 + slot * 0x100);
  return b != 0 && b != -1;
}

static int word_end(char c) { return !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'' || c == '-'); }

// The hole (1..18) whose name the message starts with (the longest name), 0 if none.
static int hole_of_text(const char *text) {
  char keep[512];
  memcpy(keep, (void *)(uintptr_t)TEXT_BUF, sizeof keep);
  int best = 0;
  size_t best_len = 0;
  for (int h = 1; h <= 18; h++) {
    *(char *)(uintptr_t)TEXT_BUF = 0;
    FUN_00407280(h);
    const char *nm = (const char *)(uintptr_t)TEXT_BUF;
    size_t n = strlen(nm);
    if (n < 2 || n >= 120 || strncmp(text, nm, n) != 0 || !word_end(text[n]) || n <= best_len) continue;
    best = h;
    best_len = n;
  }
  memcpy((void *)(uintptr_t)TEXT_BUF, keep, sizeof keep);
  return best;
}

static void note_messages(const char *date, int posted) {
  int ss = G8(SCREEN_STATE), timer = G32(0x5a7144);
  const char *text = (const char *)(uintptr_t)NEWS_TEXT;
  if (ss == 1 && text[0] && (posted || g_prev_ss != 1 || timer > g_prev_timer || strncmp(text, g_prev_text, sizeof g_prev_text - 1) != 0)) {
    HudMsg *m;
    if (g_hist_n == HUD_MSG_MAX) {
      memmove(g_hist, g_hist + 1, sizeof g_hist[0] * (HUD_MSG_MAX - 1));
      m = &g_hist[HUD_MSG_MAX - 1];
    } else {
      m = &g_hist[g_hist_n++];
    }
    memset(m, 0, sizeof *m);
    copy_str(m->text, sizeof m->text, text);
    for (char *p = m->text; *p; p++)
      if (*p == '\n' || *p == '\r' || *p == '|') *p = ' ';
    copy_str(m->date, sizeof m->date, date);
    m->t_ms = host_now_ms();
    m->seq = g_news_seq = ++g_hist_seq;
    int subj = G32(0x4c2e08);
    if (golfer_ok(subj)) {
      m->show = HUD_SHOW_GOLFER;
      m->target = subj;
    } else if ((m->target = hole_of_text(m->text)) != 0) {
      m->show = HUD_SHOW_HOLE;
    } else if ((m->target = pv_building_of_text(m->text)) >= 0) {   // task-53: golf's "WARNING: Your ... will not be operational" names a building
      m->show = HUD_SHOW_BUILDING;
    }
    fprintf(stderr, "hud: message %d (%s %d): %s\n", m->seq, m->show == HUD_SHOW_GOLFER ? "golfer" : m->show == HUD_SHOW_HOLE ? "hole" : m->show == HUD_SHOW_BUILDING ? "building" : "no target",
            m->target, m->text);
  }
  if (ss != 1 && G32(0x5a9ccc) == 0) g_news_seq = 0;
  g_prev_ss = ss;
  g_prev_timer = timer;
  copy_str(g_prev_text, sizeof g_prev_text, text);
}

// Called only after the original news function accepts and finishes a post.
// Keep polling for load-time state, and suppress its duplicate through the same previous-state fields.
void hud_news_posted(void) {
  static unsigned seen = ~0u;
  static int capture = 1;
  unsigned serial = mods_serial();
  if (serial != seen) {
    seen = serial;
    const char *v = mods_get("fixes.news_history");
    capture = !v || atoi(v) != 0;
  }
  if (!capture) return;
  char keep[512], numeric[12], date[24];
  int16_t year = G16(0x5a6d3c);
  memcpy(keep, (void *)(uintptr_t)TEXT_BUF, sizeof keep);
  memcpy(numeric, (void *)(uintptr_t)0x58a528, sizeof numeric);
  *(char *)(uintptr_t)TEXT_BUF = 0;
  FUN_0040d7b0(G32(0x834170));
  copy_str(date, sizeof date, (const char *)(uintptr_t)TEXT_BUF);
  note_messages(date, 1);
  // The formatters touch shared decimal scratch and the cached year as well as text_buf.
  memcpy((void *)(uintptr_t)TEXT_BUF, keep, sizeof keep);
  memcpy((void *)(uintptr_t)0x58a528, numeric, sizeof numeric);
  G16(0x5a6d3c) = year;
}

// ------------------------------------------------------------------ weeds (task-112)
//
// Weeds (tile flag 0x800, the yellow dandelions) spread from tile to tile and only the turf staff clear them
// inside their work zone: employee record +0x10/+0x11 is the zone's centre tile (-1 none), jobs -4 and -6 are
// the ones the main loop (FUN_00403xxx) sends to weedy tiles, within 12 tiles (16 for the technician, record
// flag 8). When 3 or more weedy tiles of a hole lie outside every zone the HUD posts one card for that hole
// ("Hire a Turf Technician or move one to cover it") with a button to People > Staff, at most once per hole
// per game month. The card is a history entry like a game message (g_hist) with HUD_SHOW_STAFF as its target.
#define EMP_BASE staff_base()
#define EMP_COUNT STAFF_CAPACITY
#define WEED_MIN 3
#define WEED_CARD_MS 14000
static double g_weed_until;
static int g_weed_seq;
static char g_weed_month[19][40];   // the month key of the last card, per hole

static void month_key(const char *date, char *out, size_t cap) {   // "12 June 2001" -> "June2001" (letters and the 4-digit year)
  size_t n = 0;
  for (const char *p = date; *p && n + 5 < cap;) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
      out[n++] = *p++;
    } else if (*p >= '0' && *p <= '9') {
      const char *q = p;
      while (*q >= '0' && *q <= '9') q++;
      if (q - p == 4)
        for (; p < q; p++) out[n++] = *p;
      p = q;
    } else {
      p++;
    }
  }
  out[n] = 0;
}

static void weed_check(const char *date) {
  static int frame, arr = -2;
  if (++frame % 90) return;
  if (arr == -2) {
    arr = -1;
    for (int i = 0; i < grid_count; i++)
      if (grid_arrays[i].addr == 0x53caf0) arr = i;
  }
  if (arr < 0 || !g_grid[arr] || !date[0]) return;
  int cx[HUD_STAFF_MAX], cy[HUD_STAFF_MAX], cr[HUD_STAFF_MAX], nz = 0;
  for (int r = 0; r < EMP_COUNT && nz < HUD_STAFF_MAX; r++) {
    int st = G8(EMP_BASE + r * 0x4c + 0x12) & 0xff, job = G8(EMP_BASE + r * 0x4c + 0x13);
    if (st == 0 || (job != -4 && job != -6)) continue;
    int zx = staff_tile_coord(G8(EMP_BASE + r * 0x4c + 0x10));
    int zy = staff_tile_coord(G8(EMP_BASE + r * 0x4c + 0x11));
    if (zx < 0 || zy < 0) continue;
    cx[nz] = zx, cy[nz] = zy, cr[nz] = (st & 8) ? 16 : 12;
    nz++;
  }
  int bad[19] = {0}, first[19];
  const uint16_t *f = (const uint16_t *)g_grid[arr];
  for (int x = 0; x < g_map_w; x++)
    for (int y = 0; y < g_map_h; y++) {
      uint16_t v = f[x * g_map_w + y];
      int hole = v & 0x1f;
      if (!(v & 0x800)) continue;
      if (hole < 1 || hole > 18) {   // weeds also grow on grass beside the holes: the hole whose tiles are nearest (within 5)
        hole = 0;
        for (int r = 1; r <= 5 && !hole; r++)
          for (int dx = -r; dx <= r && !hole; dx++)
            for (int dy = -r; dy <= r; dy++) {
              if (abs(dx) != r && abs(dy) != r) continue;
              int nx = x + dx, ny = y + dy;
              if (nx < 0 || ny < 0 || nx >= g_map_w || ny >= g_map_h) continue;
              int hh = f[nx * g_map_w + ny] & 0x1f;
              if (hh >= 1 && hh <= 18) {
                hole = hh;
                break;
              }
            }
        if (!hole) continue;
      }
      int covered = 0;
      for (int k = 0; k < nz && !covered; k++) covered = abs(x - cx[k]) <= cr[k] && abs(y - cy[k]) <= cr[k];
      if (!covered && bad[hole]++ == 0) first[hole] = x;
    }
  char key[40];
  month_key(date, key, sizeof key);
  {   // one line when the numbers change (tests read it)
    static int last_w = -1, last_z = -1;
    int w = 0, u = 0;
    for (int h = 1; h <= 18; h++) u += bad[h];
    for (int x = 0; x < g_map_w; x++)
      for (int y = 0; y < g_map_h; y++) w += (f[x * g_map_w + y] & 0x8ff) > 0x7ff;
    if (w != last_w || nz != last_z) fprintf(stderr, "hud: weeds scan: %d weedy tiles, %d outside every turf zone, %d zones\n", w, u, nz);
    last_w = w, last_z = nz;
  }
  if (host_now_ms() < g_weed_until + 20000) return;   // cards come one at a time, 20 s apart
  for (int h = 1; h <= 18; h++) {
    if (bad[h] < WEED_MIN || !strcmp(g_weed_month[h], key)) continue;
    char nm[120];
    char keep[512];
    memcpy(keep, (void *)(uintptr_t)TEXT_BUF, sizeof keep);
    *(char *)(uintptr_t)TEXT_BUF = 0;
    FUN_00407280(h);
    copy_str(nm, sizeof nm, (const char *)(uintptr_t)TEXT_BUF);
    memcpy((void *)(uintptr_t)TEXT_BUF, keep, sizeof keep);
    HudMsg *m;
    if (g_hist_n == HUD_MSG_MAX) {
      memmove(g_hist, g_hist + 1, sizeof g_hist[0] * (HUD_MSG_MAX - 1));
      m = &g_hist[HUD_MSG_MAX - 1];
    } else {
      m = &g_hist[g_hist_n++];
    }
    memset(m, 0, sizeof *m);
    snprintf(m->text, sizeof m->text, "Weeds are spreading on hole %d (%s). Hire a Turf Technician or move one to cover it.", h, nm);
    copy_str(m->date, sizeof m->date, date);
    m->t_ms = host_now_ms();
    m->seq = g_weed_seq = g_news_seq = ++g_hist_seq;
    m->show = HUD_SHOW_STAFF;
    m->target = h;
    g_weed_until = m->t_ms + WEED_CARD_MS;
    copy_str(g_weed_month[h], sizeof g_weed_month[h], key);
    fprintf(stderr, "hud: weeds hole %d: %d uncovered weedy tiles, %d turf zones, card %d month %s\n", h, bad[h], nz, m->seq, key);
    break;   // one card at a time; the next hole's follows on a later check
  }
}

// ------------------------------------------------------------------ the player's pro (task-92)
//
// The original's People panel has a second page, the player panel (dock modes 3 and 4, FUN_004362f0 draws
// it, FUN_00436060 handles its clicks): the pro's profile disc, Practice Round, Match, Begin Tournament,
// the skill list and, while the pro is out, the five shot choices. Before the pro's first round golf runs
// the skill setup (FUN_0040f190 calls FUN_004065c0 when the skill mask is empty: 10 points plus 3 for each
// accomplishment already earned); after that every accomplishment board (FUN_0046e810) ends with "Add three
// skill points to your pro" and the skill dialog. The Frosted Mint HUD hides that panel, so the People tray's
// "Your golfer" page (ui_hud.c) shows these controls and clicks the original ones.

#define PRO_SLOT 0x5a59f8   // the pro's golfer slot while out on the course, -1 when in
#define SKILLS 0x5a5a04     // the pro's skills, 12 bytes (10 shown)
#define SKILL_MASK 0x4c2c9c

static int pro_ready(void) {   // FUN_0040f190's gate: the mask plus a bit for every skill above 0
  int mask = (uint16_t)G16(SKILL_MASK);
  for (int i = 0; i < 12; i++)
    if (G8(SKILLS + i) != 0) mask |= 1 << i;
  return mask != 0;
}

static void read_pro(HudSnap *s, int flags) {
  int slot = G32(PRO_SLOT), holes = G32(0x5685f0), busy = (flags & 0x4200000) != 0;
  s->pro_ready = pro_ready();
  s->pro_out = slot != -1;
  s->pro_practice = !s->pro_out && holes >= 2 && !busy;
  s->pro_match = !s->pro_out && (flags & 0x2000) && holes >= 2 && G32(0x4c2e14) != -1 && !busy;
  s->pro_tourney = !s->pro_out && (flags & 0x400000) && !busy;
  s->pro_shots = 0;
  if (s->pro_out && slot >= 0 && slot < 152 && s->dock_mode == 3 && G32(0x579584 + slot * 0x100) == -1) {
    int t = FUN_0040bfa0(G32(0x57958c + slot * 0x100), G32(0x5794bc + slot * 0x100));
    s->pro_shots = t >= 0 && t < 64 && G8(0x578372 + t * 0x30) >= 1 ? 1 : 2;   // a bad lie: straight and low punch only
  }
  s->pro_shot = G32(0x58f330);
  for (int i = 0; i < 10; i++) s->pro_skill[i] = G8(SKILLS + i);
  copy_str(s->pro_name, sizeof s->pro_name, (const char *)(uintptr_t)0x4d6098);
}

// [ui] pro_setup: original (default, task-102c) leaves the pro as golf has it, as in 2002: the pro exists when a
// career starts, the skill points come before the first Practice Round (golf's FUN_0040f190 -> FUN_004065c0), the
// profile is People > Your pro. start runs the pro's creation (profile, then the skill setup) as a new career's
// course opens (task-92). SIMGOLF_PRO_SETUP overrides it.
static int pro_setup_at_start(void) {
  static int v = -1;
  if (v < 0) {
    const char *e = getenv("SIMGOLF_PRO_SETUP");
    if (!e || !*e) e = mods_get("ui.pro_setup");
    v = e && strcmp(e, "start") == 0;
  }
  return v;
}

void hud_clean_pages(void);

static void hud_hide_now(void) {   // a modal dialog of golf's is about to run: no HUD over it
  g_snap.visible = 0;
  g_snap.serial = ++g_serial;
  hudf_publish(&g_snap);
  hud_clean_pages();
}

static void pro_profile(void) {   // FUN_00436060 case 0, the profile disc
  G8(0x582cd0) = 0x20;
  G16(0x582d6e) = 0;
  int look = G32(0x4d60b4);
  G8(0x582cd1) = look == 0 ? 7 : (look & 7);
  FUN_004385d0(0, 0x98);
}

static void pro_action(int what) {
  fprintf(stderr, "hud: pro %s (ready %d)\n", what == HUD_PRO_SKILLS ? "skills" : what == HUD_PRO_PROFILE ? "profile" : "create",
          pro_ready());
  hud_hide_now();
  if (what == HUD_PRO_PROFILE || what == HUD_PRO_CREATE) {
    void rp_pro_next(int on);   // reports_game.c: the page says the skill points come next (task-102)
    rp_pro_next(what == HUD_PRO_CREATE && !pro_ready());
    pro_profile();
    rp_pro_next(0);
  }
  if (what == HUD_PRO_SKILLS || what == HUD_PRO_CREATE) {
    if (!pro_ready()) {
      FUN_004065c0();   // the first setup, exactly as before the first round
    } else {            // the skills with an OK button (no points to spend)
      G32(0x4c2e08) = -1;
      FUN_0045f0f0((char *)(uintptr_t)0x4d6098, (uint16_t)G16(SKILL_MASK), 0, -1, 200);
    }
  }
  fprintf(stderr, "hud: pro done (ready %d, skills", pro_ready());
  for (int i = 0; i < 10; i++) fprintf(stderr, " %d", G8(SKILLS + i));
  fprintf(stderr, ")\n");
}

// A new career's course: the pro is created once the course view has been up a moment (no hole built yet,
// not Sandbox, skills not set up). Once per course: a new game re-arms it.
static int g_create_wait, g_create_done, g_create_tick;
static void pro_create_tick(const HudSnap *s) {
  int tick = G32(0x834170);
  if (tick < g_create_tick) g_create_done = 0;   // the clock went back: another game
  g_create_tick = tick;
  if (!pro_setup_at_start() || s->sandbox || s->pro_ready || s->next_hole >= 2) {
    g_create_wait = 0;
    return;
  }
  if (g_create_done || !s->visible) return;
  if (++g_create_wait < 45) return;
  g_create_done = 1;
  pro_action(HUD_PRO_CREATE);
}

// task-96: what the design chooser needs. Bench and landmark counts come from the same mod table the
// original strip's patch reads (trays.json); flower bed 15, scenic trees 7, bridge 8 are fixed in the game.
static void read_design(HudSnap *s, int flags) {
  static int c_bench = -1, c_land = -1;
  int tool = s->build_tool;
  s->design = G32(0x56a514);
  s->landmark_mask = (uint32_t)G32(0x543cfc);
  s->landmark_free = (uint32_t)G32(0x822c70);
  s->clubhouse = tool == 15 && (flags & 0x800) != 0;
  s->design_n = tool == 1 ? mdata_cell(&c_bench, "trays", "amenities", "bench_designs", 5)
              : tool == 2 ? 15 : tool == 4 ? mdata_cell(&c_land, "trays", "amenities", "landmark_designs", 14)
              : tool == 16 ? 7 : tool == 19 ? 8 : 0;
  if (s->design_n > 16) s->design_n = 16;
  if (tool == 4) {   // short names, the way the strip's label is built (scratch: text_buf)
    char keep[256];
    memcpy(keep, (void *)(uintptr_t)TEXT_BUF, sizeof keep);
    for (int i = 0; i < s->design_n; i++) {
      *(char *)(uintptr_t)TEXT_BUF = 0;
      FUN_004074a0(i, 0);
      copy_str(s->landmark_name[i], sizeof s->landmark_name[i], (const char *)(uintptr_t)TEXT_BUF);
      if (s->landmark_name[i][0] >= 'a' && s->landmark_name[i][0] <= 'z') s->landmark_name[i][0] -= 32;
    }
    memcpy((void *)(uintptr_t)TEXT_BUF, keep, sizeof keep);
  }
}

static void inject(int x, int y);

// ------------------------------------------------------------------ People lists (task-94)
//
// The original's Golfers page was a 4x4 list of golfers with a mood face (FUN_00435760, the records at
// 0x5794b8 + slot * 0x100: hole byte +0x21, mood short +0xa4) and its Staff page a strip of 8 employee
// portraits with Fire, Move and Rename (FUN_00436e50 draws, FUN_00436c00 handles; records at 0x585850 +
// n * 0x4c: status +0x12, job +0x13, tile +0x10/+0x11, name +0x2a, wage +0x24). Both are hidden with the
// old HUD, so the snapshot carries the lists and the actions below click golf's own controls.
#define EMP_LIST 0x5aa5b8     // the draw's list of the visible portraits: employee records
#define EMP_SCROLL 0x5aa558   // first portrait shown (steps of 2)
#define EMP_SEL 0x4c7aec      // selected portrait, -1 none
#define EMP_MOVE 0x4c2e10     // record being moved, -1 none
#define EMP_BTN 0x4c7af0      // hot spot table: (x, y) shorts, 4 bytes each

static int emp_active(int r) {
  int st = G8(EMP_BASE + r * 0x4c + 0x12) & 0xff, job = G8(EMP_BASE + r * 0x4c + 0x13);
  return st != 0 && job < 0 && job != -6;
}

static int emp_job(int r) {   // 0..7: Club Pro, Celebrity, Ranger, Marshall, Groundskeeper, Technician, Soda Vendor, Refresher
  int st = G8(EMP_BASE + r * 0x4c + 0x12) & 0xff, job = G8(EMP_BASE + r * 0x4c + 0x13);
  int j = (-job - 2) * 2 + ((st & 8) >> 3);
  return j < 0 ? 0 : j > 7 ? 7 : j;
}

static void read_people(HudSnap *s) {
  char keep[512];
  int names = s->dock_mode == 2 && !s->employee_overlay;
  if (names) memcpy(keep, (void *)(uintptr_t)TEXT_BUF, sizeof keep);
  s->golfer_n = 0;
  memset(s->mood_count, 0, sizeof s->mood_count);
  for (int i = 0; i < HUD_GOLFER_MAX; i++) {
    int hole = G8(0x5794d9 + i * 0x100);
    if (hole <= 0) continue;
    int face = G16(0x57955c + i * 0x100) + 2;
    face = face < 1 ? 1 : face > 10 ? 10 : face;
    s->mood_count[(face - 1) / 2]++;
    __typeof__(s->golfer[0]) *g = &s->golfer[s->golfer_n++];
    g->slot = (short)i;
    g->hole = (signed char)hole;
    g->mood = (signed char)face;
    g->name[0] = 0;
    if (names) {
      *(char *)(uintptr_t)TEXT_BUF = 0;
      FUN_004676e0((uint32_t)i, 0);
      copy_str(g->name, sizeof g->name, (const char *)(uintptr_t)TEXT_BUF);
    }
  }
  if (names) memcpy((void *)(uintptr_t)TEXT_BUF, keep, sizeof keep);
  s->staff_n = 0;
  s->staff_limit = staff_max();
  int staff_names = s->employee_overlay;
  if (staff_names) memcpy(keep, (void *)(uintptr_t)TEXT_BUF, sizeof keep);
  for (int r = 0; r < EMP_COUNT; r++) {
    if (!emp_active(r)) continue;
    __typeof__(s->staff[0]) *e = &s->staff[s->staff_n++];
    e->rec = (short)r;
    e->job = (signed char)emp_job(r);
    e->wage = (G8(0x4c2e2c + e->job) & 0xff) * 100;   // the job's weekly wage, as the hire menu lists it
    e->name[0] = 0;
    if (staff_names) {
      *(char *)(uintptr_t)TEXT_BUF = 0;
      FUN_00467600(r);
      copy_str(e->name, sizeof e->name, (const char *)(uintptr_t)TEXT_BUF);
    }
  }
  if (staff_names) memcpy((void *)(uintptr_t)TEXT_BUF, keep, sizeof keep);
  int k = G32(EMP_SEL), visible = G32(0x5aa6c0);
  s->staff_sel = -1;
  if (s->employee_overlay && k >= 0 && k < 8 && k < visible && G32(0x59ca54) != 0) s->staff_sel = G32(EMP_LIST + k * 4);
  s->staff_moving = s->employee_overlay ? G32(EMP_MOVE) : -1;
  s->staff_fire_cost = 2500;
  static int last_g = -1, last_s = -1, last_sel = -2, last_mv = -2;
  if (s->golfer_n != last_g || s->staff_n != last_s || s->staff_sel != last_sel || s->staff_moving != last_mv) {
    last_g = s->golfer_n; last_s = s->staff_n; last_sel = s->staff_sel; last_mv = s->staff_moving;
    fprintf(stderr, "hud: people golfers %d staff %d selected %d moving %d (moods %d %d %d %d %d, waiting %d)\n", s->golfer_n, s->staff_n,
            s->staff_sel, s->staff_moving, s->mood_count[0], s->mood_count[1], s->mood_count[2], s->mood_count[3], s->mood_count[4], s->waiting); fflush(stderr);
  }
}

static int g_staff_pend = -1, g_staff_wait;   // an employee to select once the portraits have scrolled to them

static void emp_click_slot(int i, int dx) {   // golf's hit test is FUN_00467170(click - hot spot - offset) < 16
  inject(G16(EMP_BTN + i * 4) + dx, G16(EMP_BTN + i * 4 + 2) + 0x10);
}

static int emp_pos(int rec) {   // place of the record among the hired
  int pos = 0;
  for (int r = 0; r < rec; r++)
    if (emp_active(r)) pos++;
  return pos;
}

static void staff_action(int what, int rec) {
  fprintf(stderr, "hud: staff %s %d\n", what == HUD_STAFF_SELECT ? "select" : what == HUD_STAFF_FIRE ? "fire" : what == HUD_STAFF_MOVE ? "move" : "rename", rec);
  if (what == HUD_STAFF_SELECT) {
    if (rec < 0 || rec >= EMP_COUNT || !emp_active(rec)) return;
    int pos = emp_pos(rec), scroll = G32(EMP_SCROLL);
    // The original strip backs off by two every frame while fewer than seven
    // portraits remain. Start on its stable last page before synthesizing a
    // portrait click, or its list moves between lookup and click dispatch.
    int max_scroll = staff_paid_count() - 7;
    if (max_scroll < 0) max_scroll = 0;
    max_scroll &= ~1;
    if (pos < scroll || pos >= scroll + 8) scroll = pos < 8 ? 0 : (pos & ~1);
    if (scroll > max_scroll) scroll = max_scroll;
    G32(EMP_SCROLL) = scroll;
    g_staff_pend = rec;
    g_staff_wait = 0;
  } else if (what == HUD_STAFF_FIRE) {
    emp_click_slot(5, 0x10);
  } else if (what == HUD_STAFF_MOVE) {
    emp_click_slot(4, 0x10);
  } else if (what == HUD_STAFF_RENAME) {
    emp_click_slot(6, 0x10);
  }
}

// Once the portraits show the wanted employee (a frame after the scroll), click their portrait.
static int staff_pending(void) {
  if (g_staff_pend < 0) return 0;
  if (++g_staff_wait < 2) return 1;
  for (int k = 0; k < 8; k++)
    if (G32(EMP_LIST + k * 4) == g_staff_pend && k < G32(0x5aa6c0)) {
      g_staff_pend = -1;
      emp_click_slot(8 + k, 0x20);
      return 1;
    }
  if (g_staff_wait > 12) g_staff_pend = -1;
  return 1;
}

static void read_state(HudSnap *s) {
  int flags = G32(FLAGS);
  s->canvas_w = win_screen_w;
  s->canvas_h = win_screen_h;
  s->money = G32(0x571fd4) * 100;
  s->sandbox = (flags & 0x1000000) != 0;
  s->cheat = (flags & 0x20000000) != 0;
  s->fun = G32(0x59ae78);
  s->skill_x100 = G32(0x541cd8);
  s->waiting = G32(0x56d1b0);
  int golfers = 0;
  for (int i = 0; i < 152; i++) {
    signed char hole = (signed char)G8(0x5794d9 + i * 0x100);
    if (hole > 0) golfers++;
  }
  s->golfers = golfers;
  s->dock_mode = G32(DOCK_MODE);
  s->shape = G32(0x5a34f0);
  s->amenities_on = G32(0x55e924);
  s->elevation_on = G32(0x55e928);
  s->employee_overlay = G32(0x561254);
  s->terrain_tool = G32(0x4c2848);
  s->build_tool = G32(0x4c2854);
  s->elevation_tool = G32(0x542f20);
  s->elev_edit = (flags & 2) != 0;
  s->paused = (flags & 4) != 0;
  s->fast = G32(0x59b04c) != 0;
  s->zoom = G32(0x5a34e8);   // the zoom saved around the HUD draws (the live one is forced to 4 there)
  s->rotation = G32(0x5685f4);
  s->theme = G32(0x5a34e0);
  s->unlock = G32(0x5a6364);
  s->next_hole = G32(0x5685f0);
  for (int i = 0; i < 9; i++) {
    int id = i + 6;
    s->building_level[i] = G32(0x5a8c38 + id * 4);
    // 0x4c2728 (stride 20: name, footprint +0x10, cost +0x12 in $100), entry k is tool k + 6
    const char *nm = (const char *)(uintptr_t)(0x4c2728 + i * 20);
    copy_str(s->building_name[i], sizeof s->building_name[i], nm);
    int cost = G16(0x4c2728 + i * 20 + 0x12);
    s->building_cost[i] = cost > 0 ? cost * (s->building_level[i] + 2) / 2 * 100 : -1;
  }
  for (int i = 0; i < 16; i++) {
    int id = (signed char)G8(0x4c79a0 + i);
    if (id < 0 || id > 40) {
      s->terrain_name[i][0] = 0;
      s->terrain_cost[i] = -1;
      continue;
    }
    copy_str(s->terrain_name[i], sizeof s->terrain_name[i], (const char *)(uintptr_t)(0x578350 + id * 0x30));
    if (s->terrain_name[i][0] >= 'a' && s->terrain_name[i][0] <= 'z') s->terrain_name[i][0] -= 32;   // "tees" -> "Tees"
    s->terrain_cost[i] = -1;
  }
  int ss = G8(SCREEN_STATE);
  s->news_on = ss == 1 || G32(0x5a9ccc) != 0;
  copy_str(s->news, sizeof s->news, (const char *)(uintptr_t)NEWS_TEXT);
  for (char *p = s->news; *p; p++)
    if (*p == '\n' || *p == '\r' || *p == '|') *p = ' ';
  s->visible = ss < 2;
  s->capsule_visible = G32(0x53df54) == 0 || gw_native_card();   // the native golfer card (task-31) leaves the top bar alone
  // course name and date, through the game's own formatters (text_buf is scratch)
  char keep[512];
  memcpy(keep, (void *)(uintptr_t)TEXT_BUF, sizeof keep);
  *(char *)(uintptr_t)TEXT_BUF = 0;
  FUN_0040daa0(0);
  copy_str(s->course, sizeof s->course, (const char *)(uintptr_t)TEXT_BUF);
  *(char *)(uintptr_t)TEXT_BUF = 0;
  FUN_0040d7b0(G32(0x834170));
  copy_str(s->date, sizeof s->date, (const char *)(uintptr_t)TEXT_BUF);
  memcpy((void *)(uintptr_t)TEXT_BUF, keep, sizeof keep);
  note_messages(s->date, 0);
  weed_check(s->date);
  if (g_weed_until > host_now_ms() && g_weed_seq && !s->news_on) {   // the weed card shows while the game says nothing
    s->news_on = 1;
    g_news_seq = g_weed_seq;
  }
  s->news_seq = g_news_seq;
  s->msg_n = g_hist_n;
  memcpy(s->msgs, g_hist, sizeof g_hist[0] * (size_t)g_hist_n);
  read_pro(s, flags);
  if (!s->pro_out) shot_readout_reset();
  s->shot_readout = s->pro_shots && shot_readout_copy(s->shot_text);
  read_design(s, flags);
  read_people(s);
}

// ------------------------------------------------------------------ actions

// (x, y): a point of the original 800x600 layout of the wheel and trays,
// which task-3 B4 draws at the bottom of a taller canvas (HD_UI_DY down).
static void inject(int x, int y) {
  fprintf(stderr, "hud: click (%d,%d) of the original layout\n", x, y);
  y += win_screen_h - 600;
  g_inj = 1;
  g_inj_x = x;
  g_inj_y = y;
  g_inj_frames = 0;
  hudf_set_injecting(1);
  FUN_004657e0(x, y);
}

// One step towards a goal tray; returns 1 when it is reached.
static int goal_step(int goal) {
  int mode = G32(DOCK_MODE), amen = G32(0x55e924), elev = G32(0x55e928);
  switch (goal) {
    case HUD_GOAL_COURSE:
      if (mode != 0) return inject(44, 474), 0;
      if (amen) return inject(269, 500), 0;
      return 1;
    case HUD_GOAL_EXTRAS:
      if (mode != 0) return inject(44, 474), 0;
      if (!amen) return inject(237, 525), 0;
      return 1;
    case HUD_GOAL_BUILDINGS:
      if (mode != 1) return inject(117, 496), 0;
      if (elev) return inject(270, 499), 0;
      return 1;
    case HUD_GOAL_LANDSCAPE:
      if (mode != 1) return inject(117, 496), 0;
      if (!elev) return inject(236, 526), 0;
      return 1;
    case HUD_GOAL_PEOPLE:
      if (mode < 2 || mode > 4) return inject(176, 537), 0;
      return 1;
    case HUD_GOAL_CLOSE:
      if (mode == 0) return inject(44, 474), 0;
      if (mode == 1) return inject(117, 496), 0;
      if (mode >= 2 && mode <= 4) return inject(176, 537), 0;
      return 1;
  }
  return 1;
}

static void terrain_slot_point(int slot, int *x, int *y) {
  int px = G16(0x4c79b4 + slot * 4), py = G16(0x4c79b6 + slot * 4);
  int h = G32(0x570cd4 + slot * 0xb0);
  *x = px + 31;
  *y = py + h - 32;   // FUN_00432f90: y in [py + h - 54, py + h - 10)
}

static int g_show_log;   // frames until the result of a HUD_ACT_SHOW is logged
static int g_show_golfer = -1, g_show_wait;   // a golfer to show once the dock is closed (the chip strip's precondition)

static void show_golfer(int slot) {
  // what a click on the golfer chip strip does (dock closed): select the golfer, camera to their tile
  G32(0x53df54) = slot + 1;
  G8(SCREEN_STATE) = 0;
  G32(0x4c2ba0) = G32(0x5794b8 + slot * 0x100) >> 10;
  G32(0x4c2ba4) = G32(0x5794bc + slot * 0x100) >> 10;
  fprintf(stderr, "hud: show golfer %d\n", slot);
  g_show_log = 4;
}

static void run_actions(void) {
  if (g_show_log && --g_show_log == 0)
    fprintf(stderr, "hud: after show: selection %d, camera tile %d,%d\n", G32(0x53df54), G32(0x4c2ba0), G32(0x4c2ba4));
  if (g_inj) {   // a click of ours is still on its way (or was swallowed)
    if (++g_inj_frames < 4) return;
    g_inj = 0;
    hudf_set_injecting(0);
  }
  if (staff_pending()) return;
  if (g_show_golfer >= 0 && !g_goal_on) {
    if (G32(DOCK_MODE) == 5) {
      show_golfer(g_show_golfer);
      g_show_golfer = -1;
    } else if (++g_show_wait > 20) {
      g_show_golfer = -1;
    }
    return;
  }
  if (g_staff_tab && !g_goal_on) {   // the People tray is open: its Staff tab (the clicks ui_hud.c makes)
    int mode = G32(DOCK_MODE);
    if (mode >= 2 && mode <= 4) {
      if (!G32(0x561254)) inject(mode == 2 ? 232 : 231, mode == 2 ? 538 : 540);
      g_staff_tab = 0;
    } else if (++g_staff_tab_wait > 20) {
      g_staff_tab = 0;
    }
    return;
  }
  if (g_goal_on) {
    if (++g_goal_steps > 6) {
      g_goal_on = 0;
    } else if (goal_step(g_goal.goal)) {
      g_goal_on = 0;
      if (g_goal.x <= -2) {
        int x, y;
        terrain_slot_point(-2 - g_goal.x, &x, &y);
        inject(x, y);
      } else if (g_goal.x >= 0) {
        inject(g_goal.x, g_goal.y);
      }
    }
    return;
  }
  HudAction a;
  if (!hudf_take_action(&a)) return;
  switch (a.kind) {
    case HUD_ACT_CLICK: inject(a.x, a.y); break;
    case HUD_ACT_KEY: G32(0x822b9c) = a.key; break;
    case HUD_ACT_VKEY: G32(0x822b98) = a.key; break;
    case HUD_ACT_SHOW:
      if (a.x == HUD_SHOW_GOLFER && golfer_ok(a.y)) {
        if (G32(DOCK_MODE) == 5) {
          show_golfer(a.y);
        } else {   // close the dock first (the chip strip only works then), then select
          g_show_golfer = a.y;
          g_show_wait = 0;
          g_goal = (HudAction){HUD_ACT_GOAL, -1, -1, HUD_GOAL_CLOSE, 0};
          g_goal_on = 1;
          g_goal_steps = 0;
        }
      } else if (a.x == HUD_SHOW_HOLE && a.y >= 1 && a.y <= 18) {
        // the hole hotkeys: 1..9, shift+1..9 for 10..18 (select the hole, camera to its tee and green)
        G32(0x822b9c) = a.y <= 9 ? '0' + a.y : "!@#$%^&*("[a.y - 10];
        fprintf(stderr, "hud: show hole %d\n", a.y);
        g_show_log = 4;
      } else if (a.x == HUD_SHOW_STAFF) {   // task-112: People, then its Staff tab
        g_staff_tab = 1;
        g_weed_until = 0;
        g_staff_tab_wait = 0;
        g_goal = (HudAction){HUD_ACT_GOAL, -1, -1, HUD_GOAL_PEOPLE, 0};
        g_goal_on = 1;
        g_goal_steps = 0;
        fprintf(stderr, "hud: show staff\n");
      } else if (a.x == HUD_SHOW_BUILDING) {
        pv_show_building(a.y);
        G8(SCREEN_STATE) = 0;
        g_show_log = 4;
      }
      break;
    case HUD_ACT_PRO: pro_action(a.x); break;
    case HUD_ACT_DESIGN:   // the strip's pick: what FUN_00434ac0's default case stores for the next map click
      if (a.x == G32(0x4c2854)) {
        G32(0x5a9f50) = G32(0x5a9f60) = G32(0x56a514) = a.y;
        fprintf(stderr, "hud: design tool %d design %d\n", a.x, a.y);
      }
      break;
    case HUD_ACT_STAFF: staff_action(a.x, a.y); break;
    case HUD_ACT_GOAL:
      g_goal = a;
      g_goal_on = 1;
      g_goal_steps = 0;
      run_actions();
      break;
  }
}

// task-27: a modal dialog opens while the old HUD has just drawn this frame (the hire panel is called from the
// employee tray's draw): put the course back under the old HUD so the dialog keeps a clean picture.
void hud_clean_pages(void) {
  if (!g_on) return;
  for (int i = 0; i < PAGES; i++) copy_rects(i, 0);
  hide_art();
}

// Test peek (task-95): one log line per tile the player's build changed: terrain 0x5722e8, the design byte
// 0x56988c, flags 0x53caf0. A landmark is terrain 16 with design + 1; a flower bed terrain 4 with its design.
static void log_tile_changes(void) {
  enum { N = 50 * 50 };
  static uint8_t t0[N], d0[N];
  static uint16_t f0[N];
  static int have;
  int ch = 0, first[8];
  uint8_t t1[N], d1[N];
  uint16_t f1[N];
  for (int i = 0; i < N; i++) {   // golf's addresses of arrays the grid moved: through grid_translate
    t1[i] = *(volatile uint8_t *)grid_translate(0x5722e8 + i);
    d1[i] = *(volatile uint8_t *)grid_translate(0x56988c + i);
    f1[i] = *(volatile uint16_t *)grid_translate(0x53caf0 + i * 2);
  }
  if (have)
    for (int i = 0; i < N; i++)
      if (t1[i] != t0[i] || d1[i] != d0[i] || (f1[i] ^ f0[i]) & 0x1220) {
        if (ch < 8) first[ch] = i;
        ch++;
      }
  if (ch > 0) {
    fprintf(stderr, "hud: tiles changed %d\n", ch);
    for (int k = 0; k < ch && k < 8; k++) {
      int i = first[k];
      fprintf(stderr, "hud: tile %d,%d terrain %d->%d design %d->%d flags %04x->%04x tool %d\n", i / 50, i % 50, t0[i], t1[i],
              d0[i], d1[i], f0[i], f1[i], G32(0x4c2854));
    }
  }
  memcpy(t0, t1, N);
  memcpy(d0, d1, N);
  memcpy(f0, f1, sizeof f0);
  have = 1;
}

void hud_restore(void) {
  if (!g_on) return;
  for (int i = 0; i < PAGES; i++)
    if (copy_rects(i, 2)) {
      g_target = i;
      break;
    }
  int theme = G32(0x5a34e0);
  if (g_target >= 0 && (theme != g_art_theme || ++g_art_frames > 600)) {
    g_art_theme = theme;
    g_art_frames = 0;
    snap_art();
  }
  for (int i = 0; i < PAGES; i++) copy_rects(i, 0);
  hide_art();
  HudSnap *s = &g_snap;
  read_state(s);
  s->valid = 1;
  s->serial = ++g_serial;
  s->art_serial = g_art_serial;
  hudf_publish(s);
  // one line whenever what the HUD shows changes (tests read these)
  static char last[256];
  char now[256];
  snprintf(now, sizeof now, "dock %d amenities %d elevation %d staff %d terrain %d build %d elev_tool %d elev_edit %d paused %d zoom %d rot %d shape %d pro %d%d%d money %d design %d club %d staff_count %d staff_limit %d",
           s->dock_mode, s->amenities_on, s->elevation_on, s->employee_overlay, s->terrain_tool, s->build_tool,
           s->elevation_tool, s->elev_edit, s->paused, G32(0x4c2844), s->rotation, s->shape, s->pro_ready, s->pro_out, s->pro_practice, s->money, s->design, s->clubhouse, s->staff_n, s->staff_limit);
  if (strcmp(now, last)) {
    fprintf(stderr, "hud: state %s\n", now);
    memcpy(last, now, sizeof last);
  }
  log_tile_changes();
  run_actions();
  pro_create_tick(s);
}

