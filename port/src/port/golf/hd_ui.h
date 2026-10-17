// Course-view UI on wide and tall canvases (task-3 B4/B5), game-thread side.
// See hd_ui.c. Called from translator patches (tools/translate/golf.json) in
// golf's main loop FUN_0040f5c0; ghidra_prelude.h declares the same calls
// for the generated code.

#ifndef SIMGOLF_PORT_GOLF_HD_UI_H
#define SIMGOLF_PORT_GOLF_HD_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The camera a course frame was drawn with and its content shift in canvas
// pixels per camera step: camera X + 1 moves the course by (ax, ay), Y + 1 by
// (bx, by). serial counts the per-frame driver's runs.
// (ox, oy): the sub-tile camera's offset the course was drawn at (task-37).
typedef struct {
  int x, y, zoom, rot;
  double ax, ay, bx, by;
  uint32_t serial;
  int ox, oy;
  uint32_t drag_serial;
} HdUiCamera;

// Draw golf's 800x600-laid-out UI at (dx, dy) of the back buffer until
// hd_ui_view_end. Returns 0 (and does nothing) for (0, 0) or when the view
// would not fit. Not nested.
int hd_ui_view_begin(int dx, int dy);
void hd_ui_view_end(void);
int hd_ui_staff_circle(int x, int y, int radius, int color);

// The camera of the course being drawn (before the per-frame driver).
void hd_ui_frame_camera(void);
// The back buffer's pixels (16 bpp), pitch in pixels and size; NULL when it is not the
// canvas-sized surface (or a view is active). Game thread.
uint16_t *hd_ui_back_bits(int *pitch, int *w, int *h);
// golf finished the course part of the frame; its UI comes next.
void hd_ui_course_done(void);
// Keeps a view wider or taller than 800x600 from showing more black past the
// map edge than the original view could.
void hd_ui_camera_clamp(void);

// Sub-tile camera (task-37). golf's camera is a whole tile; the view may rest
// between tiles. hd_cam_ox/oy is this frame's course offset in canvas pixels
// on top of golf's camera: worldToScreen (FUN_0042fb90) adds it and
// screenToWorld (FUN_00430020) takes it off (translator patches), and the
// terrain is drawn with it (terrain_set_view_offset), so clicks, hover and
// build tools pick the tile under the pointer. Taken from the continuous
// camera at the start of each course frame (hd_ui_frame_camera); a camera
// move golf makes itself (shot follow, edge scroll, a load) snaps back to its
// whole tile. Off (hd_ui_set_subtile(0), display.smooth=0): always 0.
extern int hd_cam_ox, hd_cam_oy;
void hd_ui_set_subtile(int on);
// Overscan (task-68). golf skips a tile's objects when the tile centre is
// more than 64 px past the side edges or 42 px past the top and bottom
// (FUN_0042f940), and golfers, balls and the like as soon as their anchor
// point is off screen (FUN_0042fb90, margin 0): tall palms and houses whose
// base is just below the bottom edge were missing though their tops showed,
// and the smooth scroll's uncovered edge showed them come and go. While golf
// draws the course part of a frame (hd_ui_frame_camera to
// hd_ui_course_done) both tests reach hd_os_l/r/t/b canvas px further
// (translator patches); 0 everywhere else, so picking, sounds and the game
// see the original tests. tiles: left and right that many tile widths, the
// top a quarter of that, the bottom double (tall sprites stand up from their
// base); 0 = off (display.overscan, only with smooth scrolling).
extern int hd_os_l, hd_os_r, hd_os_t, hd_os_b;
void hd_ui_set_overscan(int tiles);
int hd_ui_subtile(void);
// The view in camera tiles (golf's camera plus the sub-tile part), and moving
// it: kept on the map as golf's own and the wide-view clamp keep the camera;
// golf's camera gets the nearest tile.
void hd_ui_camera_get(double *tx, double *ty);
void hd_ui_camera_set(double tx, double ty);
// Camera steps along its X and Y tile axes that move the course by (dx, dy)
// canvas px at the current zoom and rotation.
void hd_ui_content_to_tiles(double dx, double dy, double *tx, double *ty);
// Tests (harness "pick"): logs the tile golf's screenToWorld finds at canvas
// point (x, y), the 5x5 terrain types around it, the camera and its offset.
void hd_ui_pick_log(int x, int y);

// Zoom steps (task-56). golf's zoom is 1, 2 or 4; hd_ui_zoom_set changes it in
// one go (what the z and x commands do) and moves the camera so the point of
// the course under canvas pixel (px, py) stays under it, or under the view
// centre when use_pointer is 0 (golf's own zoom keeps the centre). It needs
// the sub-tile camera. Returns 1 when the zoom changed, 0 when it did not
// (already at the end of the range, or golf's 2x mode is on: the caller then
// uses golf's own command). The presenter (hd_native.c) is told through
// hd_ui_zoom_hook so it can glide between the two views; clicks and hovers
// that arrive during the glide are turned from the picture on screen into
// golf's view by hd_ui_zoom_map_hook.
typedef struct {
  int z0, z1;           // golf's zoom before and after
  double cx1, cy1;      // golf's camera after, in tiles (continuous: the sub-tile part included)
  int rot;
} HdUiZoom;
// NULL when there is no presenter. Called with NULL, returns 1 when smooth
// zoom is on; called with an event, returns 1 when the presenter takes it
// (it has made a snapshot of the picture on screen and will glide).
extern int (*hd_ui_zoom_hook)(const HdUiZoom *z);
// Canvas pixel (*x, *y) on the picture on screen -> the same course point in
// golf's view. Returns 1 when it moved the point; 0 when it did nothing (no
// glide, or the point is on golf's UI).
extern int (*hd_ui_zoom_map_hook)(int *x, int *y);
int hd_ui_zoom_set(int new_zoom, int use_pointer, int px, int py);
// golf's zoom and camera (tiles, continuous) right now.
void hd_ui_game_view(int *zoom, double *cx, double *cy);

// Presenter hook (native: hd_native.c): the back buffer at hd_ui_course_done
// (canvas size, pitch in pixels) and the camera it was drawn with.
typedef void (*HdUiCourseHook)(const uint16_t *bits, int pitch, const HdUiCamera *cam);
extern HdUiCourseHook hd_ui_course_hook;
// The back buffer as golf allocated it (not moved by a view): bits address,
// pitch in pixels, size. 0 when there is none yet.
int hd_ui_back_buffer(uint32_t *bits, int *pitch, int *w, int *h);

#ifdef __cplusplus
}
#endif
#endif  // SIMGOLF_PORT_GOLF_HD_UI_H
