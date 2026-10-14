// Smooth zoom (simgolf mod, task-56): the view between golf's zoom levels.
//
// golf draws the course at zoom 1, 2 or 4 only. When the camera controls
// change the zoom (golf/hd_ui.c hd_ui_zoom_set) the presenter does not cut
// from one picture to the other: it plays a short glide, a "visible view"
// with its own zoom v and camera c that goes from the view on screen to the
// new one, and shows whichever pictures golf has made scaled to fit it.
//
// The maths, in canvas pixels. A golf picture drawn at zoom z with camera c
// (tiles, the sub-tile part included) puts world point w at
//     Cc + z K + z L(c - w)
// (Cc the canvas centre, K a constant per rotation, L the linear map from
// tiles to pixels at zoom 1: camera step A moves the course by z A, step B by
// z B). The view (v, cv) puts it at Cc + v K + v L(cv - w), so that picture
// lands on screen as
//     screen = X + s * pixel,  s = v / z,  X = Cc (1 - s) + v L(cv - c)
// (K cancels). Along the glide v goes from the zoom on screen to the target
// by a constant ratio per time (an ease), and the camera follows linearly in
// 1/v, which is what keeps the course point under the pointer in place: the
// camera that does is c = w + L^-1((p - Cc) / v).
//
// Pure functions of the state, no SDL: the presenter (hd_native.c) draws,
// tests/native/zoom_*.c-style checks read the numbers from the frame-step log.

#ifndef SIMGOLF_PORT_HD_ZOOM_H
#define SIMGOLF_PORT_HD_ZOOM_H

#include "port/golf/hd_ui.h"

typedef struct {
  int active;                  // a glide runs or holds (waiting for golf's picture at the end zoom)
  double t0, dur;              // ms
  double born;                 // when it started (the clock may wait: the glide cannot show more than golf has drawn)
  double last_t;               // the time the view was last taken at (the presenter's frame time)
  double v0, v1;               // visible zoom at the start and at the end
  double c0x, c0y, c1x, c1y;   // visible camera (tiles) at the start and at the end
  int z_end, rot;
  double ax, ay, bx, by;       // L: pixels per camera step at zoom 1
  double cx, cy;               // canvas centre
} HdZoom;

// The ease: 0 -> 1 over x in 0..1.
double hdz_ease(double x);
void hdz_wait_frame(HdZoom *g, double now, int fixed);
void hdz_wait_picture(HdZoom *g, double now, double before, int fixed);

// A frame's camera in tiles, sub-tile part included.
void hdz_frame_cam(const HdUiCamera *f, double *cx, double *cy);

// Starts a glide at time now. The view on screen now is either the running
// glide's, or (none running) the live frame's, shifted by (slide_x, slide_y)
// canvas px when the smooth scroll still has the picture on its way. e: where
// golf went. dur_ms: the base duration for one level (a zoom of 2).
void hdz_begin(HdZoom *g, double now, const HdUiCamera *live, double slide_x, double slide_y, const HdUiZoom *e,
               double canvas_w, double canvas_h, double dur_ms);

// The visible view at time now.
void hdz_view(const HdZoom *g, double now, double *v, double *cx, double *cy);

// Where a picture drawn at (zoom, camera fcx/fcy) lands on screen at time now:
// screen = (*x, *y) + *s * pixel.
void hdz_xform(const HdZoom *g, double now, int zoom, double fcx, double fcy, double *s, double *x, double *y);

// The glide is over (its time is up).
int hdz_over(const HdZoom *g, double now);

#endif
