// Frosted Mint reports, game-thread side, native build (simgolf mod, task-28). Overview in
// port/reports_frosted.h. This file does not include golf_protos.h (its types clash with the C library
// headers).
//
// Hooks (translator patches, port/tools/translate/golf.reports.json):
//   rp_tick      once per main-loop frame: keeps the native layer up, opens the report a tab asked for.
//   rp_begin     the main loop is about to run report N's modal loop: keep the picture.
//   rp_t650 ...  the report's text calls: record the text, then draw it as before.
//   rp_frame     once per iteration of the report's loop (and before its first present): publish, put the
//                picture back (golf's board never shows), run the native actions.
//   rp_done      the report returned.
#include <SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/hd_native.h"
#include "port/reports_frosted.h"

#define G32(a) (*(volatile int32_t *)(uintptr_t)(a))
#define SCREEN_STATE 0x569498
#define PAGE_WIDGET 0x519cd4   // the page the reports draw their text into
#define TITLE_FONT 0x821020    // the title font of the report boards

void FUN_00476650(void *w, int font, char *s, int x, int y, int len);
void FUN_004766a0(void *w, int font, char *s, int x, int y, int wd, char *len);
void FUN_00476700(void *w, int font, char *s, int x, int y, char *len);
void FUN_00476750(void *w, int font, char *s, int x, int y, char *len);
void FUN_00476310(void *w, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
int FUN_00404b70(char *s, int x, int y, uint32_t col);
int FUN_00404bc0(char *s, int x, int y, uint32_t col);
int FUN_004049d0(char *s, int x, int y, uint32_t col);
int FUN_00404ad0(char *s, int x, int y, uint32_t col);
uint32_t FUN_004657e0(int x, int y);   // original scaled left-click entry
void FUN_00480c80(void *page);   // flush the page's changes to the screen
void hd_art_dlg_begin(void);       // hd_art.c (task-88): golf's sprites drawn from here on are its old screen
void hd_art_hide_dialog(void);     // and are not drawn as the art pack's hi-res copies
void hud_clean_pages(void);
int FUN_00455a30(void);          // best scores (modal)

static int g_on;          // this report: the native panel replaces the original
static int g_kind;
static int g_top_local_close, g_top_close_key;
static int g_log = -1;
static int g_closing;     // a close was asked: the key is on its way
static int g_pending;     // a tab asked for this report once the current one has closed
static uint32_t g_serial;
static RpSnap g_snap;
static uint16_t g_col;    // colour set by the last FUN_00476310 on the page widget
static int g_dirty;       // the report drew since the last frame: its picture must be taken off the screen
static int g_ph_n;       // task-105: the photos golf pinned up on the accomplishment board
static int g_pair_row = -1;
static int g_pair_cancel_state;
static unsigned g_pair_cancel_expected;
static int g_pair_last_selected = -1;
static int g_pair_last_count = -1;
static int g_pair_last_cursor = -999;
static int g_pair_cursor_stable;
static int g_pair_inj_row = -1;

// ------------------------------------------------------------------ the picture from before the report

uint16_t *gdi_screen_dib(int i, int *w, int *h, int *pitch, int *is565);
enum { PAGES = 8 };
static uint16_t *g_keep[PAGES];
static int g_keep_w[PAGES], g_keep_h[PAGES];

static void keep_free(void) {
  for (int i = 0; i < PAGES; i++) {
    free(g_keep[i]);
    g_keep[i] = NULL;
  }
}

static void keep_save(void) {
  keep_free();
  for (int i = 0; i < PAGES; i++) {
    int w, h, pitch, is565;
    uint16_t *bb = gdi_screen_dib(i, &w, &h, &pitch, &is565);
    if (!bb) continue;
    g_keep[i] = malloc((size_t)w * h * 2);
    if (!g_keep[i]) continue;
    for (int y = 0; y < h; y++) memcpy(g_keep[i] + (size_t)y * w, bb + (ptrdiff_t)y * pitch, (size_t)w * 2);
    g_keep_w[i] = w, g_keep_h[i] = h;
  }
}

static void keep_restore(void) {
  for (int i = 0; i < PAGES; i++) {
    if (!g_keep[i]) continue;
    int w, h, pitch, is565;
    uint16_t *bb = gdi_screen_dib(i, &w, &h, &pitch, &is565);
    if (!bb || w != g_keep_w[i] || h != g_keep_h[i]) continue;
    for (int y = 0; y < h; y++) memcpy(bb + (ptrdiff_t)y * pitch, g_keep[i] + (size_t)y * w, (size_t)w * 2);
  }
}

// ------------------------------------------------------------------ injected input

static void push_key(SDL_Keycode k, int down) {
  SDL_Event e = {0};
  e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
  e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
  e.key.keysym.sym = k;
  e.key.keysym.scancode = SDL_GetScancodeFromKey(k);
  SDL_PushEvent(&e);
}

static int g_inj, g_inj_x, g_inj_y, g_inj_stage, g_inj_wait, g_inj_btn = SDL_BUTTON_LEFT;

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
  e.button.button = (Uint8)g_inj_btn;
  e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
  e.button.clicks = 1;
  e.button.x = x;
  e.button.y = y;
  SDL_PushEvent(&e);
}

// task-102: the clicks a page asks for while one is still being sent wait here (they used to be dropped)
static int g_iq[8], g_iq_btn[8], g_iqn;
static int g_log_on(void) { return g_log > 0; }

static void inj_start(int arg, int btn) {
  g_inj = 1;
  g_inj_btn = btn;
  g_inj_x = arg & 0xffff;
  g_inj_y = (arg >> 16) & 0xffff;
  g_pair_inj_row = g_kind == RP_PAIRING && g_inj_x >= 6 && g_inj_y >= 0x32
    ? ((g_inj_y - 0x32) / 0x88) * 2 + (g_inj_x - 6) / 0x14e : -1;
  g_inj_stage = g_inj_wait = 0;
  if (g_log_on()) fprintf(stderr, "reports: click at the original's %d,%d\n", g_inj_x, g_inj_y);
}

// Pairing reads its original pending-click state and requires the cursor row to
// match the previous loop. Route that click through golf's original event entry
// after the hovered row is stable. Other task-73 pages keep their measured SDL
// click sequence.
static void inj_step(void) {
  if (!g_inj) return;
  if (g_inj_wait > 0) {
    g_inj_wait--;
    return;
  }
  int page = g_kind >= RP_PROFILE;
  int pairing = g_kind == RP_PAIRING;
  if (g_log_on() && page) fprintf(stderr, "reports: click step %d at %u ms cursor %d mask 0x%04x\n",
    g_inj_stage, (unsigned)SDL_GetTicks(), g_snap.ext[1], (unsigned)g_snap.ext[0] & 0xffffu);
  switch (g_inj_stage) {
    case 0:
      sdl_motion(g_inj_x, g_inj_y);
      g_inj_wait = !page;
      break;
    case 1:
      if (pairing) {
        int close_click = g_pair_inj_row == -2;
        int cursor_ready = close_click
          ? g_pair_last_cursor == -1
          : g_pair_inj_row >= 0 && g_pair_last_cursor == g_pair_inj_row;
        if (!cursor_ready || !g_pair_cursor_stable) {
          if (g_log_on()) fprintf(stderr, "reports: pairing click waits for stable %s (cursor %d stable %d)\n",
            close_click ? "outside cursor" : "target row", g_pair_last_cursor, g_pair_cursor_stable);
          return;
        }
        FUN_004657e0(g_inj_x, g_inj_y);
        if (g_log_on()) {
          if (close_click) fprintf(stderr, "reports: pairing click uses original close input at %d,%d\n", g_inj_x, g_inj_y);
          else fprintf(stderr, "reports: pairing click uses original input row %d at %d,%d\n",
            g_pair_inj_row, g_inj_x, g_inj_y);
        }
      } else {
        sdl_button(g_inj_x, g_inj_y, 1);
        g_inj_wait = page ? 0 : 2;
      }
      if (page && !pairing) {   // golf takes these clicks once the button is up in the same turn
        sdl_button(g_inj_x, g_inj_y, 0);
        g_inj_stage++;
      }
      break;
    case 2:
      if (!pairing) sdl_button(g_inj_x, g_inj_y, 0);
      g_inj_wait = !page;
      break;
    case 3:   // task-73 pages: park golf's pointer where it hovers over nothing (no tooltip in the kept pictures)
      // The tract loop still reads hover after pumping the click. Keep its
      // original control position until return; 690,160 is a buyable tract.
      if (page && !g_iqn) {
        if (pairing) {
          // Keep the cursor on the last row until the selector has accepted the
          // outside click; it ignores input when the cursor changes in the same turn.
          if (!g_pair_cancel_state) sdl_motion(790, 590);
        }
        else if (g_kind != RP_TRACTS) sdl_motion(690, 160);
      }
      g_inj_wait = !page;
      break;
    default:
      g_inj = 0;
      if (g_iqn) {   // the next click the page asked for
        int arg = g_iq[0], btn = g_iq_btn[0];
        memmove(g_iq, g_iq + 1, sizeof g_iq[0] * (size_t)(g_iqn - 1));
        memmove(g_iq_btn, g_iq_btn + 1, sizeof g_iq_btn[0] * (size_t)(g_iqn - 1));
        g_iqn--;
        inj_start(arg, btn);
      }
      return;
  }
  g_inj_stage++;
}

static int pair_click_arg(int index) {
  int x = 6 + (index & 1) * 0x14e + 0x70;
  int y = 0x32 + (index / 2) * 0x88 + 0x44;
  return x | (y << 16);
}

static void pair_cancel_step(void) {
  if (!g_pair_cancel_state) return;
  if (g_kind != RP_PAIRING) {
    g_pair_cancel_state = 0;
    return;
  }
  if (g_inj || g_iqn) return;

  unsigned selected = (unsigned)g_snap.ext[0] & 0xffffu;
  if (g_pair_cancel_state == 2) {
    if (selected != g_pair_cancel_expected) {
      if (g_log_on()) fprintf(stderr, "reports: pairing cancel stopped; selection did not clear (0x%04x)\n", selected);
      g_pair_cancel_state = 0;
      return;
    }
    g_pair_cancel_state = 1;
  }

  if (selected == 0) {
    if (g_pair_cancel_state != 3) {
      // Escape can be lost while the original selector is processing SDL key state.
      // Its own click path exits on a stable click outside all golfer rows.
      g_pair_cancel_state = 3;
      inj_start(0 | (0x76 << 16), SDL_BUTTON_LEFT);
      g_pair_inj_row = -2;
      if (g_log_on()) fprintf(stderr, "reports: pairing cancel clears selections then clicks outside the original rows\n");
    }
    return;
  }

  int index = 0;
  while (index < 16 && !(selected & (1u << index))) index++;
  if (index >= 16) {
    g_pair_cancel_state = 0;
    return;
  }
  g_pair_cancel_expected = selected & ~(1u << index);
  g_pair_cancel_state = 2;
  inj_start(pair_click_arg(index), SDL_BUTTON_LEFT);
  if (g_log_on()) fprintf(stderr, "reports: pairing cancel clears golfer %d through the original selector\n", index + 1);
}

// ------------------------------------------------------------------ hooks

static int is_overlay(int k) {
  return k == RP_MONTH || k == RP_COURSE || k == RP_COMMENTS || k == RP_HISTO || k == RP_KEYS || k == RP_FINANCE || k == RP_SGA ||
         k == RP_ROSTER;
}

void rp_done(void);

void rp_tick(void) {
  int on = rpf_enabled();   // keeps the layer registered and drawing before a report opens
  if (g_on && g_kind == RP_MONTH && G32(SCREEN_STATE) != RP_MONTH) rp_done();   // the per-frame report went away
  if (!g_pending) return;
  int k = g_pending;
  g_pending = 0;
  if (!on) return;
  if (g_log) fprintf(stderr, "reports: open %d from a tab\n", k);
  if (is_overlay(k)) G32(SCREEN_STATE) = k;
  else if (k == RP_BEST) {
    void rp_begin(int kind);
    rp_begin(RP_BEST);
    FUN_00455a30();
    rp_done();
  }
}

enum { PIC_ART = RP_ART - 4 };
static struct {
  int on, x, y, w, h, art, page;
  uint32_t hash;
} g_pic[3];
static int g_npic;

static void pic_add(int x, int y, int w, int h) {
  if (g_npic >= 3) return;
  g_pic[g_npic].on = 1, g_pic[g_npic].x = x, g_pic[g_npic].y = y, g_pic[g_npic].w = w, g_pic[g_npic].h = h;
  g_pic[g_npic].art = PIC_ART + g_npic, g_pic[g_npic].page = -1, g_pic[g_npic].hash = 0;
  g_npic++;
}

void rp_begin(int kind) {
  if (g_log < 0) g_log = getenv("SIMGOLF_REPORTS_LOG") != NULL;
  g_on = rpf_enabled_kind(kind);
  g_kind = kind;
  g_top_local_close = g_top_close_key = 0;
  g_closing = 0;
  g_inj = 0;
  g_iqn = 0;
  g_pair_cancel_state = 0;
  g_col = 0;
  g_dirty = 1;
  memset(&g_snap, 0, offsetof(RpSnap, it));
  g_snap.magic = RP_MAGIC;
  g_snap.kind = kind;
  if (kind == RP_PHOTO) g_snap.ext[5] = g_serial + 1;   // a new board resets the renderer's history selection
  if (kind == RP_END_YEAR || kind == RP_PAIRING) g_snap.ext[5] = g_serial + 1;
  g_npic = 0;
  g_ph_n = 0;
  g_pair_row = -1;
  g_pair_last_selected = -1;
  g_pair_last_count = -1;
  g_pair_last_cursor = -999;
  g_pair_cursor_stable = 0;
  g_pair_inj_row = -1;
  memset(g_pic, 0, sizeof g_pic);
  if (kind == RP_PROFILE) pic_add(358, 31, 76, 184);   // the portrait in golf's page, clear of the buttons around it
  if (kind == RP_PROFILE) pic_add(298, 205, 48, 48);    // task-102c: the little golfers on their disc (shirt, pants, skin)
  if (kind == RP_TRACTS) pic_add(8, 282, 784, 312);    // the map below the tract tiles, with its painted frame
  if (g_log) fprintf(stderr, "reports: open %d native %d\n", kind, g_on), fflush(stderr);
  if (g_on) {
    hd_ui_show_golf_ui(0);
    hud_clean_pages();
    hd_art_dlg_begin();   // task-102c: the art pack's hi-res copies of golf's own screen are hidden too (below)
    keep_save();
  }
}

static void copy_str(char *dst, int cap, const char *src, int len) {
  int n = 0;
  if (len < 0 || len > cap - 1) len = cap - 1;
  while (n < len && src && src[n]) {
    dst[n] = src[n];
    n++;
  }
  dst[n] = 0;
}

// One thing the report drew. Drawn again at the same place, it replaces the old one (the loops redraw rows
// when they scroll or hover).
static RpItem *slot(int type, int x, int y) {
  RpSnap *p = &g_snap;
  g_dirty = 1;
  for (int i = 0; i < p->n; i++)
    if (p->it[i].x == x && p->it[i].y == y && p->it[i].type == type) return &p->it[i];
  if (p->n >= RP_ITEMS) return NULL;
  RpItem *it = &p->it[p->n++];
  memset(it, 0, sizeof *it);
  it->type = (uint8_t)type;
  it->x = (int16_t)x;
  it->y = (int16_t)y;
  return it;
}

static RpItem *record(const char *s, int len, int x, int y, int align, int big) {
  if (!g_on || !s) return NULL;
  RpSnap *p = &g_snap;
  if (big && !p->title[0]) {
    copy_str(p->title, sizeof p->title, s, len);
    return NULL;
  }
  RpItem *it = slot(RP_IT_TEXT, x, y);
  if (!it) return NULL;
  it->group = g_kind == RP_PAIRING && g_pair_row >= 0 ? (uint8_t)g_pair_row : 0xff;
  it->align = (uint8_t)align;
  it->big = (uint8_t)big;
  it->col = g_col;
  copy_str(it->s, sizeof it->s, s, len);
  return it;
}

void FUN_00478b80(void *w, int x0, int y0, int x1, int y1, uint32_t col);
void FUN_00480b00(void *page, int x, int y, int w, int h, uint32_t col);
int FUN_00473e60(void *sprite, int target, int x, int y, int clip);

// golf's line (the histograph's curves)
void rp_line(void *w, int x0, int y0, int x1, int y1, uint32_t col) {
  if (g_on) {
    RpItem *it = slot(RP_IT_LINE, x0, y0);
    if (it) it->w = (int16_t)x1, it->h = (int16_t)y1, it->col = (uint16_t)col;
  }
  FUN_00478b80(w, x0, y0, x1, y1, col);
}

// golf's filled box (the histograph's event dots, the roster's scroll thumb)
void rp_rect(void *page, int x, int y, int w, int h, uint32_t col) {
  if (g_on) {
    RpItem *it = slot(RP_IT_RECT, x, y);
    if (it) it->w = (int16_t)w, it->h = (int16_t)h, it->col = (uint16_t)col;
  }
  FUN_00480b00(page, x, y, w, h, col);
}

// The page the report draws on: the one whose pixel at (x, y) now differs from the kept picture.
uint16_t *gdi_dib_at(uintptr_t p, int *w, int *h, int *pitch, int *is565);
static int drawn_page(int x, int y) {
  if (g_kind == RP_TRACTS || g_kind == RP_PHOTO) {   // match the actual target surface, not an animated page's changed pixel
    uint32_t wrap = g_kind == RP_PHOTO ? PAGE_WIDGET : (uint32_t)G32(0x4c1570);
    uint32_t p = wrap > 0x10000 ? G32(wrap + 4) : 0, bits = p > 0x10000 ? G32(p + 0x4c0) : 0;
    int w, h, pitch, is565;
    uint16_t *want = bits ? gdi_dib_at(bits, &w, &h, &pitch, &is565) : NULL;
    for (int i = 0; want && i < PAGES; i++)
      if (gdi_screen_dib(i, &w, &h, &pitch, &is565) == want) return i;
    return -1;
  }
  for (int i = 0; i < PAGES; i++) {
    if (!g_keep[i]) continue;
    int w, h, pitch, is565;
    uint16_t *bb = gdi_screen_dib(i, &w, &h, &pitch, &is565);
    if (!bb || w != g_keep_w[i] || h != g_keep_h[i] || x < 0 || y < 0 || x >= w || y >= h) continue;
    if (g_log > 0 && getenv("SIMGOLF_REPORTS_DUMP")) fprintf(stderr, "reports: page %d is %dx%d\n", i, w, h);
    if (bb[(ptrdiff_t)y * pitch + x] != g_keep[i][(size_t)y * w + x]) return i;
  }
  return -1;
}

// golf's sprites in a report that carry meaning (the roster's status balls, photo and heart marks): kept
// as golf's own art. Each sprite is read once, with alpha, and published to the render thread.
static struct {
  void *spr;
  int page, done;
  int plain;        // the sprite is one flat colour (a highlight box): drawn as a tinted chip
  uint16_t col;
} g_art[RP_ART];
static int g_nart;

static int sprite_size(void *spr, int *w, int *h, int *ox, int *oy) {
  *w = G32((uintptr_t)spr + 0x10), *h = G32((uintptr_t)spr + 0x14);   // the sprite's size; +0x20/+0x24 its offset
  *ox = G32((uintptr_t)spr + 0x20), *oy = G32((uintptr_t)spr + 0x24);
  return *w > 0 && *h > 0 && *w <= 128 && *h <= 128;
}

int rp_chip(void *sprite, int target, int x, int y, int clip) {
  int r = FUN_00473e60(sprite, target, x, y, clip);
  int w, h, ox, oy;
  uintptr_t sp = (uintptr_t)sprite;
  if (sp == 0x820f40 || sp == 0x820f6c || sp == 0x820fc4) return r;   // the board's close button (the panel has its own)
  if (!g_on || !sprite_size(sprite, &w, &h, &ox, &oy)) return r;
  int k = 0;
  while (k < g_nart && g_art[k].spr != sprite) k++;
  if (k == g_nart) {
    if (g_nart == RP_ART) return r;
    g_art[k].spr = sprite;
    g_art[k].page = drawn_page(x + ox + w / 2, y + oy + h / 2);
    g_art[k].done = 0;
    g_nart++;
  }
  RpItem *it = slot(RP_IT_CHIP, x + ox, y + oy);
  if (it) {
    it->w = (int16_t)w, it->h = (int16_t)h, it->art = (uint8_t)(k + 1);
    it->big = (uint8_t)g_art[k].plain, it->col = g_art[k].col;
  }
  return r;
}

// Draws each new sprite into a corner of its page over black and over white (the page is put back right
// after) and turns the two into ARGB: a = 1 - (white - black), colour = black / a.
static void capture_art(void) {
  for (int k = 0; k < g_nart; k++) {
    if (g_art[k].done) continue;
    g_art[k].done = 1;
    int sw, sh, ox, oy, w, h, pitch, is565;
    if (!sprite_size(g_art[k].spr, &sw, &sh, &ox, &oy) || g_art[k].page < 0) continue;
    uint16_t *bb = gdi_screen_dib(g_art[k].page, &w, &h, &pitch, &is565);
    if (!bb || sw + 8 > w || sh + 8 > h) continue;
    uint16_t *shot = malloc((size_t)sw * sh * 2 * 2);
    uint32_t *px = malloc((size_t)sw * sh * 4);
    if (!shot || !px) {
      free(shot), free(px);
      continue;
    }
    uint16_t white = is565 ? 0xffff : 0x7fff;
    for (int pass = 0; pass < 2; pass++) {
      for (int y = 0; y < sh + 8; y++)
        for (int x = 0; x < sw + 8; x++) bb[(ptrdiff_t)y * pitch + x] = pass ? white : 0;
      FUN_00473e60(g_art[k].spr, PAGE_WIDGET, 4 - ox, 4 - oy, 0);
      for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) shot[(size_t)pass * sw * sh + y * sw + x] = bb[(ptrdiff_t)(y + 4) * pitch + x + 4];
    }
    for (int i = 0; i < sw * sh; i++) {
      uint16_t b = shot[i], wv = shot[sw * sh + i];
      int br, bg, bl, wr, wg, wb;
      if (is565) br = (b >> 11) & 31, bg = (b >> 6) & 31, bl = b & 31, wr = (wv >> 11) & 31, wg = (wv >> 6) & 31, wb = wv & 31;
      else br = (b >> 10) & 31, bg = (b >> 5) & 31, bl = b & 31, wr = (wv >> 10) & 31, wg = (wv >> 5) & 31, wb = wv & 31;
      int a = 255 - ((wr - br) + (wg - bg) + (wb - bl)) * 255 / (3 * 31);
      a = a < 0 ? 0 : a > 255 ? 255 : a;
      uint32_t v = 0;
      if (a > 0) {
        int rr = br * 255 / 31 * 255 / a, gg = bg * 255 / 31 * 255 / a, b2 = bl * 255 / 31 * 255 / a;
        rr = rr > 255 ? 255 : rr, gg = gg > 255 ? 255 : gg, b2 = b2 > 255 ? 255 : b2;
        v = (uint32_t)a << 24 | (uint32_t)rr << 16 | (uint32_t)gg << 8 | (uint32_t)b2;
      }
      px[i] = v;
    }
    // one flat opaque colour: a highlight box, not a picture
    int same = 0;
    uint16_t c0 = shot[(sh / 2) * sw + sw / 2];
    for (int i = 0; i < sw * sh; i++) same += shot[i] == c0 && shot[sw * sh + i] == c0;
    if (sw >= 16 && same * 10 >= sw * sh * 8) {
      g_art[k].plain = 1, g_art[k].col = c0;
      for (int i = 0; i < g_snap.n; i++)
        if (g_snap.it[i].type == RP_IT_CHIP && g_snap.it[i].art == k + 1) g_snap.it[i].big = 1, g_snap.it[i].col = c0;
    }
    rpf_publish_art(k, px, sw, sh);
    if (g_log) fprintf(stderr, "reports: art %d %dx%d\n", k, sw, sh);
    free(shot), free(px);
  }
}

void rp_quad(void *w, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  if ((uintptr_t)w == PAGE_WIDGET) g_col = (uint16_t)(a & 0xffff);
  FUN_00476310(w, a, b, c, d);
}

void rp_t650(void *w, int font, char *s, int x, int y, int len) {
  if ((uintptr_t)w == PAGE_WIDGET) record(s, len, x, y, RP_AL_LEFT, font == TITLE_FONT);
  FUN_00476650(w, font, s, x, y, len);
}

void rp_t6a0(void *w, int font, char *s, int x, int y, int wd, char *len) {
  if ((uintptr_t)w == PAGE_WIDGET) record(s, (int)(intptr_t)len, x + wd / 2, y, RP_AL_CENTRE, font == TITLE_FONT);
  FUN_004766a0(w, font, s, x, y, wd, len);
}

void rp_t700(void *w, int font, char *s, int x, int y, char *len) {
  if ((uintptr_t)w == PAGE_WIDGET) record(s, (int)(intptr_t)len, x, y, RP_AL_LEFT, font == TITLE_FONT);
  FUN_00476700(w, font, s, x, y, len);
}

void rp_t750(void *w, int font, char *s, int x, int y, char *len) {
  if ((uintptr_t)w == PAGE_WIDGET) record(s, (int)(intptr_t)len, x, y, RP_AL_RIGHT, font == TITLE_FONT);
  FUN_00476750(w, font, s, x, y, len);
}

// golf's plain text calls (centred: FUN_00404b70 and FUN_00404bc0; left: FUN_004049d0 and FUN_00404ad0)
static int rp_tc_original(int fn, char *s, int x, int y, uint32_t col) {
  switch (fn) {
    case 0: return FUN_00404b70(s, x, y, col);
    case 1: return FUN_00404bc0(s, x, y, col);
    case 2: return FUN_004049d0(s, x, y, col);
    default: return FUN_00404ad0(s, x, y, col);
  }
}
int rp_tc(int fn, char *s, int x, int y, uint32_t col) {
  RpItem *it = record(s, -1, x, y, fn < 2 ? RP_AL_CENTRE : RP_AL_LEFT, 0);
  if (it) it->col = (uint16_t)col;
  return rp_tc_original(fn, s, x, y, col);
}

// ------------------------------------------------------------------ task-73 pages: a rectangle of golf's page kept as a picture

// The golfer profile's portrait (golf's own caricature art with its painted frame) is not text or a sprite: the
// rectangle is read from the page once golf has drawn the frame and sent to the render thread whenever it changes.
static void dump_pages(void) {   // debug: SIMGOLF_REPORTS_DUMP=pages saves every screen page once
  static int done;
  const char *e = getenv("SIMGOLF_REPORTS_DUMP");
  if (done || !e || strcmp(e, "pages")) return;
  done = 1;
  for (int i = 0; i < PAGES; i++) {
    int w, h, pitch, is565;
    uint16_t *bb = gdi_screen_dib(i, &w, &h, &pitch, &is565);
    if (!bb) continue;
    uint32_t *px = malloc((size_t)w * h * 4);
    if (!px) continue;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        uint16_t v = bb[(ptrdiff_t)y * pitch + x];
        int r, g, b;
        if (is565) r = ((v >> 11) & 31) * 255 / 31, g = ((v >> 5) & 63) * 255 / 63, b = (v & 31) * 255 / 31;
        else r = ((v >> 10) & 31) * 255 / 31, g = ((v >> 5) & 31) * 255 / 31, b = (v & 31) * 255 / 31;
        px[y * w + x] = 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
      }
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_ARGB8888);
    char name[200];
    snprintf(name, sizeof name, "rp_page%d.bmp", i);
    if (sf) SDL_SaveBMP(sf, name), SDL_FreeSurface(sf);
    free(px);
  }
}

static void capture_pics(void) {
  dump_pages();
  for (int k = 0; k < g_npic; k++) {
    if (!g_pic[k].on) continue;
    int w, h, pitch, is565;
    if (g_pic[k].page < 0) g_pic[k].page = drawn_page(g_pic[k].x + g_pic[k].w / 2, g_pic[k].y + g_pic[k].h / 2);
    if (g_pic[k].page < 0) continue;
    uint16_t *bb = gdi_screen_dib(g_pic[k].page, &w, &h, &pitch, &is565);
    if (!bb || g_pic[k].x < 0 || g_pic[k].y < 0 || g_pic[k].w <= 0 || g_pic[k].h <= 0 ||
        g_pic[k].x > w - g_pic[k].w || g_pic[k].y > h - g_pic[k].h) continue;
    int pw = g_pic[k].w, ph = g_pic[k].h;
    if (g_kind == RP_PROFILE && g_keep[g_pic[k].page] && g_keep_w[g_pic[k].page] == w) {   // golf has not drawn it yet: the course shows
      int same = 1;
      for (int y = 0; y < ph && same; y += 4)
        for (int x = 0; x < pw; x += 4)
          if (bb[(ptrdiff_t)(g_pic[k].y + y) * pitch + g_pic[k].x + x] != g_keep[g_pic[k].page][(size_t)(g_pic[k].y + y) * w + g_pic[k].x + x]) { same = 0; break; }
      if (same) continue;
    }
    uint32_t *px = malloc((size_t)pw * ph * 4);
    if (!px) continue;
    uint32_t hash = 2166136261u;
    for (int y = 0; y < ph; y++)
      for (int x = 0; x < pw; x++) {
        uint16_t v = bb[(ptrdiff_t)(g_pic[k].y + y) * pitch + g_pic[k].x + x];
        hash = (hash ^ v) * 16777619u;
        int r, g, b;
        if (is565) r = ((v >> 11) & 31) * 255 / 31, g = ((v >> 5) & 63) * 255 / 63, b = (v & 31) * 255 / 31;
        else r = ((v >> 10) & 31) * 255 / 31, g = ((v >> 5) & 31) * 255 / 31, b = (v & 31) * 255 / 31;
        px[y * pw + x] = 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
      }
    if (hash != g_pic[k].hash) {
      g_pic[k].hash = hash;
      rpf_publish_art(g_pic[k].art, px, pw, ph);
      if (g_log) fprintf(stderr, "reports: picture %d %dx%d from page %d\n", k, pw, ph, g_pic[k].page);
      if (g_log && getenv("SIMGOLF_REPORTS_DUMP")) {
        SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormatFrom(px, pw, ph, 32, pw * 4, SDL_PIXELFORMAT_ARGB8888);
        if (sf) SDL_SaveBMP(sf, "rp_picture.bmp"), SDL_FreeSurface(sf);
      }
    }
    free(px);
    RpItem *it = slot(RP_IT_PIC, g_pic[k].x, g_pic[k].y);
    if (it) it->w = (int16_t)pw, it->h = (int16_t)ph, it->art = (uint8_t)(g_pic[k].art + 1);
  }
}

int rp_is_on(void) { return g_on; }
int rp_game_active(void) { return g_kind != RP_NONE; }

void rp_year_continue(int y) {
  if (!g_on || g_kind != RP_END_YEAR) return;
  y += 0x16;   // the game's 0x226..0x251 button hit box is 44 pixels high
  if (y < 0) y = 0;
  if (y > 599) y = 599;
  g_snap.ext[0] = y;
}

void rp_pair_row(int index) {
  g_pair_row = g_kind == RP_PAIRING && index >= 0 && index < 16 ? index : -1;
  if (!g_on || g_pair_row < 0) return;
  // The original redraw shifts selected and hovered rows by a pixel, and may
  // move trait lines when their count changes. Replace this row's previous
  // text before recording its new layout instead of retaining both versions.
  int kept = 0;
  for (int i = 0; i < g_snap.n; i++) {
    if (g_snap.it[i].type == RP_IT_TEXT && g_snap.it[i].group == g_pair_row) continue;
    if (kept != i) g_snap.it[kept] = g_snap.it[i];
    kept++;
  }
  g_snap.n = kept;
  g_dirty = 1;
}

void rp_pair_state(unsigned selected, int cursor, int count, int picked, int style) {
  if (g_kind != RP_PAIRING) return;
  if (count < 0) count = 0;
  if (count > 16) count = 16;
  if (g_pair_last_cursor != cursor) g_pair_cursor_stable = 0;
  else g_pair_cursor_stable = 1;
  if (g_log_on() && g_pair_last_count != count) {
    fprintf(stderr, "reports: pairing candidate count %d\n", count);
    g_pair_last_count = count;
  }
  if (g_log_on() && g_pair_last_selected != (int)(selected & 0xffffu)) {
    fprintf(stderr, "reports: pairing selection mask 0x%04x\n", (unsigned)(selected & 0xffffu));
    g_pair_last_selected = (int)(selected & 0xffffu);
  }
  if (g_log_on() && g_pair_last_cursor != cursor) {
    fprintf(stderr, "reports: pairing cursor row %d\n", cursor);
  }
  g_pair_last_cursor = cursor;
  if (!g_on) return;
  g_snap.ext[0] = (int32_t)(selected & 0xffffu);
  g_snap.ext[1] = cursor;
  g_snap.ext[2] = count;
  g_snap.ext[3] = picked;
  g_snap.ext[4] = style;
  g_snap.ext[6] = g_pair_cancel_state != 0;
}

int rp_pair_cursor_override(int cursor) {
  if (!g_on || g_kind != RP_PAIRING || !g_inj) return cursor;
  if (g_pair_inj_row == -2) return -1;
  if (g_pair_inj_row >= 0 && g_pair_inj_row < 16 && g_pair_inj_row < g_snap.ext[2])
    return g_pair_inj_row;
  return cursor;
}

int rp_pair_y_limit(int original_limit, int count) {
  if (!g_on || g_kind != RP_PAIRING) return original_limit;
  if (count < 0) count = 0;
  if (count > 16) count = 16;
  int rows = (count + 1) / 2;
  int native_limit = 0x32 + rows * 0x88;
  return native_limit > original_limit ? native_limit : original_limit;
}

// golf's text calls inside functions that other screens use too (FUN_0045b0d0 wraps the bio and Gary's hints): recorded only for
// the task-73 pages.
int rp_tcp(int fn, char *s, int x, int y, uint32_t col) {
  if (g_on && g_kind >= RP_PROFILE) return rp_tc(fn, s, x, y, col);
  return rp_tc_original(fn, s, x, y, col);
}

// The golfer profile's nested screens. The face picker (FUN_00438390) is a modal loop of its own on the page, drawn over
// the profile page: while it runs the original look comes back (the page as golf drew it last, golf's own UI layer).
// The bio editor (the text widget loop) keeps the native panel up and shows the bio golf is editing, live.
void rp_frame(void);
static int g_sub, g_in_edit;
static uint16_t *g_pf;          // the profile page as golf drew it (last frame, before the picture from before it was put back)
static int g_pf_w, g_pf_h;

static void profile_page_keep(void) {
  if (g_kind != RP_PROFILE || g_npic < 1 || g_pic[0].page < 0) return;
  int w, h, pitch, is565;
  uint16_t *bb = gdi_screen_dib(g_pic[0].page, &w, &h, &pitch, &is565);
  if (!bb) return;
  if (!g_pf || g_pf_w != w || g_pf_h != h) {
    free(g_pf);
    g_pf = malloc((size_t)w * h * 2);
    g_pf_w = w, g_pf_h = h;
  }
  if (g_pf)
    for (int y = 0; y < h; y++) memcpy(g_pf + (size_t)y * w, bb + (ptrdiff_t)y * pitch, (size_t)w * 2);
}

void rp_sub(int open) {
  if (!g_on || g_kind != RP_PROFILE) return;
  if (g_log) fprintf(stderr, "reports: profile nested screen %s\n", open ? "open" : "closed");
  if (open) {
    g_sub = 1;
    int w, h, pitch, is565;
    uint16_t *bb = g_npic > 0 && g_pic[0].page >= 0 ? gdi_screen_dib(g_pic[0].page, &w, &h, &pitch, &is565) : NULL;
    if (bb && g_pf && w == g_pf_w && h == g_pf_h)
      for (int y = 0; y < h; y++) memcpy(bb + (ptrdiff_t)y * pitch, g_pf + (size_t)y * w, (size_t)w * 2);
    rpf_end();
    hd_ui_show_golf_ui(1);
  } else {
    g_sub = 0;
    hd_ui_show_golf_ui(0);
    keep_restore();
  }
}

// once per iteration of the bio editor's loop
void rp_edit(void) {
  if (!g_on || g_kind != RP_PROFILE) return;
  g_in_edit = 1;
  rp_frame();
  g_in_edit = 0;
}

// SIMGOLF_REPORTS_LOG: once a second, how often the page's loop turns and what rp_frame costs of that (card task-102)
static void frame_stats(double t0, double t1) {
  static double last_end, win_start, cost;
  static int n;
  if (g_log <= 0) return;
  if (t0 - last_end > 500 || win_start == 0) win_start = t0, n = 0, cost = 0;   // a new page or a long wait: a new window
  last_end = t1;
  n++;
  cost += t1 - t0;
  if (t1 - win_start >= 1000) {
    fprintf(stderr, "reports: kind %d loop %.1f/s, rp_frame %.2f ms each\n", g_kind, n * 1000.0 / (t1 - win_start), cost / n);
    fflush(stderr);
    win_start = t1, n = 0, cost = 0;
  }
}

// hud_game.c: the profile page is the first step of the pro's creation (golf's skill setup follows it)
static int g_pro_next;
void rp_pro_next(int on) { g_pro_next = on; }

// ------------------------------------------------------------------ task-102: golf's skill points screen (FUN_0045f0f0)

// The screen draws golf's skills with tiny yellow arrows; with the Frosted pages it is a native page. Nothing of it is
// text the page records: the levels, the points and the names are read from golf's state each turn, and a click goes
// to the arrow's place in golf's screen (x0 + 0x32..0x52, the row's top half adds, the bottom half takes away; OK at
// x0 + 0x15e, 0x14e), so golf's own rules apply (at most 10, never below the level the screen opened with).
static struct {
  int points, who, x0;
  uint8_t start[10];
  char name[64], text[RP_TEXT * 4];
} g_sk;

static volatile uint8_t *sk_levels(void) {   // where FUN_0045f0f0 reads the levels it shows
  int who = g_sk.who;
  if (who == -1 || *(volatile int16_t *)(uintptr_t)(0x57956e + who * 0x100) == 0) return (volatile uint8_t *)(uintptr_t)0x5a5a04;
  return (volatile uint8_t *)(uintptr_t)(0x5795a8 + who * 0x100);
}

int rp_skills_frosted(void) { return rpf_enabled_kind(RP_SKILLS); }

void rp_skills(char *name, int points, int who, int x0) {
  g_sk.points = points, g_sk.who = who, g_sk.x0 = x0;
  copy_str(g_sk.name, sizeof g_sk.name, name ? name : "", -1);
  // golf's text buffer holds what the caller said before the screen (the first setup, an accomplishment's three points)
  copy_str(g_sk.text, sizeof g_sk.text, points > 0 ? (const char *)(uintptr_t)0x51a068 : "", -1);
  // golf's strip text ends "...\n" ("Add three skill points to your player...\n"): one line, a full stop
  for (char *q = g_sk.text; *q; q++)
    if ((unsigned char)*q < 32) *q = ' ';
  size_t L = strlen(g_sk.text);
  while (L && g_sk.text[L - 1] == ' ') g_sk.text[--L] = 0;
  if (L >= 3 && !strcmp(g_sk.text + L - 3, "...")) g_sk.text[L - 2] = 0;
  for (int i = 0; i < 10; i++) g_sk.start[i] = *(volatile uint8_t *)(uintptr_t)(0x5a5a04 + i);
  rp_begin(RP_SKILLS);
  if (g_log > 0) fprintf(stderr, "reports: skills page for %s, %d points, who %d, native %d\n", g_sk.name, points, who, g_on);
}

// The pages' waits (FUN_004385d0 and FUN_0045f0f0 wait 50 ms, twice a turn in the profile): golf's pages turned about seven
// times a second. The native page only needs golf to take its clicks, so while it is up golf does not wait (its pump still
// yields to the system when nothing is queued). Measured on the profile, Hard, 1080p: 7 turns a second, 10 ms waits 15.
int FUN_0045bf80(int a, int b);
void rp_pace(void) {
  if (!(g_on && g_kind >= RP_PROFILE)) FUN_0045bf80(5, 0);
}

static void skills_snap(void) {
  volatile uint8_t *lv = sk_levels();
  int spent = 0;
  for (int i = 0; i < 10; i++) spent += *(volatile uint8_t *)(uintptr_t)(0x5a5a04 + i) - g_sk.start[i];
  copy_str(g_snap.title, sizeof g_snap.title, g_sk.name, -1);
  copy_str(g_snap.bio, sizeof g_snap.bio, g_sk.text, -1);
  g_snap.ext[1] = g_sk.points;
  g_snap.ext[2] = g_sk.points - spent;   // golf's own count (local_314): one per level added, back on a level taken away
  g_snap.ext[3] = g_sk.x0;
  g_snap.ext[4] = g_sk.who;   // -1: the first setup; golf's slot 0x9c: an accomplishment's new points (FUN_0046e810)
  g_snap.n = 0;
  for (int i = 0; i < 10; i++) {
    RpItem *it = &g_snap.it[g_snap.n++];
    memset(it, 0, sizeof *it);
    it->type = RP_IT_TEXT;
    it->x = (int16_t)i, it->y = (int16_t)lv[i], it->w = (int16_t)g_sk.start[i];
    const char *nm = (const char *)(uintptr_t)(uint32_t)G32(0x4c2c3c + 4 * i);   // "Power Hitter", ...
    copy_str(it->s, sizeof it->s, nm, -1);
  }
}

// ------------------------------------------------------------------ task-105: the accomplishment board (FUN_0046e810)

// Golf draws its board (a bulletin board and a mantle, 800x600) once, then waits for a click. On a wide screen it filled
// only the left part, the course showed on the right. With the Frosted pages it is a native page: each photo golf pins
// up is reported here (rp_photo: the accomplishment, the photo's place), the newest one is kept as a picture, the
// captions (course and date) come from golf's text call. The page's Continue is a click, as golf waits for one.
static void accomp_name(int id, char *out, int cap) {
  // Golf bakes these labels into plaque sprites, then the new-game reset clears the 0x4c1578 table (0x420 bytes).
  // Keep the verified original labels as UI text instead of reading the cleared table. Index is golf's award id.
  static const char *const names[22] = {
    "1st Challenge Hole", "1st Heroic Hole", "1st Skill Upgrade", "1st Tournament", "1st Strategic Hole",
    "1st Match Victory", "1st 9+ Hole Course", "1st Top 100 Hole", "1st $500,000 Tournament", "1st Classic Hole",
    "1st Top 18 Hole", "1st Tournament Victory (9+ Hole)", "1st Grand Slam Course",
    "1st $1,000,000 Tournament", "1st 18 Hole Course", "1st Tournament Victory (18 Hole)",
    "1st Grand Slam Course (18 Hole)", "1st Grand Slam Victory (Parkland)", "1st Grand Slam Victory (Desert)",
    "1st Grand Slam Victory (Tropical)", "1st Grand Slam Victory (Links)", "1st 100 Star Rating"};
  const char *src = id >= 0 && id < 22 ? names[id] : "Accomplishment";
  if (G32(0x822c88) < 2) {
    if (id == 0) src = "1st Dogleg Right Hole";
    if (id == 1) src = "1st Dogleg Left Hole";
    if (id == 4) src = "1st Par 5 Hole";
  }
  int n = 0;
  for (; n < cap - 1 && n < 0x28 && src[n]; n++) out[n] = (unsigned char)src[n] >= 32 && (unsigned char)src[n] < 127 ? src[n] : ' ';
  out[n] = 0;
}

// The projection is in the expanded game canvas, not the old 800x600 board. Read the surface being photographed
// (graphsy wrapper at PAGE_WIDGET, dimensions at +0x38/+0x3c) before the board opens. Vanilla keeps its old clamp.
int rp_photo_limit(int vertical) {
  int original = vertical ? 440 : 600, size = vertical ? 160 : 200;
  if (!rpf_enabled_kind(RP_PHOTO)) return original;
  uint32_t surface = (uint32_t)G32(PAGE_WIDGET + 4);
  if (surface < 0x10000) return original;
  int extent = G32(surface + (vertical ? 0x3c : 0x38));
  if (extent < size || extent > 16384) return original;
  if (getenv("SIMGOLF_REPORTS_LOG")) fprintf(stderr, "reports: photo source %s %d crop limit %d\n", vertical ? "height" : "width", extent, extent - size);
  return extent - size;
}

static void photo_capture(int id, int x, int y, int capture_x, int capture_y) {
  if (!g_on || g_kind != RP_PHOTO) return;
  RpItem *it = slot(RP_IT_RECT, x, y);   // one per photo: its place, the accomplishment, its name
  if (it) {
    it->w = 200, it->h = 0xa0, it->art = (uint8_t)id;
    accomp_name(id, it->s, sizeof it->s);
  }
  g_ph_n++;
  // Capture each photo immediately after its blit, before later photos or the thumbtack overlap it. The board has
  // 22 awards and no recorded report sprites, so its photo textures use their award ids (0..21) in the art array.
  g_npic = 1;
  g_pic[0].on = 1, g_pic[0].x = capture_x, g_pic[0].y = capture_y, g_pic[0].w = 200, g_pic[0].h = 0xa0;
  g_pic[0].art = id, g_pic[0].page = -1, g_pic[0].hash = 0;
  if (id >= 0 && id < 22) capture_pics();
  // Capture uses a safe scratch rectangle, while the snapshot retains golf's logical placement for caption matching.
  for (int i = 0; i < g_snap.n; i++)
    if (g_snap.it[i].type == RP_IT_PIC && g_snap.it[i].art == id + 1) {
      g_snap.it[i].x = (int16_t)x, g_snap.it[i].y = (int16_t)y;
    }
  g_pic[0].on = 0;   // never sample this board position again after keep_restore
  if (g_log > 0) fprintf(stderr, "reports: board photo %d '%s' at %d,%d\n", id, it ? it->s : "", x, y);
}

void rp_photo(int id, int x, int y) { photo_capture(id, x, y, x, y); }

int FUN_00475c60(void *source, int target, int sx, int sy, int x, int y, int w, int h);
void rp_photo_blit(void *source, int target, int x, int y, int id) {
  int safe_x = x, safe_y = y;
  if (g_on && g_kind == RP_PHOTO) {
    // Golf's 22 stacked photos eventually have negative y. Its original blitter does not safely clip that case.
    // Native pages only need the source photo, so draw into a valid scratch rectangle before capturing it.
    uint32_t surface = (uint32_t)G32((uintptr_t)(uint32_t)target + 4);
    if (surface < 0x10000) return;
    int width = G32(surface + 0x38), height = G32(surface + 0x3c);
    if (width < 200 || height < 160 || width > 16384 || height > 16384) return;
    int max_x = width - 200, max_y = height - 160;
    safe_x = safe_x < 0 ? 0 : safe_x > max_x ? max_x : safe_x;
    safe_y = safe_y < 0 ? 0 : safe_y > max_y ? max_y : safe_y;
  }
  FUN_00475c60(source, target, 0, 0, safe_x, safe_y, 200, 160);
  photo_capture(id, x, y, safe_x, safe_y);
}

// golf's waits on the board (600 ms before it takes a click, then 300 ms a turn): with the native page only the first
void rp_board_wait(int n) { FUN_0045bf80(g_on && g_kind == RP_PHOTO && n < 0x3c ? 0 : n, 0); }

static void rp_frame_body(int present);
static void rp_frame_run(int present) {
  double t0 = SDL_GetPerformanceCounter() * 1000.0 / SDL_GetPerformanceFrequency();
  rp_frame_body(present);
  if (g_on && g_kind >= RP_PROFILE) frame_stats(t0, SDL_GetPerformanceCounter() * 1000.0 / SDL_GetPerformanceFrequency());
}

void rp_frame(void) { rp_frame_run(1); }
void rp_frame_preflush(void) { rp_frame_run(0); }  // the game's original flush presents this frame

static void rp_frame_body(int present) {
  if (!g_on || g_sub) return;
  if (g_kind == RP_PROFILE) {
    g_snap.ext[0] = g_in_edit;   // the bio editor is open
    int gi = (int16_t)G32(0x582e6e) & 0xffff;
    const char *bio = (const char *)(uintptr_t)(0x4d60b8 + (uint32_t)gi * 0x230);
    if (g_in_edit) {   // golf's text widget (0x5a9f78) edits its own copy of the bio: show that, live
      uint32_t wb = G32(0x5a9f78 + 0x574);
      if (wb > 0x10000) bio = (const char *)(uintptr_t)wb;
    }
    if (gi < 0x200) copy_str(g_snap.bio, sizeof g_snap.bio, bio, -1);
    // task-102: whose page it is and, for the player's pro before the first skill setup, the points golf will give
    // (FUN_004065c0: 10, plus 3 for each accomplishment already won in the table at 0x4c15a0)
    g_snap.ext[1] = gi;
    int set_up = (*(volatile uint16_t *)(uintptr_t)0x4c2c9c) != 0;
    for (int i = 0; i < 10; i++) set_up |= *(volatile uint8_t *)(uintptr_t)(0x5a5a04 + i) != 0;
    int pts = 10;
    for (int i = 0; i < 22; i++) pts += G32(0x4c15a0 + 0x30 * i) != 0 ? 3 : 0;
    g_snap.ext[2] = gi == 0 && !set_up && g_pro_next;
    g_snap.ext[3] = pts;
    if (gi < 0x200) {   // task-102c: the colours golf has now (shirt, pants, skin, hair), for the page's chips
      const volatile uint8_t *r = (const volatile uint8_t *)(uintptr_t)(0x4d6088 + (uint32_t)gi * 0x230);
      g_snap.ext[5] = r[0x24], g_snap.ext[6] = r[0x25], g_snap.ext[7] = r[0x27] | r[0x28] << 8;
      // task-119 (tester F27, the pro drawn as a child): body type (0x23 bits 4..5), the child bit (bit 3 of the
      // flag dword at 0x2c) and whether golf uses the profile's own look (the dword non-zero), for the page's chips
      uint32_t look = (uint32_t)r[0x2c] | (uint32_t)r[0x2d] << 8 | (uint32_t)r[0x2e] << 16 | (uint32_t)r[0x2f] << 24;
      g_snap.ext[7] |= (int32_t)(((r[0x23] >> 4) & 3u) << 16 | ((look & 8u) ? 1u : 0u) << 18 | (look ? 1u : 0u) << 19);
    }
    g_snap.ext[4] = gi < 0x200 ? *(volatile uint8_t *)(uintptr_t)(0x4d60a9 + (uint32_t)gi * 0x230) : 0x80;   // 0x80: a woman (task-102c)
    if (!g_in_edit) profile_page_keep();
  }
  if (g_kind == RP_SKILLS) skills_snap();
  if (g_kind == RP_PHOTO) {   // the accomplishment just won (99: the board opened to look at), and whether skill points follow
    int id = G32(0x4e3db8);
    g_snap.ext[1] = id;
    g_snap.ext[2] = id != 99 && *(volatile int16_t *)(uintptr_t)0x4c2c9c != 0;
    g_snap.ext[3] = g_ph_n;
  }
  if (g_log > 0 && g_kind >= RP_PROFILE) {   // task-102: when golf took a click (the tests time a click from the native action)
    static uint32_t last;
    uint32_t hsh = 2166136261u;
    hsh = (hsh ^ *(volatile uint8_t *)(uintptr_t)0x4d60a8) * 16777619u;   // the pro's personality
    hsh = (hsh ^ *(volatile uint8_t *)(uintptr_t)0x582dd1) * 16777619u;   // the page's skill wants
    for (int i = 0; i < 10; i++) hsh = (hsh ^ *(volatile uint8_t *)(uintptr_t)(0x5a5a04 + i)) * 16777619u;
    if (hsh != last) fprintf(stderr, "reports: golf state changed at %u ms\n", (unsigned)SDL_GetTicks());
    last = hsh;
  }
  g_snap.serial = ++g_serial;
  rpf_publish(&g_snap);
  RpAction a;
  while (rpf_take_action(&a)) {
    if (g_kind == RP_TOP_DESIGNERS && a.kind == RP_A_CLOSE) {
      g_top_local_close = 1;
      continue;
    }
    if (g_pair_cancel_state) continue;
    if (a.kind == RP_A_CLOSE && g_kind == RP_PAIRING) {
      g_pair_cancel_state = 1;
      continue;
    }
    // the land map has no Esc of its own: its leave control is the check button (golf's right click also leaves)
    if (a.kind == RP_A_CLOSE && g_kind == RP_TRACTS) a = (RpAction){RP_A_CLICK, 688 | (560 << 16)};
    if (a.kind == RP_A_CLOSE && g_kind == RP_PHOTO) a = (RpAction){RP_A_CLICK, 400 | (300 << 16)};   // golf waits for a click
    if (a.kind == RP_A_CLOSE && g_kind == RP_SKILLS) a = (RpAction){RP_A_CLICK, (g_sk.x0 + 0x15e) | (0x14e << 16)};   // its OK
    if (a.kind == RP_A_CLOSE && g_kind == RP_END_YEAR) a = (RpAction){RP_A_CLICK, 0x23c | (g_snap.ext[0] << 16)};
    if (a.kind == RP_A_CLOSE || a.kind == RP_A_TAB) {
      if (a.kind == RP_A_TAB && a.arg != g_kind) g_pending = a.arg;
      if (!g_closing) {
        g_closing = 1;
        push_key(SDLK_ESCAPE, 1);
        push_key(SDLK_ESCAPE, 0);
      }
    } else if (a.kind == RP_A_KEY) {
      push_key((SDL_Keycode)a.arg, 1);
      push_key((SDL_Keycode)a.arg, 0);
    } else if (a.kind == RP_A_CLICK || a.kind == RP_A_RCLICK) {
      int btn = a.kind == RP_A_RCLICK ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
      if (!g_inj) inj_start(a.arg, btn);
      else if (g_kind >= RP_PROFILE && g_iqn < 8) {
        g_iq[g_iqn] = a.arg, g_iq_btn[g_iqn] = btn, g_iqn++;
        if (g_log_on()) fprintf(stderr, "reports: click at %d,%d queued (%d waiting)\n", a.arg & 0xffff, (a.arg >> 16) & 0xffff, g_iqn);
      } else if (g_log_on())
        fprintf(stderr, "reports: click at %d,%d DROPPED (the last one is still being sent, stage %d)\n", a.arg & 0xffff, (a.arg >> 16) & 0xffff, g_inj_stage);
    }
  }
  inj_step();
  pair_cancel_step();
  capture_art();
  // The land map and accomplishment board draw once, then keep_restore puts the course back. Capture their photo
  // only on that draw, otherwise later frames overwrite it with the restored course. The profile still updates live.
  if (!g_in_edit && ((g_kind != RP_TRACTS && g_kind != RP_PHOTO) || g_dirty)) capture_pics();
  keep_restore();
  // task-102c: golf drew its old screen this turn (the skill screen's arrows and ovals, the profile's buttons); the art
  // pack keeps a hi-res copy of every sprite golf draws and puts them over the picture, so they showed beside the panel
  // (SIMGOLF_REPORTS_ARTHIDE=0 shows them again, for the before and after pictures)
  static int art_hide = -1;
  if (art_hide < 0) art_hide = !getenv("SIMGOLF_REPORTS_ARTHIDE") || atoi(getenv("SIMGOLF_REPORTS_ARTHIDE")) != 0;
  if (art_hide) hd_art_hide_dialog();
  // only when golf drew: a waiting report presents no frames. Not on the profile and skill pages: their loops flush right
  // after rp_frame, and every present waits for the 30 a second cap (gdi.c win_present), so two a turn halved their pace
  // (task-102, measured: 15 turns a second with two, the click took three turns)
  if (present && g_dirty && g_kind != RP_PROFILE && g_kind != RP_SKILLS && g_kind != RP_PHOTO) FUN_00480c80((void *)0x519a60);
  g_dirty = 0;
}

// golf's drawString (the tournament results' right column)
int FUN_00478140(void *w, char *s, int x, int y, char *len);
int FUN_00477c30(void *w, char *s, int x, int y, int len);
int FUN_00478430(void *w, char *s, int x, int y, int width);
int FUN_00478080(void *w, char *s, int x, int y, int a, char *len);
void rp_done(void);
int rp_ds(void *w, char *s, int x, int y, char *len) {
  if ((uintptr_t)w == PAGE_WIDGET) record(s, (int)(intptr_t)len, x, y, RP_AL_RIGHT, 0);
  return FUN_00478140(w, s, x, y, len);
}

// A report golf draws every frame of its main loop instead of in a loop of its own (the end of month
// ranking, screen state 8): phase 0 before golf draws it, phase 1 after. Not wired yet: that box and the
// tournament leaderboard slide in beside the running game and keep the original look (task-28 notes). The panel stays up while the
// frames keep coming and goes when they stop (the main loop clears the state on any key).
void rp_ov(int kind, int phase) {
  if (phase == 0) {
    if (!(g_on && g_kind == kind)) {
      if (rpf_enabled()) rp_begin(kind);   // with ui.reports=original: nothing, every frame
    } else {
      keep_save();
      g_snap.n = 0;
      g_snap.title[0] = 0;
    }
    return;
  }
  if (!g_on || g_kind != kind) return;
  // the end of month box slides in from the side: its frame (the first box) is at 0x176 when it stands
  for (int i = 0; i < g_snap.n; i++)
    if (g_snap.it[i].type == RP_IT_RECT) {
      int dx = g_snap.it[i].x - 0x176;
      for (int j = 0; dx && j < g_snap.n; j++) {
        g_snap.it[j].x = (int16_t)(g_snap.it[j].x - dx);
        if (g_snap.it[j].type == RP_IT_LINE) g_snap.it[j].w = (int16_t)(g_snap.it[j].w - dx);
      }
      break;
    }
  g_snap.serial = ++g_serial;
  rpf_publish(&g_snap);
  RpAction a;
  while (rpf_take_action(&a))
    if (a.kind == RP_A_CLOSE || a.kind == RP_A_TAB) {
      if (a.kind == RP_A_TAB && a.arg != g_kind) g_pending = a.arg;
      push_key(SDLK_ESCAPE, 1);
      push_key(SDLK_ESCAPE, 0);
    }
  capture_art();
  keep_restore();
}

// The land purchase map and the snapshot popup draw text through golf's page string calls: recorded like the others, each
// at the place the original puts it (left; wrapped text from its corner; right aligned text ends at x).
int rp_t7c30(void *w, char *s, int x, int y, int len) {
  if ((uintptr_t)w == PAGE_WIDGET) record(s, len, x, y, RP_AL_LEFT, 0);
  return FUN_00477c30(w, s, x, y, len);
}

int rp_t8430(void *w, char *s, int x, int y, int width) {
  if ((uintptr_t)w == PAGE_WIDGET) {
    RpItem *it = record(s, -1, x, y, RP_AL_LEFT, 0);
    if (it) it->w = (int16_t)width;
  }
  return FUN_00478430(w, s, x, y, width);
}

int rp_t8080(void *w, char *s, int x, int y, int a, char *len) {
  if ((uintptr_t)w == PAGE_WIDGET) record(s, (int)(intptr_t)len, x, y, RP_AL_RIGHT, 0);
  return FUN_00478080(w, s, x, y, a, len);
}

// The decompile of the widget create (FUN_0047a4c0) calls FUN_0047abe0 with the flags where the parent widget belongs. Only the
// profile's bio text widget (FUN_00486110, flag 0x100000) needs the registration to work, and with the flags it reads
// memory at 0x1020bc and crashes; other widgets (the front end's list) keep what they had, so for them nothing changes.
static int g_wfix;
void rp_wfix(int on) { g_wfix = on; }
int rp_warg(int flags, int parent) { return g_wfix ? parent : flags; }

// The land map of the native look leaves on Esc, like its check button (the original screen ignores Esc).
int FUN_0045ae70(void);
int FUN_0045ae30(void);
int rp_esc(void) { return g_on && g_kind == RP_TRACTS && FUN_0045ae70() && FUN_0045ae30() == 0x1b; }

// A page that returns from several places: the native page closes and the value goes through.
int rp_end(int v) {
  rp_done();
  return v;
}

void rp_done(void) {
  if (g_kind == RP_PAIRING && g_pair_cancel_state == 3 && g_log_on())
    fprintf(stderr, "reports: pairing cancel returned with no golfers selected\n");
  if (g_on) {
    if (g_log) fprintf(stderr, "reports: closed %d (%d texts)\n", g_kind, g_snap.n);
    if (g_log > 0 && getenv("SIMGOLF_REPORTS_DUMP"))
      for (int i = 0; i < g_snap.n; i++)
        fprintf(stderr, "reports: text %d,%d a%d c%04x '%s'\n", g_snap.it[i].x, g_snap.it[i].y, g_snap.it[i].align,
                g_snap.it[i].col, g_snap.it[i].s);
    if (g_log) fflush(stderr);
    rpf_end();
    keep_restore();
    hd_ui_show_golf_ui(1);
    keep_free();
  }
  g_on = 0;
  g_kind = 0;
  g_top_local_close = g_top_close_key = 0;
  g_inj = 0;
  g_iqn = 0;
  g_pair_row = -1;
  g_pair_cancel_state = 0;
}

// The old board reads these fixed-size save fields. Keep the two 64-byte names bounded even if a damaged save
// has no terminator, and only format values the legacy board itself displays.
static void td_copy_field(char *dst, int cap, const volatile uint8_t *src, int width) {
  int n = 0;
  while (n < cap - 1 && n < width) {
    unsigned char c = src[n];
    if (!c) break;
    dst[n++] = c >= 32 && c != 127 ? (char)c : ' ';
  }
  while (n > 0 && dst[n - 1] == ' ') n--;
  dst[n] = 0;
}

static void td_text(int rank, int field, const char *s) {
  RpItem *it = slot(RP_IT_TEXT, field, rank);
  if (!it) return;
  copy_str(it->s, sizeof it->s, s, -1);
}

static void td_snapshot(void) {
  const uintptr_t records = 0x541ce0;
  int rows = 0;
  g_snap.ext[0] = G32(0x8392a8);   // the row addScore just inserted or found
  g_snap.ext[1] = 0;
  copy_str(g_snap.title, sizeof g_snap.title, "Top Ten Designers", -1);
  for (int rank = 0; rank < 10; rank++) {
    uintptr_t address = records + (uintptr_t)rank * 0x9c;
    volatile const uint8_t *r = (volatile const uint8_t *)address;
    if (!r[0]) continue;
    char designer[RP_TEXT], course[RP_TEXT], text[RP_TEXT];
    td_copy_field(designer, sizeof designer, r, 64);
    td_copy_field(course, sizeof course, r + 0x40, 64);
    int32_t fun = *(volatile int32_t *)(address + 0x80);
    int32_t skill_x100 = *(volatile int32_t *)(address + 0x84);
    int32_t cash = *(volatile int32_t *)(address + 0x88);
    int multiplier = *(volatile int16_t *)(address + 0x96) + 1;
    int64_t score = ((int64_t)(cash / 10) + skill_x100 + fun) * multiplier;
    int64_t skill_abs = skill_x100 < 0 ? -(int64_t)skill_x100 : (int64_t)skill_x100;
    td_text(rank, 0, designer);
    td_text(rank, 1, course);
    snprintf(text, sizeof text, "%d", fun);
    td_text(rank, 2, text);
    snprintf(text, sizeof text, "%s%lld.%02lld", skill_x100 < 0 ? "-" : "", (long long)(skill_abs / 100), (long long)(skill_abs % 100));
    td_text(rank, 3, text);
    snprintf(text, sizeof text, "%d", cash / 10);
    td_text(rank, 4, text);
    snprintf(text, sizeof text, "%lld", (long long)score);
    td_text(rank, 5, text);
    snprintf(text, sizeof text, "x%d", multiplier);
    td_text(rank, 6, text);
    rows++;
    if (g_log > 0)
      fprintf(stderr, "reports: designer row %d name=\"%s\" course=\"%s\" fun=%d skill=%d cash=%d multiplier=%d score=%lld\n",
              rank + 1, designer, course, fun, skill_x100, cash, multiplier, (long long)score);
  }
  g_snap.ext[1] = rows;
  if (g_log > 0) fprintf(stderr, "reports: designer board has %d rows, current row %d\n", rows, g_snap.ext[0]);
}

int FUN_0045c030(void);
int FUN_0045ae70(void);
int FUN_0045ae30(void);
void FUN_00483d30(void);

// FUN_004732d0 runs in the original Top Ten Designers routine before this hook. When the page is enabled, keep a
// native modal loop here so its Close and Escape actions end this page locally instead of sending Escape into golf.
int rp_top_designers(void) {
  if (g_log < 0) g_log = getenv("SIMGOLF_REPORTS_LOG") != NULL;
  if (!rpf_enabled_kind(RP_TOP_DESIGNERS)) {
    if (g_log > 0) fprintf(stderr, "reports: Top Ten Designers uses the original board\n");
    return 0;
  }
  rp_begin(RP_TOP_DESIGNERS);
  td_snapshot();
  while (!g_top_local_close) {
    g_dirty = 1;
    rp_frame();
    if (g_top_local_close) break;
    FUN_0045bf80(1, 0);
    FUN_0045c030();
    if (FUN_0045ae70() && FUN_0045ae30() == 0x1b) {
      FUN_00483d30();
      g_top_close_key = 1;
      g_top_local_close = 1;
    }
  }
  if (g_log > 0) fprintf(stderr, "reports: Top Ten Designers closed locally by %s\n", g_top_close_key ? "Escape" : "Close");
  rp_done();
  return 1;
}
