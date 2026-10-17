// Frosted Mint UI kit: drawing primitives, see ui_draw.h.
#include "port/ui/ui_draw.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265358979f
#define MAXSEG 40
#define MAXPTS (4 * (MAXSEG + 1))

UiColor uid_alpha(UiColor c, float a) {
  float v = c.a * a;
  c.a = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
  return c;
}

UiColor uid_mix(UiColor a, UiColor b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  UiColor o = {(uint8_t)(a.r + (b.r - a.r) * t + 0.5f), (uint8_t)(a.g + (b.g - a.g) * t + 0.5f),
               (uint8_t)(a.b + (b.b - a.b) * t + 0.5f), (uint8_t)(a.a + (b.a - a.a) * t + 0.5f)};
  return o;
}

void uid_init(UiDraw *d, SDL_Renderer *r) {
  memset(d, 0, sizeof *d);
  d->r = r;
}

void uid_free(UiDraw *d) {
  for (int i = 0; i < UID_BLUR_LEVELS; i++)
    if (d->lv[i]) SDL_DestroyTexture(d->lv[i]);
  for (int i = 0; i < 2; i++)
    if (d->smooth[i]) SDL_DestroyTexture(d->smooth[i]);
  if (d->full) SDL_DestroyTexture(d->full);
  free(d->v);
  free(d->idx);
  memset(d, 0, sizeof *d);
}

void uid_begin(UiDraw *d, SDL_Texture *target, int w, int h) {
  d->target = target;
  d->w = w;
  d->h = h;
  d->backdrop = NULL;
  d->clip_on = 0;
  SDL_SetRenderTarget(d->r, target);
  SDL_RenderSetClipRect(d->r, NULL);
  SDL_SetRenderDrawBlendMode(d->r, SDL_BLENDMODE_BLEND);
}

// ------------------------------------------------------------------ buffers

static void reserve(UiDraw *d, int nv, int ni) {
  if (d->nv + nv > d->vcap) {
    d->vcap = (d->nv + nv) * 2;
    d->v = realloc(d->v, sizeof *d->v * d->vcap);
  }
  if (d->ni + ni > d->icap) {
    d->icap = (d->ni + ni) * 2;
    d->idx = realloc(d->idx, sizeof *d->idx * d->icap);
  }
}

static int vert(UiDraw *d, float x, float y, UiColor c, float u, float v) {
  SDL_Vertex *p = &d->v[d->nv];
  p->position.x = x;
  p->position.y = y;
  p->color.r = c.r;
  p->color.g = c.g;
  p->color.b = c.b;
  p->color.a = c.a;
  p->tex_coord.x = u;
  p->tex_coord.y = v;
  return d->nv++;
}

static void tri(UiDraw *d, int a, int b, int c) {
  d->idx[d->ni++] = a;
  d->idx[d->ni++] = b;
  d->idx[d->ni++] = c;
}

static void flush(UiDraw *d, SDL_Texture *tex) {
  if (d->ni) SDL_RenderGeometry(d->r, tex, d->v, d->nv, d->idx, d->ni);
  d->nv = d->ni = 0;
}

// ------------------------------------------------------------------ outlines

static int segs_for(float radius) {
  int s = (int)ceilf(radius * 0.5f) + 1;
  return s < 2 ? 2 : s > MAXSEG ? MAXSEG : s;
}

static float clamp_radius(UiRect r, float radius) {
  float m = (r.w < r.h ? r.w : r.h) / 2;
  return radius < 0 ? 0 : radius > m ? m : radius;
}

typedef struct { float x[MAXPTS], y[MAXPTS], nx[MAXPTS], ny[MAXPTS]; int n; } Ring;

// The outline of r with corner radius rad, pushed out by off (in by -off),
// corner centres fixed so every ring of one shape has the same points.
static void ring(Ring *o, UiRect r, float rad, float off, int seg) {
  float cx[4] = {r.x + rad, r.x + r.w - rad, r.x + r.w - rad, r.x + rad};
  float cy[4] = {r.y + rad, r.y + rad, r.y + r.h - rad, r.y + r.h - rad};
  float a0[4] = {PI_F, 1.5f * PI_F, 0, 0.5f * PI_F};
  float R = rad + off;
  if (R < 0) R = 0;
  o->n = 0;
  for (int c = 0; c < 4; c++)
    for (int i = 0; i <= seg; i++) {
      float a = a0[c] + 0.5f * PI_F * i / seg;
      float nx = cosf(a), ny = sinf(a);
      o->x[o->n] = cx[c] + nx * R;
      o->y[o->n] = cy[c] + ny * R;
      o->nx[o->n] = nx;
      o->ny[o->n] = ny;
      o->n++;
    }
}

typedef UiColor (*ColorFn)(const void *ctx, float x, float y, float nx, float ny);

typedef struct { UiColor c; } Solid;
static UiColor solid_fn(const void *ctx, float x, float y, float nx, float ny) {
  (void)x, (void)y, (void)nx, (void)ny;
  return ((const Solid *)ctx)->c;
}

typedef struct { UiGrad g; float cx, cy, dx, dy, len; } Grad;
static UiColor grad_fn(const void *ctx, float x, float y, float nx, float ny) {
  (void)nx, (void)ny;
  const Grad *g = ctx;
  float t = ((x - g->cx) * g->dx + (y - g->cy) * g->dy) / g->len + 0.5f;
  return uid_mix(g->g.from, g->g.to, t);
}

// Filled shape: solid ring at -0.5, fringe out to +0.5 with alpha 0.
static void fill_shape(UiDraw *d, UiRect r, float rad, ColorFn fn, const void *ctx, SDL_Texture *tex, float u0,
                       float v0, float us, float vs) {
  if (r.w < 0.5f || r.h < 0.5f) return;
  rad = clamp_radius(r, rad);
  int seg = segs_for(rad);
  static Ring in, out;
  ring(&in, r, rad, -0.5f, seg);
  ring(&out, r, rad, 0.5f, seg);
  int n = in.n;
  reserve(d, 2 * n + 1, 9 * n);
  float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  int c0 = vert(d, cx, cy, fn(ctx, cx, cy, 0, 0), u0 + (cx - r.x) * us, v0 + (cy - r.y) * vs);
  int base = d->nv;
  for (int i = 0; i < n; i++) {
    UiColor c = fn(ctx, in.x[i], in.y[i], in.nx[i], in.ny[i]);
    vert(d, in.x[i], in.y[i], c, u0 + (in.x[i] - r.x) * us, v0 + (in.y[i] - r.y) * vs);
    c.a = 0;
    float u = u0 + (out.x[i] - r.x) * us, v = v0 + (out.y[i] - r.y) * vs;
    // The transparent AA fringe extends beyond the image. SDL rejects the
    // entire textured draw if any fringe UV lies outside its normalized bounds.
    if (tex) { u = fminf(1, fmaxf(0, u)); v = fminf(1, fmaxf(0, v)); }
    vert(d, out.x[i], out.y[i], c, u, v);
  }
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    int a = base + 2 * i, b = base + 2 * j;
    tri(d, c0, a, b);
    tri(d, a, a + 1, b + 1);
    tri(d, a, b + 1, b);
  }
  flush(d, tex);
}

void uid_rrect(UiDraw *d, UiRect r, float radius, UiColor c) {
  if (!c.a) return;
  Solid s = {c};
  fill_shape(d, r, radius, solid_fn, &s, NULL, 0, 0, 0, 0);
}

void uid_rrect_grad(UiDraw *d, UiRect r, float radius, UiGrad g) {
  Grad k;
  float a = g.angle * PI_F / 180.0f;
  k.g = g;
  k.dx = sinf(a);
  k.dy = -cosf(a);
  k.cx = r.x + r.w / 2;
  k.cy = r.y + r.h / 2;
  k.len = fabsf(r.w * k.dx) + fabsf(r.h * k.dy);
  if (k.len < 1) k.len = 1;
  fill_shape(d, r, radius, grad_fn, &k, NULL, 0, 0, 0, 0);
}

void uid_rect_vgrad3(UiDraw *d, UiRect r, UiColor top, UiColor mid, UiColor bottom) {
  reserve(d, 6, 12);
  float ym = r.y + r.h * 0.55f;
  int a = vert(d, r.x, r.y, top, 0, 0), b = vert(d, r.x + r.w, r.y, top, 0, 0);
  int c = vert(d, r.x, ym, mid, 0, 0), e = vert(d, r.x + r.w, ym, mid, 0, 0);
  int f = vert(d, r.x, r.y + r.h, bottom, 0, 0), g = vert(d, r.x + r.w, r.y + r.h, bottom, 0, 0);
  tri(d, a, b, e);
  tri(d, a, e, c);
  tri(d, c, e, g);
  tri(d, c, g, f);
  flush(d, NULL);
}

// Bands between rings at the given offsets with the given alphas (times c.a),
// each vertex's alpha also times weight(normal).
static void bands(UiDraw *d, UiRect r, float rad, const float *off, const float *alpha, int nr, UiColor c, int top_only) {
  rad = clamp_radius(r, rad);
  int seg = segs_for(rad + (off[nr - 1] > 0 ? off[nr - 1] : 0));
  static Ring rs[12];
  if (nr > 12) nr = 12;
  for (int k = 0; k < nr; k++) ring(&rs[k], r, rad, off[k], seg);
  int n = rs[0].n;
  reserve(d, n * nr, 6 * n * (nr - 1));
  int base = d->nv;
  for (int k = 0; k < nr; k++)
    for (int i = 0; i < n; i++) {
      float w = 1;
      if (top_only) w = rs[k].ny[i] < 0 ? -rs[k].ny[i] : 0;
      vert(d, rs[k].x[i], rs[k].y[i], uid_alpha(c, alpha[k] * w), 0, 0);
    }
  for (int k = 0; k + 1 < nr; k++)
    for (int i = 0; i < n; i++) {
      int j = (i + 1) % n;
      int a = base + k * n + i, b = base + k * n + j, a2 = a + n, b2 = b + n;
      tri(d, a, a2, b2);
      tri(d, a, b2, b);
    }
  flush(d, NULL);
}

void uid_rrect_stroke(UiDraw *d, UiRect r, float radius, float width, UiColor c) {
  if (!c.a || width <= 0) return;
  if (width >= 1) {
    float off[4] = {-width - 0.5f, -width + 0.5f, -0.5f, 0.5f}, al[4] = {0, 1, 1, 0};
    bands(d, r, radius, off, al, 4, c, 0);
  } else {
    float off[3] = {-1.5f, -0.5f, 0.5f}, al[3] = {0, width, 0};
    bands(d, r, radius, off, al, 3, c, 0);
  }
}

void uid_rrect_top_light(UiDraw *d, UiRect r, float radius, float width, UiColor c) {
  if (!c.a || width <= 0) return;
  float off[4] = {-width - 1.0f, -width, -1.0f, 0.0f}, al[4] = {0, 1, 1, 0};
  if (width <= 1) {
    float o3[3] = {-2.0f, -1.0f, 0.0f}, a3[3] = {0, width, 0};
    bands(d, r, radius, o3, a3, 3, c, 1);
    return;
  }
  bands(d, r, radius, off, al, 4, c, 1);
}

void uid_rrect_shadow(UiDraw *d, UiRect r, float radius, float x, float y, float blur, UiColor c) {
  if (!c.a) return;
  r = ui_translate(r, x, y);
  if (blur < 1) {
    uid_rrect(d, r, radius, c);
    return;
  }
  // CSS: blur radius B is a Gaussian of sigma B/2 across the edge.
  float sigma = blur / 2;
  enum { K = 9 };
  float off[K], al[K];
  for (int k = 0; k < K; k++) {
    off[k] = -blur + 2 * blur * k / (K - 1);
    al[k] = 0.5f * (float)erfc(off[k] / (sigma * 1.41421356f));
  }
  al[K - 1] = 0;
  // inside the innermost ring
  Solid s = {uid_alpha(c, al[0])};
  UiRect inner = ui_inset_all(r, blur);
  if (inner.w > 0 && inner.h > 0) {
    float rr = clamp_radius(r, radius);
    int seg = segs_for(rr);
    static Ring in;
    ring(&in, r, rr, -blur, seg);
    reserve(d, in.n + 1, 3 * in.n);
    int c0 = vert(d, r.x + r.w / 2, r.y + r.h / 2, s.c, 0, 0);
    int base = d->nv;
    for (int i = 0; i < in.n; i++) vert(d, in.x[i], in.y[i], s.c, 0, 0);
    for (int i = 0; i < in.n; i++) tri(d, c0, base + i, base + (i + 1) % in.n);
    flush(d, NULL);
  }
  bands(d, r, radius, off, al, K, c, 0);
}

int uid_rrect_backdrop(UiDraw *d, UiRect r, float radius) {
  if (!d->backdrop || d->w <= 0 || d->h <= 0) return 0;
  Solid s = {{255, 255, 255, 255}};
  fill_shape(d, r, radius, solid_fn, &s, d->backdrop, r.x / d->w, r.y / d->h, 1.0f / d->w, 1.0f / d->h);
  return 1;
}

void uid_line(UiDraw *d, float x0, float y0, float x1, float y1, float width, UiColor c) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if (len < 1e-3f) dx = 1, dy = 0, len = 1;
  dx /= len, dy /= len;
  float hw = width / 2;
  int seg = segs_for(hw) + 1;
  // capsule outline: cap at the end (from +normal round to -normal), then the start
  static float px[2][2 * MAXSEG + 4], py[2][2 * MAXSEG + 4], nx[2 * MAXSEG + 4], ny[2 * MAXSEG + 4];
  int n = 0;
  float base = atan2f(dy, dx);
  for (int e = 0; e < 2; e++) {
    float cx = e ? x0 : x1, cy = e ? y0 : y1;
    for (int i = 0; i <= seg; i++) {
      float a = base - PI_F / 2 + PI_F * i / seg + (e ? PI_F : 0);
      float ux = cosf(a), uy = sinf(a);
      nx[n] = ux, ny[n] = uy;
      px[0][n] = cx + ux * (hw - 0.5f), py[0][n] = cy + uy * (hw - 0.5f);
      px[1][n] = cx + ux * (hw + 0.5f), py[1][n] = cy + uy * (hw + 0.5f);
      if (hw < 0.5f) px[0][n] = cx, py[0][n] = cy;
      n++;
    }
  }
  UiColor ci = c;
  if (width < 1) ci = uid_alpha(c, width);
  reserve(d, 2 * n + 1, 9 * n);
  int c0 = vert(d, (x0 + x1) / 2, (y0 + y1) / 2, ci, 0, 0);
  int b = d->nv;
  for (int i = 0; i < n; i++) {
    vert(d, px[0][i], py[0][i], ci, 0, 0);
    vert(d, px[1][i], py[1][i], uid_alpha(c, 0), 0, 0);
  }
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n, a = b + 2 * i, k = b + 2 * j;
    tri(d, c0, a, k);
    tri(d, a, a + 1, k + 1);
    tri(d, a, k + 1, k);
  }
  flush(d, NULL);
}

void uid_triangle(UiDraw *d, float x0, float y0, float x1, float y1, float x2, float y2, UiColor c) {
  reserve(d, 3, 3);
  int a = vert(d, x0, y0, c, 0, 0), b = vert(d, x1, y1, c, 0, 0), e = vert(d, x2, y2, c, 0, 0);
  tri(d, a, b, e);
  flush(d, NULL);
}

void uid_image(UiDraw *d, SDL_Texture *tex, const SDL_Rect *src, UiRect dst, float radius, UiColor mod) {
  if (!tex) return;
  int tw = 1, th = 1;
  SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
  SDL_Rect s = src ? *src : (SDL_Rect){0, 0, tw, th};
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  if (radius <= 0) {
    SDL_SetTextureColorMod(tex, mod.r, mod.g, mod.b);
    SDL_SetTextureAlphaMod(tex, mod.a);
    SDL_FRect f = {dst.x, dst.y, dst.w, dst.h};
    SDL_RenderCopyF(d->r, tex, &s, &f);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
    return;
  }
  Solid k = {mod};
  float us = (float)s.w / tw / dst.w, vs = (float)s.h / th / dst.h;
  fill_shape(d, dst, radius, solid_fn, &k, tex, (float)s.x / tw, (float)s.y / th, us, vs);
}

void uid_set_clip(UiDraw *d, const UiRect *r) {
  if (!r) {
    d->clip_on = 0;
    SDL_RenderSetClipRect(d->r, NULL);
    return;
  }
  UiRect k = ui_rect_round(*r);
  d->clip = (SDL_Rect){(int)k.x, (int)k.y, (int)k.w, (int)k.h};
  d->clip_on = 1;
  SDL_RenderSetClipRect(d->r, &d->clip);
}

// ------------------------------------------------------------------ backdrop

static SDL_Texture *ensure(UiDraw *d, SDL_Texture **t, int *tw, int *th, int w, int h) {
  if (*t && *tw == w && *th == h) return *t;
  if (*t) SDL_DestroyTexture(*t);
  *t = SDL_CreateTexture(d->r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
  *tw = w;
  *th = h;
  if (*t) SDL_SetTextureScaleMode(*t, SDL_ScaleModeLinear);
  return *t;
}

static void copy_to(UiDraw *d, SDL_Texture *dst, SDL_Texture *src, float ox, float oy, int w, int h, uint8_t alpha) {
  SDL_SetRenderTarget(d->r, dst);
  SDL_SetTextureBlendMode(src, alpha == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
  SDL_SetTextureAlphaMod(src, alpha);
  SDL_FRect f = {ox, oy, (float)w, (float)h};
  SDL_RenderCopyF(d->r, src, NULL, &f);
  SDL_SetTextureAlphaMod(src, 255);
}

void uid_no_backdrop(UiDraw *d) { d->backdrop = NULL; }

int uid_backdrop(UiDraw *d, SDL_Texture *src, float blur_px) {
  d->backdrop = NULL;
  if (!src || blur_px < 2 || d->w < 4 || d->h < 4) return 0;
  // scale down to about blur/2: 30 px at 1080p -> 1/16, 60 px at 4K -> 1/32
  int levels = (int)floorf(log2f(blur_px / 2) + 0.5f);
  if (levels < 1) levels = 1;
  if (levels > UID_BLUR_LEVELS) levels = UID_BLUR_LEVELS;
  SDL_ScaleMode old;
  SDL_GetTextureScaleMode(src, &old);
  SDL_SetTextureScaleMode(src, SDL_ScaleModeLinear);
  SDL_Texture *prev = src;
  int w = d->w, h = d->h;
  for (int i = 0; i < levels; i++) {
    w = (w + 1) / 2;
    h = (h + 1) / 2;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    SDL_Texture *t = ensure(d, &d->lv[i], &d->lw[i], &d->lh[i], w, h);
    if (!t) {
      SDL_SetRenderTarget(d->r, d->target);
      SDL_SetTextureScaleMode(src, old);
      return 0;
    }
    copy_to(d, t, prev, 0, 0, w, h, 255);
    prev = t;
  }
  SDL_SetTextureScaleMode(src, old);
  // two 2x2 box passes, the second shifted back: a 3x3 tent at the low size
  SDL_Texture *a = ensure(d, &d->smooth[0], &d->sw, &d->sh, w, h);
  SDL_Texture *b = a ? ensure(d, &d->smooth[1], &d->sw1, &d->sh1, w, h) : NULL;
  if (a && b) {
    copy_to(d, a, prev, 0, 0, w, h, 255);
    copy_to(d, a, prev, 1, 0, w, h, 128);
    copy_to(d, a, prev, 0, 1, w, h, 85);
    copy_to(d, a, prev, 1, 1, w, h, 64);
    copy_to(d, b, a, 0, 0, w, h, 255);
    copy_to(d, b, a, -1, 0, w, h, 128);
    copy_to(d, b, a, 0, -1, w, h, 85);
    copy_to(d, b, a, -1, -1, w, h, 64);
    d->backdrop = b;
  } else {
    d->backdrop = prev;
  }
  // Back up in steps of four with linear filtering, to the full size: panels
  // then sample a smooth picture (SDL's software renderer samples geometry
  // textures nearest, and one bilinear step from 1/16 shows its grid).
  SDL_Texture *up = d->backdrop;
  for (int i = levels - 3; i >= 0; i -= 2) {
    copy_to(d, d->lv[i], up, 0, 0, d->lw[i], d->lh[i], 255);
    up = d->lv[i];
  }
  SDL_Texture *full = ensure(d, &d->full, &d->fw, &d->fh, d->w, d->h);
  if (full) {
    copy_to(d, full, up, 0, 0, d->w, d->h, 255);
    d->backdrop = full;
  }
  SDL_SetTextureBlendMode(d->backdrop, SDL_BLENDMODE_BLEND);
  SDL_SetRenderTarget(d->r, d->target);
  SDL_SetRenderDrawBlendMode(d->r, SDL_BLENDMODE_BLEND);
  return 1;
}
