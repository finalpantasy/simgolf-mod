// Native presentation (simgolf mod, task-3): the game's 800x600 RGB555 frame
// shown sharp at any window or screen size (1080p to 8K, ultrawide), on a
// render thread of its own, so the display runs at the monitor's refresh or a
// chosen cap while the game keeps its original pace.
//
// Why a thread: golf's main loop (FUN_0040f5c0) does one simulation step and
// one draw per loop and then busy-waits until 87 ms have passed since the step
// began (0x41e631: (175 + 75 if [0x59e7b8] & 0x20000) / 2 ms, about 11.4 game
// frames per second). Game speed is counted in those loops, so they stay as
// they are. The game thread only hands each finished frame over (hd_present);
// this thread uploads, scales and presents on its own clock.
//
// Scaling ("filter"): sharp = nearest-neighbour to the next whole multiple of
// 800x600, then a bilinear step down to the screen (crisp pixels, no shimmer,
// no blur); integer = whole multiples only (borders if needed); nearest;
// smooth = plain bilinear. The 4:3 frame keeps its aspect, centred.
//
// Mouse: real SDL mouse events carry window coordinates; an event watch maps
// them to the 800x600 frame before win32.c sees them (clamped, so the black
// bars count as the screen edge and edge-scroll as the original's full-screen
// mode did). Scripted events (harness.c) have no window id and stay as given.
//
// Settings, golf.ini next to the exe; the mods store's display.<key> (a mod's
// [settings.display], mods.ini [display], the bridge's mods_set) beats it,
// SIMGOLF_DISPLAY="key=value;key=value" beats both (tests). Only smooth
// changes while the game runs; the rest is read at start:
//   hd=1          0 = the original: an 800x600 window presented as before
//   display=borderless | windowed   (headless runs are always windowed)
//   window=WxH    windowed size; default the biggest multiple of 800x600 that fits
//   filter=sharp | integer | nearest | smooth
//   vsync=1       wait for the monitor's refresh
//   fps=0         frame cap (60, 120, 144, 240, ...); 0 = no cap
//   render=WxH    draw at this size off screen (captures and benchmarks at 4K
//                 on any machine); the window shows that picture scaled
//   canvas=auto   the game's own screen, sized by ui_scale below, so wide and
//                 big screens show more of the course (title and menus stay
//                 800x600, pillarboxed); canvas=WxH fixes it (ui_scale then
//                 only follows from it), canvas=800x600 is the original framing
//   ui_scale=auto screen pixels per game pixel, for the course and golf's UI
//                 alike: auto = the screen height / 720 rounded down to a
//                 quarter, at least 1 (1080p 1.5, 1440p 2, 4K 3, 1600x900
//                 1.25), so the UI is about as big as at 720 lines and every
//                 extra pixel shows more course; fit = 600 lines high (the
//                 original size, the most zoomed in); or a number (1, 1.5, 2,
//                 2.5, 3, ...). Non-whole scales go through the sharp filter.
//   layers=1      course and UI as separate layers (golf/hd_ui.c): the course
//                 slides under a fixed UI while scrolling, and native UI
//                 (hd_ui_add_layer, the Frosted Mint cards) draws on top
//   ui_probe=0    1: a test layer through the UI layer API (a mint square
//                 24 canvas px at the bottom right of the course view)
//   smooth=1      smooth scrolling: the course layer moves to each new camera
//                 position over one game frame at the presentation rate
//                 instead of jumping a whole tile (needs layers=1), and the
//                 camera controls stop anywhere, not on whole tiles (the
//                 sub-tile camera, golf/hd_ui.c, task-37; also with hd=0)
//   smooth_zoom=1 the wheel zoom glides between golf's zoom levels (hd_zoom.c,
//                 task-56; needs smooth=1; changes live); 0 = golf's instant
//                 zoom. zoom_ms=200: the glide for one level (a zoom of 2)
//   overscan=2    with smooth=1: golf draws scenery and golfers this many tiles
//                 past the view edges, so nothing pops in or out at the edges
//                 while scrolling (golf/hd_ui.h, task-68); 0 = golf's own culling
//   edge_fix=1    with smooth=1: the edge a slide uncovers shows the picture as
//                 it was on screen, and the hi-res terrain changes on the same
//                 presented frame as the course (task-68); 0 = as before, for A/B
//   terrain=1     the course terrain drawn at screen resolution (hd_terrain_native.c)
//   art=original  the art pack (hd_art.c): original, upscaled or locked for every
//                 category; art_<category> for one; art_dir, art_cache_mb, art_pack=0
//   fpslog=path   one line per presented frame (time, frame ms, game frames)

#define WIN32_LEAN_AND_MEAN
#include "port/release_data.h"
#include <windows.h>

#include <SDL.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mods/mods.h"
#include "port/golf/hd_ui.h"
#include "port/hd_art.h"
#include "port/course_lamps_native.h"
#include "port/hud_frosted.h"
#include "port/hd_native.h"
#include "port/ui/ui_course_map.h"
#include "port/hd_present_policy.h"
#include "port/dialogs_frosted.h"
#include "port/frontend_frosted.h"
#include "port/reports_frosted.h"
#include "port/options_frosted.h"
#include "port/environment.h"
#include "port/environment_view.h"
#include "port/weather_state.h"
#include "port/weather_climate.h"
#include "port/environment_fx.h"
#include "port/environment_receiver.h"
#include "port/weather_audio.h"
#include "port/golf/weather_game.h"
#include "platform/terrain_lighting.h"
#include "port/hd_zoom.h"
#include "port/hdball_render.h"
#include "port/water_native.h"
#include "port/rainbow.h"
#include "port/host.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x2
#endif

#define FRAME_W HD_FRAME_W   // the game's canvas (canvas=WxH), 800x600 in the original
#define FRAME_H HD_FRAME_H
int hd_frame_w = 800, hd_frame_h = 600;
void win_set_screen_size(int w, int h);   // win32.c (win32.h clashes with windows.h)
int terrain_hd_seen(void);                 // terrain_hd.cpp
enum { F_SHARP, F_INTEGER, F_NEAREST, F_SMOOTH };
static const char *const k_filters[] = {"sharp", "integer", "nearest", "smooth"};

typedef struct {
  int hd, borderless, win_w, win_h, filter, vsync, fps, render_w, render_h, terrain, canvas_w, canvas_h;
  char fpslog[MAX_PATH];
  double ui_scale;   // 0 auto, -1 fit (600 high), else screen px per game px
  int layers, smooth, ui_probe;
  int smooth_zoom, zoom_ms;   // task-56
  int overscan, edge_fix;     // task-68
  int fix_zoom_clock, fix_present_pacing;
  int fix_drag_scroll;
  int camera_glide;
} Cfg;

static Cfg g_cfg = {1, 1, 0, 0, F_SHARP, 1, 0, 0, 0, 1, 0, 0, "", 0, 1, 1, 0, 1, 200, 2, 1, 1, 1, 1, 0};
static double g_ui_px = 1;            // the scale in use: output px per canvas px (course view)
static int g_active;                  // the render thread presents
static int g_mod_on = 1;              // mods.ini hd_presentation (0: the original throughout)
static SDL_Window *g_window;
static int g_start_w, g_start_h;
static unsigned g_start_flags;

// SDL's window event watcher resizes the D3D swap chain synchronously.
// The window owner must also own rendering, so maximize/restore cannot
// change GPU targets while the presenter is drawing them.
int hd_native_poll_event(SDL_Event *event) {
  return g_active ? SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) > 0
                  : SDL_PollEvent(event);
}

void hd_native_pump_events(void) {
  if (!g_active) SDL_PumpEvents();
}

// Frame hand-over (game thread -> render thread).
static CRITICAL_SECTION g_lock;
static uint16_t *g_frame;   // FRAME_W x FRAME_H: the course layer when layered, else the whole frame
static uint16_t *g_ui;      // the finished frame when layered: its UI is where it differs from g_frame
static uint8_t *g_mask;     // hdt_capture's terrain mask for g_frame
static int g_frame_course;                  // g_frame shows the course view (hdt_capture)
static int g_frame_layered;                 // g_frame/g_ui are the course and UI layers
static HdUiCamera g_frame_cam;              // the camera g_frame was drawn with (layered)
static uint8_t g_frame_note[HD_NOTE_MAX];   // task-31: what the game thread attached to g_frame (layered)
static int g_frame_note_size;
static uint8_t g_note_pending[HD_NOTE_MAX];  // game thread only: attached since the last present
static int g_note_pending_size;
static uint8_t g_frame_note2[HD_NOTE_MAX];   // task-53: the second note channel (cart path view), same life as the first
static int g_frame_note2_size;
static uint8_t g_frame_note3[HD_NOTE3_MAX];   // task-115: the third note channel (shot aim arrows)
static int g_frame_note3_size;
static LampFrame g_frame_lamps;
static uint8_t g_note3_pending[HD_NOTE3_MAX];
static int g_note3_pending_size;
static uint8_t g_note2_pending[HD_NOTE_MAX];
static int g_note2_pending_size;
static HdBallFrame g_frame_ball;            // the balls of that frame (task-41)
static EnvironmentConfig g_frame_environment;
static double g_environment_seconds;
static WeatherClimateTable g_climate_table;
static WeatherClimate g_climate;
static WeatherAirTable g_air_table;
static WeatherAir g_air;
static WeatherSurfaceTuning g_surface_tuning;
static AtmosphereTuning g_atmosphere_tuning;

static WeatherState sample_weather(const EnvironmentConfig *c, int course, double seconds, double hour) {
  WeatherState w = weather_state_sample(course && c->enabled,
    c->regional_weather ? 3 : c->weather, c->regional_weather ? c->regional_storm : c->rain_strength,
    seconds, hour, 0x53474f4cU);
  w.black_background = c->black_background;
  w.wildlife_legacy=!c->weather_wildlife;
  w.edge_aa=c->edge_aa;
  w.cloud_seed = c->cloud_seed;
  if (c->regional_weather && w.enabled) {
    w.rain = c->regional_rain; w.cloud = c->regional_cloud;
    w.wind = .08 + .22 * w.cloud + .2 * w.rain + .4 * w.storm;
    double angle = .3 + .18 * sin(seconds * .025);
    w.wind_x = cos(angle) * w.wind; w.wind_z = sin(angle) * w.wind;
  }
  AtmosphereSample a=atmosphere_sample(&c->atmosphere,seconds,hour,c->celsius,c->humidity,w.cloud,w.rain,w.wind,c->cloud_seed);
  if(c->gusts){double base=w.wind;w.wind=fmin(1,base*a.gust);w.wind_x=cos(.3)*w.wind;w.wind_z=sin(.3)*w.wind;
    w.wind_drift_x=base*cos(.3)*a.drift;w.wind_drift_z=base*sin(.3)*a.drift;w.wind_advection=1;}
  w.mist=c->mist&&c->air_valid?a.mist:0;w.haze=c->haze?a.haze:0;w.golden=c->golden_hour?a.golden:0;
  w.distant_thunder=c->distant_thunder&&c->regional_weather&&c->approaching_storm>.1&&w.storm<.12?c->atmosphere.distant_gain:0;
  w.distant_event_id=c->approach_id|0x80000000u;w.distant_age=c->approach_age-2.5;
  return w;
}
static int g_environment_running;
static volatile LONG g_frame_serial, g_game_frames;

// Course snapshot (game thread only): the back buffer when golf finished the
// course part of a frame (golf/hd_ui.c), used by the next present.
static uint16_t *g_snap;
static HdUiCamera g_snap_cam;
static int g_snap_fresh;

// Native UI layers (hd_ui_add_layer) and input handlers (hd_ui_add_input).
#define MAX_UI 12   // task-113: the debug overlay is one more layer
static struct { HdUiDraw draw; void *user; } g_ui_layers[MAX_UI];
static struct { HdUiInput input; void *user; } g_ui_inputs[MAX_UI];
static volatile LONG g_n_ui_layers, g_n_ui_inputs, g_show_golf_ui = 1;
static HdUiDraw g_top_draw;      // the top layer (hd_ui_set_top)
static HdUiInput g_top_input;
static void *g_top_user;
static volatile LONG g_shown_course;   // the render thread shows the course view (for input handlers)

// Window -> frame mapping for the mouse: gx = wx * sx - ox (under g_lock).
static float g_map_sx, g_map_sy, g_map_ox, g_map_oy;
static int g_map_ok, g_map_w, g_map_h;   // the part of the frame on screen

// Smooth zoom (task-56): the game thread announces a zoom step (zoom_hook) and
// waits for the render thread (zoom_poll) to make a snapshot of the picture on
// screen before golf draws at the new zoom; g_zoom is the glide in progress.
static CRITICAL_SECTION g_zlock;
static HdZoom g_zoom;             // under g_zlock
static HdUiZoom g_zev;            // under g_zlock: the announced step ...
static double g_zev_t;            // ... and when
static volatile LONG g_zev_serial, g_zev_result;
static HANDLE g_zev_ack;

// Screenshot requests (harness "hdshot").
static char g_shot_path[MAX_PATH];
static volatile LONG g_shot_pending;
static HANDLE g_shot_done, g_ready;
static int g_start_ok;

// ------------------------------------------------------------------ settings

static double now_s(void) { return host_now_ms() / 1000.0; }

static int parse_size(const char *v, int *w, int *h) {
  int a = 0, b = 0;
  if (sscanf(v, "%dx%d", &a, &b) == 2 && a >= 160 && b >= 120 && a <= 16384 && b <= 16384) {
    *w = a;
    *h = b;
    return 1;
  }
  return 0;
}

static void set_key(const char *k, const char *v) {
  if (!_stricmp(k, "hd")) g_cfg.hd = atoi(v) != 0;
  else if (!_stricmp(k, "display")) g_cfg.borderless = _stricmp(v, "windowed") != 0;
  else if (!_stricmp(k, "window")) parse_size(v, &g_cfg.win_w, &g_cfg.win_h);
  else if (!_stricmp(k, "render")) {
    if (!parse_size(v, &g_cfg.render_w, &g_cfg.render_h)) g_cfg.render_w = g_cfg.render_h = 0;
  } else if (!_stricmp(k, "filter")) {
    for (int i = 0; i < 4; i++)
      if (!_stricmp(v, k_filters[i])) g_cfg.filter = i;
  } else if (!_stricmp(k, "vsync")) g_cfg.vsync = atoi(v) != 0;
  else if (!_stricmp(k, "fps")) g_cfg.fps = atoi(v) < 0 ? 0 : atoi(v);
  else if (!_stricmp(k, "canvas")) {
    if (!parse_size(v, &g_cfg.canvas_w, &g_cfg.canvas_h)) g_cfg.canvas_w = g_cfg.canvas_h = 0;   // auto
  }
  else if (!_stricmp(k, "terrain")) g_cfg.terrain = atoi(v) != 0;
  else if (!_stricmp(k, "ui_scale")) {
    if (!_stricmp(v, "auto")) g_cfg.ui_scale = 0;
    else if (!_stricmp(v, "fit")) g_cfg.ui_scale = -1;
    else if (atof(v) >= 1 && atof(v) <= 16) g_cfg.ui_scale = atof(v);
  } else if (!_stricmp(k, "layers")) g_cfg.layers = atoi(v) != 0;
  else if (!_stricmp(k, "smooth")) g_cfg.smooth = atoi(v) != 0;
  else if (!_stricmp(k, "ui_probe")) g_cfg.ui_probe = atoi(v) != 0;
  else if (!_stricmp(k, "smooth_zoom")) g_cfg.smooth_zoom = atoi(v) != 0;
  else if (!_stricmp(k, "zoom_ms")) g_cfg.zoom_ms = atoi(v) < 40 ? 40 : atoi(v) > 2000 ? 2000 : atoi(v);
  else if (!_stricmp(k, "overscan")) g_cfg.overscan = atoi(v) < 0 ? 0 : atoi(v) > 8 ? 8 : atoi(v);   // task-68
  else if (!_stricmp(k, "edge_fix")) g_cfg.edge_fix = atoi(v) != 0;
  else if (!_stricmp(k, "fpslog")) snprintf(g_cfg.fpslog, sizeof g_cfg.fpslog, "%s", v);
  else if (!_strnicmp(k, "art", 3)) hd_art_set_key(k, v);   // the art pack (hd_art.c, task-17)
}

static void trim(char *s) {
  char *p = s;
  while (*p == ' ' || *p == '\t') p++;
  memmove(s, p, strlen(p) + 1);
  size_t n = strlen(s);
  while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

static void set_pair(char *line) {
  trim(line);
  if (!*line || *line == ';' || *line == '#' || *line == '[') return;
  char *eq = strchr(line, '=');
  if (!eq) return;
  *eq = 0;
  trim(line);
  trim(eq + 1);
  set_key(line, eq + 1);
}

static const char *const k_store_keys[] = {"hd", "display", "filter", "vsync", "fps", "canvas", "ui_scale", "terrain", "layers", "smooth", "smooth_zoom", "zoom_ms", "overscan", "edge_fix"};

static void read_settings(void) {
  char ini[MAX_PATH + 16], line[MAX_PATH + 32];
  GetModuleFileNameA(NULL, ini, MAX_PATH);
  char *slash = strrchr(ini, '\\');
  if (slash) strcpy(slash + 1, "golf.ini");
  FILE *f = fopen(ini, "r");
  while (f && fgets(line, sizeof line, f)) set_pair(line);
  if (f) fclose(f);
  // The mods store over golf.ini: a mod's [settings.display], mods.ini
  // [display] or the bridge's mods_set (display.ui_scale=2, ...).
  for (int i = 0; i < (int)(sizeof k_store_keys / sizeof k_store_keys[0]); i++) {
    char key[64];
    snprintf(key, sizeof key, "display.%s", k_store_keys[i]);
    const char *v = mods_get(key);
    if (v && *v) set_key(k_store_keys[i], v);
  }
  g_cfg.camera_glide = mods_get_int("camera.glide", 0) != 0;
  hd_art_read_store();
  const char *env = getenv("SIMGOLF_DISPLAY");
  if (env && *env) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", env);
    for (char *t = strtok(buf, ";,"); t; t = strtok(NULL, ";,")) {
      snprintf(line, sizeof line, "%s", t);
      set_pair(line);
    }
  }
  if (host_headless()) g_cfg.borderless = 0;   // never a full-screen window in tests
}

// ------------------------------------------------------------------ geometry

typedef struct { int x, y, w, h; } Rect;

// The place of an sw x sh picture in a cw x ch canvas, aspect kept, centred.
static Rect fit(int cw, int ch, int sw, int sh, int filter) {
  double s = fmin((double)cw / sw, (double)ch / sh);
  if (filter == F_INTEGER && s >= 1) s = floor(s + 1e-9);
  Rect r;
  r.w = (int)lround(sw * s);
  r.h = (int)lround(sh * s);
  r.x = (cw - r.w) / 2;
  r.y = (ch - r.h) / 2;
  return r;
}

volatile int hd_raw_mouse_x, hd_raw_mouse_y;   // the last window mouse event in canvas pixels, not clamped to the picture (hd_native.h)

static int SDLCALL map_mouse(void *user, SDL_Event *e) {
  (void)user;
  int *x, *y;
  if (e->type == SDL_MOUSEMOTION && e->motion.windowID) x = &e->motion.x, y = &e->motion.y;
  else if ((e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) && e->button.windowID)
    x = &e->button.x, y = &e->button.y;
  else return 1;
  EnterCriticalSection(&g_lock);
  if (g_map_ok) {
    int gx = (int)floorf(*x * g_map_sx - g_map_ox), gy = (int)floorf(*y * g_map_sy - g_map_oy);
    hd_raw_mouse_x = gx;   // before the clamp: full-screen native screens (front end) use the whole window
    hd_raw_mouse_y = gy;
    *x = gx < 0 ? 0 : gx > g_map_w - 1 ? g_map_w - 1 : gx;
    *y = gy < 0 ? 0 : gy > g_map_h - 1 ? g_map_h - 1 : gy;
  }
  LeaveCriticalSection(&g_lock);
  return 1;
}

// ------------------------------------------------------------------ render thread

// One canvas-sized picture and its whole-multiple copy for the sharp filter.
typedef struct {
  SDL_Texture *src, *pre;
  int pre_k, pre_w, pre_h;
  LONG serial, pre_serial;   // what src holds, and what pre was made from
} Layer;

typedef struct {
  SDL_Renderer *r;
  // course[cur]: the course layer (or the whole frame when not layered);
  // course[!cur]: the course layer before it, still sliding out (smooth).
  Layer course[2], overlay, ui;   // overlay: course with bare terrain cut out; ui: golf's UI (premultiplied)
  int cur, have_prev;
  int art_main, art_ui;   // the art pack's list under the overlay (-1 none), its list under golf's UI
  int layered_now;        // the frame shown is layered (course and UI)
  SDL_Texture *canvas;
  int canvas_w, canvas_h;
  int course_view;   // the course view: the whole canvas; anything else: its top-left 800x600
  int layered;       // course[cur] and ui are separate layers
  LONG shown_serial;
  SDL_BlendMode premul;
  uint32_t lut[32768];
  uint16_t *local, *local_ui;   // FRAME_W x FRAME_H
  uint8_t *mask;
  const uint8_t *cut, *cut_ui;   // hd_art_cut_mask() for the overlay and golf's UI layer
  int terrain_on;       // the terrain layer draws under the overlay
  HdBallFrame ball;   // the balls of the latest game frame (hdball_render.c)
  EnvironmentConfig environment;
  EnvironmentRenderer environment_renderer;
  EnvironmentFxState *environment_fx;
  uint8_t *weather_receiver, *weather_basins;
  LONG weather_basin_serial, weather_basin_map_serial;
  int *weather_flood;
  size_t weather_receiver_count;
  LONG weather_receiver_serial;
  double environment_seconds, environment_frame_time;
  int environment_running;
  uint64_t environment_cache_key;
  int weather_backend;
  CourseRelief relief;
  uint8_t *weather_features;
  WeatherSurfaceState surface;
  RainbowState rainbow;
  RainbowDraw rainbow_draw;
  double surface_time;
  double surface_log_time;
  HANDLE timer;
  int profile;
  double profile_terrain,profile_shadow,profile_fx;
  // smooth scrolling (canvas pixels): course[cur] is drawn at off * (1 - a),
  // a going 0 -> 1 over off_dur ms from off_t0; course[!cur] at that + prev_d.
  HdUiCamera cam;
  uint8_t note[HD_NOTE_MAX];   // the note of the frame on screen
  int note_size;
  uint8_t note2[HD_NOTE_MAX];
  int note2_size;
  uint8_t note3[HD_NOTE3_MAX];
  int note3_size;
  LampFrame lamps;
  double off_x, off_y, off_t0, off_dur, prev_dx, prev_dy;
  double last_arrival, interval;   // course frames: arrival time, interval (running mean)
  // smooth zoom (task-56). zsnap: the course picture (terrain, sprites, ball; not golf's UI) as it
  // was on screen when the zoom glide started, at rest size, with the zoom and camera it was drawn at.
  SDL_Texture *zsnap;
  int zsnap_w, zsnap_h, zsnap_ok, zsnap_zoom;
  LONG zsnap_serial, zev_seen;
  double zsnap_cx, zsnap_cy;
  HdZoom zg;           // this frame's copy of g_zoom
  int zon;             // the glide shapes this frame
  double zs, zx, zy;   // the live course picture's place this frame: screen = (zx, zy) + zs * canvas pixel
  int tw, th;          // the render target size of the last frame
  SDL_Texture *pix;    // Pixel HD (task-71): the course picture on a coarse grid, enlarged with the nearest filter
  int pix_w, pix_h;
  // task-68: the course picture as it was on screen last frame (terrain, hi-res
  // sprites, overlay, ball; not golf's UI), at offset slast_o from the place of
  // course layer slast_serial. A slide fills the edge it uncovers from this,
  // not from the bare course layer, so nothing there changes when a game frame lands.
  SDL_Texture *slast;
  int slast_w, slast_h, slast_ok;
  LONG slast_serial;
  double slast_ox, slast_oy;
  // task-68: harness "hddump": the course part of each presented frame, made small
  SDL_Texture *dump_tex;
  int dump_w, dump_h;
  FILE *dump_f, *dump_csv;
  long dump_n;
} Rt;
static int weather_receiver(Rt *t);
static double environment_time(const Rt *t,double now);

static void draw_reflections(Rt *t,SDL_Rect clip,double x,double y,double k,double now){
 if(!t->environment.enabled||t->environment.reflections<=0||!weather_receiver(t))return;
 SDL_Rect old,combined;SDL_bool clipped=SDL_RenderIsClipEnabled(t->r);SDL_RenderGetClipRect(t->r,&old);
 if(clipped){if(!SDL_IntersectRect(&old,&clip,&combined))return;clip=combined;}
 SDL_RenderSetClipRect(t->r,&clip);
 EnvironmentSample sun=environment_sample(&t->environment,environment_time(t,now));
 double strength=t->environment.reflections*(.2+.8*sun.daylight)*(1-.45*sun.rain);
 hd_art_reflections(t->r,t->weather_receiver,FRAME_W,FRAME_H,x,y,k,
   t->environment_seconds,strength,t->environment.reflection_ripple);
 SDL_RenderSetClipRect(t->r,clipped?&old:NULL);
}

enum { FILL_FRAME, FILL_OVERLAY, FILL_UI };

static void fill(Rt *t, Layer *L, int mode, LONG serial) {
  void *pixels;
  int pitch;
  if (SDL_LockTexture(L->src, NULL, &pixels, &pitch)) return;
  for (int y = 0; y < FRAME_H; y++) {
    uint32_t *d = (uint32_t *)((char *)pixels + (size_t)y * pitch);
    const uint16_t *s = t->local + y * FRAME_W;
    const uint8_t *m = t->mask + y * FRAME_W;
    const uint16_t *u = t->local_ui + y * FRAME_W;
    if (mode == FILL_FRAME)
      for (int x = 0; x < FRAME_W; x++) d[x] = t->lut[s[x] & 0x7fff];
    else if (mode == FILL_OVERLAY) {   // bare terrain: clear; terrain in shadow: half black; the rest as drawn
      const uint8_t *c = t->cut ? t->cut + y * FRAME_W : NULL;   // pixels of hi-res sprites (hd_art.c): clear
      int ter = t->terrain_on;
      for (int x = 0; x < FRAME_W; x++)
        d[x] = c && c[x] ? 0 : !ter ? t->lut[s[x] & 0x7fff] : m[x] == 255 ? 0 : m[x] == 128 ? 0x80000000u : t->lut[s[x] & 0x7fff];
    }
    else {   // golf's UI: what the finished frame has on top of the course, less its hi-res sprites
      const uint8_t *c = t->cut_ui ? t->cut_ui + y * FRAME_W : NULL;
      for (int x = 0; x < FRAME_W; x++) d[x] = u[x] != s[x] && !(c && c[x]) ? t->lut[u[x] & 0x7fff] : 0;
    }
  }
  SDL_UnlockTexture(L->src);
  L->serial = serial;
}

// Where course[cur] is now, relative to its place (canvas pixels).
static void slide_offset(const Rt *t, double now, double *x, double *y) {
  double a = t->off_dur > 0 ? (now - t->off_t0) / t->off_dur : 1;
  a = a < 0 ? 0 : a > 1 ? 1 : a;
  *x = t->off_x * (1 - a);
  *y = t->off_y * (1 - a);
}

static void fill_art(Rt *t, LONG serial);

static void upload(Rt *t, double now) {
  LONG serial = g_frame_serial;
  if (serial == t->shown_serial) return;
  HdUiCamera before = t->cam;
  int was_layered = t->layered && t->course_view;
  EnterCriticalSection(&g_lock);
  memcpy(t->local, g_frame, (size_t)FRAME_W * FRAME_H * 2);
  t->course_view = g_frame_course;
  t->layered = g_frame_layered;
  if (t->course_view) memcpy(t->mask, g_mask, (size_t)FRAME_W * FRAME_H);
  if (t->layered) {
    memcpy(t->local_ui, g_ui, (size_t)FRAME_W * FRAME_H * 2);
    t->cam = g_frame_cam;
  }
  hd_art_take();   // the art pack's list for this frame
  t->note_size = t->layered && t->course_view ? g_frame_note_size : 0;
  if (t->note_size > 0) memcpy(t->note, g_frame_note, (size_t)t->note_size);
  t->note2_size = t->layered && t->course_view ? g_frame_note2_size : 0;
  if (t->note2_size > 0) memcpy(t->note2, g_frame_note2, (size_t)t->note2_size);
  t->note3_size = t->layered && t->course_view ? g_frame_note3_size : 0;
  if (t->note3_size > 0) memcpy(t->note3, g_frame_note3, (size_t)t->note3_size);
  t->lamps=g_frame_lamps;
  memcpy(&t->ball, &g_frame_ball, sizeof t->ball);
  t->environment = g_frame_environment;
  t->environment_seconds = g_environment_seconds;
  t->environment_running = g_environment_running;
  t->environment_frame_time = now;
  serial = g_frame_serial;
  LeaveCriticalSection(&g_lock);
  hd_art_prepare(t->r, 1);

  // Smooth scrolling: a camera step of a few tiles at the same zoom and
  // rotation becomes a slide from where the course is on screen now.
  int layered = t->layered && t->course_view;
  // Pause and modal redraws can briefly publish an unlayered frame. Moisture
  // belongs to the weather session and must survive those presentation changes.
  if (!t->environment.enabled) { memset(&t->surface, 0, sizeof t->surface);memset(&t->rainbow,0,sizeof t->rainbow); t->surface_time = 0; }
  hdball_ingest(&t->ball, &t->cam, layered, now);   // task-41: the round ball's positions
  double cx, cy;
  slide_offset(t, now, &cx, &cy);
  if (layered) {
    if (t->last_arrival > 0) {
      double iv = now - t->last_arrival;
      if (iv > 20 && iv < 400) t->interval = t->interval > 0 ? t->interval * 0.8 + iv * 0.2 : iv;
    }
    t->last_arrival = now;
  }
  // the sub-tile camera's offset moves the content as well (task-37)
  int mx = t->cam.x - before.x, my = t->cam.y - before.y, mo = t->cam.ox != before.ox || t->cam.oy != before.oy;
  double dx = mx * t->cam.ax + my * t->cam.bx + (t->cam.ox - before.ox),
         dy = mx * t->cam.ay + my * t->cam.by + (t->cam.oy - before.oy);
  // Manual drags can cross many tiles in either direction between game
  // frames. Keep the displayed camera continuous while the views overlap.
  // The compositor replays the guard band in disoccluded corners, so a
  // diagonal must not cancel a slide.
  int manual = g_cfg.fix_drag_scroll && t->cam.drag_serial != before.drag_serial;
  int nearby = abs(mx) <= 4 && abs(my) <= 4;
  if (manual && !nearby)
    nearby = fabs(cx - dx) < FRAME_W && fabs(cy - dy) < FRAME_H;
  int slide = g_cfg.smooth && layered && was_layered && t->cam.zoom == before.zoom && t->cam.rot == before.rot &&
              (mx || my || mo) && nearby;
  if (slide) {
    t->cur ^= 1;   // the course on screen becomes the one sliding out
    t->have_prev = 1;
    t->off_x = cx - dx;
    t->off_y = cy - dy;
    t->prev_dx = dx;
    t->prev_dy = dy;
    t->off_t0 = now;
    // Direct dragging catches up within one game frame. The optional glide
    // retains the accepted softer trail. Other camera movement is unchanged.
    t->off_dur = t->interval > 0 ? t->interval : 87;
    if (!manual || g_cfg.camera_glide) t->off_dur *= 1.2;
  } else if (layered && was_layered && g_cfg.smooth && t->cam.zoom == before.zoom && t->cam.rot == before.rot &&
             !mx && !my && !mo) {
    // same camera: the new picture takes the old one's place mid-slide
    double left = t->off_dur - (now - t->off_t0);
    t->off_x = cx;
    t->off_y = cy;
    t->off_t0 = now;
    t->off_dur = left > 1 ? left : 0;
  } else {
    t->have_prev = 0;
    t->off_x = t->off_y = 0;
    t->off_dur = 0;
  }
  fill(t, &t->course[t->cur], FILL_FRAME, serial);
  t->layered_now = layered;
  fill_art(t, serial);
  t->shown_serial = serial;
}

// The layers that depend on the art pack's lists: again when textures
// arrive for a frame that is still shown (a paused game, a slow load).
static void fill_art(Rt *t, LONG serial) {
  int layered = t->layered_now;
  // The art pack (hd_art.c): with layers, its course list goes under the
  // course overlay and its top list under golf's UI layer; without, its one
  // list covers the whole picture (title, menus, reports).
  const uint8_t *cc = hd_art_cut_mask(HD_ART_COURSE), *ct = hd_art_cut_mask(HD_ART_TOP);
  t->cut = layered ? cc : ct;
  t->art_main = t->cut ? (layered ? HD_ART_COURSE : HD_ART_TOP) : -1;
  t->cut_ui = layered ? ct : NULL;
  t->art_ui = t->cut_ui != NULL;
  t->terrain_on = t->course_view && g_cfg.terrain;
  if (t->terrain_on || t->cut) fill(t, &t->overlay, FILL_OVERLAY, serial);
  if (layered) fill(t, &t->ui, FILL_UI, serial);
}

// Draws the top-left sw x sh of one layer at d in target with the given blend mode.
static void draw_layer_ref(Rt *t, Layer *L, SDL_Texture *target, Rect d, int sw, int sh, SDL_BlendMode blend, int ref_w) {
  int filter = g_cfg.filter;
  SDL_Rect dst = {d.x, d.y, d.w, d.h}, src = {0, 0, sw, sh};
  // ref_w: the width the picture has at rest. A zoom glide (task-56) draws it
  // bigger or smaller; the whole-multiple copy and the blocky look follow the
  // width at rest, so the glide does not rebuild the copy every frame.
  int k = (int)ceil((double)ref_w / sw - 1e-6);
  if (filter == F_SHARP && k >= 2 && ref_w != k * sw) {
    if (k > 8) k = 8;
    if (L->pre_k != k || L->pre_w != sw || L->pre_h != sh) {
      if (L->pre) SDL_DestroyTexture(L->pre);
      L->pre = SDL_CreateTexture(t->r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, sw * k, sh * k);
      L->pre_k = L->pre ? k : 0;
      L->pre_w = sw;
      L->pre_h = sh;
      L->pre_serial = -1;
      if (L->pre) SDL_SetTextureScaleMode(L->pre, SDL_ScaleModeLinear);
    }
    if (L->pre) {
      if (L->pre_serial != L->serial) {
        SDL_Rect clip;
        int clipped = SDL_RenderIsClipEnabled(t->r);
        if (clipped) SDL_RenderGetClipRect(t->r, &clip);
        SDL_SetRenderTarget(t->r, L->pre);
        SDL_SetTextureScaleMode(L->src, SDL_ScaleModeNearest);
        SDL_SetTextureBlendMode(L->src, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(t->r, L->src, &src, NULL);
        L->pre_serial = L->serial;
        SDL_SetRenderTarget(t->r, target);
        if (clipped) SDL_RenderSetClipRect(t->r, &clip);
      }
      SDL_SetTextureBlendMode(L->pre, blend);
      SDL_RenderCopy(t->r, L->pre, NULL, &dst);
      return;
    }
  }
  // Whole multiples and the nearest filter stay blocky; anything else blends.
  int nearest = d.w == ref_w && (filter == F_NEAREST || filter == F_INTEGER || (filter == F_SHARP && d.w % sw == 0));
  SDL_SetTextureScaleMode(L->src, nearest ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
  SDL_SetTextureBlendMode(L->src, blend);
  SDL_RenderCopy(t->r, L->src, &src, &dst);
}

static void draw_layer(Rt *t, Layer *L, SDL_Texture *target, Rect d, int sw, int sh, SDL_BlendMode blend) {
  draw_layer_ref(t, L, target, d, sw, sh, blend, d.w);
}

// The part of the frame on screen: all of it in the course view; for title,
// menus and full-screen pages, which golf lays out in 800x600, the top-left
// 800x600 (shown pillarboxed, as the original looks on a wide monitor).
static void source_size(int course, int *sw, int *sh) {
  *sw = course || FRAME_W < 800 ? FRAME_W : 800;
  *sh = course || FRAME_H < 600 ? FRAME_H : 600;
}

static Rect moved(Rect d, double dx, double dy) {
  d.x += (int)lround(dx);
  d.y += (int)lround(dy);
  return d;
}

// ------------------------------------------------------------------ smooth zoom (task-56)

// The live course picture (course layer, terrain at full resolution, hi-res
// sprites, the round ball) with canvas pixel p at screen s * p + (x, y) (canvas
// px, relative to d), d being its place at rest.
static double environment_time(const Rt *t, double now) {
  return t->environment_seconds + (t->environment_running ? fmin(0.25, fmax(0, (now - t->environment_frame_time) / 1000)) : 0);
}

static void draw_cast_shadows(Rt *t, SDL_Rect clip, double x, double y, double scale,
                              int terrain, double now, int snapshot) {
  double started=t->profile?host_now_ms():0;
  EnvironmentSample sun = environment_sample(&t->environment, environment_light_seconds(&t->environment,environment_time(t, now)));
  double dx = 0, dy = 0;
  int visible = environment_shadow_slope(&sun, t->cam.ax, t->cam.ay, t->cam.bx, t->cam.by, &dx, &dy);
  int enabled = t->layered && t->course_view && terrain && t->environment.enabled && t->environment.cast_shadows;
  // Snapshots retain a clean base. At night replacement stays enabled so
  // the art's fixed midday shadows cannot reappear as the sun fades away.
  double opacity = visible && !snapshot ? t->environment.atmosphere.shadow_opacity * sun.sun_strength * environment_shadow_transmission(&t->environment,&sun) : 0;
  double ground_width = hypot(t->cam.ax, t->cam.bx);
  double ground_aspect = ground_width > 1e-9 ? hypot(t->cam.ay, t->cam.by) / ground_width : 0.5;
  hd_art_lighting(&t->environment,&sun,t->cam.ax,t->cam.ay,t->cam.bx,t->cam.by);
  WeatherState wind=sample_weather(&t->environment,1,environment_time(t,now),sun.hour);
  hd_art_wind(wind.enabled&&t->environment.gusts,wind.seconds,wind.wind_x);
  int cast_count=hd_art_draw_shadows(t->r, clip, x, y, scale, t->mask, FRAME_W, FRAME_H,
                      enabled, dx, dy, opacity, ground_aspect);
  if(getenv("SIMGOLF_ATMOSPHERE_LOG")){
    static double last_cast_log;
    if(now-last_cast_log>1000){last_cast_log=now;fprintf(stderr,"weather: solar shadows enabled %d drawn %d slope %.3f %.3f opacity %.3f\n",enabled,cast_count,dx,dy,opacity);}
  }
  if(!snapshot)hd_art_enclosure_ground(t->r,t->mask,FRAME_W,FRAME_H,x,y,scale);
  if(t->profile)t->profile_shadow+=host_now_ms()-started;
}

static void draw_live(Rt *t, SDL_Texture *target, Rect d, double s, double x, double y, int sw, int sh,
                      SDL_Texture *terrain, double now, int snapshot) {
  double k = (double)d.w / sw, kk = k * s;
  Rect cd = {d.x + (int)lround(x * k), d.y + (int)lround(y * k), (int)lround(d.w * s), (int)lround(d.h * s)};
  draw_layer_ref(t, &t->course[t->cur], target, cd, sw, sh, SDL_BLENDMODE_NONE, d.w);
  if (terrain) {
    SDL_Rect dst = {cd.x, cd.y, cd.w, cd.h};
    SDL_RenderCopy(t->r, terrain, NULL, &dst);
    water_draw(t->r, dst.x, dst.y, dst.w, dst.h, now);   // task-78: the water over the ground, under golf's sprites
  }
  SDL_Rect shadow_clip = {d.x, d.y, d.w, d.h};
  draw_cast_shadows(t, shadow_clip, cd.x, cd.y, kk, terrain != NULL, now, snapshot);
  if(!snapshot)draw_reflections(t,shadow_clip,cd.x,cd.y,kk,now);
  if (t->art_main >= 0) hd_art_draw(t->art_main, t->r, cd.x, cd.y, kk);
  if (terrain || t->art_main >= 0) draw_layer_ref(t, &t->overlay, target, cd, sw, sh, t->premul, d.w);
  if (t->course_view) {
    SDL_Rect clip = {d.x, d.y, d.w, d.h};
    hdball_draw(t->r, cd.x, cd.y, &clip, (float)kk, sw, sh, now);
  }
}

// The picture on screen as the glide began, in a texture of its own: golf's
// next picture (the new zoom) replaces the layers it is made of.
static int snapshot_course(Rt *t, double now) {
  int sw, sh;
  source_size(1, &sw, &sh);
  if (t->tw < 1 || t->th < 1) return 0;
  Rect d = fit(t->tw, t->th, sw, sh, g_cfg.filter);
  if (!t->zsnap || t->zsnap_w != d.w || t->zsnap_h != d.h) {
    if (t->zsnap) SDL_DestroyTexture(t->zsnap);
    t->zsnap = SDL_CreateTexture(t->r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, d.w, d.h);
    t->zsnap_w = d.w;
    t->zsnap_h = d.h;
    if (!t->zsnap) return 0;
    SDL_SetTextureBlendMode(t->zsnap, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(t->zsnap, SDL_ScaleModeLinear);
  }
  SDL_Texture *terrain = g_cfg.terrain ? hdt_render(t->r, d.w, d.h, g_cfg.edge_fix ? t->shown_serial : LONG_MAX) : NULL;
  SDL_Texture *was = SDL_GetRenderTarget(t->r);
  SDL_SetRenderTarget(t->r, t->zsnap);
  SDL_RenderSetClipRect(t->r, NULL);
  SDL_SetRenderDrawColor(t->r, 0, 0, 0, 255);
  SDL_RenderClear(t->r);
  Rect z = {0, 0, d.w, d.h};
  draw_live(t, t->zsnap, z, 1, 0, 0, sw, sh, terrain, now, 1);
  SDL_RenderSetClipRect(t->r, NULL);
  SDL_SetRenderTarget(t->r, was);
  t->zsnap_zoom = t->cam.zoom;
  hdz_frame_cam(&t->cam, &t->zsnap_cx, &t->zsnap_cy);
  t->zsnap_serial = t->shown_serial;
  t->zsnap_ok = 1;
  return 1;
}

// The course while the glide runs: golf's latest picture and the snapshot,
// each scaled and placed to fit the visible view; the one shown larger goes
// first (it covers the screen), the sharper one over it.
static void glide_course(Rt *t, SDL_Texture *target, Rect d, int sw, int sh, SDL_Texture *terrain, double now) {
  double k = (double)d.w / sw, fcx, fcy, s, x, y;
  hdz_frame_cam(&t->cam, &fcx, &fcy);
  hdz_xform(&t->zg, t->zg.last_t, t->cam.zoom, fcx, fcy, &s, &x, &y);
  t->zs = s, t->zx = x, t->zy = y;
  SDL_Rect clip = {d.x, d.y, d.w, d.h};
  SDL_RenderSetClipRect(t->r, &clip);
  // A clean snapshot cannot cover the live cast pass. With dynamic shadows
  // the current scene alone follows the same glide transform.
  int back = t->zsnap_ok && t->zsnap && t->zsnap_zoom != t->cam.zoom &&
             !(t->environment.enabled && (t->environment.cast_shadows||t->environment.reflections>0));
  double sb = 0, xb = 0, yb = 0;
  if (back) hdz_xform(&t->zg, t->zg.last_t, t->zsnap_zoom, t->zsnap_cx, t->zsnap_cy, &sb, &xb, &yb);
  SDL_Rect bd = {d.x + (int)lround(xb * k), d.y + (int)lround(yb * k), (int)lround(d.w * sb), (int)lround(d.h * sb)};
  if (back && sb > s) SDL_RenderCopy(t->r, t->zsnap, NULL, &bd);
  draw_live(t, target, d, s, x, y, sw, sh, terrain, now, 0);
  if (back && sb <= s) SDL_RenderCopy(t->r, t->zsnap, NULL, &bd);
  SDL_RenderSetClipRect(t->r, NULL);
}

// Render thread, before the next frame is taken: golf announced a zoom step
// (zoom_hook). The picture on screen is snapshotted while golf's layers still
// show it, the glide starts, and the game thread, which waits, goes on.
static void zoom_poll(Rt *t, double now) {
  LONG serial = g_zev_serial;
  if (serial == t->zev_seen) return;
  t->zev_seen = serial;
  HdUiZoom e;
  HdZoom g;
  double issued;
  EnterCriticalSection(&g_zlock);
  e = g_zev;
  issued = g_zev_t;
  g = g_zoom;
  LeaveCriticalSection(&g_zlock);
  int ok = 0;
  if (g_cfg.smooth_zoom && g_cfg.smooth && t->layered && t->course_view && t->shown_serial >= 0 && now - issued < 200 &&
      t->cam.rot == e.rot && t->cam.zoom >= 1 && t->tw > 0) {
    double v = t->cam.zoom, vcx, vcy;
    double tnow = g.active ? g.last_t : now;   // the view the last frame showed
    if (g.active) hdz_view(&g, tnow, &v, &vcx, &vcy);
    else t->zsnap_ok = 0;
    // The snapshot must cover the screen at the zoom being shown (its zoom at most v): the frame on screen
    // when it fits, else the one the glide already holds.
    int live_fits = t->cam.zoom <= v + 1e-6, back_fits = t->zsnap_ok && t->zsnap_zoom <= v + 1e-6;
    int take = !back_fits || (live_fits && t->cam.zoom >= t->zsnap_zoom);
    int have = 1;
    if (take && !(t->zsnap_ok && t->zsnap_serial == t->shown_serial)) have = snapshot_course(t, now);
    if (have) {
      double sx = 0, sy = 0;
      if (!g.active) slide_offset(t, now, &sx, &sy);
      hdz_begin(&g, tnow, &t->cam, sx, sy, &e, FRAME_W, FRAME_H, g_cfg.zoom_ms);
      EnterCriticalSection(&g_zlock);
      g_zoom = g;
      LeaveCriticalSection(&g_zlock);
      t->have_prev = 0;   // the smooth scroll's slide is over: the glide places the picture
      t->off_x = t->off_y = t->off_dur = 0;
      ok = 1;
      fprintf(stderr, "zoom: %.1f s: glide to %d (view %.2f to %.2f over %.0f ms, camera %.2f,%.2f, snapshot of zoom %d)\n",
              now / 1000.0, e.z1, g.v0, g.v1, g.dur, e.cx1, e.cy1, t->zsnap_zoom);
    }
  }
  InterlockedExchange(&g_zev_result, ok);
  SetEvent(g_zev_ack);
}

// Render thread, after the frame is taken: keeps the glide going, ends it when
// golf's picture at the new zoom is there and the time is up.
static void zoom_settle(Rt *t, double now) {
  t->zon = 0;
  EnterCriticalSection(&g_zlock);
  HdZoom *g = &g_zoom;
  if (g->active) {
    int live_ok = t->layered && t->course_view && g_cfg.smooth_zoom;
    if (!live_ok || now > g->born + g->dur + 1500) {
      g->active = 0;   // not the course any more, or golf's picture never came
    } else {
      if (t->cam.zoom == g->z_end) hdz_frame_cam(&t->cam, &g->c1x, &g->c1y);   // the end view is golf's camera, wherever the controls have taken it
      // The view cannot show more than golf's pictures hold: zooming out has nothing to show beyond the
      // picture on screen until golf's picture at the new zoom is there. The clock waits for it.
      double floor = t->cam.zoom, v, cx, cy;
      if (t->zsnap_ok && t->zsnap_zoom < floor) floor = t->zsnap_zoom;
      // A stall (golf's new picture takes a while to upload) must not eat the glide: the clock gives back
      // all but one frame of it.
      double clock_before = g->t0;
      hdz_wait_frame(g, now, g_cfg.fix_zoom_clock);
      hdz_view(g, now, &v, &cx, &cy);
      if (v < floor - 1e-6) {
        hdz_wait_picture(g, now, clock_before, g_cfg.fix_zoom_clock);
        hdz_view(g, now, &v, &cx, &cy);
        if (v < floor - 1e-6) g->t0 = now;
      }
      g->last_t = now;
      if (t->cam.zoom == g->z_end && hdz_over(g, now)) g->active = 0;
    }
    if (g->active) {
      t->zg = *g;
      t->zon = 1;
    }
  }
  if (!g->active) t->zsnap_ok = 0;
  LeaveCriticalSection(&g_zlock);
}

// ------------------------------------------------------------------ frame dump (task-68)

// harness.c "hddump <path>|off": while on, the course part of every presented
// frame (before golf's UI and the native UI layers), scaled to 640 px wide,
// goes to <path> as raw RGB24, one line per frame in <path>.csv. Tests only:
// the edge popping measure (tests/native/pan_edges.sh).
static volatile LONG g_dump_on;
static char g_dump_path[260];

static void dump_course(Rt *t, SDL_Texture *target, Rect d, double ox, double oy, double now) {
  if (!g_dump_on) {
    if (t->dump_f) {
      fclose(t->dump_f);
      fclose(t->dump_csv);
      t->dump_f = t->dump_csv = NULL;
      fprintf(stderr, "hd: dump closed, %ld frames\n", t->dump_n);
    }
    return;
  }
  if (!target || d.w < 1) return;
  int dw = 640, dh = (int)lround(640.0 * d.h / d.w);
  if (!t->dump_f) {
    char csv[280];
    snprintf(csv, sizeof csv, "%s.csv", g_dump_path);
    t->dump_f = fopen(g_dump_path, "wb");
    t->dump_csv = fopen(csv, "w");
    if (!t->dump_f || !t->dump_csv) {
      if (t->dump_f) fclose(t->dump_f);
      if (t->dump_csv) fclose(t->dump_csv);
      t->dump_f = t->dump_csv = NULL;
      InterlockedExchange(&g_dump_on, 0);
      fprintf(stderr, "hd: dump %s FAILED\n", g_dump_path);
      return;
    }
    fprintf(t->dump_csv, "w=%d,h=%d\nn,t_ms,serial,slide_x,slide_y,cam_x,cam_y,cam_ox,cam_oy,have_prev,zoom\n", dw, dh);
    t->dump_n = 0;
  }
  if (!t->dump_tex || t->dump_w != dw || t->dump_h != dh) {
    if (t->dump_tex) SDL_DestroyTexture(t->dump_tex);
    t->dump_tex = SDL_CreateTexture(t->r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, dw, dh);
    t->dump_w = dw;
    t->dump_h = dh;
    if (!t->dump_tex) return;
  }
  static uint8_t *px;
  static size_t cap;
  if (cap < (size_t)dw * dh * 3) {
    free(px);
    cap = (size_t)dw * dh * 3;
    px = malloc(cap);
  }
  SDL_Rect src = {d.x, d.y, d.w, d.h};
  SDL_BlendMode was;
  SDL_ScaleMode scale;
  SDL_GetTextureBlendMode(target, &was);
  SDL_GetTextureScaleMode(target, &scale);
  SDL_SetRenderTarget(t->r, t->dump_tex);
  SDL_RenderSetClipRect(t->r, NULL);
  SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE);
  SDL_SetTextureScaleMode(target, SDL_ScaleModeLinear);
  SDL_RenderCopy(t->r, target, &src, NULL);
  int ok = px && !SDL_RenderReadPixels(t->r, NULL, SDL_PIXELFORMAT_RGB24, px, dw * 3);
  SDL_SetRenderTarget(t->r, target);
  SDL_SetTextureBlendMode(target, was);
  SDL_SetTextureScaleMode(target, scale);
  if (!ok) return;
  fwrite(px, 1, (size_t)dw * dh * 3, t->dump_f);
  fprintf(t->dump_csv, "%ld,%.3f,%ld,%.2f,%.2f,%d,%d,%d,%d,%d,%d\n", t->dump_n++, now, (long)t->shown_serial, ox, oy,
          t->cam.x, t->cam.y, t->cam.ox, t->cam.oy, t->have_prev, t->cam.zoom);
}

// task-68: keeps the course part of the picture just drawn (target's rect d)
// for the next slide's uncovered edge.
static void keep_course(Rt *t, SDL_Texture *target, Rect d, double ox, double oy) {
  if (!t->slast || t->slast_w != d.w || t->slast_h != d.h) {
    if (t->slast) SDL_DestroyTexture(t->slast);
    t->slast = SDL_CreateTexture(t->r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, d.w, d.h);
    t->slast_w = d.w;
    t->slast_h = d.h;
    t->slast_ok = 0;
    if (!t->slast) return;
    SDL_SetTextureBlendMode(t->slast, SDL_BLENDMODE_NONE);
  }
  SDL_Rect src = {d.x, d.y, d.w, d.h};
  SDL_BlendMode was;
  SDL_GetTextureBlendMode(target, &was);
  SDL_SetRenderTarget(t->r, t->slast);
  SDL_RenderSetClipRect(t->r, NULL);
  SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE);
  SDL_RenderCopy(t->r, target, &src, NULL);
  SDL_SetRenderTarget(t->r, target);
  SDL_SetTextureBlendMode(target, was);
  t->slast_ok = 1;
  t->slast_serial = t->course[t->cur].serial;
  t->slast_ox = ox;
  t->slast_oy = oy;
}

// Draws the picture into target (cw x ch); returns where the frame went.
// Course view: the course layer, the terrain at full resolution over it, the
// course again with its bare terrain cut out (hd_terrain_native.c), then
// golf's UI layer and the native UI layers. While scrolling smoothly the
// course parts sit at the slide offset and the course before them fills the
// edge they uncover.
static int weather_receiver(Rt *t) {
  size_t count = (size_t)FRAME_W * FRAME_H;
  if (!count || count > 4u * 1024u * 1024u) return 0;
  if (!t->weather_receiver || !t->weather_flood || !t->weather_basins || t->weather_receiver_count != count) {
    free(t->weather_receiver); free(t->weather_flood); free(t->weather_basins);free(t->weather_features);
    t->weather_receiver = malloc(count);
    t->weather_basins = malloc(count);
    t->weather_basin_serial = t->weather_basin_map_serial = -1;
    t->weather_flood = malloc(count * sizeof(int));
    t->weather_features=malloc(count*3);
    t->weather_receiver_serial = -1;
    if (!t->weather_receiver || !t->weather_flood || !t->weather_basins) return 0;
    t->weather_receiver_count = count;
  }
  if (t->weather_receiver_serial == t->shown_serial) return 1;
  if (!environment_receiver_build(t->weather_receiver, t->weather_flood, FRAME_W, FRAME_H,
                                  t->local, t->mask)) return 0;
  HdtScene scene;
  if(g_cfg.terrain&&hdt_scene(&scene)&&scene.serial<=t->shown_serial){
    if(t->weather_basin_serial!=scene.serial||t->weather_basin_map_serial!=scene.map_serial){
      int nv=0;for(int i=0;i<scene.nbatches;i++){int end=scene.batches[i*5]+scene.batches[i*5+1];if(end>nv)nv=end;}
      memset(t->weather_basins,127,count);
      atmosphere_receivers(t->weather_basins,FRAME_W,FRAME_H,scene.verts,scene.world,
        scene.batches,scene.nbatches,nv,scene.tw,scene.th,scene.wheight,scene.map_w,scene.map_h);
      t->weather_basin_serial=scene.serial;t->weather_basin_map_serial=scene.map_serial;
    }
    // Sprite motion only reclassifies receivers. Static terrain retention is reused.
    for(size_t p=0;p<count;p++)if(t->weather_receiver[p]==127)t->weather_receiver[p]=t->weather_basins[p];
  }
  if(t->weather_features&&t->relief.features&&t->relief.width==FRAME_W&&t->relief.height==FRAME_H){
    memcpy(t->weather_features,t->relief.features,count*3);
    hd_art_fog_features(t->weather_features,FRAME_W,FRAME_H,t->relief.height_pixels*28);
  }
  t->weather_receiver_serial = t->shown_serial;
  return 1;
}

static Rect compose(Rt *t, SDL_Texture *target, int cw, int ch, int *sw, int *sh, double now) {
  t->profile_terrain=t->profile_shadow=t->profile_fx=0;
  EnvironmentSample sun = environment_sample(&t->environment, environment_light_seconds(&t->environment,environment_time(t, now)));
  int lighting = t->course_view && t->layered && t->environment.enabled && t->environment.lighting;
  terrain_lighting_sky((float)sun.daylight);
  terrain_lighting_ambient((float)t->environment.ambient_fill);
  terrain_lighting_linear(t->environment.linear_light);
  terrain_lighting_set(lighting, t->environment.terrain_shadows, t->environment.occlusion, t->environment.quality,
                        (float)sun.sun_x, (float)sun.sun_y, (float)sun.sun_z, (float)(sun.sun_strength*(t->environment.weather_shadows?environment_shadow_transmission(&t->environment,&sun):1)));
  uint64_t key = t->environment.enabled ? 1u + (unsigned)(sun.hour * 12) * 512u + (t->environment.linear_light?256u:0u) +
    (unsigned)(t->environment.weather & 3) * 64u + (unsigned)(t->environment.quality & 3) * 16u +
    t->environment.cast_shadows + t->environment.lighting * 2 + t->environment.terrain_shadows * 4 + t->environment.occlusion * 8 : 0;
  if(t->environment.enabled){
    double enclosure=isfinite(t->environment.enclosure_shade)?fmax(0,fmin(1,t->environment.enclosure_shade)):0;
    double material=isfinite(t->environment.material_wetness)?fmax(0,fmin(1,t->environment.material_wetness)):0;
    double mist=isfinite(t->environment.valley_mist)?fmax(0,fmin(1,t->environment.valley_mist)):0;
    key|=(uint64_t)(t->environment.relief_shadows!=0)<<24|(uint64_t)(t->environment.weather_shadows!=0)<<25|
      (uint64_t)lround(enclosure*255)<<26|(uint64_t)lround(material*255)<<34|(uint64_t)lround(mist*255)<<42;
  }
  if (key != t->environment_cache_key) {
    t->environment_cache_key = key;
    t->slast_ok = t->zsnap_ok = 0;
  }
  source_size(t->course_view, sw, sh);
  Rect d = fit(cw, ch, *sw, *sh, g_cfg.filter);
  double terrain_started=t->profile?host_now_ms():0;
  SDL_Texture *terrain = t->course_view && g_cfg.terrain ? hdt_render(t->r, d.w, d.h, g_cfg.edge_fix ? t->shown_serial : LONG_MAX) : NULL;
  HdtScene relief_scene;
  int geometry=t->environment.enabled&&terrain&&hdt_scene(&relief_scene);
  if(geometry&&(t->environment.relief_shadows||t->environment.material_wetness>0||t->environment.valley_mist>0)){
    if(!course_relief_build(&t->relief,&relief_scene,FRAME_W,FRAME_H))course_relief_destroy(&t->relief);
  }
  hd_art_relief(geometry?&t->relief:NULL,t->environment.relief_shadows);
  if(getenv("SIMGOLF_DEPTH_LOG")&&geometry){static double last;
    if(now-last>1000){last=now;fprintf(stderr,"depth: scene %ld pixels_per_y %.6f relief %.4f active %d enclosure %.3f material %.3f valley %.3f shadow_softness %.3f transmission %.4f\n",
      t->relief.serial,t->relief.height_pixels,t->relief.max_lift,t->environment.relief_shadows,t->environment.enclosure_shade,t->environment.material_wetness,t->environment.valley_mist,
      environment_shadow_softness(&t->environment,&sun),environment_shadow_transmission(&t->environment,&sun));}
  }
  if(t->profile)t->profile_terrain=host_now_ms()-terrain_started;
  SDL_SetRenderTarget(t->r, target);
  SDL_SetRenderDrawColor(t->r, 0, 0, 0, 255);
  SDL_RenderClear(t->r);
  int layered = t->layered && t->course_view;
  double ox = 0, oy = 0, k = (double)d.w / *sw;   // output px per canvas px
  // Pixel HD (task-71): the course picture (terrain, sprites, ball; not golf's UI or the native layers) is
  // drawn at 1/pg of the size into a small texture and enlarged with the nearest filter: a crisp pixel grid.
  int pg = t->course_view ? hd_art_pixel_grid(k) : 0;
  SDL_Texture *dest = target;   // where the course picture goes
  if (pg >= 2) {
    int pw = (cw + pg - 1) / pg, ph = (ch + pg - 1) / pg;
    if (!t->pix || t->pix_w != pw || t->pix_h != ph) {
      if (t->pix) SDL_DestroyTexture(t->pix);
      t->pix = SDL_CreateTexture(t->r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, pw, ph);
      t->pix_w = pw;
      t->pix_h = ph;
      if (t->pix) {
        SDL_SetTextureScaleMode(t->pix, SDL_ScaleModeNearest);
        SDL_SetTextureBlendMode(t->pix, SDL_BLENDMODE_NONE);
      }
    }
    if (t->pix) {
      dest = t->pix;
      SDL_SetRenderTarget(t->r, dest);
      SDL_SetRenderDrawColor(t->r, 0, 0, 0, 255);
      SDL_RenderClear(t->r);
      SDL_RenderSetScale(t->r, 1.0f / pg, 1.0f / pg);
    } else {
      pg = 0;
    }
  } else {
    pg = 0;
  }
  int glide = layered && t->zon;                  // a zoom glide places the course picture (task-56)
  if (layered && !glide) slide_offset(t, now, &ox, &oy);
  int sliding = lround(ox * k) || lround(oy * k);
  Rect cd = moved(d, ox * k, oy * k);
  if (glide) {
    glide_course(t, dest, d, *sw, *sh, terrain, now);
  } else {
  if (sliding) {
    SDL_Rect clip = {d.x, d.y, d.w, d.h};
    SDL_RenderSetClipRect(t->r, &clip);
    // Draw the recorded guard band at the displayed camera before the cropped
    // pictures. Opposite diagonal corners now have real world-aligned pixels.
    if (g_cfg.fix_drag_scroll && g_cfg.terrain) {
      SDL_Texture *guard = hdt_render_guard(t->r, g_cfg.edge_fix ? t->shown_serial : LONG_MAX);
      SDL_SetRenderTarget(t->r, dest);
      SDL_RenderSetScale(t->r, pg ? 1.0f / pg : 1.0f, pg ? 1.0f / pg : 1.0f);
      SDL_RenderSetClipRect(t->r, &clip);
      if (guard) {
        SDL_Rect padded = {d.x + (int)lround((ox - *sw) * k),
                           d.y + (int)lround((oy - *sh) * k), d.w * 3, d.h * 3};
        SDL_RenderCopy(t->r, guard, NULL, &padded);
      }
    }
    // Move the previous composed picture with the same world displacement.
    int same = g_cfg.edge_fix && t->slast_ok && t->slast_serial == t->course[t->cur].serial;
    int before = g_cfg.edge_fix && t->slast_ok && t->have_prev && t->slast_serial == t->course[!t->cur].serial;
    if (same || before) {
      Rect p = moved(d, (ox + (before ? t->prev_dx : 0) - t->slast_ox) * k, (oy + (before ? t->prev_dy : 0) - t->slast_oy) * k);
      SDL_Rect dst = {p.x, p.y, p.w, p.h};
      SDL_RenderCopy(t->r, t->slast, NULL, &dst);
    } else if (t->have_prev)
      draw_layer(t, &t->course[!t->cur], dest, moved(d, (ox + t->prev_dx) * k, (oy + t->prev_dy) * k), *sw, *sh,
                 SDL_BLENDMODE_NONE);
  }
  draw_layer(t, &t->course[t->cur], dest, cd, *sw, *sh, SDL_BLENDMODE_NONE);
  int art = t->art_main;
  if (terrain) {
    SDL_Rect dst = {cd.x, cd.y, cd.w, cd.h};
    SDL_RenderCopy(t->r, terrain, NULL, &dst);
    water_draw(t->r, dst.x, dst.y, dst.w, dst.h, now);   // task-78: the water over the ground, under golf's sprites
  }
  SDL_Rect shadow_clip = {d.x, d.y, d.w, d.h};
  draw_cast_shadows(t, shadow_clip, cd.x, cd.y, k, terrain != NULL, now, 0);
  draw_reflections(t,shadow_clip,cd.x,cd.y,k,now);
  if (art >= 0) hd_art_draw(art, t->r, cd.x, cd.y, k);   // hi-res sprites (hd_art.c), in golf's order
  if (terrain || art >= 0) draw_layer(t, &t->overlay, dest, cd, *sw, *sh, t->premul);
  if (sliding) SDL_RenderSetClipRect(t->r, NULL);
  if (t->course_view) {   // task-41: the round ball, over the course and under golf's UI
    SDL_Rect clip = {d.x, d.y, d.w, d.h};
    hdball_draw(t->r, cd.x, cd.y, &clip, (float)k, *sw, *sh, now);
  }
  }
  if (pg) {
    SDL_RenderSetScale(t->r, 1.0f, 1.0f);
    SDL_RenderSetClipRect(t->r, NULL);
    SDL_SetRenderTarget(t->r, target);
    SDL_Rect all = {0, 0, t->pix_w * pg, t->pix_h * pg};
    SDL_RenderCopy(t->r, t->pix, NULL, &all);
  }
  if (!glide) {
    if (layered && g_cfg.smooth && g_cfg.edge_fix) keep_course(t, target, d, ox, oy);
    else t->slast_ok = 0;
  }
  if (layered && t->course_view) {
    SDL_Rect course = {d.x, d.y, d.w, d.h};
    EnvironmentView view = environment_view_camera(&t->cam, *sw, *sh, k,
      glide ? t->zs : 1, glide ? t->zx : (cd.x - d.x) / k,
      glide ? t->zy : (cd.y - d.y) / k);
    double seconds = t->environment_seconds + (t->environment_running ? fmin(0.25, fmax(0, (now - t->environment_frame_time) / 1000)) : 0);
    // Global colour grading stays shared with the existing renderer. The
    // richer atmosphere handles clouds/rain in a separate GPU/fallback pass.
    EnvironmentConfig grade = t->environment;
    int fx_ready = t->environment.enabled && weather_receiver(t);
    if (fx_ready || t->environment.black_background) {
      grade.overlay_suppressed = 1;
    }
    environment_draw(&t->environment_renderer, t->r, course, &grade,
                     seconds, view.x, view.y, view.scale);
    if (fx_ready) {
      WeatherState weather = sample_weather(&t->environment, 1, seconds, sun.hour);
      weather.readability=t->environment.readability*sun.daylight;
        weather.cloud = sun.cloud;
      weather.material_wetness=t->environment.material_wetness;weather.valley_mist=t->environment.valley_mist;
      weather.light[0]=sun.sun_x;weather.light[1]=sun.sun_y;weather.light[2]=sun.sun_z;weather.light_strength=sun.sun_strength;
      if(geometry)for(int i=0;i<3;i++)weather.eye[i]=relief_scene.view[i];
      if(pg)weather.edge_aa=0; // Preserve the deliberately chosen Pixel HD grid.
      if(getenv("SIMGOLF_RENDER_FIDELITY_LOG")){
        static int last_pg=-1,last_linear=-1;static double last_requested=-1;
        if(last_pg!=pg||last_linear!=t->environment.linear_light||last_requested!=t->environment.edge_aa){
          last_pg=pg;last_linear=t->environment.linear_light;last_requested=t->environment.edge_aa;
          fprintf(stderr,"render fidelity: pixel_grid %d linear %d requested_aa %.3f effective_aa %.3f\n",pg,last_linear,last_requested,weather.edge_aa);
        }
      }
      WeatherSurfaceTuning tuning = g_surface_tuning;
      tuning.wet_seconds = t->environment.wet_seconds;
      tuning.dry_seconds = t->environment.dry_seconds;
      tuning.puddle_drain=t->environment.atmosphere.puddle_drain;
      for(int i=0;i<4;i++){weather.material_gloss[i]=tuning.material_gloss[i];weather.material_power[i]=tuning.material_power[i];weather.material_absorb[i]=tuning.material_absorb[i];}
      weather.material_tuning_valid=1;
      double dt = t->surface_time > 0 ? fmin(.25, fmax(0, seconds - t->surface_time)) : 0;
      t->surface_time = seconds;
      if (t->environment.wet_surfaces) weather_surface_update(&t->surface, &tuning,
        weather.rain, weather.cloud, weather.night, weather.wind, dt);
      else memset(&t->surface, 0, sizeof t->surface);
      weather.wetness = t->surface.wetness; weather.puddles = t->surface.puddles;
      rainbow_update(&t->rainbow,weather.rain,weather.cloud,sun.daylight,dt,t->environment.cloud_seed);
      weather.evaporation = t->surface.evaporation;
      weather.impact_strength = t->environment.splashes ? tuning.splash_strength : 0;
      weather.flash_gain = tuning.flash_gain;
      weather.cloud_size = tuning.cloud_size;
      weather.cloud_shade = tuning.cloud_shade;
      weather.cloud_feather = tuning.cloud_feather;
      weather.cloud_morph_seconds = tuning.cloud_morph_seconds;
      if(getenv("SIMGOLF_ATMOSPHERE_LOG")&&now-t->surface_log_time>1000)
        fprintf(stderr,"weather: atmosphere seconds %.3f hour %.2f mist %.4f haze %.4f gust %.4f drift %.3f golden %.4f air %.2f %.2f\n",seconds,sun.hour,weather.mist,weather.haze,weather.wind,weather.wind_drift_x,weather.golden,t->environment.celsius,t->environment.humidity);
      if (getenv("SIMGOLF_WEATHER_SURFACE_LOG") && now-t->surface_log_time>1000) {
        t->surface_log_time=now;int water_pixels=0;
        for(size_t p=0;p<t->weather_receiver_count;++p)water_pixels+=t->weather_receiver[p]==160;
        fprintf(stderr,"weather: surface host_ms %.3f seconds %.4f rain %.3f wet %.4f pool %.4f evaporation %.4f water_pixels %d\n",
          now,seconds,weather.rain,weather.wetness,weather.puddles,weather.evaporation,water_pixels);
      }
      if (!t->environment.lightning) weather.lightning = 0;
      double receiver_scale = glide ? k * t->zs : k;
      double receiver_x = glide ? d.x + t->zx * k : cd.x;
      double receiver_y = glide ? d.y + t->zy * k : cd.y;
      EnvironmentFxReceiver receiver = {t->weather_receiver, FRAME_W, FRAME_H, FRAME_W,
        (uint64_t)t->weather_receiver_serial, receiver_x, receiver_y, receiver_scale,
        geometry&&t->relief.features?t->weather_features:NULL};
      double fx_started=t->profile?host_now_ms():0;
      environment_fx_draw(&t->environment_fx, t->r, course, &receiver, &weather,
        view.x, view.y, view.scale, t->environment.quality, t->environment.glowbugs,
        t->environment.night_sky, t->environment.reduced_flashes, t->environment.shader);
      if(t->environment.rainbows){
        double amount=t->environment.rainbow_preview?sun.daylight:t->rainbow.intensity;
        double sx=-(t->cam.ax*sun.sun_x+t->cam.bx*sun.sun_z);
        double basis=hypot(t->cam.ax,t->cam.bx);
        rainbow_draw(&t->rainbow_draw,t->r,course,t->weather_receiver,FRAME_W,FRAME_H,
          receiver_x,receiver_y,receiver_scale,amount*t->environment.rainbow_strength,basis>0?sx/basis:0);
      }
      if(t->profile)t->profile_fx=host_now_ms()-fx_started;
      if(getenv("SIMGOLF_WILDLIFE_LOG")){
        static double last; if(now-last>1000){last=now;fprintf(stderr,"weather: wildlife seconds %.3f night %.3f rain %.3f storm %.3f wind %.3f activity %.5f legacy %d\n",seconds,weather.night,weather.rain,weather.storm,weather.wind,weather_night_activity(&weather),weather.wildlife_legacy);}
      }
      EnvironmentFxStatus status;
      environment_fx_get_status(t->environment_fx, &status);
      if (status.backend != t->weather_backend) {
        t->weather_backend = status.backend;
        fprintf(stderr, "weather: backend %d shader_compiled %d receiver %d (%s)\n",
          status.backend, status.shader_compiled, status.receiver_uploaded, status.detail);
      }
    } else {
      environment_fx_destroy(&t->environment_fx);
    }
  }
  if(layered && t->course_view && weather_receiver(t)) {
    double lamp_scale=glide?k*t->zs:k;
    double lamp_x=glide?d.x+t->zx*k:cd.x, lamp_y=glide?d.y+t->zy*k:cd.y;
    double night=t->environment.enabled?fmax(0,fmin(1,(.65-sun.daylight)/.65)):0;
    night=night*night*(3-2*night);
    lamps_render(t->r,&t->lamps,(SDL_Rect){d.x,d.y,d.w,d.h},t->weather_receiver,FRAME_W,FRAME_H,lamp_x,lamp_y,lamp_scale,night);
  }
  if (layered && (g_dump_on || t->dump_f)) dump_course(t, target, d, ox, oy, now);   // complete course, before UI
  if (layered && g_show_golf_ui) {
    if (t->art_ui) hd_art_draw(HD_ART_TOP, t->r, d.x, d.y, k);   // golf's UI art in hi-res, under the rest of its UI
    draw_layer(t, &t->ui, target, d, *sw, *sh, t->premul);
  }
  InterlockedExchange(&g_shown_course, t->course_view);
  LONG n = g_n_ui_layers;
  if (n || g_top_draw) {
    HdUiFrame f = {t->r, target, {d.x, d.y, d.w, d.h}, (float)k, *sw, *sh, t->course_view, now};
    f.layered = layered;
    f.slide_x = layered ? (float)ox : 0;
    f.slide_y = layered ? (float)oy : 0;
    if (glide) {   // course things pinned to the picture (golfer tags) follow it: p' = zoom_s * p + zoom_xy
      f.zoom_on = 1;
      f.zoom_s = (float)t->zs;
      f.zoom_x = (float)t->zx;
      f.zoom_y = (float)t->zy;
    }
    f.note = layered && t->note_size > 0 ? t->note : NULL;
    f.note_size = layered ? t->note_size : 0;
    f.note2 = layered && t->note2_size > 0 ? t->note2 : NULL;
    f.note2_size = layered ? t->note2_size : 0;
    f.note3 = layered && t->note3_size > 0 ? t->note3 : NULL;
    f.note3_size = layered ? t->note3_size : 0;
    for (LONG i = 0; i < n; i++) {
      g_ui_layers[i].draw(&f, g_ui_layers[i].user);
      SDL_SetRenderTarget(t->r, target);
      SDL_RenderSetClipRect(t->r, NULL);
    }
    if (g_top_draw) {
      g_top_draw(&f, g_top_user);
      SDL_SetRenderTarget(t->r, target);
      SDL_RenderSetClipRect(t->r, NULL);
    }
  }
  return d;
}

static int write_bmp(const char *path, const uint32_t *argb, int w, int h) {
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  const int row = (w * 3 + 3) & ~3;
  uint8_t hdr[54] = {'B', 'M'};
  uint32_t size = 54 + (uint32_t)row * h;
  memcpy(hdr + 2, &size, 4);
  hdr[10] = 54;
  hdr[14] = 40;
  memcpy(hdr + 18, &w, 4);
  memcpy(hdr + 22, &h, 4);
  hdr[26] = 1;
  hdr[28] = 24;
  fwrite(hdr, 1, 54, f);
  uint8_t *line = calloc(1, row);
  for (int y = h - 1; y >= 0; y--) {
    for (int x = 0; x < w; x++) {
      uint32_t c = argb[(size_t)y * w + x];
      line[x * 3 + 0] = (uint8_t)c;
      line[x * 3 + 1] = (uint8_t)(c >> 8);
      line[x * 3 + 2] = (uint8_t)(c >> 16);
    }
    fwrite(line, 1, row, f);
  }
  free(line);
  fclose(f);
  return 1;
}

static void take_shot(Rt *t, int cw, int ch) {
  uint32_t *px = malloc((size_t)cw * ch * 4);
  int ok = px && !SDL_RenderReadPixels(t->r, NULL, SDL_PIXELFORMAT_ARGB8888, px, cw * 4) &&
           write_bmp(g_shot_path, px, cw, ch);
  free(px);
  fprintf(stderr, "hd: shot %s %dx%d%s\n", g_shot_path, cw, ch, ok ? "" : " FAILED");
  if (!ok) g_shot_path[0] = 0;
  InterlockedExchange(&g_shot_pending, 0);
  SetEvent(g_shot_done);
}

static void wait_until(Rt *t, double when) {
  double left = when - host_now_ms();
  if (left > 1.5 && t->timer) {
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)((left - 1.0) * 10000.0);   // relative, 100 ns units
    if (SetWaitableTimer(t->timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(t->timer, INFINITE);
  } else if (left > 2.0) {
    Sleep((DWORD)(left - 1.5));
  }
  while (host_now_ms() < when) YieldProcessor();
}


// Test aid only (card task-106): SIMGOLF_FRAMELOG=<file> writes one line per presented frame
// (n, ms since start, course view, the front-end screen covering it or -1, game frame serial, dialog gap,
// report gap: 1 when the game has a dialog / report up and our layer drew none); with
// SIMGOLF_FRAMELOG_SHOTS=<prefix> saves up to 60 ordinary uncovered frames and
// up to 60 actual dialog/report gap frames separately, plus explicitly requested frames.
static void frame_log(Rt *t, int cw, int ch, double now) {
  static int on = -1, n, shots, gap_shots;
  static FILE *f;
  static double t0;
  static const char *prefix;
  if (on < 0) {
    const char *p = getenv("SIMGOLF_FRAMELOG");
    on = p && *p;
    if (on) {
      f = fopen(p, "w");
      on = f != NULL;
    }
    prefix = getenv("SIMGOLF_FRAMELOG_SHOTS");
    t0 = now;
  }
  if (!on) return;
  int cover = fef_cover_screen();
  int dgap = dlgf_gap(), rgap = rpf_gap();   // a dialog / report is up in the game and the layer drew none: the original shows
  fprintf(f, "%d,%.1f,%d,%d,%ld,%d,%d", ++n, now - t0, t->course_view, cover, (long)g_frame_serial, dgap, rgap);
  if (getenv("SIMGOLF_TEXTURE_TRACE")) fprintf(f, ",%d,%d", t->layered, t->layered_now);
  fputc('\n', f);
  fflush(f);
  const char *at = getenv("SIMGOLF_FRAMELOG_AT");   // frame numbers to save whatever covers them, "3,40,300"
  int want = 0;
  for (const char *q = at; q && *q;) {
    if (atoi(q) == n) want = 1;
    q = strchr(q, ',');
    if (q) q++;
  }
  int coverage_gap = dgap || rgap;
  int gap_shot = getenv("SIMGOLF_TEXTURE_TRACE") ? (t->course_view && !t->layered_now) : (coverage_gap || (cover < 0 && !t->course_view));
  int ordinary_shot = gap_shot && !coverage_gap;
  if (prefix && *prefix && (want || (coverage_gap && gap_shots < 60) || (ordinary_shot && shots < 60))) {
    uint32_t *px = malloc((size_t)cw * ch * 4);
    char path[600];
    snprintf(path, sizeof path, "%s%d.bmp", prefix, n);
    if (px && !SDL_RenderReadPixels(t->r, NULL, SDL_PIXELFORMAT_ARGB8888, px, cw * 4)) {
      write_bmp(path, px, cw, ch);
      if (!want) {
        if (coverage_gap) gap_shots++;
        else if (ordinary_shot) shots++;
      }
    }
    free(px);
  }
}

typedef struct {
  double t0, sum, min, max;
  int n;
  float ms[4096];
  LONG game0;
} Stats;

static int cmp_float(const void *a, const void *b) {
  float x = *(const float *)a, y = *(const float *)b;
  return x < y ? -1 : x > y;
}

static void stats_add(Stats *s, double now, double ms, int cw, int ch, const char *driver, int k) {
  if (s->n < 4096) s->ms[s->n] = (float)ms;
  s->n++;
  s->sum += ms;
  if (ms < s->min || s->n == 1) s->min = ms;
  if (ms > s->max) s->max = ms;
  if (now - s->t0 < 5000) return;
  int m = s->n < 4096 ? s->n : 4096;
  qsort(s->ms, m, sizeof s->ms[0], cmp_float);
  LONG games = g_game_frames;
  fprintf(stderr,
          "hd: %.1f fps, frame ms avg %.2f min %.2f p99 %.2f max %.2f; game %.1f frames/s; %dx%d %s%s%d, vsync %d, cap %d (%s)\n",
          s->n * 1000.0 / (now - s->t0), s->sum / s->n, s->min, s->ms[m * 99 / 100], s->max,
          (games - s->game0) * 1000.0 / (now - s->t0), cw, ch, k_filters[g_cfg.filter], k > 1 ? " x" : " x",
          k > 1 ? k : 1, g_cfg.vsync, g_cfg.fps, driver);
  s->t0 = now;
  s->n = 0;
  s->sum = s->max = 0;
  s->game0 = games;
}

static double render_cpu_ms(void){
  FILETIME created,exited,kernel,user;
  if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return 0;
  ULARGE_INTEGER k,u;k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
  u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
  return (double)(k.QuadPart+u.QuadPart)/10000.0;
}
static DWORD WINAPI render_main(void *arg) {
  (void)arg;
  g_window = SDL_CreateWindow("SimGolf", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              g_start_w, g_start_h, g_start_flags);
  if (!g_window) {
    fprintf(stderr, "hd: no window (%s)\n", SDL_GetError());
    g_start_ok = 0;
    SetEvent(g_ready);
    return 0;
  }
  Rt *t = calloc(1, sizeof *t);
  t->profile=getenv("SIMGOLF_RENDER_PROFILE")!=NULL;
  t->local = calloc((size_t)FRAME_W * FRAME_H, 2);
  t->local_ui = calloc((size_t)FRAME_W * FRAME_H, 2);
  t->mask = calloc((size_t)FRAME_W * FRAME_H, 1);
  Uint32 flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE | (g_cfg.vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
  t->r = SDL_CreateRenderer(g_window, -1, flags);
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
  if (!t->r) t->r = SDL_CreateRenderer(g_window, -1, flags);
  Layer *layers[] = {&t->course[0], &t->course[1], &t->overlay, &t->ui};
  int made = 0;
  for (int i = 0; t->r && i < 4; i++) {
    layers[i]->src = SDL_CreateTexture(t->r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, FRAME_W, FRAME_H);
    layers[i]->serial = layers[i]->pre_serial = -1;
    made += layers[i]->src != NULL;
  }
  if (!t->r || made < 4) {
    fprintf(stderr, "hd: no renderer (%s); the original presentation takes over\n", SDL_GetError());
    if (t->r) { ui_course_map_clear(t->r); SDL_DestroyRenderer(t->r); }
    SDL_DestroyWindow(g_window);
    g_window = NULL;
    g_start_ok = 0;
    SetEvent(g_ready);
    return 0;
  }
  SDL_RendererInfo info;
  SDL_GetRendererInfo(t->r, &info);
  for (int c = 0; c < 32768; c++) {
    uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
    r = r << 3 | r >> 2;
    g = g << 3 | g >> 2;
    b = b << 3 | b >> 2;
    t->lut[c] = 0xff000000u | r << 16 | g << 8 | b;
  }
  t->shown_serial = -1;
  t->premul = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                         SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
  t->timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  FILE *fpslog = g_cfg.fpslog[0] ? fopen(g_cfg.fpslog, "w") : NULL;
  if (fpslog) fprintf(fpslog, "t_ms,frame_ms,game_frames,course_dx,course_dy,cam_x,cam_y,cam_ox,cam_oy,zoom_on,zoom_view,zoom_s,zoom_x,zoom_y,live_zoom,t_draw%s\n",t->profile?",prepare_ms,compose_ms,submit_ms,present_ms,cpu_ms,terrain_ms,shadow_ms,fx_ms":"");
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  g_start_ok = 1;
  SetEvent(g_ready);
  fprintf(stderr, "hd: render thread on %s, filter %s, vsync %d, fps cap %d (0 = none), canvas %s\n", info.name,
          k_filters[g_cfg.filter], g_cfg.vsync, g_cfg.fps, g_cfg.render_w ? "off screen" : "the window");

  Stats st = {0};
  st.t0 = host_now_ms();
  double last = st.t0, next = st.t0;
  double last_fps_flush=st.t0;
  if(t->profile)fprintf(stderr,"render profile: thread %lu\n",GetCurrentThreadId());
  for (;;) {
    double frame_started=t->profile?host_now_ms():0;
    double cpu_started=t->profile?render_cpu_ms():0;
    // Dispatch native window messages and SDL resize watchers between frames,
    // on the same thread that owns the window and its renderer.
    SDL_PumpEvents();
    if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) {
      Sleep(20);
      last = next = host_now_ms();
      continue;
    }
    zoom_poll(t, host_now_ms());   // before the next frame replaces the picture a zoom glide starts from
    upload(t, host_now_ms());
    zoom_settle(t, host_now_ms());
    if (hd_art_prepare(t->r, 0) && t->shown_serial >= 0)   // textures the loader finished for the frame shown
      fill_art(t, t->shown_serial);
    int ow = 0, oh = 0, ww = 0, wh = 0;
    SDL_GetRendererOutputSize(t->r, &ow, &oh);
    SDL_GetWindowSize(g_window, &ww, &wh);
    if (ow < 1 || oh < 1) ow = FRAME_W, oh = FRAME_H;
    int cw = g_cfg.render_w ? g_cfg.render_w : ow, ch = g_cfg.render_w ? g_cfg.render_h : oh;
    SDL_Texture *target = NULL;
    if (g_cfg.render_w || g_n_ui_layers) {   // task-26: native UI layers frost the picture, so it must be a texture
      if (!t->canvas || t->canvas_w != cw || t->canvas_h != ch) {
        if (t->canvas) { environment_fx_destroy(&t->environment_fx);rainbow_destroy(&t->rainbow_draw); SDL_DestroyTexture(t->canvas); }
        t->canvas = SDL_CreateTexture(t->r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, cw, ch);
        t->canvas_w = cw;
        t->canvas_h = ch;
        if (!t->canvas) fprintf(stderr, "hd: no %dx%d canvas (%s)\n", cw, ch, SDL_GetError());
        else SDL_SetTextureScaleMode(t->canvas, SDL_ScaleModeLinear);
      }
      target = t->canvas;
      if (!target) cw = ow, ch = oh;
    }
    int sw, sh;
    double t_draw = host_now_ms(), slide_x = 0, slide_y = 0;
    t->tw = cw;
    t->th = ch;
    if (t->layered && t->course_view && !t->zon) slide_offset(t, t_draw, &slide_x, &slide_y);
    Rect d = compose(t, target, cw, ch, &sw, &sh, t_draw);
    double composed=t->profile?host_now_ms():0;
    if (g_shot_pending) take_shot(t, cw, ch);
    water_after_frame(t->r, cw, ch);   // task-78: look-test clips (water.clip)
    Rect w = {0, 0, cw, ch};   // the canvas inside the window
    if (target) {
      // Fit the canvas into the window, aspect kept.
      double s = fmin((double)ow / cw, (double)oh / ch);
      w.w = (int)lround(cw * s);
      w.h = (int)lround(ch * s);
      w.x = (ow - w.w) / 2;
      w.y = (oh - w.h) / 2;
      SDL_Rect wr = {w.x, w.y, w.w, w.h};
      SDL_SetRenderTarget(t->r, NULL);
      SDL_SetRenderDrawColor(t->r, 0, 0, 0, 255);
      SDL_RenderClear(t->r);
      SDL_RenderCopy(t->r, target, NULL, &wr);
    }
    // window point -> output pixel -> canvas -> frame
    float rx = ww > 0 ? (float)ow / ww : 1, ry = wh > 0 ? (float)oh / wh : 1;
    float cx = (float)cw / w.w, cy = (float)ch / w.h, fx = (float)sw / d.w, fy = (float)sh / d.h;
    EnterCriticalSection(&g_lock);
    g_map_sx = rx * cx * fx;
    g_map_sy = ry * cy * fy;
    g_map_ox = (w.x * cx + d.x) * fx;
    g_map_oy = (w.y * cy + d.y) * fy;
    int flipped = g_map_ok && (g_map_w != sw || g_map_h != sh);
    g_map_w = sw;
    g_map_h = sh;
    g_map_ok = 1;
    LeaveCriticalSection(&g_lock);
    static int flips;
    if (flipped && flips++ < 40) fprintf(stderr, "hd: %.1f s: mouse maps to %dx%d of the frame (%s)\n", now_s(), sw, sh, t->course_view ? "course" : "page");

    frame_log(t, ow, oh, host_now_ms());
    double submit_finished=t->profile?host_now_ms():0;
    if (g_cfg.fps > 0 && g_cfg.fix_present_pacing) {
      next = hd_present_deadline(next, host_now_ms(), 1000.0 / g_cfg.fps);
      wait_until(t, next);
    }
    double submitted=t->profile?host_now_ms():0;
    SDL_RenderPresent(t->r);
    double now = host_now_ms();
    double cpu_used=t->profile?render_cpu_ms()-cpu_started:0;
    double ms = now - last;
    last = now;
    stats_add(&st, now, ms, cw, ch, info.name, g_cfg.filter == F_SHARP ? t->course[t->cur].pre_k : 1);
    if (fpslog) {
      double zv = t->cam.zoom, zcx, zcy;
      if (t->zon) hdz_view(&t->zg, t->zg.last_t, &zv, &zcx, &zcy);
      fprintf(fpslog, "%.3f,%.3f,%ld,%.2f,%.2f,%d,%d,%d,%d,%d,%.4f,%.4f,%.2f,%.2f,%d,%.3f", now, ms, (long)g_game_frames, slide_x,
              slide_y, t->cam.x, t->cam.y, t->cam.ox, t->cam.oy, t->zon, zv, t->zon ? t->zs : 1.0, t->zon ? t->zx : 0.0,
              t->zon ? t->zy : 0.0, t->cam.zoom, t_draw);   // t_draw: the time the picture's positions are for (task-68)
      if(t->profile)fprintf(fpslog,",%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",t_draw-frame_started,composed-t_draw,submit_finished-composed,now-submitted,cpu_used,t->profile_terrain,t->profile_shadow,t->profile_fx);
      fputc('\n',fpslog);
      // The native process can exit without a render-thread CRT shutdown.
      // Keep complete profiling rows durable beyond the bounded one-second tail.
      if(now-last_fps_flush>=1000){fflush(fpslog);last_fps_flush=now;}
    }
    if (g_cfg.fps > 0 && !g_cfg.fix_present_pacing) {
      double period = 1000.0 / g_cfg.fps;
      next += period;
      if (next < now - period) next = now;   // fell behind (a stall): start over, don't race
      wait_until(t, next);
    } else if (g_cfg.fps <= 0 && !g_cfg.vsync) {
      next = now;
    }
  }
}

// ------------------------------------------------------------------ hooks

// golf/hd_ui.c, game thread: golf finished the course part of a frame.
static void course_hook(const uint16_t *bits, int pitch, const HdUiCamera *cam) {
  if (!g_snap || pitch < FRAME_W) return;
  for (int y = 0; y < FRAME_H; y++) memcpy(g_snap + y * FRAME_W, bits + (size_t)y * pitch, (size_t)FRAME_W * 2);
  g_snap_cam = *cam;
  g_snap_fresh = 1;
  hd_art_course_done(bits, pitch);
}

// gdi.c, before the window exists: size, flags, DPI awareness.
void hd_native_prepare(int *w, int *h, unsigned *flags) {
  InitializeCriticalSection(&g_lock);
  InitializeCriticalSection(&g_zlock);
  g_zev_ack = CreateEventA(NULL, FALSE, FALSE, NULL);
  read_settings();
  g_atmosphere_tuning=atmosphere_defaults();
  char atmosphere_path[MAX_PATH];{ char release_dir[MAX_PATH];snprintf(release_dir,sizeof release_dir,"%s/data",host_exe_dir());release_data_path(atmosphere_path,sizeof atmosphere_path,release_dir,"weather-atmosphere.csv"); }
  fprintf(stderr,"weather: atmosphere tuning %s\n",atmosphere_load(&g_atmosphere_tuning,atmosphere_path)?"loaded":"safe defaults");
  g_surface_tuning = weather_surface_defaults();
  char surface_path[MAX_PATH];
  { char release_dir[MAX_PATH];snprintf(release_dir,sizeof release_dir,"%s/data",host_exe_dir());release_data_path(surface_path,sizeof surface_path,release_dir,"weather-surfaces.csv"); }
  fprintf(stderr, "weather: surface tuning %s\n", weather_surface_load(&g_surface_tuning, surface_path) ? "loaded" : "safe defaults");
  // The mod switch (mods.ini [mods] hd_presentation=0, the "vanilla" profile)
  // turns all of it off, like golf.ini hd=0.
  int mod_on = g_mod_on = mods_enabled("hd_presentation");
  if (!mod_on) g_cfg.hd = 0;
  hdt_init(g_cfg.hd && g_cfg.terrain);
  // the sub-tile camera goes with smooth scrolling, at hd=0 too (it is golf's
  // own drawing); off with the mod, and with layers=0 at hd=1
  hd_ui_set_subtile(mod_on && g_cfg.smooth && (!g_cfg.hd || g_cfg.layers));
  hd_ui_set_overscan(mod_on && g_cfg.smooth && (!g_cfg.hd || g_cfg.layers) ? g_cfg.overscan : 0);   // task-68, with it
  if (!g_cfg.hd) {
    fprintf(stderr, "hd: off (%s), the original 800x600 window\n", mod_on ? "hd=0" : "mod hd_presentation off");
    hd_art_init(0);
    return;
  }
  // Real pixels, not Windows' blurry DPI stretch of an unaware window
  // (Windows 10 1703+; looked up so older SDK headers don't matter).
  typedef BOOL(WINAPI * SetCtx)(HANDLE);
  SetCtx set_ctx = (SetCtx)(void *)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
  if (set_ctx) set_ctx((HANDLE)(intptr_t)-4);   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
  SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
  // The size the picture will have: the canvas follows its shape.
  int out_w = *w, out_h = *h;
  if (g_cfg.borderless) {
    *flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_DisplayMode m;
    if (!SDL_GetDesktopDisplayMode(0, &m)) out_w = m.w, out_h = m.h;
  } else {
    *flags |= SDL_WINDOW_RESIZABLE;
    if (g_cfg.win_w) {
      *w = g_cfg.win_w;
      *h = g_cfg.win_h;
    } else if (!host_headless()) {
      SDL_Rect u;
      int k = 1;
      if (!SDL_GetDisplayUsableBounds(0, &u))
        while ((k + 1) * 800 <= u.w * 9 / 10 && (k + 1) * 600 <= u.h * 9 / 10) k++;
      *w = 800 * k;
      *h = 600 * k;
    }
    out_w = *w, out_h = *h;
  }
  if (g_cfg.render_w) out_w = g_cfg.render_w, out_h = g_cfg.render_h;
  // canvas=auto: the picture divided by the UI scale; never under 800x600
  // (the original's layout), never over 4096 (the DIB limit).
  int cw = g_cfg.canvas_w, ch = g_cfg.canvas_h;
  double s = g_cfg.ui_scale;
  if (out_w < 1 || out_h < 1) out_w = 800, out_h = 600;
  if (!cw) {
    if (s == 0) s = fmax(1.0, floor(out_h / 720.0 * 4 + 1e-9) / 4);   // auto
    if (s < 0 || out_h / s < 600) s = out_h / 600.0;                   // fit: 600 high
    if (out_w / s > 4096) s = out_w / 4096.0;
    if (out_h / s > 4096) s = out_h / 4096.0;
    cw = (int)lround(out_w / s);
    ch = (int)lround(out_h / s);
    if (cw < 800) cw = 800;
    if (ch < 600) ch = 600;
  }
  hd_frame_w = cw & ~1;
  hd_frame_h = ch & ~1;
  g_ui_px = fmin((double)out_w / hd_frame_w, (double)out_h / hd_frame_h);
  win_set_screen_size(hd_frame_w, hd_frame_h);
  g_frame = calloc((size_t)FRAME_W * FRAME_H, 2);
  g_ui = calloc((size_t)FRAME_W * FRAME_H, 2);
  g_snap = calloc((size_t)FRAME_W * FRAME_H, 2);
  g_mask = calloc((size_t)FRAME_W * FRAME_H, 1);
  if (g_cfg.layers) hd_ui_course_hook = course_hook;
  hd_art_init(g_cfg.layers);   // the art pack needs the course layer
  if (!g_cfg.layers) g_cfg.smooth = 0;
  fprintf(stderr, "hd: game canvas %dx%d (%s, ui scale %.3g) for a %dx%d picture; layers %d, smooth %d\n", FRAME_W,
          FRAME_H, g_cfg.canvas_w ? "canvas=" : "canvas=auto", g_ui_px, out_w, out_h, g_cfg.layers, g_cfg.smooth);
}

static void probe_layer(const HdUiFrame *f, void *user);

// Game thread (golf/hd_ui.c hd_ui_zoom_set, after golf's zoom and camera moved
// but before it draws at them): a zoom step. Without an event: is smooth zoom
// on? With one: hands it to the render thread and waits for the snapshot of
// the picture on screen; 1 when the glide has started.
static int zoom_hook(const HdUiZoom *e) {
  int on = g_active && g_cfg.hd && g_cfg.layers && g_cfg.smooth && g_cfg.smooth_zoom && g_shown_course;
  if (!e) return on;
  if (!on) return 0;
  EnterCriticalSection(&g_zlock);
  g_zev = *e;
  g_zev_t = host_now_ms();
  LeaveCriticalSection(&g_zlock);
  InterlockedExchange(&g_zev_result, 0);
  ResetEvent(g_zev_ack);
  InterlockedIncrement(&g_zev_serial);
  if (WaitForSingleObject(g_zev_ack, 120) != WAIT_OBJECT_0) return 0;   // the render thread is busy: golf's zoom just cuts
  return (int)g_zev_result;
}

// Game thread, golf/camera_input.c: a pointer position (canvas px) on the
// picture on screen -> the same course point in golf's view, while a glide
// shows a view golf has already left. With NULL: is a glide running?
static int zoom_map(int *x, int *y) {
  if (!g_zoom.active) return 0;
  HdZoom g;
  EnterCriticalSection(&g_zlock);
  g = g_zoom;
  LeaveCriticalSection(&g_zlock);
  if (!g.active || hdz_over(&g, g.last_t)) return 0;
  if (!x || !y) return 1;
  int px = *x, py = *y;
  if (px < 0 || py < 0 || px >= FRAME_W || py >= FRAME_H) return 0;
  int ui;
  EnterCriticalSection(&g_lock);
  ui = g_frame_layered && g_ui[(size_t)py * FRAME_W + px] != g_frame[(size_t)py * FRAME_W + px];   // golf's own UI is not the course
  LeaveCriticalSection(&g_lock);
  if (ui) return 0;
  int zoom;
  double cx, cy, s, ox, oy;
  hd_ui_game_view(&zoom, &cx, &cy);
  hdz_xform(&g, g.last_t, zoom, cx, cy, &s, &ox, &oy);
  if (s < 0.05) return 0;
  int nx = (int)lround((px - ox) / s), ny = (int)lround((py - oy) / s);
  nx = nx < 0 ? 0 : nx > FRAME_W - 1 ? FRAME_W - 1 : nx;
  ny = ny < 0 ? 0 : ny > FRAME_H - 1 ? FRAME_H - 1 : ny;
  *x = nx;
  *y = ny;
  return 1;
}

// gdi.c: create the window on its render thread. NULL selects the original
// game-thread presentation, which creates its own window and renderer.
SDL_Window *hd_native_start(int w, int h, unsigned flags) {
  if (!g_cfg.hd) return NULL;
  g_start_w = w;
  g_start_h = h;
  g_start_flags = flags;
  opt_register();   // the Options page's top layer (card task-24)
  hudf_early();     // hide the original HUD on the first course frame too
  fef_early();      // task-106: the front end's layer is there before the first picture
  rpf_early();      // reports sit below their nested dialogs
  dlgf_early();     // draw order: HUD, front end, reports, dialogs
  g_ready = CreateEventA(NULL, FALSE, FALSE, NULL);
  g_shot_done = CreateEventA(NULL, FALSE, FALSE, NULL);
  HANDLE th = CreateThread(NULL, 1 << 20, render_main, NULL, 0, NULL);
  if (!th) return 0;
  WaitForSingleObject(g_ready, INFINITE);
  CloseHandle(th);
  if (!g_start_ok) return 0;
  SDL_AddEventWatch(map_mouse, NULL);
  if (g_cfg.ui_probe) hd_ui_add_layer(probe_layer, NULL);
  g_active = 1;
  if (g_cfg.layers) {
    hd_ui_zoom_hook = zoom_hook;
    hd_ui_zoom_map_hook = zoom_map;
  }
  return g_window;
}

// ui_probe=1: proves the UI layer API end to end (tests read the square).
static void probe_layer(const HdUiFrame *f, void *user) {
  (void)user;
  if (!f->course) return;
  SDL_Rect r = {f->view.x + (int)lroundf((f->canvas_w - 40) * f->px), f->view.y + (int)lroundf((f->canvas_h - 40) * f->px),
                (int)lroundf(24 * f->px), (int)lroundf(24 * f->px)};
  SDL_SetRenderDrawBlendMode(f->renderer, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(f->renderer, 0x3c, 0xd0, 0xa0, 255);
  SDL_RenderFillRect(f->renderer, &r);
}

// gdi.c's win_present: hand the finished frame over.
int hd_present(const uint16_t *screen) {
  // Read settings on the game thread, then publish under g_lock. The render
  // thread never reads the mutable mod store or live game memory.
  EnvironmentConfig environment = environment_defaults();
  environment.atmosphere=g_atmosphere_tuning;
  environment.mist=mods_get_int("environment.mist",1)!=0;
  environment.haze=mods_get_int("environment.haze",1)!=0;
  environment.gusts=mods_get_int("environment.gusts",1)!=0;
  environment.golden_hour=mods_get_int("environment.golden_hour",1)!=0;
  environment.distant_thunder=mods_get_int("environment.distant_thunder",1)!=0;
  environment.enabled = mods_get_int("environment.enabled", 0) != 0;
  environment.day_night = mods_get_int("environment.day_night", 0) != 0;
  environment.hour = mods_get_num("environment.hour", 14);
  environment.cycle_minutes = mods_get_num("environment.cycle_minutes", 20);
  // New keys avoid old two-minute preview preferences accelerating this cycle.
  environment.day_minutes=mods_get_num("environment.day_minutes",20);
  environment.night_minutes=mods_get_num("environment.night_minutes",10);
  environment.moon_strength=mods_get_num("environment.moon_strength",.18);
  environment.ambient_fill=mods_get_num("environment.ambient_fill",.7);
  environment.surface_light=mods_get_num("environment.surface_light",.65);
  environment.contact_shadow=mods_get_num("environment.contact_shadow",.6);
  environment.shadow_softness=mods_get_num("environment.shadow_softness",1.8);
  environment.night_lights=0; // the maintainer retired building lamps; ignore old preview preferences.
  environment.light_radius=mods_get_num("environment.light_radius",22);
  environment.cloud_strength = mods_get_num("environment.cloud_strength", 0.65);
  environment.rain_strength = mods_get_num("environment.rain_strength", 0.65);
  environment.lighting = mods_get_int("environment.lighting", 1) != 0;
  environment.cast_shadows = mods_get_int("environment.cast_shadows", 1) != 0;
  environment.terrain_shadows = mods_get_int("environment.terrain_shadows", 1) != 0;
  environment.occlusion = mods_get_int("environment.occlusion", 1) != 0;
  environment.quality = mods_get_int("environment.quality", 1);
  environment.shader = mods_get_int("environment.shader", 1) != 0;
  environment.linear_light=mods_get_int("environment.linear_light",1)!=0;
  environment.shadow_cache=mods_get_int("environment.shadow_cache",1)!=0;
  environment.lighting_hz=mods_get_num("environment.lighting_hz",15);
  environment.edge_aa=mods_get_num("environment.edge_aa",.65);
  environment.readability=mods_get_num("environment.readability",.8);
  environment.reflections=mods_get_num("environment.reflections",.44);
  environment.reflection_ripple=mods_get_num("environment.reflection_ripple",.8);
  environment.rainbows=mods_get_int("environment.rainbows",0)!=0;
  environment.rainbow_strength=mods_get_num("environment.rainbow_strength",.075);
  environment.rainbow_preview=mods_get_int("environment.rainbow_preview",0)!=0;
  environment.relief_shadows=mods_get_int("environment.relief_shadows",1)!=0;
  environment.weather_shadows=mods_get_int("environment.weather_shadows",1)!=0;
  environment.enclosure_shade=mods_get_num("environment.enclosure_shade",.55);
  environment.material_wetness=mods_get_num("environment.material_wetness",1);
  environment.valley_mist=mods_get_num("environment.valley_mist",1);
  environment.weather_wildlife=mods_get_int("environment.weather_wildlife",1)!=0;
  environment.glowbugs = mods_get_int("environment.glowbugs", 1) != 0;
  environment.night_sky = mods_get_int("environment.night_sky", 1) != 0;
  environment.black_background = mods_get_int("environment.black_background", 1) != 0;
  environment.wet_surfaces = mods_get_int("environment.wet_surfaces", 1) != 0;
  environment.splashes = mods_get_int("environment.splashes", 1) != 0;
  environment.wet_seconds = mods_get_num("environment.wet_seconds", g_surface_tuning.wet_seconds);
  environment.dry_seconds = mods_get_num("environment.dry_seconds", g_surface_tuning.dry_seconds);
  environment.lightning = mods_get_int("environment.lightning", 1) != 0;
  environment.reduced_flashes = mods_get_int("environment.reduced_flashes", 1) != 0;
  environment.sound = mods_get_int("environment.sound", 1) != 0;
  environment.gameplay = mods_get_int("environment.gameplay", 0) != 0;
  environment.weather_volume = mods_get_num("environment.weather_volume", 50) / 100;
  const char *weather = mods_get("environment.weather");
  environment.weather = weather && !strcmp(weather, "storm") ? 3 : weather && !strcmp(weather, "rain") ? 2 : weather && !strcmp(weather, "cloudy") ? 1 : 0;
  water_poll();   // task-78: display.water, water.style, water.clip
  // display.smooth may change while the game runs (mods store, bridge)
  static unsigned seen_serial;
  if (mods_serial() != seen_serial) {
    seen_serial = mods_serial();
    g_cfg.fix_zoom_clock = mods_get_int("fixes.zoom_clock", 1) != 0;
    g_cfg.fix_present_pacing = mods_get_int("fixes.present_pacing", 1) != 0;
    g_cfg.fix_drag_scroll = mods_get_int("fixes.drag_scroll", 1) != 0;
    g_cfg.camera_glide = mods_get_int("camera.glide", 0) != 0;
    const char *v = mods_get("display.smooth");
    if (v && *v && (g_cfg.layers || !g_cfg.hd)) {
      g_cfg.smooth = atoi(v) != 0;
      hd_ui_set_subtile(g_mod_on && g_cfg.smooth);
    }
    if ((v = mods_get("display.overscan")) && *v) set_key("overscan", v);   // task-68
    hd_ui_set_overscan(g_mod_on && g_cfg.smooth && (!g_cfg.hd || g_cfg.layers) ? g_cfg.overscan : 0);
    if ((v = mods_get("display.smooth_zoom")) && *v) g_cfg.smooth_zoom = atoi(v) != 0;   // task-56
    if ((v = mods_get("display.zoom_ms")) && *v) set_key("zoom_ms", v);
  }
  if (!g_active) return 0;
  int hud_game_paused(void);
  if (hd_present_hold_course(g_snap_fresh, g_frame_layered, g_frame_course,
                                    hud_course_transition(), opt_is_open(), hud_game_paused(), dlg_game_active(), dlgf_active(), rp_game_active(),
                                    fef_cover_screen() >= 0, mods_get_int("fixes.hud_transitions", 1))) {
    // Original tray handlers and the paused Options page can present partial
    // back-buffer updates. Keep the complete course publication until redraw.
    hd_art_discard_pending();
    weather_audio_publish(NULL, 0, 0);
    return 1;
  }
  static HdBallFrame balls;
  hdball_publish(&balls);   // task-41: this game frame's balls
  static uint8_t *mask;
  if (!mask) mask = malloc((size_t)FRAME_W * FRAME_H);
  // A course snapshot since the last present (golf's main loop drew this
  // frame): the course and its UI go over as two layers. Modal screens
  // present without one and stay a single picture.
  int layered = g_snap_fresh;
  g_snap_fresh = 0;
  // Before golf has drawn any course terrain, the terrain cache holds the
  // title picture, which would pass for a course frame.
  int course = terrain_hd_seen() && hdt_capture(layered ? g_snap : screen, mask, g_frame_serial + 1);   // the serial this frame gets below
  if (!course) layered = 0;
  EnterCriticalSection(&g_lock);
  memcpy(g_frame, layered ? g_snap : screen, (size_t)FRAME_W * FRAME_H * 2);
  if (layered) {
    memcpy(g_ui, screen, (size_t)FRAME_W * FRAME_H * 2);
    g_frame_cam = g_snap_cam;
    hud_course_frame_presented();
  }
  memcpy(&g_frame_ball, &balls, sizeof balls);
  // Freeze presentation time in game pause and away from the course.
  // Never modify date, weather, economy or save fields.
  static double environment_last;
  double environment_now = host_now_ms();
  int hud_game_paused(void);
  if (g_environment_running && environment_last > 0)
    g_environment_seconds += fmin(0.25, fmax(0, (environment_now - environment_last) / 1000));
  environment_last = environment_now;
  g_environment_running = course && environment.enabled && !hud_game_paused();
  if (course) {
    static uint32_t cloud_session_seed;
    if(!cloud_session_seed) {
      const char *seed=getenv("SIMGOLF_SEED");
      cloud_session_seed=seed?(uint32_t)strtoul(seed,NULL,10):(uint32_t)environment_now;
      if(!cloud_session_seed)cloud_session_seed=1;
    }
    // Existing course property/theme identity, presentation RNG only.
    uint32_t property=*(const uint32_t *)(uintptr_t)0x0059bf90;
    uint32_t theme=*(const uint8_t *)(uintptr_t)0x005a34e0;
    environment.cloud_seed=cloud_session_seed^(property*0x85ebca6bU)^(theme*0xc2b2ae35U);
  }
  if (course && environment.enabled && mods_get_int("environment.regional_weather", 0)) {
    static int climate_loaded;
    if (!climate_loaded) {
      char path[MAX_PATH];
      { char release_dir[MAX_PATH];snprintf(release_dir,sizeof release_dir,"%s/data",host_exe_dir());release_data_path(path,sizeof path,release_dir,"weather-climate.csv"); }
      const char *climate_fixture=getenv("SIMGOLF_WEATHER_CLIMATE_FILE");
      if(climate_fixture&&*climate_fixture)snprintf(path,sizeof path,"%s",climate_fixture);
      climate_loaded = weather_climate_load(&g_climate_table, path) ? 1 : -1;
      fprintf(stderr, "weather: regional climate table %s\n", climate_loaded > 0 ? "loaded" : "missing or invalid; fixed weather retained");
    }
    if (climate_loaded > 0) {
      // seedCourseTable 0046f2b0: region label at slot+0, property at+28, stride46.
      // formatCourseTitle 0040daa0: current property index at0059bf90.
      int property = *(const int *)(uintptr_t)0x0059bf90;
      int theme = *(const uint8_t *)(uintptr_t)0x005a34e0;
      uint32_t tick = *(const uint32_t *)(uintptr_t)0x00834170;
      char place[25] = {0};
      if (property >= 0 && property <= 16)
        memcpy(place, (const void *)(uintptr_t)(0x00571fd8 + property * 46), 24);
      g_climate.lead_seconds=g_atmosphere_tuning.front_lead;
      weather_climate_update(&g_climate, &g_climate_table, place, theme, tick,
        0x53474f4cU ^ (uint32_t)(property + 1), g_environment_seconds,
        mods_get_num("environment.weather_minutes", 3));
      environment.regional_weather = g_climate.initialized;
      environment.regional_rain = g_climate.rain;
      environment.regional_cloud = g_climate.cloud;
      environment.regional_storm = g_climate.storm;
      environment.approaching_storm=g_climate.target_storm;
      environment.approach_age=g_environment_seconds-g_climate.front_started;
      environment.approach_id=g_climate.identity^(g_climate.front*0x9e3779b9u);
      static uint32_t logged_front, logged_identity;
      if (g_climate.front != logged_front || g_climate.identity != logged_identity) {
        logged_front = g_climate.front; logged_identity = g_climate.identity;
        fprintf(stderr, "weather: regional front %u place \"%s\" theme %d month %u kind %d rain %.3f cloud %.3f storm %.3f seconds %.3f\n",
          g_climate.front, place, theme, 3 + ((tick >> 10) & 7), g_climate.kind,
          g_climate.target_rain, g_climate.target_cloud, g_climate.target_storm, g_environment_seconds);
      }
    }
  } else g_climate.initialized = 0;
  EnvironmentSample weather_sun = environment_sample(&environment, g_environment_seconds);
  if (course && environment.enabled && environment.day_night && getenv("SIMGOLF_ATMOSPHERE_LOG")) {
    static double logged_seconds=-1;
    if (fabs(g_environment_seconds-logged_seconds)>=1) {
      logged_seconds=g_environment_seconds;
      fprintf(stderr,"weather: cycle host_ms %.3f seconds %.6f tick %u hour %.6f key %.6f daylight %.6f\n",
        environment_now,g_environment_seconds,*(const uint32_t *)(uintptr_t)0x00834170,
        weather_sun.hour,weather_sun.sun_strength,weather_sun.daylight);
    }
  }
  WeatherState weather_state = sample_weather(&environment, course, g_environment_seconds, weather_sun.hour);
  WeatherState report_state=weather_state;report_state.cloud=weather_sun.cloud;
  if(course&&environment.enabled) {
    static int air_loaded;
    if(!air_loaded) {
      char path[MAX_PATH];{ char release_dir[MAX_PATH];snprintf(release_dir,sizeof release_dir,"%s/data",host_exe_dir());release_data_path(path,sizeof path,release_dir,"weather-air.csv"); }
      air_loaded=weather_air_load(&g_air_table,path)?1:-1;
      fprintf(stderr,"weather: display air table %s\n",air_loaded>0?"loaded":"missing or invalid");
    }
    if(air_loaded>0) {
      int property=*(const int *)(uintptr_t)0x0059bf90;
      int theme=*(const uint8_t *)(uintptr_t)0x005a34e0;
      uint32_t tick=*(const uint32_t *)(uintptr_t)0x00834170;
      char place[25]={0};
      if(property>=0&&property<=16)memcpy(place,(const void *)(uintptr_t)(0x00571fd8+property*46),24);
      uint32_t front=g_climate.initialized?g_climate.front:(uint32_t)(g_environment_seconds/180);
      weather_air_update(&g_air,&g_air_table,place,theme,tick,environment.cloud_seed,front,
        g_environment_seconds,report_state.hour,report_state.rain,report_state.cloud);
    }
  } else g_air.initialized=0;
  environment.air_valid=g_air.initialized;environment.celsius=g_air.c;environment.humidity=g_air.rh;
  environment.season=course?(((*(const uint32_t *)(uintptr_t)0x00834170>>10)&7)<3?0:((*(const uint32_t *)(uintptr_t)0x00834170>>10)&7)<6?1:2):0;
  weather_state=sample_weather(&environment,course,g_environment_seconds,weather_sun.hour);
  if(weather_state.enabled&&environment.gusts)water_set_wind(atan2(weather_state.wind_z,weather_state.wind_x),(float)weather_state.wind);
  else water_set_wind(-1000,-1);
  g_frame_environment = environment;
  g_frame_layered = layered;
  g_frame_course = course;
  g_frame_note_size = layered ? g_note_pending_size : 0;
  if (g_frame_note_size > 0) memcpy(g_frame_note, g_note_pending, (size_t)g_frame_note_size);
  g_note_pending_size = 0;
  g_frame_note2_size = layered ? g_note2_pending_size : 0;
  if (g_frame_note2_size > 0) memcpy(g_frame_note2, g_note2_pending, (size_t)g_frame_note2_size);
  g_note2_pending_size = 0;
  g_frame_note3_size = layered ? g_note3_pending_size : 0;
  if (g_frame_note3_size > 0) memcpy(g_frame_note3, g_note3_pending, (size_t)g_frame_note3_size);
  g_note3_pending_size = 0;
  if (course) memcpy(g_mask, mask, (size_t)FRAME_W * FRAME_H);
  lamps_game_frame(&g_frame_lamps);
  if(!layered||!course)g_frame_lamps.enabled=0;
  hd_art_publish(layered, screen);
  InterlockedIncrement(&g_frame_serial);
  LeaveCriticalSection(&g_lock);
  const char *temperature_units=mods_get("environment.temperature_units");
  hudf_publish_weather(course&&environment.enabled&&mods_get_int("environment.report_icon",1),
    weather_condition(&report_state),report_state.night>.5,g_air.initialized,g_air.c,g_air.rh,
    !temperature_units||strcmp(temperature_units,"celsius"));
  weather_audio_publish(&weather_state, environment.weather_volume,
    g_environment_running && environment.sound);
  WeatherGameConfig weather_game = {environment.gameplay && g_environment_running,
    mods_get_int("environment.rain_roll_steps", 1), mods_get_int("environment.storm_roll_bonus", 1),
    mods_get_int("environment.rain_mood_penalty", 1), mods_get_int("environment.storm_mood_bonus", 1)};
  weather_game_publish(&weather_state, &weather_game);
  static uint32_t logged_strike, logged_thunder;
  if (weather_state.event_id && weather_state.event_id != logged_strike && weather_state.strike_age >= 0) {
    logged_strike = weather_state.event_id;
    fprintf(stderr, "weather: lightning event %u seconds %.3f host_ms %.3f\n",
      weather_state.event_id, weather_state.seconds, environment_now);
  }
  if (weather_state.event_id && weather_state.event_id != logged_thunder && weather_state.thunder_age >= 0) {
    logged_thunder = weather_state.event_id;
    fprintf(stderr, "weather: thunder event %u seconds %.3f host_ms %.3f\n",
      weather_state.event_id, weather_state.seconds, environment_now);
  }
  InterlockedIncrement(&g_game_frames);
  return 1;
}

long hd_native_game_frames(void) { return (long)g_game_frames; }

// hdball.c: the round ball needs the layered presenter (the course layer it is drawn over).
int hd_native_balls_ok(void) { return g_active && g_cfg.hd && g_cfg.layers; }

// harness.c "hdshot": the presented picture at canvas size, as a 24-bit BMP.
// Relative names go to SIMGOLF_SHOTS. Returns 0 when this module is off.
int hd_native_shot(const char *name) {
  if (!g_active) return 0;
  const char *dir = getenv("SIMGOLF_SHOTS");
  int abs = name[0] == '/' || name[0] == '\\' || (name[0] && name[1] == ':');
  if (dir && *dir && !abs) snprintf(g_shot_path, sizeof g_shot_path, "%s/%s", dir, name);
  else snprintf(g_shot_path, sizeof g_shot_path, "%s", name);
  ResetEvent(g_shot_done);
  InterlockedExchange(&g_shot_pending, 1);
  if (WaitForSingleObject(g_shot_done, 10000) != WAIT_OBJECT_0) return -1;
  return g_shot_path[0] ? 1 : -1;
}

// harness.c "hddump" (task-68): path starts the dump, NULL ends it. Returns 0 when this module is off.
int hd_native_dump(const char *path) {
  if (!g_active) return 0;
  if (path) snprintf(g_dump_path, sizeof g_dump_path, "%s", path);
  InterlockedExchange(&g_dump_on, path != NULL);
  return 1;
}

// harness.c "windowmouse" (task-15b): the window pixel that map_mouse turns
// into frame point (x, y), so scripts written in frame pixels still go
// through the real window -> frame mapping. Returns 0 when this module is off.
int hd_native_unmap(int *x, int *y) {
  if (!g_active) return 0;
  EnterCriticalSection(&g_lock);
  int ok = g_map_ok && g_map_sx > 0 && g_map_sy > 0;
  if (ok) {
    *x = (int)lroundf((*x + 0.5f + g_map_ox) / g_map_sx);   // lands inside frame pixel x
    *y = (int)lroundf((*y + 0.5f + g_map_oy) / g_map_sy);
  }
  LeaveCriticalSection(&g_lock);
  return ok;
}

// ------------------------------------------------------------------ UI layer API (hd_native.h)

int hd_ui_add_layer(HdUiDraw draw, void *user) {
  LONG i = g_n_ui_layers;
  if (!draw || i >= MAX_UI) return 0;
  g_ui_layers[i].draw = draw;
  g_ui_layers[i].user = user;
  InterlockedIncrement(&g_n_ui_layers);   // published after the entry is complete
  return 1;
}

int hd_ui_add_input(HdUiInput input, void *user) {
  LONG i = g_n_ui_inputs;
  if (!input || i >= MAX_UI) return 0;
  g_ui_inputs[i].input = input;
  g_ui_inputs[i].user = user;
  InterlockedIncrement(&g_n_ui_inputs);
  return 1;
}

int hd_ui_set_top(HdUiDraw draw, HdUiInput input, void *user) {
  if (!draw || !input) return 0;
  g_top_user = user;
  g_top_input = input;
  g_top_draw = draw;   // published last: the render thread tests it
  return 1;
}

void hd_ui_note_set(const void *data, int size) {
  if (!data || size <= 0 || size > HD_NOTE_MAX) {
    g_note_pending_size = 0;
    return;
  }
  memcpy(g_note_pending, data, (size_t)size);
  g_note_pending_size = size;
}

void hd_ui_note2_set(const void *data, int size) {
  if (!data || size <= 0 || size > HD_NOTE_MAX) {
    g_note2_pending_size = 0;
    return;
  }
  memcpy(g_note2_pending, data, (size_t)size);
  g_note2_pending_size = size;
}

void hd_ui_show_golf_ui(int on) { InterlockedExchange(&g_show_golf_ui, on != 0); }

double hd_ui_scale(void) { return g_active ? g_ui_px : 1.0; }

// win32.c's event pump, game thread: native UI sees each event first (mouse
// positions already in canvas pixels) and may keep it from the game.
int hd_ui_event(SDL_Event *e) {
  if (g_top_input && g_top_input(e, (int)g_shown_course, g_top_user)) return 1;
  LONG n = g_n_ui_inputs;
  for (LONG i = n - 1; i >= 0; i--)
    if (g_ui_inputs[i].input(e, (int)g_shown_course, g_ui_inputs[i].user)) return 1;
  return 0;
}

void hd_ui_note3_set(const void *data, int size) {
  if (!data || size <= 0 || size > HD_NOTE3_MAX) {
    g_note3_pending_size = 0;
    return;
  }
  memcpy(g_note3_pending, data, (size_t)size);
  g_note3_pending_size = size;
}
