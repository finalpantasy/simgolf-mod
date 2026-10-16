// Modern camera controls (simgolf mod, task-15), see camera_input.h.
//
// The camera is the game's own: the view centre tile 0x4c2ba0/0x4c2ba4
// (0..49, the main loop clamps it every frame, as after its edge scroll), the
// zoom 0x4c2844 (1, 2 or 4 = more detail) and the rotation 0x5685f4 (0, 2, 4,
// 6). worldToScreen (0x42fb90) moves a tile 8*zoom px across and 5*zoom px
// down per camera step on each axis, so the course only scrolls in whole
// tiles; a drag maps the mouse offset from where it started through the
// inverse of that step, so the ground under the pointer follows it as closely
// as whole tiles allow. Zoom goes through the game's own z (in) and x (out)
// commands, posted in its key mailbox 0x822b9c, the handler the zoom buttons
// and keys share.
//
// The controls only act while the course view's main loop draws frames: its
// drawMoneyLabels (0x40c910) ran lately and the name entry box (0x45b2c0) has
// not opened since. Trays, tools, golfer cards and messages keep those frames
// coming (some poll the modal "key or click?" check 0x45ae70 every frame,
// which alone means nothing). Dialogs and menus that stop the course (the
// wrench and information menus, reports) count once frames have stopped for
// MODAL_GRACE_MS while 0x45ae70 polls. Entries are counted by PORT_TRACE
// (ghidra_prelude.h): task-15c found the trace ring overwritten between looks
// in nine of ten scans, so busy frames could hide the course frame. Anywhere
// else (menus, dialogs, typing a name) every event goes to the game untouched.
//
// golf.ini, section [camera] (defaults in brackets; all off = the original):
//   drag_button=middle         buttons that pan when dragged: middle, right
//                              (or both) or none; the maintainer 2026-10-09: middle
//                              only, the right button stays the game's
//   wasd=1                     W/A/S/D pan the course view (they shadow the
//                              game's W water and S sand trap and D hotkeys
//                              there; the build buttons still have them)
//   wheel_zoom=1               mouse wheel zooms
//   zoom_anchor=pointer        the wheel zooms toward the mouse pointer (the course
//                              under it stays under it); centre = the view centre,
//                              as golf's own zoom (task-56; pointer needs the sub-tile
//                              camera, display.smooth=1). The glide between the zoom
//                              levels is display.smooth_zoom (hd_native.c, hd_zoom.c).
//   edge_scroll=0              1: the game's scrolling at the screen edge
//   pan_speed=900              W/A/S/D speed, screen pixels per second
// A right press that moves less than 6 px before release reaches the game as
// the original click; past 6 px it pans and the click is dropped.
//
// Sub-tile camera (task-37, hd_ui.c, on unless display.smooth=0): the drag
// and the keys move a continuous camera, so the ground under the pointer
// follows it to the pixel and the view stops wherever the input stops; golf
// draws and picks with the leftover offset. Off: whole tiles, as above.

#include "port/golf/camera_input.h"
#include "port/golf/hd_ui.h"

#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "port/grid.h"
#include "port/runtime.h"
#include "port/win32.h"

#define G(addr) (*(volatile int32_t *)(uintptr_t)(addr))
#define CAM_X 0x4c2ba0
#define CAM_Y 0x4c2ba4
#define CAM_MAX (MAP_W - 1)   // 49 on the original map (grid.h)
#define ZOOM 0x4c2844
#define ROT 0x5685f4
#define VIEW_W 0x822c8c
#define KEY_MAILBOX 0x822b9c
#define FN_COURSE_FRAME 0x40c910   // drawMoneyLabels: once per course frame
#define FN_MODAL_WAIT 0x45ae70     // "key or click pending?" of modal loops
#define FN_TEXT_ENTRY 0x45b2c0     // the name entry box (save, rename, ...)
#define MODAL_GRACE_MS 150         // course frames this recent: always active
#define ACTIVE_MS 600              // a slow frame (busy course) may take this long
#define CLICK_SLOP 6               // screen px a right click may move and stay a click
#define INJECTED 0x5ca3e7au        // mouse id ("which") of the clicks we hand back

static struct {
  int loaded, drag_middle, drag_right, wasd, wheel_zoom, edge_scroll;
  double pan_speed;
  int zoom_pointer;   // task-56: the wheel zooms toward the pointer (0: the view centre, as golf's own zoom)
  int fix_drag_scroll;
} cfg = {0, 1, 0, 1, 1, 0, 900, 1, 1};

static void trim(char *s) {
  char *a = s;
  while (*a == ' ' || *a == '\t') a++;
  memmove(s, a, strlen(a) + 1);
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
}

static void watch_start(void);

// Options > Controls (card task-24): the mods store ([camera] in golf.ini is in it already, mods.ini and
// options.ini over it) keeps the settings live. SIMGOLF_CAMERA_INI (tests) names a file that stays in charge.
static void sync_store(void) {
#ifndef __EMSCRIPTEN__
  static unsigned seen = ~0u;
  const char *v;
  if ((v = getenv("SIMGOLF_CAMERA_INI")) && *v) return;
  if (mods_serial() == seen) return;
  seen = mods_serial();
  cfg.fix_drag_scroll = mods_get_int("fixes.drag_scroll", 1) != 0;
  if ((v = mods_get("camera.drag_button")) && *v) {
    cfg.drag_middle = strstr(v, "middle") != NULL;
    cfg.drag_right = strstr(v, "right") != NULL;
  }
  if ((v = mods_get("camera.wasd")) && *v) cfg.wasd = atoi(v) != 0;
  if ((v = mods_get("camera.wheel_zoom")) && *v) cfg.wheel_zoom = atoi(v) != 0;
  if ((v = mods_get("camera.edge_scroll")) && *v) cfg.edge_scroll = atoi(v) != 0;
  if ((v = mods_get("camera.pan_speed")) && atof(v) > 0) cfg.pan_speed = atof(v);
  if ((v = mods_get("camera.zoom_anchor")) && *v) cfg.zoom_pointer = strncasecmp(v, "cent", 4) != 0;
#endif
}

static void load_settings(void) {
  cfg.loaded = 1;
  watch_start();
  char path[1024] = "";
  const char *env = getenv("SIMGOLF_CAMERA_INI");   // tests: another golf.ini
  if (env && *env) {
    snprintf(path, sizeof path, "%s", env);
  } else {
    char *base = SDL_GetBasePath();
    if (base) {
      snprintf(path, sizeof path, "%sgolf.ini", base);
      SDL_free(base);
    }
  }
  FILE *f = path[0] ? fopen(path, "r") : NULL;
  if (f) {
    char line[256];
    int in_camera = 0;
    while (fgets(line, sizeof line, f)) {
      trim(line);
      if (line[0] == '[') { in_camera = !strncasecmp(line, "[camera]", 8); continue; }
      char *eq = strchr(line, '=');
      if (!in_camera || !eq || line[0] == ';' || line[0] == '#') continue;
      *eq = 0;
      char *key = line, *val = eq + 1;
      trim(key);
      trim(val);
      if (!strcasecmp(key, "drag_button")) {
        cfg.drag_middle = strstr(val, "middle") != NULL;
        cfg.drag_right = strstr(val, "right") != NULL;
      } else if (!strcasecmp(key, "wasd")) cfg.wasd = atoi(val) != 0;
      else if (!strcasecmp(key, "wheel_zoom")) cfg.wheel_zoom = atoi(val) != 0;
      else if (!strcasecmp(key, "edge_scroll")) cfg.edge_scroll = atoi(val) != 0;
      else if (!strcasecmp(key, "pan_speed") && atof(val) > 0) cfg.pan_speed = atof(val);
      else if (!strcasecmp(key, "zoom_anchor")) cfg.zoom_pointer = strncasecmp(val, "cent", 4) != 0;
    }
    fclose(f);
  }
  fprintf(stderr, "camera: drag_button=%s%s%s wasd=%d wheel_zoom=%d zoom_anchor=%s edge_scroll=%d pan_speed=%.0f (%s)\n",
          cfg.drag_middle ? "middle" : "", cfg.drag_middle && cfg.drag_right ? "," : "",
          cfg.drag_right ? "right" : (cfg.drag_middle ? "" : "none"), cfg.wasd, cfg.wheel_zoom,
          cfg.zoom_pointer ? "pointer" : "centre", cfg.edge_scroll, cfg.pan_speed, f ? path : "no golf.ini [camera], defaults");
}

// ------------------------------------------------------------------ is the course view in charge?

// Entry counters of three functions (ghidra_prelude.h PORT_TRACE), compared
// at every event and tick: the time a counter last moved is when that
// function last ran, to within one game loop yield.
extern uint32_t port_watch_addr[3];
extern uint32_t port_watch_hits[3];
enum { W_COURSE, W_MODAL, W_TEXT };
static uint32_t g_seen[3];
static double g_t[3] = {-1e9, -1e9, -1e9};

static void watch_start(void) {
  port_watch_addr[W_COURSE] = FN_COURSE_FRAME;
  port_watch_addr[W_MODAL] = FN_MODAL_WAIT;
  port_watch_addr[W_TEXT] = FN_TEXT_ENTRY;
}

static int course_active(double now) {
  double course = now - g_t[W_COURSE];
  if (g_t[W_TEXT] >= g_t[W_COURSE]) return 0;   // typing a name
  if (course < MODAL_GRACE_MS) return 1;         // frames are coming
  // a modal loop polls for a click and the course stopped drawing: a dialog
  return course < ACTIVE_MS && g_t[W_MODAL] <= g_t[W_COURSE];
}

static void scan_trace(double now) {
  int moved[3];
  for (int i = 0; i < 3; i++) {
    uint32_t h = port_watch_hits[i];
    moved[i] = h != g_seen[i];
    g_seen[i] = h;
    if (moved[i]) g_t[i] = now;
  }
  // text entry starts from a course frame and blocks it: if both ran since
  // the last look, the text entry came last
  if (moved[W_TEXT] && moved[W_COURSE]) g_t[W_COURSE] = now - 0.001;
  static int dbg = -1, last = -1;
  if (dbg < 0) dbg = getenv("SIMGOLF_CAMERA_DEBUG") != NULL;
  int act;
  if (dbg && (act = course_active(now)) != last) {
    fprintf(stderr, "camera-debug: %.1f s: active %d (course frame %.0f ms ago, modal wait %.0f, text entry %.0f)\n",
            now / 1000.0, act, now - g_t[W_COURSE], now - g_t[W_MODAL], now - g_t[W_TEXT]);
    last = act;
  }
}

// ------------------------------------------------------------------ screen offset -> camera tiles

// Camera steps (along its X and Y tile axes) that move the course content by
// (dx, dy) screen pixels.
static void content_to_tiles(double dx, double dy, double *tx, double *ty) {
  int z = G(ZOOM);
  if (z < 1 || z > 8) z = 4;
  double sx = 8.0 * z, sy = 5.0 * z;
  int w = win_tier_w;   // worldToScreen's tile scale (task-3: wide canvases keep 800x600's)
  if (w == 1024) { sx *= 80 / 64.0; sy *= 52 / 40.0; }
  else if (w == 1280) { sx *= 104 / 64.0; sy *= 68 / 40.0; }
  // screen shift of the content for camera X + 1 (a) and camera Y + 1 (b)
  double ax, ay, bx, by;
  switch (G(ROT)) {
    case 2: ax = sx; ay = sy; bx = -sx; by = sy; break;
    case 4: ax = sx; ay = -sy; bx = sx; by = sy; break;
    case 6: ax = -sx; ay = -sy; bx = sx; by = -sy; break;
    default: ax = -sx; ay = sy; bx = -sx; by = -sy; break;
  }
  double det = ax * by - bx * ay;
  *tx = (dx * by - bx * dy) / det;
  *ty = (ax * dy - dx * ay) / det;
}

static int clamp_tile(long v) { return v < 0 ? 0 : v > CAM_MAX ? CAM_MAX : (int)v; }

// Logged whenever the camera or zoom changes (tests read these lines).
static int g_log_x = -1, g_log_y = -1, g_log_z = -1;
static const char *g_cause = "game";

static void log_camera(void) {
  int x = G(CAM_X), y = G(CAM_Y), z = G(ZOOM);
  if (x == g_log_x && y == g_log_y && z == g_log_z) return;
  fprintf(stderr, "camera: %.1f s: %d,%d zoom %d rot %d (%s)\n", port_now_ms() / 1000.0, x, y, z,
          G(ROT), g_cause);
  g_log_x = x; g_log_y = y; g_log_z = z;
  g_cause = "game";
}

// ------------------------------------------------------------------ drag

static struct {
  int button;               // SDL button held for a drag, 0 = none
  int panning;              // moved past the click slop (or middle)
  double slop;              // CLICK_SLOP in frame pixels for this window
  int x0, y0;               // anchor: pointer ...
  int cam_x0, cam_y0;       // ... and camera there
  double sub_x0, sub_y0;    // ... (the sub-tile camera)
  int set_x, set_y;         // what we last wrote
} g_drag;

static uint32_t g_drag_serial;

uint32_t camera_input_drag_serial(void) { return g_drag_serial; }

static double g_click_up_at;   // a handed-back right click releases then
static int g_click_x, g_click_y;

// task-56: while a zoom glide shows a view golf has already left, the game
// gets the point of its own view that lies under the pointer on screen (not on
// golf's UI), so a click or hover picks the tile the player sees. The glide's
// end hands the game the pointer itself back.
static int g_ptr_x = -1, g_ptr_y = -1;   // where the pointer really is (canvas px)
static int g_remapped;                   // the game was given a remapped point

static void zoom_remap(int *x, int *y) {
  if (!hd_ui_zoom_map_hook) return;
  int nx = *x, ny = *y;
  if (hd_ui_zoom_map_hook(&nx, &ny)) {
    *x = nx;
    *y = ny;
    g_remapped = 1;
  }
}

static void push_button(Uint8 button, int down, int x, int y) {
  zoom_remap(&x, &y);
  SDL_Event e;
  memset(&e, 0, sizeof e);
  e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
  // no window id: the 4K presenter (hd_native.c) maps only events that carry
  // one from window pixels, and this point is in the 800x600 frame already
  e.button.which = INJECTED;
  e.button.button = button;
  e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
  e.button.clicks = 1;
  e.button.x = x;
  e.button.y = y;
  SDL_PushEvent(&e);
}

// Events reach us in 800x600 frame pixels; with the 4K presenter one frame
// pixel is several screen pixels, so the click allowance shrinks with it.
static double click_slop(Uint32 window_id) {
  SDL_Window *w = window_id ? SDL_GetWindowFromID(window_id) : NULL;
  int ww = 0, wh = 0;
  if (w) SDL_GetWindowSize(w, &ww, &wh);
  double s = fmin(ww / (double)WIN_SCREEN_W, wh / (double)WIN_SCREEN_H);
  double slop = s > 1 ? CLICK_SLOP / s : CLICK_SLOP;
  return slop < 2 ? 2 : slop;
}

static void drag_anchor(int x, int y) {
  g_drag.x0 = x; g_drag.y0 = y;
  g_drag.cam_x0 = g_drag.set_x = G(CAM_X);
  g_drag.cam_y0 = g_drag.set_y = G(CAM_Y);
  hd_ui_camera_get(&g_drag.sub_x0, &g_drag.sub_y0);
}

static void drag_to(int x, int y) {
  // someone else moved the camera (the map edge clamp, the game following a
  // shot): carry on from here
  if (G(CAM_X) != g_drag.set_x || G(CAM_Y) != g_drag.set_y) drag_anchor(x, y);
  double tx, ty;
  g_cause = g_drag.button == SDL_BUTTON_MIDDLE ? "middle-drag" : "right-drag";
  if (hd_ui_subtile()) {
    hd_ui_content_to_tiles(x - g_drag.x0, y - g_drag.y0, &tx, &ty);
    hd_ui_camera_set(g_drag.sub_x0 + tx, g_drag.sub_y0 + ty);   // kept on the map there
    g_drag.set_x = G(CAM_X);
    g_drag.set_y = G(CAM_Y);
    if (cfg.fix_drag_scroll) {
      // Use the clamped position as the next anchor. Motion beyond a map
      // edge must not build up a dead zone before a reverse drag can move.
      drag_anchor(x, y);
      ++g_drag_serial;
    }
    log_camera();
    return;
  }
  content_to_tiles(x - g_drag.x0, y - g_drag.y0, &tx, &ty);
  int nx = clamp_tile(g_drag.cam_x0 + lround(tx)), ny = clamp_tile(g_drag.cam_y0 + lround(ty));
  G(CAM_X) = g_drag.set_x = nx;
  G(CAM_Y) = g_drag.set_y = ny;
  hd_ui_camera_clamp();   // wide views stop at the map edge (task-3); a clamp re-anchors the drag
  if (cfg.fix_drag_scroll) {
    if (G(CAM_X) != nx || G(CAM_Y) != ny || nx != g_drag.cam_x0 + lround(tx) || ny != g_drag.cam_y0 + lround(ty))
      drag_anchor(x, y);
    ++g_drag_serial;
  }
  log_camera();
}

// ------------------------------------------------------------------ keys and wheel

enum { K_W, K_A, K_S, K_D, K_N };
static const SDL_Scancode k_scan[K_N] = {SDL_SCANCODE_W, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D};
static const int k_dx[K_N] = {0, 1, 0, -1};   // content moves opposite to the view
static const int k_dy[K_N] = {1, 0, -1, 0};
static int g_held[K_N];          // key down was taken (its up and repeats are too)
static char g_eat_text;          // the text event that follows a taken key
static double g_eat_text_until;
static double g_acc_x, g_acc_y, g_last_tick;
static int g_pending_zoom;       // wheel notches not yet handed to the game

static int key_index(SDL_Scancode sc) {
  for (int k = 0; k < K_N; k++)
    if (k_scan[k] == sc) return k;
  return -1;
}

static int any_held(void) { return g_held[K_W] | g_held[K_A] | g_held[K_S] | g_held[K_D]; }

static void keys_pan(double now) {
  double dt = g_last_tick > 0 ? now - g_last_tick : 0;
  if (dt > 100) dt = 100;
  double vx = 0, vy = 0;
  for (int k = 0; k < K_N; k++)
    if (g_held[k]) { vx += k_dx[k]; vy += k_dy[k]; }
  if ((vx == 0 && vy == 0) || !course_active(now)) return;
  double tx, ty;
  if (hd_ui_subtile()) {
    double cx, cy;
    hd_ui_content_to_tiles(vx * cfg.pan_speed * dt / 1000, vy * cfg.pan_speed * dt / 1000, &tx, &ty);
    hd_ui_camera_get(&cx, &cy);
    g_cause = "keys";
    hd_ui_camera_set(cx + tx, cy + ty);
    log_camera();
    return;
  }
  content_to_tiles(vx * cfg.pan_speed * dt / 1000, vy * cfg.pan_speed * dt / 1000, &tx, &ty);
  g_acc_x += tx;
  g_acc_y += ty;
  int sx = (int)g_acc_x, sy = (int)g_acc_y;   // whole steps, the rest carries
  if (!sx && !sy) return;
  g_acc_x -= sx;
  g_acc_y -= sy;
  g_cause = "keys";
  G(CAM_X) = clamp_tile(G(CAM_X) + sx);
  G(CAM_Y) = clamp_tile(G(CAM_Y) + sy);
  hd_ui_camera_clamp();   // wide views stop at the map edge (task-3)
  log_camera();
}

// A fresh press moves one step at once, so a tap does something (the
// sub-tile camera moves from the first tick on).
static void keys_kick(int k) {
  if (hd_ui_subtile()) return;
  double tx, ty;
  content_to_tiles(k_dx[k], k_dy[k], &tx, &ty);
  double m = fabs(tx) > fabs(ty) ? fabs(tx) : fabs(ty);
  if (m <= 0) return;
  g_acc_x = tx / m * 0.999;
  g_acc_y = ty / m * 0.999;
}

static void note_ignored(const char *what, double now);

// The key bindings (golf/keymap.c, card task-24) press and release a pan key by its binding.
void camera_input_pan_key(int k, int down) {
  if (!cfg.loaded) load_settings();
  sync_store();
  if (k < 0 || k >= K_N) return;
  double now = port_now_ms();
  scan_trace(now);
  if (!down) {
    if (!g_held[k]) return;
    g_held[k] = 0;
    if (!any_held()) g_acc_x = g_acc_y = 0;
    return;
  }
  if (g_held[k]) return;
  if (!course_active(now)) {
    note_ignored("a pan key", now);
    return;
  }
  if (!any_held()) keys_kick(k);
  g_held[k] = 1;
}

int camera_input_active(void) {
  if (!cfg.loaded) load_settings();
  double now = port_now_ms();
  scan_trace(now);
  return course_active(now);
}

int camera_input_wasd(void) {
  if (!cfg.loaded) load_settings();
  sync_store();
  return cfg.wasd;
}

// ------------------------------------------------------------------ entry points

// A camera input the game got instead because the course view was not in
// charge: one golf.log line every 2 s at most, so a "the camera didn't move"
// report comes with its reason.
static void note_ignored(const char *what, double now) {
  static double last;
  if (now - last < 2000) return;
  last = now;
  fprintf(stderr, "camera: %.1f s: %s went to the game: course frame %.0f ms ago, modal wait %.0f ms ago, "
          "text entry %.0f ms ago\n", now / 1000.0, what, now - g_t[W_COURSE], now - g_t[W_MODAL],
          now - g_t[W_TEXT]);
}

int camera_input_event(SDL_Event *e) {
  if (!cfg.loaded) load_settings();
  sync_store();
  double now = port_now_ms();
  scan_trace(now);
  int active = course_active(now);
  switch (e->type) {
    case SDL_MOUSEMOTION:
      if (!cfg.edge_scroll) {
        // the game scrolls when the pointer is on the outermost pixel
        if (e->motion.x < 1) e->motion.x = 1;
        if (e->motion.x > WIN_SCREEN_W - 2) e->motion.x = WIN_SCREEN_W - 2;
        if (e->motion.y < 1) e->motion.y = 1;
        if (e->motion.y > WIN_SCREEN_H - 2) e->motion.y = WIN_SCREEN_H - 2;
      }
      g_ptr_x = e->motion.x;
      g_ptr_y = e->motion.y;
      if (g_drag.button) {
        int x = e->motion.x, y = e->motion.y;
        if (!g_drag.panning && (abs(x - g_drag.x0) >= g_drag.slop || abs(y - g_drag.y0) >= g_drag.slop))
          g_drag.panning = 1;
        if (g_drag.panning && active) drag_to(x, y);
      }
      if (e->motion.which != INJECTED) zoom_remap(&e->motion.x, &e->motion.y);
      return 0;   // the game still sees the pointer move

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
      if (e->button.which == INJECTED) return 0;
      if (!cfg.edge_scroll) {
        if (e->button.x < 1) e->button.x = 1;
        if (e->button.x > WIN_SCREEN_W - 2) e->button.x = WIN_SCREEN_W - 2;
        if (e->button.y < 1) e->button.y = 1;
        if (e->button.y > WIN_SCREEN_H - 2) e->button.y = WIN_SCREEN_H - 2;
      }
      Uint8 b = e->button.button;
      int x = e->button.x, y = e->button.y;
      g_ptr_x = x;
      g_ptr_y = y;
      zoom_remap(&e->button.x, &e->button.y);   // x, y stay the pointer itself (drags)
      if (e->type == SDL_MOUSEBUTTONDOWN) {
        int pans = (b == SDL_BUTTON_MIDDLE && cfg.drag_middle) || (b == SDL_BUTTON_RIGHT && cfg.drag_right);
        if (pans && !active) note_ignored(b == SDL_BUTTON_MIDDLE ? "middle drag" : "right drag", now);
        if (!pans || !active || g_drag.button) return 0;
        g_drag.button = b;
        g_drag.panning = b == SDL_BUTTON_MIDDLE;
        g_drag.slop = click_slop(e->button.windowID);
        drag_anchor(x, y);
        return 1;   // a right press waits to see if it is a click
      }
      if (b != g_drag.button) return 0;
      g_drag.button = 0;
      if (b == SDL_BUTTON_RIGHT && !g_drag.panning) {
        // a click after all: the game gets it where the press was, the
        // release a moment later (it may look at the button state per frame)
        push_button(SDL_BUTTON_RIGHT, 1, g_drag.x0, g_drag.y0);
        g_click_x = g_drag.x0;
        g_click_y = g_drag.y0;
        g_click_up_at = now + 60;
      }
      return 1;
    }

    case SDL_MOUSEWHEEL:
      if (cfg.wheel_zoom && !active && e->wheel.y) note_ignored("wheel", now);
      if (!cfg.wheel_zoom || !active || e->wheel.y == 0) return 0;
      g_pending_zoom += e->wheel.y > 0 ? 1 : -1;
      if (g_pending_zoom > 2) g_pending_zoom = 2;   // only three zoom levels
      if (g_pending_zoom < -2) g_pending_zoom = -2;
      return 1;

    case SDL_KEYDOWN:
    case SDL_KEYUP: {
      int k = key_index(e->key.keysym.scancode);
      if (k < 0 || !cfg.wasd) return 0;
      if (e->type == SDL_KEYUP) {
        if (!g_held[k]) return 0;
        g_held[k] = 0;
        if (!any_held()) g_acc_x = g_acc_y = 0;
        return 1;
      }
      if (g_held[k]) return 1;   // auto-repeat of a key we hold
      if (!active) note_ignored("W/A/S/D", now);
      if (!active || (e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) return 0;
      if (!any_held()) keys_kick(k);
      g_held[k] = 1;
      g_eat_text = (char)SDL_GetKeyFromScancode(k_scan[k]);
      g_eat_text_until = now + 200;
      return 1;
    }

    case SDL_TEXTINPUT: {
      char c = e->text.text[0];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if (!g_eat_text || now > g_eat_text_until || c != g_eat_text || e->text.text[1]) return 0;
      g_eat_text = 0;
      return 1;
    }

    case SDL_WINDOWEVENT:
      if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        // keys and buttons released elsewhere never come back to us
        g_drag.button = 0;
        memset(g_held, 0, sizeof g_held);
      }
      return 0;
  }
  return 0;
}

void camera_input_tick(void) {
  if (!cfg.loaded) load_settings();
  sync_store();
  double now = port_now_ms();
  scan_trace(now);
  if (g_click_up_at > 0 && now >= g_click_up_at) {
    g_click_up_at = 0;
    push_button(SDL_BUTTON_RIGHT, 0, g_click_x, g_click_y);
  }
  if (any_held()) keys_pan(now);
  g_last_tick = now;
  if (g_remapped && hd_ui_zoom_map_hook && !hd_ui_zoom_map_hook(NULL, NULL)) {
    // the zoom glide is over: golf has had a remapped pointer; hand it the real one
    g_remapped = 0;
    if (g_ptr_x >= 0) {
      SDL_Event m;
      memset(&m, 0, sizeof m);
      m.type = SDL_MOUSEMOTION;
      m.motion.which = INJECTED;
      m.motion.x = g_ptr_x;
      m.motion.y = g_ptr_y;
      SDL_PushEvent(&m);
    }
  }
  if (g_pending_zoom && course_active(now)) {
    // The wheel goes to hd_ui_zoom_set when the view can be placed anywhere: it zooms toward the pointer
    // ([camera] zoom_anchor) and the presenter can glide (display.smooth_zoom). Else golf's own commands
    // z and x, through its key mailbox: the zoom keeps the view centre and cuts.
    int direct = hd_ui_subtile() && (cfg.zoom_pointer || (hd_ui_zoom_hook && hd_ui_zoom_hook(NULL)));
    if (direct) {
      int z = G(ZOOM), nz = z, n = g_pending_zoom;
      for (; n > 0 && nz < 4; n--) nz *= 2;
      for (; n < 0 && nz > 1; n++) nz /= 2;
      g_pending_zoom = 0;
      if (nz != z) {
        g_cause = "wheel";
        int px = g_ptr_x >= 0 ? g_ptr_x : WIN_SCREEN_W / 2, py = g_ptr_x >= 0 ? g_ptr_y : WIN_SCREEN_H / 2;
        if (!hd_ui_zoom_set(nz, cfg.zoom_pointer, px, py) && G(KEY_MAILBOX) == 0) {
          G(KEY_MAILBOX) = nz > z ? 'z' : 'x';   // golf's 2x mode: its own command
        }
      }
    } else if (G(KEY_MAILBOX) == 0) {
      G(KEY_MAILBOX) = g_pending_zoom > 0 ? 'z' : 'x';
      g_pending_zoom += g_pending_zoom > 0 ? -1 : 1;
      g_cause = "wheel";
    }
  }
  if (course_active(now)) log_camera();
}
