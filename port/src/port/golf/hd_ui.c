#include "port/course_lamps.h"
// Course-view UI on wide and tall canvases (simgolf mod, task-3 steps B4/B5),
// the game-thread half. Shared by the native and browser builds; at the
// original 800x600 canvas every function here leaves the game as it was.
//
// 1. Bottom anchoring. golf lays out its course-view UI for 800x600. The top
//    left (info box) stays put, the top right meters move by translator
//    patches (B1), and the bottom-left wheel with the build panels it opens
//    (FUN_00432ba0, FUN_00433190, FUN_00434350, FUN_00435760, FUN_004362f0)
//    is drawn through a "view" of the back buffer: hd_ui_view_begin(dx, dy)
//    moves the back buffer surface's pixel pointer by (dx, dy) and limits its
//    size and clip to 800x600 for the length of those calls, so every sprite,
//    text and fill they draw lands at the bottom of a taller canvas without
//    touching their hundreds of coordinates. Their mouse input gets the same
//    offset taken off (patches in tools/translate/golf.json, FUN_0040f5c0).
//    graphsy surface layout: port/docs/graphsy-surface.md.
//
// 2. Layers. hd_ui_course_done() runs once per course frame at the point
//    where golf has finished the course (terrain, objects, golfers, bubbles,
//    the 2x zoom) and starts its UI. A presenter (hd_native.c) copies the back
//    buffer there: that copy is the course layer, whatever differs in the
//    finished frame is the UI layer. hd_ui_frame_camera() records the camera
//    the course was drawn with, for smooth scrolling (B5).
//
// 3. Camera clamp. golf keeps the view centre on the map (tiles 0..49 per
//    axis). A wider or taller view then shows far more black past the map
//    edge than the original did; hd_ui_camera_clamp() keeps the view's edge
//    no further past the map than an 800x600 view could go, with the grid's
//    own size (terrain_grid_size), so bigger maps (task-7/8) clamp right.
//
// 4. Sub-tile camera (task-37). golf's camera is a whole tile (a 64x40 px
//    grid at zoom 4). The camera controls keep a continuous camera instead
//    (hd_ui_camera_set): golf gets the nearest tile and the rest becomes a
//    pixel offset that worldToScreen, screenToWorld and the terrain all draw
//    and pick with, so the view rests anywhere and clicks still hit the tile
//    under the pointer. golf's UI never goes through that projection, so it
//    and the native UI layers stay put.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/golf/hd_ui.h"
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif
#include "port/golf/camera_input.h"
#include "port/grid.h"
#include "port/win32.h"

#define G(addr) (*(volatile int32_t *)(uintptr_t)(addr))
#define BACK_BUFFER 0x4c1570u   // PTR_DAT_004c1570: the back buffer's wrapper
#define CAM_X 0x4c2ba0
#define CAM_Y 0x4c2ba4
#define ZOOM 0x4c2844
#define ROT 0x5685f4
#define TERRAIN_CAM_X 0x4d2048   // FUN_004498a0: the camera the terrain was last drawn at
#define TILE_POS_CAM_X 0x543d04  // FUN_0040f5c0: the camera of its tile screen position cache
#define TILE_TYPES 0x5722e8      // golf's terrain type per tile, [x * 50 + y]

int terrain_grid_size(int *w, int *h);   // platform/terrain_dll.cpp
void terrain_set_line_offset_fix(int on);
void place_frame_begin(void);
void place_drag_frame(void);
void place_drag_draw(void);
void terrain_set_view_offset(int ox, int oy);   // platform/terrain_dll.cpp
int *FUN_00430020(int x, int y, int *tx, int *ty, int exact);   // screenToWorld (generated)
void gdi_set_view(const void *bits, int dx, int dy);   // gdi.c: GDI text and blits follow the view

HdUiCourseHook hd_ui_course_hook;

// task-31 (golfers_game.c; no-ops in the browser build, golf/golfers_hooks.c)
void gw_frame_begin(void);
void gw_course_done(void);
// task-53 (paths_game.c; no-ops in the browser build, golf/golfers_hooks.c)
void pv_frame_begin(void);
void pv_course_done(void);
void aim_frame_begin(void);   // task-115: shot aim arrows (golf/aim_hooks.c)
void aim_course_done(void);
void opt_game_frame(void);   // options_frosted.c: pauses the game while the Options page is up
void dbg_overlay_frame(void);   // debug_overlay.c (task-113): reads the game state for the overlay; a no-op in the browser build

// ------------------------------------------------------------------ back buffer view

typedef struct {
  uint8_t *s;                 // graphsy surface (NULL: no view active)
  uint32_t bits, locked;      // +0x4c0, +0x4cc
  int32_t w, h;               // +0x38, +0x3c
  int32_t clip[4], bounds[4]; // +0x44, +0x54
  int dx, dy;
} View;
static View g_view;

// The back buffer's graphsy surface when it looks like one: 16 bpp, the
// canvas size, a pitch and bits.
static uint8_t *back_surface(void) {
  uint32_t wrap = *(volatile uint32_t *)(uintptr_t)BACK_BUFFER;
  if (wrap < 0x10000) return NULL;
  uint32_t p = *(volatile uint32_t *)(uintptr_t)(wrap + 4);
  if (p < 0x10000) return NULL;
  uint8_t *s = (uint8_t *)(uintptr_t)p;
  if (*(int32_t *)(s + 0x24) != 16 || *(int32_t *)(s + 0x38) != win_screen_w ||
      *(int32_t *)(s + 0x3c) != win_screen_h || *(int32_t *)(s + 0x40) < win_screen_w || !*(uint32_t *)(s + 0x4c0))
    return NULL;
  return s;
}

int hd_ui_view_begin(int dx, int dy) {
  if (g_view.s || (dx == 0 && dy == 0)) return 0;
  if (dx < 0 || dy < 0 || dx + 800 > win_screen_w || dy + 600 > win_screen_h) return 0;
  uint8_t *s = back_surface();
  if (!s) return 0;
  View *v = &g_view;
  v->s = s;
  v->dx = dx; v->dy = dy;
  v->bits = *(uint32_t *)(s + 0x4c0);
  v->locked = *(uint32_t *)(s + 0x4cc);
  v->w = *(int32_t *)(s + 0x38);
  v->h = *(int32_t *)(s + 0x3c);
  memcpy(v->clip, s + 0x44, 16);
  memcpy(v->bounds, s + 0x54, 16);
  uint32_t shift = (uint32_t)((dy * *(int32_t *)(s + 0x40) + dx) * 2);
  *(uint32_t *)(s + 0x4c0) = v->bits + shift;
  if (v->locked) *(uint32_t *)(s + 0x4cc) = v->locked + shift;
  *(int32_t *)(s + 0x38) = 800;
  *(int32_t *)(s + 0x3c) = 600;
  // the clip rect moved into the view's frame, within 800x600
  int32_t c[4] = {v->clip[0] - dx, v->clip[1] - dy, v->clip[2] - dx, v->clip[3] - dy};
  if (c[0] < 0) c[0] = 0;
  if (c[1] < 0) c[1] = 0;
  if (c[2] > 800) c[2] = 800;
  if (c[3] > 600) c[3] = 600;
  if (c[2] < c[0]) c[2] = c[0];
  if (c[3] < c[1]) c[3] = c[1];
  memcpy(s + 0x44, c, 16);
  int32_t b[4] = {0, 0, 800, 600};
  memcpy(s + 0x54, b, 16);
  gdi_set_view((const void *)(uintptr_t)v->bits, dx, dy);
  return 1;
}

void hd_ui_view_end(void) {
  View *v = &g_view;
  if (!v->s) return;
  uint8_t *s = v->s;
  *(uint32_t *)(s + 0x4c0) = v->bits;
  // a lock taken inside the view ended inside it too (golf pairs them)
  *(uint32_t *)(s + 0x4cc) = v->locked;
  *(int32_t *)(s + 0x38) = v->w;
  *(int32_t *)(s + 0x3c) = v->h;
  memcpy(s + 0x44, v->clip, 16);
  memcpy(s + 0x54, v->bounds, 16);
  gdi_set_view(NULL, 0, 0);
  v->s = NULL;
}

int FUN_00407c60(int x, int y, int radius, int color);
int hd_ui_staff_circle(int x, int y, int radius, int color) {
#ifndef __EMSCRIPTEN__
  const char *setting = mods_get("fixes.staff_circle");
  if (setting && atoi(setting) == 0) return FUN_00407c60(x, y, radius, color);
#endif
  // Employee controls are bottom anchored. Draw their world-space ring on
  // the full course surface without the controls' additional pixel shift.
  int active = g_view.s != NULL, dx = g_view.dx, dy = g_view.dy;
  if (active) hd_ui_view_end();
  int result = FUN_00407c60(x, y, radius, color);
  if (active) hd_ui_view_begin(dx, dy);
  return result;
}

// ------------------------------------------------------------------ layers

static HdUiCamera g_frame_cam;

// Content shift in screen pixels for camera X + 1 (a) and Y + 1 (b) at the
// current zoom and rotation (worldToScreen 0x42fb90; camera_input.c has the
// same table for the drag).
static void axes(double *ax, double *ay, double *bx, double *by) {
  int z = G(ZOOM);
  if (z < 1 || z > 8) z = 4;
  double sx = 8.0 * z, sy = 5.0 * z;
  if (win_tier_w == 1024) { sx *= 80 / 64.0; sy *= 52 / 40.0; }
  else if (win_tier_w == 1280) { sx *= 104 / 64.0; sy *= 68 / 40.0; }
  switch (G(ROT)) {
    case 2: *ax = sx; *ay = sy; *bx = -sx; *by = sy; break;
    case 4: *ax = sx; *ay = -sy; *bx = sx; *by = sy; break;
    case 6: *ax = -sx; *ay = -sy; *bx = sx; *by = -sy; break;
    default: *ax = -sx; *ay = sy; *bx = -sx; *by = -sy; break;
  }
}

static void sub_apply(void);

// ------------------------------------------------------------------ overscan (task-68, hd_ui.h)

int hd_os_l, hd_os_r, hd_os_t, hd_os_b;
static int g_os_tiles;

void hd_ui_set_overscan(int tiles) { g_os_tiles = tiles < 0 ? 0 : tiles > 8 ? 8 : tiles; }

// Start of a course frame: the margins at this zoom (a tile is 16 x zoom px wide).
static void overscan_begin(void) {
  int z = G(ZOOM), w = g_os_tiles * 16 * (z > 0 ? z : 1);
  hd_os_l = hd_os_r = w;
  hd_os_t = w / 4;
  hd_os_b = w * 2;
#ifndef __EMSCRIPTEN__
  if (g_os_tiles && mods_get_int("fixes.drag_scroll", 1)) {
    hd_os_l = hd_os_r = win_screen_w;
    hd_os_t = hd_os_b = win_screen_h;
  }
#endif
}

void hd_ui_frame_camera(void) {
  sub_apply();
  place_frame_begin();
  place_drag_frame();
  opt_game_frame();
  dbg_overlay_frame();
  overscan_begin();
  pv_frame_begin();   // before the golfers': its world layer goes under their name tags
  aim_frame_begin();
  gw_frame_begin();
  HdUiCamera *c = &g_frame_cam;
  c->x = G(CAM_X);
  c->y = G(CAM_Y);
  c->zoom = G(ZOOM);
  c->rot = G(ROT);
  axes(&c->ax, &c->ay, &c->bx, &c->by);
  c->ox = hd_cam_ox;
  c->oy = hd_cam_oy;
  c->drag_serial = camera_input_drag_serial();
  c->serial++;
}

int hd_ui_back_buffer(uint32_t *bits, int *pitch, int *w, int *h) {
  uint8_t *s = g_view.s;
  if (s) {   // inside a view: the real buffer is the one it saved
    *bits = g_view.bits;
    *w = g_view.w;
    *h = g_view.h;
  } else {
    if (!(s = back_surface())) return 0;
    *bits = *(uint32_t *)(s + 0x4c0);
    *w = *(int32_t *)(s + 0x38);
    *h = *(int32_t *)(s + 0x3c);
  }
  *pitch = *(int32_t *)(s + 0x40);
  return 1;
}

// The back buffer's pixels, pitch in pixels, size (task-41: the round ball takes golf's own ball out of them).
uint16_t *hd_ui_back_bits(int *pitch, int *w, int *h) {
  uint8_t *s = back_surface();
  if (!s || g_view.s) return NULL;
  *pitch = *(int32_t *)(s + 0x40);
  *w = *(int32_t *)(s + 0x38);
  *h = *(int32_t *)(s + 0x3c);
  return (uint16_t *)(uintptr_t)*(uint32_t *)(s + 0x4c0);
}

void hd_ui_course_done(void) {
  void fe_course_done(void);
  fe_course_done();   // a real course redraw ends the front-end page, even with layers=0
  hd_os_l = hd_os_r = hd_os_t = hd_os_b = 0;   // task-68: golf's own culling from here on
  if (!hd_ui_course_hook) return;
  uint8_t *s = back_surface();
  if (!s || g_view.s) return;
  place_drag_draw();
  hd_ui_course_hook((const uint16_t *)(uintptr_t)*(uint32_t *)(s + 0x4c0), *(int32_t *)(s + 0x40), &g_frame_cam);
  gw_course_done();
  pv_course_done();
  aim_course_done();
  lamps_game_course_done();
}

// ------------------------------------------------------------------ camera clamp

// Keeps a camera position (tiles, continuous) where a view wider or taller
// than 800x600 shows no more black past the map edge than an 800x600 view
// could at the same spot. Returns 0 when there is nothing to keep it to (an
// original-size view, no terrain yet); (tx, ty) are untouched when inside.
static int wide_clamp(double *tx, double *ty) {
  if (win_screen_w <= 800 && win_screen_h <= 600) return 0;   // the original's own clamp is enough
  int n_w, n_h;
  if (!terrain_grid_size(&n_w, &n_h)) return 0;
  double ax, ay, bx, by;
  axes(&ax, &ay, &bx, &by);
  // Q(i, j) = -(i A + j B): where tile (i, j) sits in a fixed screen frame;
  // the view centre is Q(camera). The map's extent in that frame, by its
  // corner tiles (the range golf's own clamp lets the centre reach).
  double qx[4], qy[4];
  int ci[4] = {0, n_w - 1, 0, n_w - 1}, cj[4] = {0, 0, n_h - 1, n_h - 1};
  for (int k = 0; k < 4; k++) {
    qx[k] = -(ci[k] * ax + cj[k] * bx);
    qy[k] = -(ci[k] * ay + cj[k] * by);
  }
  double x0 = qx[0], x1 = qx[0], y0 = qy[0], y1 = qy[0];
  for (int k = 1; k < 4; k++) {
    x0 = fmin(x0, qx[k]); x1 = fmax(x1, qx[k]);
    y0 = fmin(y0, qy[k]); y1 = fmax(y1, qy[k]);
  }
  // An 800x600 view at the extreme shows (400, 300) px past the corner tile;
  // a bigger view may show no more than that.
  double mx = (win_screen_w - 800) / 2.0, my = (win_screen_h - 600) / 2.0;
  double px = -(*tx * ax + *ty * bx), py = -(*tx * ay + *ty * by);
  double lo_x = x0 + mx, hi_x = x1 - mx, lo_y = y0 + my, hi_y = y1 - my;
  double nx = lo_x > hi_x ? (x0 + x1) / 2 : fmin(fmax(px, lo_x), hi_x);
  double ny = lo_y > hi_y ? (y0 + y1) / 2 : fmin(fmax(py, lo_y), hi_y);
  if (nx == px && ny == py) return 1;
  // back to camera tiles: -[ax bx; ay by] [cx; cy] = [nx; ny]
  double det = ax * by - bx * ay;
  *tx = (-nx * by + bx * ny) / det;
  *ty = (-ax * ny + nx * ay) / det;
  return 1;
}

static void map_size(int *w, int *h) {
  if (!terrain_grid_size(w, h)) *w = *h = 50;
}

// The main loop's clamp after golf's own, and after camera_input's whole-tile moves.
static int sub_owns(void);

void hd_ui_camera_clamp(void) {
  if (sub_owns()) return;   // the sub-tile camera is kept on the map already
  int n_w, n_h;
  if (!terrain_grid_size(&n_w, &n_h)) return;
  int cx = G(CAM_X), cy = G(CAM_Y);
  double tx = cx, ty = cy;
  if (!wide_clamp(&tx, &ty)) return;
  int ix = (int)lround(tx), iy = (int)lround(ty);
  if (ix < 0) ix = 0;
  if (iy < 0) iy = 0;
  if (ix > n_w - 1) ix = n_w - 1;
  if (iy > n_h - 1) iy = n_h - 1;
  if (ix == cx && iy == cy) return;
  static int logged;
  if (logged++ < 50) fprintf(stderr, "hd_ui: camera %d,%d kept to %d,%d (view %dx%d, map edge)\n", cx, cy, ix, iy, win_screen_w, win_screen_h);
  G(CAM_X) = ix;
  G(CAM_Y) = iy;
}

// ------------------------------------------------------------------ sub-tile camera

int hd_cam_ox, hd_cam_oy;
static int g_sub_on = 1;
// The continuous camera, while golf's camera is still the tile it was given
// (ix, iy); anything else moving golf's camera hands it back to golf.
static struct { int have, ix, iy; double tx, ty; } g_sub;

static int sub_owns(void) { return g_sub_on && g_sub.have && G(CAM_X) == g_sub.ix && G(CAM_Y) == g_sub.iy; }

void hd_ui_set_subtile(int on) {
  g_sub_on = on != 0;
  if (!g_sub_on) g_sub.have = 0;
}

int hd_ui_subtile(void) { return g_sub_on; }

void hd_ui_content_to_tiles(double dx, double dy, double *tx, double *ty) {
  double ax, ay, bx, by;
  axes(&ax, &ay, &bx, &by);
  double det = ax * by - bx * ay;
  *tx = (dx * by - bx * dy) / det;
  *ty = (ax * dy - dx * ay) / det;
}

void hd_ui_camera_get(double *tx, double *ty) {
  if (sub_owns()) {
    *tx = g_sub.tx;
    *ty = g_sub.ty;
  } else {
    *tx = G(CAM_X);
    *ty = G(CAM_Y);
  }
}

// golf's own clamp (0 to the map size - 1, FUN_0040f5c0) and the wide-view one.
static void keep_on_map(double *tx, double *ty) {
  int n_w, n_h;
  map_size(&n_w, &n_h);
  wide_clamp(tx, ty);
  *tx = fmin(fmax(*tx, 0), n_w - 1);
  *ty = fmin(fmax(*ty, 0), n_h - 1);
}

void hd_ui_camera_set(double tx, double ty) {
  keep_on_map(&tx, &ty);
  int ix = (int)lround(tx), iy = (int)lround(ty);
  if (!g_sub_on) tx = ix, ty = iy;
  g_sub.tx = tx;
  g_sub.ty = ty;
  g_sub.ix = ix;
  g_sub.iy = iy;
  g_sub.have = 1;
  G(CAM_X) = ix;
  G(CAM_Y) = iy;
}

// ------------------------------------------------------------------ zoom steps (task-56)

#define ZOOM_PIXELS 0x4c2840   // zoom * 8 (golf's z and x commands keep it)
#define ZOOM_CACHE_A 0x4c2848  // cleared by golf's zoom out
#define ZOOM_CACHE_B 0x4c2854
#define ZOOM_2X 0x5a9cbc       // golf's 2x mode: its zoom out only leaves that

int (*hd_ui_zoom_hook)(const HdUiZoom *z);
int (*hd_ui_zoom_map_hook)(int *x, int *y);

void hd_ui_game_view(int *zoom, double *cx, double *cy) {
  *zoom = G(ZOOM);
  hd_ui_camera_get(cx, cy);
}

int hd_ui_zoom_set(int nz, int use_pointer, int px, int py) {
  int z0 = G(ZOOM);
  if (nz == z0 || nz < 1 || nz > 4 || (z0 != 1 && z0 != 2 && z0 != 4) || (nz != 1 && nz != 2 && nz != 4)) return 0;
  if (G(ZOOM_2X) != 0) return 0;
  double cx0, cy0, dx = 0, dy = 0;
  hd_ui_camera_get(&cx0, &cy0);
  if (use_pointer && g_sub_on) {
    // The view after the zoom is the one before it, scaled by r about the pointer: the content moves
    // (r - 1) * (centre - pointer) in the new pixels, (1 - 1/r) * (centre - pointer) in the old ones.
    double r = (double)nz / z0, k = 1 - 1 / r;
    hd_ui_content_to_tiles(k * (win_screen_w / 2.0 - px), k * (win_screen_h / 2.0 - py), &dx, &dy);   // at the old zoom
  }
  G(ZOOM) = nz;
  G(ZOOM_PIXELS) = nz * 8;
  if (nz < z0) {
    G(ZOOM_CACHE_A) = -1;
    G(ZOOM_CACHE_B) = -1;
  }
  hd_ui_camera_set(cx0 + dx, cy0 + dy);   // kept on the map at the new zoom
  double cx1, cy1;
  hd_ui_camera_get(&cx1, &cy1);
  if (hd_ui_zoom_hook) {
    HdUiZoom z = {z0, nz, cx1, cy1, G(ROT)};
    hd_ui_zoom_hook(&z);
  }
  return 1;
}

// Start of a course frame, before the terrain: this frame's offset.
static void sub_apply(void) {
  int ox = 0, oy = 0;
  if (sub_owns()) {
    // a zoom or rotation since may need the view kept on the map again
    double tx = g_sub.tx, ty = g_sub.ty;
    keep_on_map(&tx, &ty);
    if (tx != g_sub.tx || ty != g_sub.ty) hd_ui_camera_set(tx, ty);
    double ax, ay, bx, by, fx = g_sub.tx - g_sub.ix, fy = g_sub.ty - g_sub.iy;
    axes(&ax, &ay, &bx, &by);
    ox = (int)lround(fx * ax + fy * bx);
    oy = (int)lround(fx * ay + fy * by);
  } else {
    g_sub.have = 0;
  }
  if (ox == hd_cam_ox && oy == hd_cam_oy) return;
  hd_cam_ox = ox;
  hd_cam_oy = oy;
  terrain_set_view_offset(ox, oy);
  G(TERRAIN_CAM_X) = G(CAM_X) + 1000;   // FUN_004498a0 redraws the whole terrain, as after a long jump
  G(TILE_POS_CAM_X) = -1;               // the main loop forgets its tile screen positions
  static int dbg = -1;
  if (dbg < 0) dbg = getenv("SIMGOLF_CAMERA_DEBUG") != NULL;
  if (dbg) fprintf(stderr, "hd_ui: view %d,%d %+d,%+d px zoom %d rot %d\n", G(CAM_X), G(CAM_Y), ox, oy, G(ZOOM), G(ROT));
}

void hd_ui_pick_log(int x, int y) {
  int i = -1, j = -1, mx = x, my = y;
  // during a zoom glide a click is turned from the picture on screen into golf's view (task-56)
  if (hd_ui_zoom_map_hook && hd_ui_zoom_map_hook(&mx, &my) && (mx != x || my != y))
    fprintf(stderr, "hd_ui: pick-map %d,%d -> %d,%d (zoom glide)\n", x, y, mx, my);
  FUN_00430020(mx, my, &i, &j, 0);
  char types[5 * 5 * 5 + 1], *p = types;
  for (int dj = -2; dj <= 2; dj++)
    for (int di = -2; di <= 2; di++) {
      int a = i + di, b = j + dj;
      // golf's own type grid is 50 tiles a side (0x32 strides in its code)
      int t = a >= 0 && b >= 0 && a < 50 && b < 50 ? *(volatile int8_t *)grid_translate(TILE_TYPES + a * 50 + b) : -1;
      p += sprintf(p, "%s%d", p == types ? "" : ",", t);
    }
  fprintf(stderr, "hd_ui: pick %d,%d -> tile %d,%d (camera %d,%d %+d,%+d px zoom %d rot %d) types %s\n", x, y, i, j,
          G(CAM_X), G(CAM_Y), hd_cam_ox, hd_cam_oy, G(ZOOM), G(ROT), types);
}
