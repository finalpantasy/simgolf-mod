// Frosted Mint UI kit: line icons, see ui_icons.h.
#include "port/ui/ui_icons.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Shapes separated by ';': an SVG path ("M..."), "O cx cy r" circle,
// "E cx cy rx ry" ellipse, "R x y w h rx" rounded rectangle.
static const struct { const char *name, *src; } k_icons[UI_ICON_COUNT] = {
    [UI_ICON_NONE] = {"none", ""},
    [UI_ICON_GOLF_BALL] = {"golf-ball", "O12 10 7;M9 8h.1M14 7h.1M11 12h.1M15 11h.1M12 17v4M8 21h8"},
    [UI_ICON_HEART] = {"heart", "M12 21L3 12a5 5 0 0 1 9-5a5 5 0 0 1 9 5z"},
    [UI_ICON_PLAY] = {"play", "M7 5l12 7-12 7z"},
    [UI_ICON_PAUSE] = {"pause", "M8 5v14M16 5v14"},
    [UI_ICON_FAST] = {"fast", "M4 6l8 6-8 6zM12 6l8 6-8 6z"},
    [UI_ICON_PLUS] = {"plus", "M12 5v14M5 12h14"},
    [UI_ICON_MINUS] = {"minus", "M5 12h14"},
    [UI_ICON_ROTL] = {"rotl", "M4 12a8 8 0 1 0 2.3-5.6M4 4v4h4"},
    [UI_ICON_ROTR] = {"rotr", "M20 12a8 8 0 1 1-2.3-5.6M20 4v4h-4"},
    [UI_ICON_UNDO] = {"undo", "M9 14L4 9l5-5M4 9h10a6 6 0 0 1 0 12h-3"},
    [UI_ICON_REDO] = {"redo", "M15 14l5-5-5-5M20 9H10a6 6 0 0 0 0 12h3"},
    [UI_ICON_FLAG] = {"flag", "M6 21V3M6 4h11l-2 4 2 4H6"},
    [UI_ICON_HOUSE] = {"house", "M3 11l9-7 9 7M5 10v10h14V10M10 20v-6h4v6"},
    [UI_ICON_MOUNTAIN] = {"mountain", "M2 20l7-12 4 6 3-4 6 10z"},
    [UI_ICON_USERS] = {"users", "O9 8 3.5;M2.5 20a6.5 6.5 0 0 1 13 0M16 4.5a3.5 3.5 0 0 1 0 7M18 14a6 6 0 0 1 3.5 6"},
    [UI_ICON_CHART] = {"chart", "M4 20V10M10 20V4M16 20v-7M22 20H2"},
    [UI_ICON_GEAR] = {"gear", "O12 12 3;M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1"},
    [UI_ICON_COINS] = {"coins", "E10 7 6 3;M4 7v5c0 1.7 2.7 3 6 3s6-1.3 6-3V7M4 12v5c0 1.7 2.7 3 6 3 1.4 0 2.6-.2 3.6-.6M20 11.5c0 1.6-2.4 2.9-5.5 3"},
    [UI_ICON_SMILE] = {"smile", "O12 12 9;M8 14a5 5 0 0 0 8 0M9 9.5h.01M15 9.5h.01"},
    [UI_ICON_TROPHY] = {"trophy", "M7 4h10v5a5 5 0 0 1-10 0zM7 6H4a3 3 0 0 0 3 4M17 6h3a3 3 0 0 1-3 4M12 14v4M8 21h8M9 18h6"},
    [UI_ICON_STAR] = {"star", "M12 3l2.7 5.6 6.1.9-4.4 4.3 1 6.1L12 17l-5.4 2.9 1-6.1-4.4-4.3 6.1-.9z"},
    [UI_ICON_FOLDER] = {"folder", "M3 6h6l2 2h10v11H3z"},
    [UI_ICON_SPARKLE] = {"sparkle", "M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8zM19 16l.8 2.2L22 19l-2.2.8L19 22l-.8-2.2L16 19l2.2-.8z"},
    [UI_ICON_PALETTE] = {"palette", "M12 3a9 9 0 1 0 0 18c1.5 0 2-1 2-2 0-1.5-1.5-2 0-3.5 1-1 7 1 7-4.5C21 6.6 17 3 12 3z;O7.5 11 1;O10.5 7 1;O15.5 7.5 1"},
    [UI_ICON_CUP] = {"cup", "M6 3h12l-1 7a5 5 0 0 1-10 0zM12 15v4M8 21h8"},
    [UI_ICON_DOOR] = {"door", "M14 3H5v18h9M10 12h11M17 8l4 4-4 4"},
    [UI_ICON_BACK] = {"back", "M15 5l-7 7 7 7"},
    [UI_ICON_CHEV] = {"chev", "M9 5l7 7-7 7"},
    [UI_ICON_LOCK] = {"lock", "R5 11 14 10 2;M8 11V8a4 4 0 0 1 8 0v3"},
    [UI_ICON_X] = {"x", "M6 6l12 12M18 6L6 18"},
    [UI_ICON_EYE] = {"eye", "M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z;O12 12 3"},
    [UI_ICON_CAMERA] = {"camera", "M3 8h4l2-3h6l2 3h4v12H3z;O12 13 4"},
    [UI_ICON_BELL] = {"bell", "M6 16V11a6 6 0 0 1 12 0v5l2 2H4zM10 21h4"},
    [UI_ICON_INFO] = {"info", "O12 12 9;M12 11v6M12 7.5h.01"},
    [UI_ICON_PATH] = {"path", "M5 21c1-5 6-5 7-9s5-5 7-9"},
    [UI_ICON_TREE] = {"tree", "M12 21v-5M12 3l-6 9h4l-4 4h12l-4-4h4z"},
    [UI_ICON_PIN] = {"pin", "M12 21s-7-6.2-7-12a7 7 0 0 1 14 0c0 5.8-7 12-7 12z;O12 9 2.5"},
    [UI_ICON_CHECK] = {"check", "M5 12.5l5 5L20 7"},
    [UI_ICON_UP] = {"up", "M5 15l7-7 7 7"},
    [UI_ICON_DOWN] = {"down", "M5 9l7 7 7-7"},
    [UI_ICON_TRASH] ={"trash", "M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13M10 11v6M14 11v6"},
    [UI_ICON_WEATHER_SUN] = {"weather_sun", "O12 12 4;M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.5 1.5M17.5 17.5L19 19M5 19l1.5-1.5M17.5 6.5L19 5"},
    [UI_ICON_WEATHER_MOON] = {"weather_moon", "M20 14a8.5 8.5 0 0 1-10-10A8.5 8.5 0 1 0 20 14z"},
    [UI_ICON_WEATHER_PARTLY] = {"weather_partly", "O8 8 3;M8 1v1M1 8h1M3 3l1 1M13 3l-1 1M6 20a4 4 0 0 1-1-8 6 6 0 0 1 11-1 4.5 4.5 0 1 1 2 9z"},
    [UI_ICON_WEATHER_CLOUD] = {"weather_cloud", "M6 19a4 4 0 0 1-1-8 6 6 0 0 1 11-1 4.5 4.5 0 1 1 2 9z"},
    [UI_ICON_WEATHER_RAIN] = {"weather_rain", "M5 15a3.5 3.5 0 0 1 0-7 5.5 5.5 0 0 1 10-1 4 4 0 0 1 4 8M7 18l-1 3M12 17l-1 3M17 18l-1 3"},
    [UI_ICON_WEATHER_STORM] = {"weather_storm", "M5 15a3.5 3.5 0 0 1 0-7 5.5 5.5 0 0 1 10-1 4 4 0 0 1 4 8M13 12l-4 6h4l-2 5 7-8h-5z"},
};

const char *ui_icon_name(UiIcon i) { return i >= 0 && i < UI_ICON_COUNT ? k_icons[i].name : "none"; }

UiIcon ui_icon_by_name(const char *name) {
  for (int i = 1; i < UI_ICON_COUNT; i++)
    if (!strcmp(k_icons[i].name, name)) return (UiIcon)i;
  return UI_ICON_NONE;
}

// ------------------------------------------------------------------ flattening

typedef struct { float *x0, *y0, *x1, *y1; int n, cap; float px, py; } Segs;

static void seg(Segs *s, float x0, float y0, float x1, float y1) {
  if (s->n == s->cap) {
    s->cap = s->cap ? s->cap * 2 : 128;
    s->x0 = realloc(s->x0, sizeof(float) * s->cap);
    s->y0 = realloc(s->y0, sizeof(float) * s->cap);
    s->x1 = realloc(s->x1, sizeof(float) * s->cap);
    s->y1 = realloc(s->y1, sizeof(float) * s->cap);
  }
  s->x0[s->n] = x0, s->y0[s->n] = y0, s->x1[s->n] = x1, s->y1[s->n] = y1;
  s->n++;
}

static void line_to(Segs *s, float x, float y) {
  seg(s, s->px, s->py, x, y);
  s->px = x, s->py = y;
}

static void cubic(Segs *s, float x1, float y1, float x2, float y2, float x, float y) {
  float x0 = s->px, y0 = s->py;
  for (int i = 1; i <= 16; i++) {
    float t = i / 16.0f, u = 1 - t;
    line_to(s, u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x,
            u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y);
  }
}

static void quad(Segs *s, float x1, float y1, float x, float y) {
  float x0 = s->px, y0 = s->py;
  for (int i = 1; i <= 12; i++) {
    float t = i / 12.0f, u = 1 - t;
    line_to(s, u * u * x0 + 2 * u * t * x1 + t * t * x, u * u * y0 + 2 * u * t * y1 + t * t * y);
  }
}

static float vang(float ux, float uy, float vx, float vy) {
  float a = atan2f(uy, ux), b = atan2f(vy, vx), d = b - a;
  while (d > 3.14159265f) d -= 6.2831853f;
  while (d < -3.14159265f) d += 6.2831853f;
  return d;
}

// SVG elliptical arc, endpoint to centre form (SVG 1.1 F.6.5), no rotation needed here.
static void arc(Segs *s, float rx, float ry, float rot, int large, int sweep, float x, float y) {
  float x1 = s->px, y1 = s->py;
  if (rx == 0 || ry == 0 || (x1 == x && y1 == y)) {
    line_to(s, x, y);
    return;
  }
  rx = fabsf(rx), ry = fabsf(ry);
  float c = cosf(rot * 3.14159265f / 180), sn = sinf(rot * 3.14159265f / 180);
  float dx = (x1 - x) / 2, dy = (y1 - y) / 2;
  float xp = c * dx + sn * dy, yp = -sn * dx + c * dy;
  float lam = xp * xp / (rx * rx) + yp * yp / (ry * ry);
  if (lam > 1) rx *= sqrtf(lam), ry *= sqrtf(lam);
  float num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp;
  float den = rx * rx * yp * yp + ry * ry * xp * xp;
  float k = den > 0 && num > 0 ? sqrtf(num / den) : 0;
  if (large == sweep) k = -k;
  float cxp = k * rx * yp / ry, cyp = -k * ry * xp / rx;
  float cx = c * cxp - sn * cyp + (x1 + x) / 2, cy = sn * cxp + c * cyp + (y1 + y) / 2;
  float t1 = vang(1, 0, (xp - cxp) / rx, (yp - cyp) / ry);
  float dt = vang((xp - cxp) / rx, (yp - cyp) / ry, (-xp - cxp) / rx, (-yp - cyp) / ry);
  if (!sweep && dt > 0) dt -= 6.2831853f;
  if (sweep && dt < 0) dt += 6.2831853f;
  int n = (int)(fabsf(dt) / 0.12f) + 2;
  for (int i = 1; i <= n; i++) {
    float t = t1 + dt * i / n;
    float ex = rx * cosf(t), ey = ry * sinf(t);
    if (i == n) line_to(s, x, y);
    else line_to(s, c * ex - sn * ey + cx, sn * ex + c * ey + cy);
  }
}

static int num(const char **p, float *out) {
  const char *s = *p;
  while (*s == ' ' || *s == ',') s++;
  char *end;
  float v = strtof(s, &end);
  if (end == s) return 0;
  *out = v;
  *p = end;
  return 1;
}

static void ellipse(Segs *s, float cx, float cy, float rx, float ry) {
  int n = (int)(fmaxf(rx, ry) * 4) + 16;
  s->px = cx + rx, s->py = cy;
  for (int i = 1; i <= n; i++) {
    float t = 6.2831853f * i / n;
    line_to(s, cx + rx * cosf(t), cy + ry * sinf(t));
  }
}

static void path(Segs *s, const char *p) {
  char cmd = 0;
  float sx = 0, sy = 0, cx2 = 0, cy2 = 0;   // subpath start; last control point
  s->px = s->py = 0;
  while (*p) {
    while (*p == ' ' || *p == ',') p++;
    if (!*p) break;
    if (isalpha((unsigned char)*p)) cmd = *p++;
    int rel = islower((unsigned char)cmd);
    float ox = rel ? s->px : 0, oy = rel ? s->py : 0, a[7];
    char up = (char)toupper((unsigned char)cmd);
    int need = up == 'M' || up == 'L' || up == 'T' ? 2 : up == 'H' || up == 'V' ? 1 : up == 'C' ? 6
             : up == 'S' || up == 'Q' ? 4 : up == 'A' ? 7 : 0;
    if (up == 'Z') {
      line_to(s, sx, sy);
      cmd = 0;
      continue;
    }
    if (!need) break;
    int ok = 1;
    for (int i = 0; i < need && ok; i++) ok = num(&p, &a[i]);
    if (!ok) break;
    switch (up) {
      case 'M':
        s->px = sx = a[0] + ox, s->py = sy = a[1] + oy;
        cmd = rel ? 'l' : 'L';
        break;
      case 'L': line_to(s, a[0] + ox, a[1] + oy); break;
      case 'H': line_to(s, a[0] + (rel ? s->px : 0), s->py); break;
      case 'V': line_to(s, s->px, a[0] + (rel ? s->py : 0)); break;
      case 'C':
        cubic(s, a[0] + ox, a[1] + oy, a[2] + ox, a[3] + oy, a[4] + ox, a[5] + oy);
        cx2 = a[2] + ox, cy2 = a[3] + oy;
        break;
      case 'S': {
        float rx = 2 * s->px - cx2, ry = 2 * s->py - cy2;
        cx2 = a[0] + ox, cy2 = a[1] + oy;
        cubic(s, rx, ry, a[0] + ox, a[1] + oy, a[2] + ox, a[3] + oy);
        break;
      }
      case 'Q':
        quad(s, a[0] + ox, a[1] + oy, a[2] + ox, a[3] + oy);
        cx2 = a[0] + ox, cy2 = a[1] + oy;
        break;
      case 'A': arc(s, a[0], a[1], a[2], a[3] != 0, a[4] != 0, a[5] + ox, a[6] + oy); break;
    }
    if (up != 'C' && up != 'S' && up != 'Q') cx2 = s->px, cy2 = s->py;
  }
}

static void build(Segs *s, const char *src) {
  char buf[512];
  while (*src) {
    const char *e = strchr(src, ';');
    size_t n = e ? (size_t)(e - src) : strlen(src);
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, src, n);
    buf[n] = 0;
    float v[5] = {0};
    const char *p = buf + 1;
    if (buf[0] == 'O') {
      for (int i = 0; i < 3; i++) num(&p, &v[i]);
      ellipse(s, v[0], v[1], v[2], v[2]);
    } else if (buf[0] == 'E') {
      for (int i = 0; i < 4; i++) num(&p, &v[i]);
      ellipse(s, v[0], v[1], v[2], v[3]);
    } else if (buf[0] == 'R') {
      for (int i = 0; i < 5; i++) num(&p, &v[i]);
      char rp[200];
      float x = v[0], y = v[1], w = v[2], h = v[3], r = v[4];
      snprintf(rp, sizeof rp, "M%g %gH%gA%g %g 0 0 1 %g %gV%gA%g %g 0 0 1 %g %gH%gA%g %g 0 0 1 %g %gV%gA%g %g 0 0 1 %g %gz",
               x + r, y, x + w - r, r, r, x + w, y + r, y + h - r, r, r, x + w - r, y + h, x + r, r, r, x, y + h - r,
               y + r, r, r, x + r, y);
      path(s, rp);
    } else {
      path(s, buf);
    }
    src += n + (e ? 1 : 0);
    if (!e) break;
  }
}

int ui_icon_raster(UiIcon i, int size, float stroke, uint8_t *alpha) {
  memset(alpha, 0, (size_t)size * size);
  if (i <= UI_ICON_NONE || i >= UI_ICON_COUNT || size <= 0) return 0;
  Segs s = {0};
  build(&s, k_icons[i].src);
  float k = size / 24.0f, hw = stroke * k / 2;
  if (hw < 0.6f) hw = 0.6f;
  for (int n = 0; n < s.n; n++) {
    float x0 = s.x0[n] * k, y0 = s.y0[n] * k, x1 = s.x1[n] * k, y1 = s.y1[n] * k;
    int bx0 = (int)floorf(fminf(x0, x1) - hw - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + hw + 1);
    int by0 = (int)floorf(fminf(y0, y1) - hw - 1), by1 = (int)ceilf(fmaxf(y0, y1) + hw + 1);
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > size) bx1 = size;
    if (by1 > size) by1 = size;
    float dx = x1 - x0, dy = y1 - y0, ll = dx * dx + dy * dy;
    for (int y = by0; y < by1; y++)
      for (int x = bx0; x < bx1; x++) {
        float px = x + 0.5f - x0, py = y + 0.5f - y0;
        float t = ll > 0 ? (px * dx + py * dy) / ll : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float ex = px - t * dx, ey = py - t * dy, d = sqrtf(ex * ex + ey * ey);
        float cov = hw + 0.5f - d;
        if (cov <= 0) continue;
        int a = cov >= 1 ? 255 : (int)(cov * 255 + 0.5f);
        uint8_t *o = &alpha[(size_t)y * size + x];
        if (a > *o) *o = (uint8_t)a;
      }
  }
  int n = s.n;
  free(s.x0), free(s.y0), free(s.x1), free(s.y1);
  return n;
}
