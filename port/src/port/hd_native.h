// Native presentation (simgolf mod, task-3): hd_native.c presents, and
// hd_terrain_native.c draws the course terrain at screen resolution.

#ifndef SIMGOLF_PORT_HD_NATIVE_H
#define SIMGOLF_PORT_HD_NATIVE_H

#include <SDL.h>
#include <stdint.h>

// The game's canvas, WIN_SCREEN_W/H (win32.h clashes with windows.h).
extern int hd_frame_w, hd_frame_h;
#define HD_FRAME_W hd_frame_w
#define HD_FRAME_H hd_frame_h

// Game thread. Fills mask (HD_FRAME_W x HD_FRAME_H: 255 bare terrain, 128
// terrain in shadow, 0 anything else) and returns 1 when the frame shows the
// course (the whole canvas is shown; other pages only use 800x600 of it);
// hands a newly recorded terrain scene to the render thread when the layer
// is on. frame: the serial the frame carrying this terrain will have.
void hdt_init(int on);
int hdt_capture(const uint16_t *screen, uint8_t *mask, long frame);

// Render thread. The latest terrain scene drawn into a w x h texture
// (straight alpha = coverage), or NULL when there is none yet. shown: the
// serial of the course frame on screen; a newer scene waits for its frame
// (task-68: the ground and the course must move on the same presented frame).
SDL_Texture *hdt_render(SDL_Renderer *r, int w, int h, long shown);
SDL_Texture *hdt_render_guard(SDL_Renderer *r, long shown);

// ------------------------------------------------------------------ UI layer (task-3 B4)
//
// The presenter draws, bottom to top: the course layer (terrain at screen
// resolution, objects, golfers; slides while scrolling), golf's own UI layer
// (what golf drew after the course: info box, meters, wheel and panels,
// tooltips), then the native UI layers below, at screen resolution. The
// Frosted Mint UI (task-26 to task-32) is one of these.
//
// Canvas pixels are golf's (win_screen_w x win_screen_h in the course view,
// the top-left 800x600 on title, menus and full-screen pages); view says
// where that area is on the target, px how many target pixels one canvas
// pixel covers (the UI scale in use). A layer draws with plain SDL_Render*
// calls; the target and clip are reset after each layer.
typedef struct {
  SDL_Renderer *renderer;
  SDL_Texture *target;   // NULL: the window
  SDL_Rect view;         // the canvas area on the target
  float px;              // target px per canvas px
  int canvas_w, canvas_h;
  int course;            // 1: the course view; 0: title, menus, full-screen pages
  double now_ms;
  // task-31: what is in the course layer of this frame moves. slide_x/y: where
  // the course layer is now relative to its place, in canvas px (smooth
  // scrolling slides it; native things pinned to the course add this). note:
  // the blob the game thread attached to the frame (hd_ui_note_set), NULL
  // when it attached none or the frame is not a layered course frame.
  int layered;           // the course and golf's UI are separate layers (course frames with layers=1)
  float slide_x, slide_y;
  const void *note;
  int note_size;
  const void *note2;   // task-53: a second blob (hd_ui_note2_set), the cart path view's
  int note2_size;
  const void *note3;   // task-115: a third blob (hd_ui_note3_set), the shot aim arrows'
  int note3_size;
  // task-56: a zoom glide places the course picture. A point p of the note
  // (canvas px) is at zoom_s * p + (zoom_x, zoom_y) now; slide_x/y are 0
  // then. zoom_on 0 (the rest of the time): zoom_s is 0 and unused.
  int zoom_on;
  float zoom_s, zoom_x, zoom_y;
} HdUiFrame;
typedef void (*HdUiDraw)(const HdUiFrame *f, void *user);
// Input on the game thread before golf sees it (win32.c's pump); mouse
// positions are canvas pixels. Return 1 to keep the event from the game.
typedef int (*HdUiInput)(SDL_Event *e, int course, void *user);

int hd_ui_add_layer(HdUiDraw draw, void *user);   // up to 8, drawn in the order added
int hd_ui_add_input(HdUiInput input, void *user); // up to 8, the last added asked first
// The top layer (the Options page, card task-24): drawn after every other layer and asked first for input,
// whatever was added when. One slot; returns 1 when set.
int hd_ui_set_top(HdUiDraw draw, HdUiInput input, void *user);
// Game thread: attaches a blob (up to HD_NOTE_MAX bytes) to the frame being
// finished; the presenter hands it back as HdUiFrame.note together with the
// picture it belongs to, so a layer can pin things to what is in the course
// layer (golfers) without a frame of lag. Cleared by each present.
#define HD_NOTE_MAX 65536
void hd_ui_note_set(const void *data, int size);
#define HD_NOTE3_MAX 4096
void hd_ui_note3_set(const void *data, int size);   // task-115: a third channel for the shot aim arrows
void hd_ui_note2_set(const void *data, int size);   // task-53: a second channel, so two features can each attach a note
void hd_ui_show_golf_ui(int on);                  // 0 hides golf's UI layer (course view)
double hd_ui_scale(void);                         // screen px per canvas px (1 when off)
long hd_native_game_frames(void);                 // game frames presented so far (task-106)
int hd_ui_event(SDL_Event *e);                    // win32.c
// The window mouse position of the event being delivered, in canvas pixels but
// not clamped to the picture (on title and menu pages the 800x600 picture sits
// in the middle of a wider window; full-screen native screens need all of it).
extern volatile int hd_raw_mouse_x, hd_raw_mouse_y;

#endif  // SIMGOLF_PORT_HD_NATIVE_H
