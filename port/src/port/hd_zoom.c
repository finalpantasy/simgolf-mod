// Smooth zoom maths (simgolf mod, task-56), see hd_zoom.h.

#include "port/hd_zoom.h"

#include <math.h>
#include <string.h>

double hdz_ease(double x) {
  if (x <= 0) return 0;
  if (x >= 1) return 1;
  return sin(x * 1.5707963267948966);   // out sine: quick to answer, settles gently
}

void hdz_frame_cam(const HdUiCamera *f, double *cx, double *cy) {
  double det = f->ax * f->by - f->bx * f->ay;
  double fx = 0, fy = 0;
  if (fabs(det) > 1e-9) {
    fx = (f->ox * f->by - f->bx * f->oy) / det;
    fy = (f->ax * f->oy - f->ox * f->ay) / det;
  }
  *cx = f->x + fx;
  *cy = f->y + fy;
}

void hdz_view(const HdZoom *g, double now, double *v, double *cx, double *cy) {
  double x = g->dur > 0 ? (now - g->t0) / g->dur : 1;
  double e = hdz_ease(x);
  *v = g->v0 * pow(g->v1 / g->v0, e);
  double u0 = 1 / g->v0, u1 = 1 / g->v1, u = 1 / *v;
  double f = fabs(u1 - u0) > 1e-9 ? (u - u0) / (u1 - u0) : e;
  *cx = g->c0x + (g->c1x - g->c0x) * f;
  *cy = g->c0y + (g->c1y - g->c0y) * f;
}

// Give stalled frames back to the glide. The default limits every clock step,
// including the 12..20 ms gap that the original guard let through unchanged.
void hdz_wait_frame(HdZoom *g, double now, int fixed) {
  double elapsed = now - g->last_t;
  if (elapsed > (fixed ? 12 : 20)) g->t0 += elapsed - 12;
}

// Waiting for a new picture consumes no glide time. A frame already refunded
// part of its delay above, so replace that refund instead of adding it twice.
void hdz_wait_picture(HdZoom *g, double now, double before, int fixed) {
  double elapsed = now - g->last_t;
  if (fixed) g->t0 = before + elapsed;
  else g->t0 += elapsed;
}

int hdz_over(const HdZoom *g, double now) { return now >= g->t0 + g->dur; }

void hdz_begin(HdZoom *g, double now, const HdUiCamera *live, double slide_x, double slide_y, const HdUiZoom *e,
               double canvas_w, double canvas_h, double dur_ms) {
  double v0, c0x, c0y;
  if (g->active && g->rot == e->rot) {
    hdz_view(g, now, &v0, &c0x, &c0y);
  } else {
    v0 = live->zoom;
    hdz_frame_cam(live, &c0x, &c0y);
    // the smooth scroll still has the picture on its way: the view on screen is the camera that would put it there
    double det = live->ax * live->by - live->bx * live->ay;
    if (fabs(det) > 1e-9) {
      c0x += (slide_x * live->by - live->bx * slide_y) / det;
      c0y += (live->ax * slide_y - slide_x * live->ay) / det;
    }
    g->ax = live->ax / live->zoom;
    g->ay = live->ay / live->zoom;
    g->bx = live->bx / live->zoom;
    g->by = live->by / live->zoom;
  }
  g->active = 1;
  g->rot = e->rot;
  g->cx = canvas_w / 2;
  g->cy = canvas_h / 2;
  g->v0 = v0;
  g->v1 = e->z1;
  g->z_end = e->z1;
  g->c0x = c0x;
  g->c0y = c0y;
  g->c1x = e->cx1;
  g->c1y = e->cy1;
  double oct = fabs(log2(g->v1 / g->v0));
  oct = oct < 0.25 ? 0.25 : oct > 2.5 ? 2.5 : oct;
  g->dur = dur_ms * (0.65 + 0.35 * oct);
  g->t0 = now;
  g->born = now;
  g->last_t = now;
}

void hdz_xform(const HdZoom *g, double now, int zoom, double fcx, double fcy, double *s, double *x, double *y) {
  double v, cx, cy;
  hdz_view(g, now, &v, &cx, &cy);
  double sc = v / zoom;
  double dx = cx - fcx, dy = cy - fcy;
  *s = sc;
  *x = g->cx * (1 - sc) + v * (dx * g->ax + dy * g->bx);
  *y = g->cy * (1 - sc) + v * (dx * g->ay + dy * g->by);
}
