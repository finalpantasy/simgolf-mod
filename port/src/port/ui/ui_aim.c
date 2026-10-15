// Frosted Mint shot aim arrows (task-115): see ui_aim.h.
#include "port/ui/ui_aim.h"

#include <math.h>
#include <string.h>

#include "port/ui/ui_draw.h"

static float pin_x(const PvMap *m, float x) {
  return m->view_x + ((m->zoom_on ? m->zoom_s * x + m->zoom_x : x + m->slide_x)) * m->px;
}
static float pin_y(const PvMap *m, float y) {
  return m->view_y + ((m->zoom_on ? m->zoom_s * y + m->zoom_y : y + m->slide_y)) * m->px;
}

typedef struct { float x, y; } P;

// Catmull-Rom through p[0..n-1], `sub` pieces per span; returns the number of points written to out
static int smooth(const P *p, int n, P *out, int sub) {
  int k = 0;
  for (int i = 0; i + 1 < n; i++) {
    P a = p[i > 0 ? i - 1 : 0], b = p[i], c = p[i + 1], d = p[i + 2 < n ? i + 2 : n - 1];
    for (int s = 0; s < sub; s++) {
      float u = (float)s / (float)sub, u2 = u * u, u3 = u2 * u;
      out[k].x = 0.5f * ((2 * b.x) + (-a.x + c.x) * u + (2 * a.x - 5 * b.x + 4 * c.x - d.x) * u2 + (-a.x + 3 * b.x - 3 * c.x + d.x) * u3);
      out[k].y = 0.5f * ((2 * b.y) + (-a.y + c.y) * u + (2 * a.y - 5 * b.y + 4 * c.y - d.y) * u2 + (-a.y + 3 * b.y - 3 * c.y + d.y) * u3);
      k++;
    }
  }
  out[k++] = p[n - 1];
  return k;
}

static void poly(UiDraw *d, const P *p, int n, float w, UiColor col) {
  for (int i = 0; i + 1 < n; i++) uid_line(d, p[i].x, p[i].y, p[i + 1].x, p[i + 1].y, w, col);
  for (int i = 1; i + 1 < n; i++) uid_rrect(d, (UiRect){p[i].x - w / 2, p[i].y - w / 2, w, w}, w / 2, col);   // round joins
}

static void ring(UiDraw *d, float cx, float cy, float rx, float ry, float w, UiColor col, UiColor fill) {
  enum { N = 28 };
  P q[N + 1];
  for (int i = 0; i <= N; i++) {
    float a = (float)i * 6.2831853f / N;
    q[i].x = cx + cosf(a) * rx;
    q[i].y = cy + sinf(a) * ry;
  }
  for (int i = 0; i < N; i++) uid_triangle(d, cx, cy, q[i].x, q[i].y, q[i + 1].x, q[i + 1].y, fill);
  poly(d, q, N + 1, w, col);
}

void ui_aim_draw(UiCtx *c, const AimNote *n, const PvMap *m, double t_ms) {
  if (n->n < 2 || n->n > AIM_PTS) return;
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  P e[AIM_PTS];
  for (int i = 0; i < n->n; i++) {
    e[i].x = pin_x(m, n->ex[i]);
    e[i].y = pin_y(m, n->ey[i]);
  }
  P curve[AIM_PTS * 12 + 2];
  int nc = smooth(e, n->n, curve, 10);
  float s = ui_s(c, 1.0f);
  UiColor mint = t->accent, dark = {10, 24, 20, 120}, white = {255, 255, 255, 235};
  // the ground track: a thin dashed trace under the arc, so a fade or a draw reads on the grass
  if (n->ng >= 2 && n->ng <= AIM_PTS) {
    P g[AIM_PTS], gc[AIM_PTS * 12 + 2];
    for (int i = 0; i < n->ng; i++) {
      g[i].x = pin_x(m, n->gx[i]);
      g[i].y = pin_y(m, n->gy[i]);
    }
    int ng = smooth(g, n->ng, gc, 10);
    UiColor gcol = uid_alpha(mint, 0.55f);
    for (int i = 0; i + 1 < ng; i += 2) {
      uid_line(d, gc[i].x, gc[i].y + s, gc[i + 1].x, gc[i + 1].y + s, 3.0f * s, (UiColor){0, 0, 0, 60});
      uid_line(d, gc[i].x, gc[i].y, gc[i + 1].x, gc[i + 1].y, 1.8f * s, gcol);
    }
  }
  // where it lands: a ring on the ground under the end of the arc
  float lx = e[n->n - 1].x, ly = e[n->n - 1].y;
  if (n->ng >= n->n) {
    lx = pin_x(m, n->gx[n->n - 1]);
    ly = pin_y(m, n->gy[n->n - 1]);
  }
  (void)t_ms;
  // A small fixed target leaves the actual landing point visible at every zoom.
  float rr = 5.0f * s;
  ring(d, lx, ly, rr, rr * 0.5f, 1.3f * s, mint, (UiColor){0, 0, 0, 0});
  uid_rrect(d, (UiRect){lx - 2.2f * s, ly - 2.2f * s, 4.4f * s, 4.4f * s}, 2.2f * s, mint);
  poly(d, curve, nc, 4.4f * s, dark);
  poly(d, curve, nc, 2.0f * s, white);
  // The original preview's flight samples make height and sideways bend readable.
  for (int i = 1; i < n->n; i++) {
    uid_rrect(d, (UiRect){e[i].x - 3.0f * s, e[i].y - 3.0f * s, 6.0f * s, 6.0f * s}, 3.0f * s, dark);
    uid_rrect(d, (UiRect){e[i].x - 1.8f * s, e[i].y - 1.8f * s, 3.6f * s, 3.6f * s}, 1.8f * s, white);
  }
  // the head
  P a = curve[nc - 1], b = curve[nc - 1 - (nc > 5 ? 4 : 1)];
  float dx = a.x - b.x, dy = a.y - b.y, len = sqrtf(dx * dx + dy * dy);
  if (len > 0.01f) {
    dx /= len;
    dy /= len;
    float hl = 16 * s, hw = 9 * s, nx = -dy, ny = dx;
    // End the arrow at golf's final sample. Extending beyond it moves the visible
    // target past the cup on short shots even when the underlying aim is correct.
    P tip = a, base = {a.x - dx * hl, a.y - dy * hl};
    uid_triangle(d, tip.x, tip.y, base.x + nx * (hw + 1.5f * s), base.y + ny * (hw + 1.5f * s),
                 base.x - nx * (hw + 1.5f * s), base.y - ny * (hw + 1.5f * s), dark);
    uid_triangle(d, tip.x, tip.y, base.x + nx * hw, base.y + ny * hw, base.x - nx * hw, base.y - ny * hw, mint);
  }
  // the start: a small dot where the ball is
  uid_rrect(d, (UiRect){e[0].x - 4 * s, e[0].y - 4 * s, 8 * s, 8 * s}, 4 * s, dark);
  uid_rrect(d, (UiRect){e[0].x - 2.6f * s, e[0].y - 2.6f * s, 5.2f * s, 5.2f * s}, 2.6f * s, mint);
}
