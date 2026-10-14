// Frosted Mint cart path help, game-thread side (simgolf mod, task-53). See paths_frosted.h.
//
// What golf knows (card task-53 notes; golf_02.c FUN_0042f340, golf_01.c FUN_0040f5c0):
//   * tile flags (GRID(9), 16 bit per tile): 0x20 a cart path, 0x400 a building tile, 0x40 "reached by the
//     flood fill from the clubhouse", set after every placement. The flood spreads over tiles with 0x20 or 0x400.
//   * building records (0x58bcb8 + 16 * i, 256 of them): +0 kind (6..16 need a path), +2 row, +4 col, +7 flags
//     (0x40: operational, i.e. joined, or forced on in Easy and Sandbox). +8 the level; the footprint is a
//     square of (kind size from the table at 0x4c26b0 + 0x10, plus level - 1 except for snack bars) tiles.
// This file only reads those, once per course frame, and never writes game state.
#pragma clang diagnostic error "-Wimplicit-function-declaration"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/grid.h"
#include "port/hd_native.h"
#include "port/hud_frosted.h"
#include "port/paths_frosted.h"
#include "port/ui/ui_paths.h"

#define G32(a) (*(volatile int32_t *)(uintptr_t)(a))
#define G16(a) (*(volatile int16_t *)(uintptr_t)(a))
#define G8(a) (*(volatile int8_t *)(uintptr_t)(a))

#define FLAGS 0x59e7b8
#define DIFFICULTY 0x822c88
#define BUILD_TOOL 0x4c2854
#define REC 0x58bcb8
#define NAMES 0x4c26b0
#define CAM_X 0x4c2ba0
#define CAM_Y 0x4c2ba4
#define ZOOM 0x4c2844
#define LEVELS 0x5a8c38   // per building kind: the highest level built (FUN_00467130 keeps the larger)

uint32_t FUN_0042fb90(int x, int y, int *sx, int *sy, int margin);   // worldToScreen, terrain height included

static PvNote g_note;
static uint32_t g_serial;

// the corner lattice of this frame: screen position of the tile corner (a, b), stamped with the frame
typedef struct { int16_t x, y; uint32_t stamp; } Corner;
static Corner *g_corner;
static int g_corner_w;

static int corner(int a, int b, int16_t *x, int16_t *y) {
  Corner *c = &g_corner[a * g_corner_w + b];
  if (c->stamp != g_serial) {
    int sx = 0, sy = 0;
    FUN_0042fb90(a * 1024, b * 1024, &sx, &sy, 4000);
    c->x = (int16_t)(sx < -32000 ? -32000 : sx > 32000 ? 32000 : sx);
    c->y = (int16_t)(sy < -32000 ? -32000 : sy > 32000 ? 32000 : sy);
    c->stamp = g_serial;
  }
  *x = c->x;
  *y = c->y;
  return 1;
}

static int add_quad(PvQuad *q, int a, int b) {
  int16_t x, y;
  corner(a, b, &x, &y), q->p[0] = x, q->p[1] = y;
  corner(a + 1, b, &x, &y), q->p[2] = x, q->p[3] = y;
  corner(a + 1, b + 1, &x, &y), q->p[4] = x, q->p[5] = y;
  corner(a, b + 1, &x, &y), q->p[6] = x, q->p[7] = y;
  const uint16_t *flags = (const uint16_t *)g_grid[9];
  q->path = (flags[a * g_map_w + b] & 0x20) != 0;
  return 1;
}

static void copy_str(char *dst, int cap, const char *src) {
  int n = 0;
  while (n < cap - 1 && src[n]) {
    dst[n] = src[n];
    n++;
  }
  dst[n] = 0;
}

static int building_ok(int idx) {
  int kind = G16(REC + idx * 16);
  return kind >= 6 && kind <= 16;
}

static int placing_now(void) {
  int t = G32(BUILD_TOOL);
  return t == 0 || (t >= 6 && t <= 15);
}

// kept for the notification (a message names a building kind, not a record): the last building shown per kind
static int g_last_shown[17];
static int g_unconn[PV_BUILDINGS], g_n_unconn;   // record numbers of the unconnected buildings of the last frame

static int log_on = -1;

void pv_frame_begin(void) {
  pvf_enabled();   // registers the world layer before the golfers' (their tags stay over the veil)
}

void pv_course_done(void) {
  if (log_on < 0) log_on = getenv("SIMGOLF_PATHS_LOG") != NULL;
  if (!pvf_enabled()) return;
  int w = g_map_w, h = g_map_h;
  if (w <= 0 || h <= 0 || !g_grid[9]) return;
  if (g_corner_w != w + 1) {
    free(g_corner);
    g_corner = calloc((size_t)(w + 1) * (size_t)(h + 1), sizeof *g_corner);
    g_corner_w = w + 1;
    if (!g_corner) {
      g_corner_w = 0;
      return;
    }
  }
  const uint16_t *fl = (const uint16_t *)g_grid[9];
  PvNote *n = &g_note;
  ++g_serial;
  memset(n, 0, offsetof(PvNote, q));
  n->magic = PV_MAGIC;
  n->serial = g_serial;
  n->zoom = G32(ZOOM);
  n->placing = placing_now();
  int flags = G32(FLAGS);
  n->not_needed = G32(DIFFICULTY) == 0 || (flags & 0x1000000) != 0;
  n->view = pvf_view_user() || n->placing;

  // buildings: the footprint is the square FUN_0042f340 tests, size from the kind's table entry (+0x10 of the
  // 0x14-byte entry at NAMES), grown by the building level for every kind but the snack bar (7)
  g_n_unconn = 0;
  for (int i = 0; i < 256 && n->n_bld < PV_BUILDINGS; i++) {
    if (!building_ok(i)) continue;
    int kind = G16(REC + i * 16);
    int row = G16(REC + i * 16 + 2), col = G16(REC + i * 16 + 4);
    int size = G8(NAMES + kind * 0x14 + 0x10);
    if (kind != 7) {
      int want = G32(REC + i * 16 + 8) + 1, have = G32(LEVELS + kind * 4);
      int lvl = have < want ? want : have;
      if (lvl > 99 && want <= 99) lvl = 99;
      size += lvl - 1;
    }
    if (size < 1) size = 1;
    if (size > 12) size = 12;
    if (row < 0 || col < 0 || row >= h || col >= w) continue;
    PvBuilding *pb = &n->bld[n->n_bld++];
    // the middle of the footprint, in world units (1/1024 tile): tile (a, b) spans a * 1024 .. (a + 1) * 1024
    int sx = 0, sy = 0;
    FUN_0042fb90(row * 1024 + size * 512, col * 1024 + size * 512, &sx, &sy, 4000);
    pb->x = (int16_t)sx;
    pb->y = (int16_t)sy;
    pb->kind = (int16_t)kind;
    pb->idx = (int16_t)i;
    pb->row = (int16_t)row;
    pb->col = (int16_t)col;
    pb->connected = (G8(REC + i * 16 + 7) & 0x40) != 0;
    copy_str(pb->name, sizeof pb->name, (const char *)(uintptr_t)(NAMES + kind * 0x14));
    if (!pb->name[0]) copy_str(pb->name, sizeof pb->name, "Building");
    if (!pb->connected && g_n_unconn < PV_BUILDINGS) g_unconn[g_n_unconn++] = i;
  }

  // tiles
  if (n->view) {
    int cw = 0, ch = 0;
    extern int win_screen_w, win_screen_h;
    cw = win_screen_w, ch = win_screen_h;
    int zoom = n->zoom > 0 ? n->zoom : 4;
    int mx = 8 * zoom * 2 + 32, my = 5 * zoom * 2 + 32;   // a tile's size at this zoom, so a tile whose centre is just off screen still draws
    int nq = 0;
    // two passes: the network first (they come first in the note), then path tiles outside it
    for (int pass = 0; pass < 2; pass++) {
      for (int a = 0; a < h && nq < PV_QUADS; a++)
        for (int b = 0; b < w && nq < PV_QUADS; b++) {
          uint16_t f = fl[a * w + b];
          int net = (f & 0x40) != 0 && (f & 0x420) != 0;
          int off = (f & 0x20) != 0 && (f & 0x40) == 0;
          if (pass == 0 ? !net : !off) continue;
          int sx = 0, sy = 0;
          FUN_0042fb90(a * 1024 + 512, b * 1024 + 512, &sx, &sy, 200);
          if (sx < -mx || sx > cw + mx || sy < -my || sy > ch + my) continue;
          add_quad(&n->q[nq++], a, b);
        }
      if (pass == 0) n->n_net = nq;
      else n->n_off = nq - n->n_net;
    }
  }
  hd_ui_note2_set(n, (int)sizeof *n);
  if (log_on) {
    static int last_n[5] = {-1, -1, -1, -1, -1};
    int unc = g_n_unconn;
    if (last_n[0] != n->view || last_n[1] != n->n_bld || last_n[2] != unc || last_n[3] != n->n_net * 4096 + n->n_off || last_n[4] != n->not_needed) {
      last_n[0] = n->view, last_n[1] = n->n_bld, last_n[2] = unc, last_n[3] = n->n_net * 4096 + n->n_off, last_n[4] = n->not_needed;
      fprintf(stderr, "paths: frame %u: view %d (user %d, placing %d), %d network tiles, %d other path tiles, %d buildings, %d not joined%s\n",
              n->serial, n->view, pvf_view_user(), n->placing, n->n_net, n->n_off, n->n_bld, unc, n->not_needed ? " (not needed here)" : "");
      for (int i = 0; i < n->n_bld && i < 16; i++)
        fprintf(stderr, "paths:   building %d \"%s\" kind %d at %d,%d connected %d\n", n->bld[i].idx, n->bld[i].name, n->bld[i].kind,
                n->bld[i].x, n->bld[i].y, n->bld[i].connected);
    }
  }
}

// ------------------------------------------------------------------ for the notification (hud_game.c)

static int word_end(char c) { return !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'' || c == '-'); }

// golf's monthly warning reads "WARNING: Your <building> will not be operational until you ... your clubhouse."
// (golf_01.c ~20457). Returns the record number of an unconnected building of that name, or -1. Several of one
// kind take turns (the message does not say which one), so each shown message points at the next.
int pv_building_of_text(const char *text) {
  static const char k_pre[] = "WARNING: Your ";
  if (strncmp(text, k_pre, sizeof k_pre - 1) != 0) return -1;
  text += sizeof k_pre - 1;
  int best = -1, kind = 0;
  size_t best_len = 0;
  for (int k = 6; k <= 16; k++) {
    const char *nm = (const char *)(uintptr_t)(NAMES + k * 0x14);
    size_t len = strlen(nm);
    if (len < 2 || len > 18 || strncmp(text, nm, len) != 0 || !word_end(text[len]) || len <= best_len) continue;
    best = k;
    best_len = len;
  }
  kind = best;
  if (kind < 0) return -1;
  // the unconnected buildings of that kind, now (not only the last frame's list: the message may come first)
  int list[256], nl = 0;
  for (int i = 0; i < 256; i++)
    if (G16(REC + i * 16) == kind && (G8(REC + i * 16 + 7) & 0x40) == 0) list[nl++] = i;
  if (!nl) return -1;
  int pick = list[0];
  for (int j = 0; j < nl; j++)
    if (list[j] > g_last_shown[kind]) {
      pick = list[j];
      break;
    }
  g_last_shown[kind] = pick;
  return pick;
}

// Camera to a building (what the golfer jump does for a golfer) and the path view on.
void pv_show_building(int idx) {
  if (idx < 0 || idx > 255 || !building_ok(idx)) return;
  int row = G16(REC + idx * 16 + 2), col = G16(REC + idx * 16 + 4);
  G32(CAM_X) = row;
  G32(CAM_Y) = col;
  pvf_set_view(1);
  fprintf(stderr, "paths: show building %d at tile %d,%d\n", idx, row, col);
}
