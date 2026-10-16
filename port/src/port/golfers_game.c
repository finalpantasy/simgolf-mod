// Frosted Mint golfers in the world, game-thread side, native build (simgolf
// mod, task-31). The overview is in port/golfers_frosted.h and the top of
// golf/golfers_hooks.c (which holds the browser build's no-op versions of these
// hooks). This file does not include golf_protos.h (its types clash with the
// C library headers); the few game functions it calls are declared below with
// the same ABI.
//
// What each hook does, with ui.golfers=frosted_mint and the presenter up:
//   gw_frame_begin / gw_course_done   open and close the recording of a course
//       frame (called from golf/hd_ui.c); at the end the note is complete and
//       goes to the presenter, which pairs it with the course layer it was
//       drawn in.
//   gw_seen / gw_name                 a golfer sprite was drawn; the name text
//       it was about to draw becomes a tag in the note.
//   gw_thought_begin / gw_face / gw_thought_end   the thought loop in the main
//       loop: the strip, sprites and text of the original bubble are skipped
//       (gw_hide) and the line becomes a bubble in the note.
//   gc_begin / gc_text / gc_rect / gc_mouse / gc_end   the golfer card: the
//       original draws as always into the frame (its logic, hover tests and
//       tooltips run unchanged), every text and bar it draws is copied into the
//       note, and its pixels are put back afterwards. Its pointer is parked
//       off the card so it only reacts to the clicks the native card injects.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "port/golfers_frosted.h"
#include "port/ui_font.h"
#include "port/hd_native.h"
#include "port/hd_art.h"

#define G32(a) (*(volatile int32_t *)(uintptr_t)(a))
#define G16(a) (*(volatile int16_t *)(uintptr_t)(a))
#define G8(a) (*(volatile int8_t *)(uintptr_t)(a))

_Static_assert(sizeof(GwNote) <= HD_NOTE_MAX, "Course UI note must fit the frame snapshot");

#define DOCK_MODE 0x567afc
#define FLAGS 0x59e7b8
#define SELECTION 0x53df54
#define TEXT_BUF 0x51a068
#define MOUSE_X 0x822d60
#define MOUSE_Y 0x822d64
#define SLOTS 152

// translated game functions (build/gen/golf/golf_protos.h)
uint32_t FUN_004657e0(int x, int y);   // left click entry
int FUN_00473cb0(void *sprite, int target, int x, int y, int sx, int sy, int div, int clip);
int FUN_00404b70(char *s, int x, int y, uint32_t col);
int FUN_00404bc0(char *s, int x, int y, uint32_t col);
int FUN_004049d0(char *s, int x, int y, uint32_t col);
int FUN_00404ad0(char *s, int x, int y, uint32_t col);
void FUN_00480b00(void *screen, int x, int y, int w, int h, uint32_t col);
int FUN_0045c200(int slot, int x, int y, int mode);   // the golfer's head
uint32_t FUN_004676e0(uint32_t slot, int what);       // the golfer's name -> text buffer

// gdi.c: the screen-sized DIB whose pixels contain address p
uint16_t *gdi_dib_at(uintptr_t p, int *w, int *h, int *pitch, int *is565);
extern int win_screen_w, win_screen_h;

extern int gw_hide;
extern int hd_cam_ox, hd_cam_oy;   // golf/hd_ui.c: the sub-tile camera's offset of this frame

// ------------------------------------------------------------------ state

_Static_assert(sizeof(GwNote) <= HD_NOTE_MAX, "a GwNote must fit the presenter's note");
static GwNote g_note;
static int g_on;       // this frame: the native golfers replace the original ones
static int g_rec;      // the recording window of a course frame is open
static uint32_t g_serial;
static struct { int x, y, seen; } g_pos[SLOTS];
static int g_type;     // thought type of the bubble being drawn
static int g_face = -1;
static unsigned char g_art_done[GW_ART_MAX];
static int g_want_art[GW_ART_MAX];   // faces and moods seen this frame, not read yet
static int g_n_want;

// card
static int g_card_cap;
static int g_card_art_mark = -1;
static uint16_t *g_save;
static size_t g_save_cap;
static int g_save_w, g_save_h;
enum { REC_MAX = 160 };
typedef struct { int kind; int fn; int x, y, w, h; uint32_t col; char s[200]; } Rec;   // kind 0 text, 1 bar
static Rec g_rec_list[REC_MAX];
static int g_n_rec;
static int g_port_slot = -1, g_port_frames;
static int g_inj, g_inj_x, g_inj_y, g_inj_frames;

static void copy_str(char *dst, int cap, const char *src) {
  int n = 0;
  while (n < cap - 1 && src[n]) {
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
        uint16_t b = shot[0][(y0 + y) * BW + x0 + x], wv = shot[1][(y0 + y) * BW + x0 + x];
        int br, bg, bl, wr, wg, wb;
        if (is565) {
          br = (b >> 11) & 31, bg = (b >> 6) & 31, bl = b & 31;
          wr = (wv >> 11) & 31, wg = (wv >> 6) & 31, wb = wv & 31;
        } else {
          br = (b >> 10) & 31, bg = (b >> 5) & 31, bl = b & 31;
          wr = (wv >> 10) & 31, wg = (wv >> 5) & 31, wb = wv & 31;
        }
        int d = ((wr - br) + (wg - bg) + (wb - bl)) * 255 / (3 * 31);
        int a = 255 - d;
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        uint32_t px = 0;
        if (a > 0) {
          int r = br * 255 / 31 * 255 / a, g = bg * 255 / 31 * 255 / a, bb2 = bl * 255 / 31 * 255 / a;
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

static void draw_sprite(int bx, int by, void *user) {
  void *sprite = user;
  int ox = G32((uintptr_t)sprite + 0x20), oy = G32((uintptr_t)sprite + 0x24);
  FUN_00473cb0(sprite, G32(0x4c1570), bx + 8 - (ox > 0 ? 0 : ox), by + 8 - (oy > 0 ? 0 : oy), 1, 1, 1, 0);
}

static void publish_sprite(int key, uintptr_t sprite) {
  int w = 0, h = 0;
  uint32_t *px = capture(120, 120, draw_sprite, (void *)sprite, &w, &h);
  gwf_publish_art(key, px, w, h);
  free(px);
  g_art_done[key] = 1;
}

static void draw_head(int bx, int by, void *user) {
  FUN_0045c200((int)(intptr_t)user, bx + 24, by + 8, -1);
}

static void publish_head(int slot) {
  int w = 0, h = 0;
  uint32_t *px = capture(140, 140, draw_head, (void *)(intptr_t)slot, &w, &h);
  gwf_publish_art(GW_ART_PORTRAIT, px, w, h);
  free(px);
}

// golf's sprite for an art key: faces 0x5628e8 + i * 0x2c, moods 0x59b778 + mood * 0x2c
static uintptr_t sprite_of(int key) {
  if (key >= GW_ART_MOOD && key < GW_ART_MOOD + 11) return 0x59b778 + (uintptr_t)(key - GW_ART_MOOD) * 0x2c;
  if (key >= GW_ART_FACE && key < GW_ART_MOOD) return 0x5628e8 + (uintptr_t)key * 0x2c;
  return 0;
}

static void want_art(int key) {
  if (key < 0 || key >= GW_ART_PORTRAIT || g_art_done[key]) return;
  for (int i = 0; i < g_n_want; i++)
    if (g_want_art[i] == key) return;
  if (g_n_want < GW_ART_MAX) g_want_art[g_n_want++] = key;
}

// ------------------------------------------------------------------ the frame

static int mood_key(int mood) {
  if (mood < 1) mood = 1;
  if (mood > 10) mood = 10;
  return GW_ART_MOOD + mood;
}

// SIMGOLF_GOLFERS_LOG=1 (tests, tuning): the frame's tags and bubbles are
// recorded and logged even when the original ones stay (ui.golfers=original).
static int g_log = -1;

int gw_popover_active(void) { return g_on && uif_enabled(); }

int gw_popover_text(int fn, const char *text, int x, int y, unsigned colour) {
  if (g_on && uif_enabled() && (g_rec || (fn & 8)) && text && *text && strlen(text) < sizeof g_note.popovers[0].text &&
      g_note.n_popovers < GW_POPOVERS) {
    GwPopover *p = &g_note.popovers[g_note.n_popovers++];
    p->x = x; p->y = y; p->fn = fn; p->colour = colour;
    copy_str(p->text, sizeof p->text, text);
    if (g_log) fprintf(stderr, "popovers: fn %d at %d,%d colour %08x \"%s\"\n", fn, x, y, colour, text);
    if (!g_rec) hd_ui_note_set(&g_note, (int)sizeof g_note);
    return 0;
  }
  switch (fn & 7) {
    case 4:
    case 0: return FUN_00404b70((char *)text, x, y, colour);
    case 1: return FUN_00404bc0((char *)text, x, y, colour);
    case 2: return FUN_004049d0((char *)text, x, y, colour);
    default: return FUN_00404ad0((char *)text, x, y, colour);
  }
}

void gw_frame_begin(void) {
  if (g_log < 0) g_log = getenv("SIMGOLF_GOLFERS_LOG") != NULL;
  g_on = gwf_enabled();
  g_rec = g_on || g_log;
  gw_hide = 0;
  if (!g_rec) return;
  g_note.n_tags = g_note.n_bubbles = 0;
  g_note.n_popovers = 0;
  g_note.card.on = 0;
  memset(g_pos, 0, sizeof g_pos);
}

void gw_seen(int slot, int x, int y) {
  if (!g_rec || slot < 0 || slot >= SLOTS) return;
  g_pos[slot].x = x;
  g_pos[slot].y = y;
  g_pos[slot].seen = 1;
}

static int selected_slot(void) {
  int sel = G32(SELECTION);
  return sel > 0 ? sel - 1 : -1;
}

int gw_name(int slot, const char *text, int x, int y, int mood) {
  if (!g_rec) return 0;
  if (g_note.n_tags >= GW_TAGS) return g_on;
  GwTag *t = &g_note.tags[g_note.n_tags++];
  t->slot = (int16_t)slot;
  t->selected = slot == selected_slot();
  t->x = x;
  t->y = y;
  t->mood = (int16_t)mood;
  copy_str(t->name, sizeof t->name, text);
  return g_on;
}

int gw_active(void) { return g_on && g_rec; }

void gw_thought_begin(int type, int state) {
  (void)state;
  g_type = type;
  g_face = -1;
  gw_hide = g_on && g_rec;
}

void gw_face(int idx) { g_face = idx; }

void gw_thought_end(int slot, int x, int y, const char *text, unsigned colour) {
  if (!g_rec) return;
  if (!text || !text[0] || g_note.n_bubbles >= GW_BUBBLES) return;
  GwBubble *b = &g_note.bubbles[g_note.n_bubbles++];
  b->slot = (int16_t)slot;
  unsigned c = colour & 0x7fff;
  b->tone = (c == 0x43f0 || c == 0x2308) ? 1 : (c == 0x7d08 || c == 0x6000) ? -1 : 0;
  b->x = x;
  b->y = y;
  b->kind = g_type == 0x32;
  int mood = slot >= 0 && slot < SLOTS ? G16(0x57955c + slot * 0x100) + 2 : 5;
  int key = mood_key(mood);
  if (b->kind && g_face >= 0 && g_face < GW_ART_MOOD) key = g_face;
  b->art = (int16_t)key;
  if (g_on) want_art(key);
  copy_str(b->text, sizeof b->text, text);
}

// Staff (FUN_004038f0 draws their name, the speech loop in the main loop their lines): the same tags and bubbles.
int gw_staff_name(int idx, const char *text, int x, int y) {
  if (!g_rec) return 0;
  if (g_note.n_tags >= GW_TAGS) return g_on;
  GwTag *t = &g_note.tags[g_note.n_tags++];
  t->slot = (int16_t)(1000 + idx);
  t->selected = 0;
  t->x = x;
  t->y = y;
  t->mood = 0;
  copy_str(t->name, sizeof t->name, text);
  return g_on;
}

void gw_staff_begin(void) { gw_hide = g_on && g_rec; }

void gw_staff_end(int idx, int x, int y, const char *text) {
  if (!g_rec || !text || !text[0] || g_note.n_bubbles >= GW_BUBBLES) return;
  GwBubble *b = &g_note.bubbles[g_note.n_bubbles++];
  b->slot = (int16_t)(1000 + idx);
  b->tone = 0;
  b->x = x;
  b->y = y;
  b->kind = 0;
  b->art = (int16_t)mood_key(6);
  if (g_on) want_art(b->art);
  copy_str(b->text, sizeof b->text, text);
}

static void log_state(void);

// SIMGOLF_FACES_DUMP=dir (task-60): once, write golf's own mood sprites (0x59b778 + i * 0x2c, i = 0..16) as
// dir/mood_<i>.bmp (32 bit with alpha), for the old-versus-new contact sheet. Nothing else uses it.
static void dump_mood_sprites(void) {
  static int once;
  const char *dir = getenv("SIMGOLF_FACES_DUMP");
  if (once || !dir || !*dir) return;
  once = 1;
  for (int i = 0; i < 17; i++) {
    int w = 0, h = 0;
    uint32_t *px = capture(120, 120, draw_sprite, (void *)(uintptr_t)(0x59b778 + (uintptr_t)i * 0x2c), &w, &h);
    if (!px) {
      fprintf(stderr, "faces: mood sprite %d is empty\n", i);
      continue;
    }
    SDL_Surface *sf = SDL_CreateRGBSurfaceFrom(px, w, h, 32, w * 4, 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
    char path[1024];
    snprintf(path, sizeof path, "%s/mood_%d.bmp", dir, i);
    if (sf) SDL_SaveBMP(sf, path);
    fprintf(stderr, "faces: mood sprite %d is %dx%d -> %s\n", i, w, h, path);
    if (sf) SDL_FreeSurface(sf);
    free(px);
  }
}

void gw_course_done(void) {
  if (!g_rec) return;
  log_state();
  dump_mood_sprites();
  g_rec = 0;
  gw_hide = 0;
  // the selected golfer always has a tag (the card hangs off it)
  int sel = selected_slot();
  if (sel >= 0 && sel < SLOTS && g_pos[sel].seen) {
    int has = 0;
    for (int i = 0; i < g_note.n_tags; i++)
      if (g_note.tags[i].slot == sel) has = 1;
    if (!has && g_note.n_tags < GW_TAGS) {
      char keep[512];
      memcpy(keep, (void *)(uintptr_t)TEXT_BUF, sizeof keep);
      *(char *)(uintptr_t)TEXT_BUF = 0;
      FUN_004676e0((uint32_t)sel, 0);
      GwTag *t = &g_note.tags[g_note.n_tags++];
      t->slot = (int16_t)sel;
      t->selected = 1;
      t->x = g_pos[sel].x;
      t->y = g_pos[sel].y;
      t->mood = G16(0x57955c + sel * 0x100);
      copy_str(t->name, sizeof t->name, (const char *)(uintptr_t)TEXT_BUF);
      memcpy((void *)(uintptr_t)TEXT_BUF, keep, sizeof keep);
    }
  }
  // art the bubbles will want: the 10 mood faces once, then new faces as they appear
  if (g_on && !g_art_done[GW_ART_MOOD + 1])
    for (int m = 1; m <= 10; m++) want_art(GW_ART_MOOD + m);
  for (int i = 0; i < g_n_want; i++) {
    uintptr_t sp = sprite_of(g_want_art[i]);
    if (sp) publish_sprite(g_want_art[i], sp);
  }
  g_n_want = 0;
  g_note.magic = GW_NOTE_MAGIC;
  g_note.serial = ++g_serial;
  g_note.dock_mode = G32(DOCK_MODE);
  g_note.paused = (G32(FLAGS) & 4) != 0;
  g_note.zoom = G32(0x5a34e8);   // the zoom the HUD draws save (the live one is forced to 4 around them)
  // the card, when one is open, adds itself in gc_end
  g_note.card.on = 0;
  if (g_on) hd_ui_note_set(&g_note, (int)sizeof g_note);
  // SIMGOLF_GOLFERS_LOG=1: what the frame holds, twice a second (tests and tuning read these)
  static Uint32 last_log;
  if (g_log && SDL_GetTicks() - last_log >= 500) {
    last_log = SDL_GetTicks();
    fprintf(stderr, "golfers: frame %u: %d tags, %d bubbles, zoom %d, selected %d, cam offset %d,%d\n", g_note.serial, g_note.n_tags,
            g_note.n_bubbles, g_note.zoom, selected_slot(), hd_cam_ox, hd_cam_oy);
    for (int i = 0; i < g_note.n_tags && i < 40; i++)
      fprintf(stderr, "golfers:   tag slot %d \"%s\" at %d,%d\n", g_note.tags[i].slot, g_note.tags[i].name, g_note.tags[i].x,
              g_note.tags[i].y);
    for (int i = 0; i < g_note.n_bubbles && i < 20; i++)
      fprintf(stderr, "golfers:   bubble slot %d tone %d kind %d at %d,%d \"%s\"\n", g_note.bubbles[i].slot, g_note.bubbles[i].tone,
              g_note.bubbles[i].kind, g_note.bubbles[i].x, g_note.bubbles[i].y, g_note.bubbles[i].text);
  }
}

// ------------------------------------------------------------------ the card

// where the original card's buttons are (hit centres), by layout and hit id
static int card_button_point(int layout, int id, int *x, int *y) {
  if (layout == 0) {
    static const int cx[5] = {0x13e, 0x17f, 0x1c0, 0x201, 0x253};
    if (id < 0 || id > 4) return 0;
    *x = cx[id];
    *y = 0x106;
    return 1;
  }
  switch (id) {   // layout 1: ids 0, 5, 2, 3, 4
    case 0: *x = 0x126; break;
    case 5: *x = 0x167; break;
    case 2: *x = 0x1a8; break;
    case 3: *x = 0x1e9; break;
    case 4: *x = 0x221; break;
    default: return 0;
  }
  *y = 0x11f;
  return 1;
}

static int card_layout(int slot) {
  if (slot < 0 || slot >= SLOTS) return 0;
  return G16(0x579568 + slot * 0x100) != -1 && G32(0x824144) != 0;
}

static void run_actions(int slot) {
  if (g_inj) {
    if (++g_inj_frames < 4) return;
    g_inj = 0;
    gwf_set_injecting(0);
  }
  GwAction a;
  if (!gwf_take_action(&a)) return;
  int x, y;
  int id = a.kind == GW_ACT_CLOSE ? 4 : a.id;
  if (!card_button_point(card_layout(slot), id, &x, &y)) return;
  fprintf(stderr, "golfers: card click on button %d (%d,%d) of the original card\n", id, x, y);
  g_inj = 1;
  g_inj_x = x;
  g_inj_y = y;
  g_inj_frames = 0;
  gwf_set_injecting(1);
  FUN_004657e0(x, y);
}

enum { CARD_W = 800, CARD_H = 400 };

// One line whenever the selection, the followed golfer or the card's page changes (tests read these).
static void log_state(void) {
  static int last_sel = -2, last_follow = -2, last_story = -2;
  int sel = G32(SELECTION), follow = G32(0x4c2e0c), story = G32(0x824144) != 0;
  if (sel != last_sel || follow != last_follow || story != last_story) {
    last_sel = sel, last_follow = follow, last_story = story;
    fprintf(stderr, "golfers: state selection %d follow %d story %d\n", sel, follow, story);
  }
}

int gw_native_card(void) { return g_on && gwf_card_enabled(); }   // for hud_game.c

void gc_begin(int slot) {
  g_card_cap = 0;
  g_card_art_mark = -1;
  if (!g_on || !gwf_card_enabled()) return;
  int pitch, is565, w, h;
  uint16_t *bb = back_pixels(&pitch, &is565, &w, &h);
  if (!bb || w < CARD_W || h < CARD_H) return;
  size_t need = (size_t)CARD_W * CARD_H;
  if (need > g_save_cap) {
    g_save = realloc(g_save, need * 2);
    g_save_cap = need;
  }
  if (!g_save) return;
  for (int y = 0; y < CARD_H; y++) memcpy(g_save + (size_t)y * CARD_W, bb + (ptrdiff_t)y * pitch, CARD_W * 2);
  g_save_w = CARD_W;
  g_save_h = CARD_H;
  g_card_cap = 1;
  g_n_rec = 0;
  run_actions(slot);
  g_card_art_mark = hd_art_mark();
}

int gc_text(int fn, const char *text, int x, int y, unsigned colour) {
  if (g_card_cap && g_n_rec < REC_MAX) {
    Rec *r = &g_rec_list[g_n_rec++];
    r->kind = 0;
    r->fn = fn;
    r->x = x;
    r->y = y;
    r->col = colour;
    copy_str(r->s, sizeof r->s, text ? text : "");
  }
  switch (fn) {
    case 0: return FUN_00404b70((char *)text, x, y, colour);
    case 1: return FUN_00404bc0((char *)text, x, y, colour);
    case 2: return FUN_004049d0((char *)text, x, y, colour);
    default: return FUN_00404ad0((char *)text, x, y, colour);
  }
}

void gc_rect(int x, int y, int w, int h, unsigned colour) {
  if (g_card_cap && g_n_rec < REC_MAX) {
    Rec *r = &g_rec_list[g_n_rec++];
    r->kind = 1;
    r->x = x;
    r->y = y;
    r->w = w;
    r->h = h;
    r->col = colour;
    r->s[0] = 0;
  }
  FUN_00480b00((void *)0x519a60, x, y, w, h, colour);
}

void gc_mouse(int *x, int *y) {
  if (!g_card_cap) return;
  if (g_inj) {
    *x = g_inj_x;
    *y = g_inj_y;
  } else {
    *x = *y = -10000;   // nothing under the pointer: the native card gets the clicks
  }
}

static int tone_of(uint32_t col) {
  switch (col & 0x7fff) {
    case 0x7ff0: return -2;
    case 0x7d08: return -1;
    case 0x211f: return 1;
    case 0x4010: return 2;
    default: return 0;
  }
}

// golf colours its comment lines green (good), red (bad) or white
static int line_tone(uint32_t col) {
  int r = (col >> 10) & 31, gr = (col >> 5) & 31, b = col & 31;
  if (gr > r + 6 && gr > b + 6) return 1;
  if (r > gr + 6 && r > b + 6) return -1;
  return 0;
}

static void fill_card(int slot, GwCard *g) {
  memset(g, 0, sizeof *g);
  g->on = 1;
  g->slot = slot;
  g->layout = card_layout(slot);
  g->ax = g->ay = -1;
  if (slot >= 0 && slot < SLOTS && g_pos[slot].seen) {
    g->ax = g_pos[slot].x;
    g->ay = g_pos[slot].y;
  }
  g->has_story = slot >= 0 && slot < SLOTS && G16(0x579568 + slot * 0x100) != -1;
  copy_str(g->btn[0], 24, "Profile");
  copy_str(g->btn[1], 24, "Eject");
  copy_str(g->btn[2], 24, "Snapshot");
  if (g->has_story) copy_str(g->btn[3], 24, "Story");
  int bar_row[GW_METERS] = {0}, bar_seen[GW_METERS] = {0};
  for (int i = 0; i < g_n_rec; i++) {
    const Rec *r = &g_rec_list[i];
    if (r->kind == 0) {
      if (r->fn == 1 && r->y == 0x28) copy_str(g->name, sizeof g->name, r->s);
      else if (r->fn == 0 && r->y == 0x3e) copy_str(g->line1, sizeof g->line1, r->s);
      else if (r->fn == 0 && r->y == 0x4b) copy_str(g->line2, sizeof g->line2, r->s);
      else if (r->fn == 1 && r->y == 0x5b) copy_str(g->quote, sizeof g->quote, r->s);     // golf draws the golfer's line in red
      else if (r->fn == 0 && r->y == 0x69) copy_str(g->skills, sizeof g->skills, r->s);   // and what they want more of under it
      else if (r->x == 0x24a && r->y >= 0x2c && r->y <= 0x6c && ((r->y - 0x2c) & 15) == 0)
        copy_str(g->label[(r->y - 0x2c) >> 4], 20, r->s);
      else if (r->fn == 0 && r->y == 0x84) g->has_scores = 1;
      else if (r->fn == 0 && r->y == 0x94) {
        g->has_scores = 1;
        if (r->x == 0x256) g->total = (int16_t)atoi(r->s);
        else {
          int hole = (r->x - 0x10a) / 0x12;
          if (hole >= 0 && hole < GW_HOLES) {
            if ((r->col & 0x7fff) == 0x4210) {
              g->cur_hole = (int16_t)(hole + 1);
              g->score[hole] = (int16_t)atoi(r->s);   // the grey number is the live stroke count, not only a hole marker
            }
            else {
              g->score[hole] = (int16_t)atoi(r->s);
              g->tone[hole] = (int8_t)tone_of(r->col);
            }
          }
        }
      } else if ((r->fn == 2 || r->fn == 3) && g->n_story < 5) {
        g->story_tone[g->n_story] = (int8_t)line_tone(r->col);
        copy_str(g->story[g->n_story++], 100, r->s);
      }
    } else if (r->x >= 0x222 - 1 && r->y >= 0x36 && r->y <= 0x76 && ((r->y - 0x36) & 15) == 0 && r->h == 4) {
      int m = (r->y - 0x36) >> 4;
      if (!bar_seen[m]) {   // the bright bar, full width: its colour says low or fine
        bar_seen[m] = 1;
        g->warn[m] = (r->col & 0x7fff) == 0x7d08;
      } else if (!bar_row[m]) {   // the dark bar: how much is missing
        bar_row[m] = 1;
        int dark = r->w < 0 ? 0 : r->w > 80 ? 80 : r->w;
        g->meter[m] = (int16_t)((80 - dark) * 1000 / 80);
      }
    }
  }
  if (g->layout == 1 && !g->name[0]) copy_str(g->name, sizeof g->name, "Golfer");
}

void gc_end(int slot) {
  if (!g_card_cap) return;
  g_card_cap = 0;
  hd_art_hide_from(g_card_art_mark);
  // the head, read while the original card's pixels are still in the frame (capture() restores what it touches)
  if (slot != g_port_slot || ++g_port_frames > 120) {
    g_port_slot = slot;
    g_port_frames = 0;
    publish_head(slot);
  }
  // the original card's pixels out of the frame
  int pitch, is565, w, h;
  uint16_t *bb = back_pixels(&pitch, &is565, &w, &h);
  if (bb && w >= g_save_w && h >= g_save_h)
    for (int y = 0; y < g_save_h; y++) memcpy(bb + (ptrdiff_t)y * pitch, g_save + (size_t)y * g_save_w, (size_t)g_save_w * 2);
  fill_card(slot, &g_note.card);
  g_note.magic = GW_NOTE_MAGIC;
  hd_ui_note_set(&g_note, (int)sizeof g_note);
  static int last = -1;
  if (slot * 2 + g_note.card.layout != last) {
    last = slot * 2 + g_note.card.layout;
    fprintf(stderr, "golfers: card of golfer %d: \"%s\" | %s | %s | %s | \"%s\" layout %d scores %d\n", slot, g_note.card.name,
            g_note.card.line1, g_note.card.line2, g_note.card.skills, g_note.card.quote, g_note.card.layout,
            g_note.card.has_scores);
  }
}
