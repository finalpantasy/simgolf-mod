// Frosted Mint UI kit: context and components, see ui_kit.h.
#include "port/ui/ui_kit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EVQ 256
#define ANIMS 128
#define STATES 64
#define ICON_CACHE 192

typedef struct { uint32_t id; float v; double t; } Anim;
typedef struct { uint32_t id; float v; } State;
typedef struct { UiIcon icon; int px; SDL_Texture *tex; double retry; } IconTex;   // retry: when a failed texture may be built again (task-114k)

struct UiCtx {
  struct UiCtx *next_context;
  SDL_Renderer *r;
  UiTokens tok;
  UiDraw d;
  UiText t;
  int fonts;
  // input
  SDL_mutex *lock;
  UiRouter router;
  UiEvent q[EVQ];
  int qn;
  UiInteract in;
  UiHitList prev, cur;
  // frame
  int w, h;
  float scale;
  double now;
  int in_frame;
  Anim anims[ANIMS];
  State states[STATES];
  IconTex icons[ICON_CACHE];
  int nicons;
  // deferred tooltip
  int tip_on;
  UiRect tip_anchor;
  char tip[128];
  // scroll
  int scroll_on;
  UiRect scroll_view;
  float scales[8];
  int nscale;
  int scale_over;   // pushes beyond the stack (task-114n): they change nothing and their pops undo nothing
};

// ------------------------------------------------------------------ setup

void ui_tokens_load_all(UiTokens *t, const char *base_path, const char *const *overlays, int n) {
  char err[1024];
  ui_tokens_default(t);
  const char *paths[17];
  int np = 0;
  if (base_path) paths[np++] = base_path;
  for (int i = 0; i < n && np < 17; i++) paths[np++] = overlays[i];
  for (int i = 0; i < np; i++) {
    FILE *f = fopen(paths[i], "rb");
    if (!f) {
      if (i == 0) fprintf(stderr, "ui: %s not found, built-in Frosted Mint tokens\n", paths[i]);
      continue;
    }
    fclose(f);
    err[0] = 0;
    int k = ui_tokens_load(t, paths[i], err, sizeof err);
    fprintf(stderr, "ui: tokens %s: %d keys%s%s", paths[i], k < 0 ? 0 : k, err[0] ? ", " : "\n", err);
  }
}

// ---- task-113: a registry of every context, so the debug overlay can ask each Frosted Mint layer
// whether it covers a screen point (old-UI leaks, "what drew this").
#define DBG_CTX_MAX 16
static struct { UiCtx *c; char name[24]; } g_dbg[DBG_CTX_MAX];
static int g_ndbg;
// Recovery owns every context, including ones beyond the debug-view limit.
static UiCtx *g_contexts;

void ui_debug_name(UiCtx *c, const char *name) {
  for (int i = 0; i < g_ndbg; i++)
    if (g_dbg[i].c == c) snprintf(g_dbg[i].name, sizeof g_dbg[i].name, "%s", name);
}

int ui_debug_count(void) { return g_ndbg; }

// Whether the context's last finished frame has a blocking region at (x, y) in target pixels.
int ui_debug_probe(int i, float x, float y, const UiCtx *skip, char *name, int cap, uint32_t *id, UiRect *rect) {
  if (i < 0 || i >= g_ndbg || g_dbg[i].c == skip) return 0;
  UiCtx *c = g_dbg[i].c;
  if (name) snprintf(name, cap, "%s", g_dbg[i].name[0] ? g_dbg[i].name : "layer");
  SDL_LockMutex(c->lock);
  int k = ui_hits_test(&c->router.published, x, y, UI_HIT_BLOCK | UI_HIT_MODAL);
  if (k >= 0) {
    if (id) *id = c->router.published.v[k].id;
    if (rect) *rect = c->router.published.v[k].r;
  }
  SDL_UnlockMutex(c->lock);
  return k >= 0;
}

UiCtx *ui_create(SDL_Renderer *r, const UiTokens *tokens, const char *font_dir) {
  UiCtx *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->next_context=g_contexts;g_contexts=c;
  if (g_ndbg < DBG_CTX_MAX) {
    g_dbg[g_ndbg].name[0] = 0;
    g_dbg[g_ndbg++].c = c;
  }
  c->r = r;
  if (tokens) c->tok = *tokens;
  else ui_tokens_default(&c->tok);
  uid_init(&c->d, r);
  c->fonts = uit_init(&c->t, r, font_dir, c->tok.text_gamma);
  c->lock = SDL_CreateMutex();
  ui_router_init(&c->router);
  ui_interact_init(&c->in);
  c->scale = 1;
  return c;
}

void ui_destroy(UiCtx *c) {
  if (!c) return;
  UiCtx **link=&g_contexts;
  while(*link && *link!=c)link=&(*link)->next_context;
  if(*link)*link=c->next_context;
  for (int i = 0; i < g_ndbg; i++)
    if (g_dbg[i].c == c) g_dbg[i] = g_dbg[--g_ndbg];
  for (int i = 0; i < c->nicons; i++)
    if (c->icons[i].tex) SDL_DestroyTexture(c->icons[i].tex);
  uid_free(&c->d);
  uit_free(&c->t);
  if (c->lock) SDL_DestroyMutex(c->lock);
  free(c);
}

const UiTokens *ui_tokens(const UiCtx *c) { return &c->tok; }
void ui_set_tokens(UiCtx *c, const UiTokens *t) { c->tok = *t; }

// ------------------------------------------------------------------ input

int ui_blocks_point(UiCtx *c, float x, float y) {
  SDL_LockMutex(c->lock);
  int blocked = ui_hits_modal(&c->router.published) ||
    ui_hits_test(&c->router.published, x, y, UI_HIT_BLOCK) >= 0;
  SDL_UnlockMutex(c->lock);
  return blocked;
}

int ui_route_event(UiCtx *c, const UiEvent *e) {
  SDL_LockMutex(c->lock);
  int to = ui_route(&c->router, e);
  if (to & UI_ROUTE_UI) ui_evq_push(c->q, &c->qn, EVQ, e);
  SDL_UnlockMutex(c->lock);
  return to;
}

void ui_input_reset(UiCtx *c) {
  UiEvent e = {UI_EV_LEAVE};
  ui_route_event(c, &e);
}

// ------------------------------------------------------------------ frame

void ui_begin(UiCtx *c, SDL_Texture *target, int w, int h, float scale, SDL_Texture *backdrop, double now) {
  UiEvent ev[EVQ];
  int n;
  SDL_LockMutex(c->lock);
  n = c->qn;
  memcpy(ev, c->q, sizeof ev[0] * n);
  c->qn = 0;
  SDL_UnlockMutex(c->lock);
  c->w = w;
  c->h = h;
  c->scale = scale > 0 ? scale : 1;
  c->nscale = 0;
  c->scale_over = 0;
  c->now = now;
  ui_interact_frame(&c->in, &c->prev, ev, n, now);
  ui_hits_clear(&c->cur);
  c->tip_on = 0;
  c->in_frame = 1;
  uid_begin(&c->d, target, w, h);
  if (backdrop && c->tok.glass_blur > 0) uid_backdrop(&c->d, backdrop, c->tok.glass_blur * c->scale);
}

static void draw_tooltip(UiCtx *c);

static void publish(UiCtx *c) {
  SDL_LockMutex(c->lock);
  c->router.published = c->cur;
  SDL_UnlockMutex(c->lock);
  c->prev = c->cur;
}

void ui_end(UiCtx *c) {
  if (c->tip_on) draw_tooltip(c);
  uid_set_clip(&c->d, NULL);
  publish(c);
  c->in_frame = 0;
}

void ui_skip_frame(UiCtx *c) {
  ui_hits_clear(&c->cur);
  publish(c);
  SDL_LockMutex(c->lock);
  c->qn = 0;
  c->router.ui_buttons = 0;
  SDL_UnlockMutex(c->lock);
  ui_interact_init(&c->in);
}

float ui_scale(const UiCtx *c) { return c->scale; }

void ui_push_scale(UiCtx *c, float k) {
  if (c->nscale < 8) { c->scales[c->nscale++] = c->scale; c->scale *= k; }
  else c->scale_over++;
}

void ui_pop_scale(UiCtx *c) {
  if (c->scale_over > 0) c->scale_over--;
  else if (c->nscale > 0) c->scale = c->scales[--c->nscale];
}

int ui_pointer(const UiCtx *c, float *x, float *y) {
  *x = c->in.x;
  *y = c->in.y;
  return c->in.inside;
}

float ui_coverage(const UiCtx *c, int step) {
  if (step < 1) step = 1;
  long in = 0, all = 0;
  for (int y = step / 2; y < c->h; y += step)
    for (int x = step / 2; x < c->w; x += step, all++)
      if (ui_hits_test(&c->prev, (float)x, (float)y, UI_HIT_BLOCK | UI_HIT_MODAL) >= 0) in++;
  return all ? (float)in / all : 0;
}
float ui_s(const UiCtx *c, float v) { return ui_px(c->scale, v); }
UiRect ui_screen(const UiCtx *c) { return ui_rect(0, 0, (float)c->w, (float)c->h); }
double ui_now(const UiCtx *c) { return c->now; }
int ui_is_hot(const UiCtx *c, uint32_t id) { return id && c->in.hot == id; }
int ui_is_active(const UiCtx *c, uint32_t id) { return id && c->in.active == id; }
int ui_clicked(const UiCtx *c, uint32_t id) { return ui_interact_clicked(&c->in, id); }
int ui_right_clicked(const UiCtx *c, uint32_t id) { return c->in.right_clicked == id; }
float ui_wheel(const UiCtx *c, uint32_t id) { return c->in.wheel_id == id ? c->in.wheel : 0; }
UiDraw *ui_drawer(UiCtx *c) { return &c->d; }
UiText *ui_texter(UiCtx *c) { return &c->t; }
void ui_hit(UiCtx *c, UiRect r, float radius, uint32_t id, uint32_t flags) { ui_hits_add(&c->cur, r, radius, id, flags); }

float ui_anim(UiCtx *c, uint32_t id, float target, float ms) {
  Anim *a = NULL, *free_slot = NULL;
  for (int i = 0; i < ANIMS; i++) {
    if (c->anims[i].id == id) {
      a = &c->anims[i];
      break;
    }
    if (!free_slot && (!c->anims[i].id || c->now - c->anims[i].t > 2000)) free_slot = &c->anims[i];
  }
  if (!a) {
    a = free_slot ? free_slot : &c->anims[id % ANIMS];
    a->id = id;
    a->v = target;
    a->t = c->now;
    return target;
  }
  double dt = c->now - a->t;
  a->t = c->now;
  if (ms <= 0) {
    a->v = target;
  } else {
    float step = (float)(dt / ms);
    if (a->v < target) a->v = fminf(target, a->v + step);
    else a->v = fmaxf(target, a->v - step);
  }
  float x = a->v;   // ease out
  return 1 - (1 - x) * (1 - x);
}

float *ui_state(UiCtx *c, uint32_t id) {
  for (int i = 0; i < STATES; i++)
    if (c->states[i].id == id) return &c->states[i].v;
  for (int i = 0; i < STATES; i++)
    if (!c->states[i].id) {
      c->states[i].id = id;
      c->states[i].v = 0;
      return &c->states[i].v;
    }
  return &c->states[id % STATES].v;
}

// ------------------------------------------------------------------ text and icons

UiFont ui_font(const UiCtx *c, int style) {
  UiType t = c->tok.type[style < 0 || style >= UI_TYPE_COUNT ? UI_TYPE_BODY : style];
  UiFont f;
  f.px = ui_text_px(c->scale, t.size, c->tok.text_floor);
  f.weight = t.weight;
  f.tracking = t.tracking;
  f.line = ui_px(c->scale, t.line);
  if (f.line < f.px * 1.15f) f.line = floorf(f.px * 1.3f);
  return f;
}

float ui_text_width(UiCtx *c, int style, const char *s) {
  UiFont f = ui_font(c, style);
  return uit_measure(&c->t, f.weight, f.px, s, f.tracking);
}

float ui_text(UiCtx *c, float x, float y, int style, UiColor col, const char *s) {
  UiFont f = ui_font(c, style);
  return uit_draw(&c->t, x, y, f.weight, f.px, s, f.tracking, col);
}

static float caps_baseline(UiCtx *c, UiRect r, UiFont f) {
  (void)c;
  return floorf(r.y + r.h / 2 + f.px * 0.36f + 0.5f);
}

void ui_text_in(UiCtx *c, UiRect r, int style, UiColor col, const char *s, float align) {
  UiFont f = ui_font(c, style);
  float w = uit_measure(&c->t, f.weight, f.px, s, f.tracking);
  uit_draw(&c->t, floorf(r.x + (r.w - w) * align + 0.5f), caps_baseline(c, r, f), f.weight, f.px, s, f.tracking, col);
}

void ui_text_fit(UiCtx *c, UiRect r, int style, UiColor col, const char *s, float align) {
  UiFont f = ui_font(c, style);
  if (uit_measure(&c->t, f.weight, f.px, s, f.tracking) <= r.w) {
    ui_text_in(c, r, style, col, s, align);
    return;
  }
  char buf[256];
  float ell = uit_measure(&c->t, f.weight, f.px, "\xE2\x80\xA6", f.tracking);
  int n = uit_prefix(&c->t, f.weight, f.px, s, f.tracking, r.w, 250, ell);
  memcpy(buf, s, (size_t)n);
  while (n > 0 && buf[n - 1] == ' ') n--;
  memcpy(buf + n, "\xE2\x80\xA6", 4);
  ui_text_in(c, r, style, col, buf, align);
}

static SDL_Texture *icon_tex(UiCtx *c, UiIcon icon, int px) {
  for (int i = 0; i < c->nicons; i++)
    if (c->icons[i].icon == icon && c->icons[i].px == px) {
      if (c->icons[i].tex || c->now < c->icons[i].retry) return c->icons[i].tex;
      c->icons[i] = c->icons[--c->nicons];   // a failed build is tried again after a second
      i = -1;
      continue;
    }
  if (px < 4 || px > 512) return NULL;
  uint8_t *a = malloc((size_t)px * px);
  uint32_t *argb = malloc((size_t)px * px * 4);
  SDL_Texture *t = NULL;
  if (a && argb && ui_icon_raster(icon, px, c->tok.icon_stroke, a)) {
    for (int i = 0; i < px * px; i++) argb[i] = (uint32_t)a[i] << 24 | 0xFFFFFFu;
    t = SDL_CreateTexture(c->r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, px, px);
    if (t) {
      SDL_UpdateTexture(t, NULL, argb, px * 4);
      SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
  }
  free(a);
  free(argb);
  if (c->nicons == ICON_CACHE) {   // full: start over
    for (int i = 0; i < c->nicons; i++)
      if (c->icons[i].tex) SDL_DestroyTexture(c->icons[i].tex);
    c->nicons = 0;
  }
  c->icons[c->nicons++] = (IconTex){icon, px, t, t ? 0 : c->now + 1.0};
  return t;
}

// A mood face (ui_faces.c) at the exact pixel size, cached with the icons (key UI_ICON_COUNT + mood).
static SDL_Texture *face_tex(UiCtx *c, int mood, int px) {
  UiIcon key = (UiIcon)(UI_ICON_COUNT + mood);
  for (int i = 0; i < c->nicons; i++)
    if (c->icons[i].icon == key && c->icons[i].px == px) {
      if (c->icons[i].tex || c->now < c->icons[i].retry) return c->icons[i].tex;
      c->icons[i] = c->icons[--c->nicons];
      i = -1;
      continue;
    }
  if (px < 4 || px > 512) return NULL;
  uint32_t *argb = malloc((size_t)px * px * 4);
  SDL_Texture *t = NULL;
  if (argb && ui_face_raster(mood, px, argb)) {
    t = SDL_CreateTexture(c->r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, px, px);
    if (t) {
      SDL_UpdateTexture(t, NULL, argb, px * 4);
      SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    }
  }
  free(argb);
  if (c->nicons == ICON_CACHE) {
    for (int i = 0; i < c->nicons; i++)
      if (c->icons[i].tex) SDL_DestroyTexture(c->icons[i].tex);
    c->nicons = 0;
  }
  c->icons[c->nicons++] = (IconTex){key, px, t, t ? 0 : c->now + 1.0};
  return t;
}

void ui_face(UiCtx *c, int mood, UiRect r, float size_px, float alpha) {
  int px = (int)(size_px + 0.5f);
  SDL_Texture *t = face_tex(c, mood < 0 ? 0 : mood > 10 ? 10 : mood, px);
  if (!t) return;
  UiRect d = ui_rect(floorf(r.x + (r.w - px) / 2 + 0.5f), floorf(r.y + (r.h - px) / 2 + 0.5f), (float)px, (float)px);
  uid_image(&c->d, t, NULL, d, 0, uid_alpha((UiColor){255, 255, 255, 255}, alpha));
}

void ui_icon(UiCtx *c, UiIcon icon, UiRect r, float size_px, UiColor col) {
  int px = (int)(size_px + 0.5f);
  SDL_Texture *t = icon_tex(c, icon, px);
  if (!t) return;
  UiRect d = ui_rect(floorf(r.x + (r.w - px) / 2 + 0.5f), floorf(r.y + (r.h - px) / 2 + 0.5f), (float)px, (float)px);
  uid_image(&c->d, t, NULL, d, 0, col);
}

// ------------------------------------------------------------------ components

static UiShadow scaled_shadow(const UiCtx *c, UiShadow s) {
  s.x *= c->scale;
  s.y *= c->scale;
  s.blur *= c->scale;
  return s;
}

static void shadow(UiCtx *c, UiRect r, float radius, UiShadow s) {
  s = scaled_shadow(c, s);
  uid_rrect_shadow(&c->d, r, radius, s.x, s.y, s.blur, s.color);
}

static float hair(const UiCtx *c) { return c->scale < 1.5f ? 1 : floorf(c->scale + 0.25f); }

void ui_panel(UiCtx *c, UiRect r, float radius, int flags) {
  const UiTokens *t = &c->tok;
  r = ui_rect_round(r);
  if (!(flags & UI_PANEL_NOSHADOW)) shadow(c, r, radius, t->glass_shadow);
  int frosted = !(flags & UI_PANEL_SOLID) && uid_rrect_backdrop(&c->d, r, radius);
  UiColor fill = !frosted || (flags & UI_PANEL_READABLE) ? t->glass_solid : flags & UI_PANEL_STRONG ? t->glass_fill_strong : t->glass_fill;
  uid_rrect(&c->d, r, radius, fill);
  uid_rrect_stroke(&c->d, r, radius, hair(c), t->glass_border);
  uid_rrect_top_light(&c->d, r, radius, hair(c), t->glass_highlight);
  if (!(flags & UI_PANEL_NOBLOCK)) ui_hits_add(&c->cur, r, radius, 0, UI_HIT_BLOCK);
}

static UiColor darker(UiColor c, float k) {
  UiColor b = {0, 0, 0, c.a};
  return uid_mix(c, b, k);
}

static UiRect pressed(UiCtx *c, uint32_t id, UiRect r) {
  float p = ui_anim(c, id ^ 0x5157u, ui_is_active(c, id) && ui_is_hot(c, id) ? 1.0f : 0.0f, c->tok.motion_press);
  float k = 1 - 0.03f * p;
  return ui_rect(r.x + r.w * (1 - k) / 2, r.y + r.h * (1 - k) / 2, r.w * k, r.h * k);
}

int ui_button(UiCtx *c, uint32_t id, UiRect r, const char *label, UiIcon icon, int kind, int flags) {
  const UiTokens *t = &c->tok;
  int dis = flags & UI_DISABLED;
  float radius = kind == UI_BTN_PRIMARY ? ui_s(c, 14) : r.h / 2;
  if (!dis) ui_hits_add(&c->cur, r, radius, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
  else ui_hits_add(&c->cur, r, radius, 0, UI_HIT_BLOCK);
  float hov = ui_anim(c, id, !dis && ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
  UiRect b = pressed(c, id, r);
  float a = dis ? 0.4f : 1;
  UiColor txt;
  if (kind == UI_BTN_PRIMARY) {
    uid_rrect(&c->d, b, radius, uid_alpha(darker(t->accent, 0.06f * hov), a));
    txt = uid_alpha(t->text_on_accent, a);
  } else if (kind == UI_BTN_PILL) {
    UiColor f = uid_mix(t->surface_inset, t->glass_fill_strong, hov);
    uid_rrect(&c->d, b, radius, uid_alpha(f, a));
    uid_rrect_stroke(&c->d, b, radius, hair(c), uid_alpha(t->surface_inset_border, a));
    txt = uid_alpha(t->text_primary, a);
  } else {
    if (hov > 0) uid_rrect(&c->d, b, radius, uid_alpha(t->surface_inset, hov * a));
    txt = uid_alpha(t->text_primary, a);
  }
  int style = UI_TYPE_LABEL;
  float iw = icon ? ui_s(c, 18) : 0, gap = icon && label && *label ? ui_s(c, 8) : 0;
  float tw = label ? ui_text_width(c, style, label) : 0;
  float x = b.x + (b.w - iw - gap - tw) / 2;
  if (icon) ui_icon(c, icon, ui_rect(x, b.y, iw, b.h), iw, txt);
  if (label && *label) ui_text_in(c, ui_rect(x + iw + gap, b.y, tw + 1, b.h), style, txt, label, 0);
  return !dis && ui_clicked(c, id);
}

int ui_icon_button(UiCtx *c, uint32_t id, UiRect r, UiIcon icon, int flags, const char *tooltip) {
  const UiTokens *t = &c->tok;
  int dis = flags & UI_DISABLED, sel = flags & UI_SELECTED;
  float radius = flags & UI_ROUND ? r.w / 2 : ui_s(c, t->radius_control);
  ui_hits_add(&c->cur, r, radius, dis ? 0 : id, dis ? UI_HIT_BLOCK : UI_HIT_BLOCK | UI_HIT_INTERACT);
  float hov = ui_anim(c, id, !dis && ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
  UiRect b = pressed(c, id, r);
  if (sel) uid_rrect(&c->d, b, radius, t->seg_on);
  else if (hov > 0) uid_rrect(&c->d, b, radius, uid_alpha(t->surface_inset, hov));
  float isz = ui_s(c, flags & UI_COMPACT ? 18 : 22);
  ui_icon(c, icon, b, isz, uid_alpha(t->text_primary, dis ? 0.4f : 1));
  if (tooltip && !dis) ui_tooltip(c, id, r, tooltip);
  return !dis && ui_clicked(c, id);
}

static float seg_w(UiCtx *c, const char *label, UiIcon icon) {
  float w = ui_s(c, 24);
  if (label && *label) w += ui_text_width(c, UI_TYPE_LABEL, label);
  if (icon) w += ui_s(c, 18) + (label && *label ? ui_s(c, 6) : 0);
  if (w < ui_s(c, 34)) w = ui_s(c, 34);
  return floorf(w + 0.5f);
}

float ui_segmented_width(UiCtx *c, const char *const *labels, const UiIcon *icons, int n) {
  float w = ui_s(c, 6) + ui_s(c, 2) * (n - 1);
  for (int i = 0; i < n; i++) w += seg_w(c, labels ? labels[i] : NULL, icons ? icons[i] : UI_ICON_NONE);
  return w;
}

int ui_segmented(UiCtx *c, uint32_t id, UiRect r, const char *const *labels, const UiIcon *icons, int n, int *sel) {
  const UiTokens *t = &c->tok;
  float pad = ui_s(c, 3), gap = ui_s(c, 2);
  float rt = ui_s(c, t->radius_control), ri = ui_s(c, t->radius_seg_inner);
  uid_rrect(&c->d, r, rt, t->seg_track);
  ui_hits_add(&c->cur, r, rt, 0, UI_HIT_BLOCK);
  float nat = ui_segmented_width(c, labels, icons, n);
  float extra = r.w > nat ? (r.w - nat) / n : 0;   // full-width variant: share the rest
  float x = r.x + pad;
  int changed = 0;
  for (int i = 0; i < n; i++) {
    const char *lab = labels ? labels[i] : NULL;
    UiIcon ic = icons ? icons[i] : UI_ICON_NONE;
    float w = seg_w(c, lab, ic) + extra;
    UiRect s = ui_rect(x, r.y + pad, w, r.h - 2 * pad);
    uint32_t sid = ui_idi(id, i);
    ui_hits_add(&c->cur, s, ri, sid, UI_HIT_BLOCK | UI_HIT_INTERACT);
    int on = sel && *sel == i;
    float hov = ui_anim(c, sid, ui_is_hot(c, sid) && !on ? 1.0f : 0.0f, t->motion_hover);
    if (on) {
      shadow(c, s, ri, t->seg_on_shadow);
      uid_rrect(&c->d, s, ri, t->seg_on);
    } else if (hov > 0) {
      uid_rrect(&c->d, s, ri, uid_alpha(t->seg_on, 0.45f * hov));
    }
    UiColor col = on ? t->text_primary : t->text_secondary;
    float iw = ic ? ui_s(c, 18) : 0, g = ic && lab && *lab ? ui_s(c, 6) : 0;
    float tw = lab ? ui_text_width(c, UI_TYPE_LABEL, lab) : 0;
    float cx = s.x + (s.w - iw - g - tw) / 2;
    if (ic) ui_icon(c, ic, ui_rect(cx, s.y, iw, s.h), iw, col);
    if (lab && *lab) ui_text_in(c, ui_rect(cx + iw + g, s.y, tw + 1, s.h), UI_TYPE_LABEL, col, lab, 0);
    if (ui_clicked(c, sid) && sel && *sel != i) {
      *sel = i;
      changed = 1;
    }
    x += w + gap;
  }
  return changed;
}

static int dock_item(UiCtx *c, uint32_t id, UiRect slot, UiIcon icon, const char *label, int active, const char *tooltip,
                     int round);

int ui_dock_item(UiCtx *c, uint32_t id, UiRect slot, UiIcon icon, const char *label, int active, const char *tooltip) {
  return dock_item(c, id, slot, icon, label, active, tooltip, 0);
}

int ui_round_item(UiCtx *c, uint32_t id, float cx, float cy, float d, UiIcon icon, int active, const char *tooltip) {
  UiRect slot = ui_rect_round(ui_rect(cx - d / 2, cy - d / 2, d, d));
  ui_panel(c, ui_inset_all(slot, -ui_s(c, 4)), d / 2 + ui_s(c, 4), UI_PANEL_NOSHADOW);
  return dock_item(c, id, slot, icon, NULL, active, tooltip, 1);
}

static int dock_item(UiCtx *c, uint32_t id, UiRect slot, UiIcon icon, const char *label, int active, const char *tooltip,
                     int round) {
  const UiTokens *t = &c->tok;
  float on = ui_anim(c, id ^ 0xd0c4u, active ? 1.0f : 0.0f, t->motion_open);
  int bare = !label;   // icon only (compact and minimal HUDs): same size, centred, the label as tooltip
  float sq = bare ? fminf(slot.w, slot.h)
                  : ui_s(c, t->dock_square) + (ui_s(c, t->dock_square_on) - ui_s(c, t->dock_square)) * on;
  sq = floorf(sq + 0.5f);
  UiFont mf = ui_font(c, UI_TYPE_MICRO);
  float dot = ui_s(c, 5), dot_gap = ui_s(c, 3), gap = ui_s(c, 4);
  float bottom = slot.y + slot.h - (dot + dot_gap) * on;
  float label_top = bottom - mf.line;
  UiRect sqr = bare ? ui_rect_round(ui_center(slot, sq, sq))
                    : ui_rect(floorf(slot.x + (slot.w - sq) / 2 + 0.5f), floorf(label_top - gap - sq + 0.5f), sq, sq);
  float rad = round ? sq / 2 : ui_s(c, t->radius_tile);
  ui_hits_add(&c->cur, ui_rect(slot.x, sqr.y, slot.w, slot.y + slot.h - sqr.y), rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
  float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
  UiRect b = pressed(c, id, sqr);
  if (on > 0.01f) {
    shadow(c, b, rad, (UiShadow){t->dock_active_shadow.x, t->dock_active_shadow.y, t->dock_active_shadow.blur,
                                 uid_alpha(t->dock_active_shadow.color, on)});
  }
  if (on < 0.99f) {
    shadow(c, b, rad, (UiShadow){t->dock_square_shadow.x, t->dock_square_shadow.y, t->dock_square_shadow.blur,
                                 uid_alpha(t->dock_square_shadow.color, 1 - on)});
    UiColor f = uid_mix(t->dock_fill, t->glass_fill_strong, hov * 0.8f);
    uid_rrect(&c->d, b, rad, uid_alpha(f, 1 - on));
  }
  if (on > 0.01f) {
    UiGrad g = t->accent_grad;
    g.from = uid_alpha(g.from, on);
    g.to = uid_alpha(g.to, on);
    uid_rrect_grad(&c->d, b, rad, g);
  }
  UiColor ic = uid_mix(t->icon_muted, t->text_on_accent, on);
  ui_icon(c, icon, b, bare ? floorf(sq * 0.45f + 0.5f) : ui_s(c, t->dock_icon), ic);
  if (!bare) ui_text_in(c, ui_rect(slot.x, label_top, slot.w, mf.line), UI_TYPE_MICRO, t->text_secondary, label, 0.5f);
  if (on > 0.01f && !bare) {
    float dx = slot.x + slot.w / 2 - dot / 2, dy = slot.y + slot.h - dot;
    uid_rrect(&c->d, ui_rect(dx, dy, dot, dot), dot / 2, uid_alpha(t->accent, on));
  }
  if (tooltip) ui_tooltip(c, id, sqr, tooltip);
  return ui_clicked(c, id);
}

float ui_tile_height(const UiCtx *c) {
  const UiTokens *t = &c->tok;
  UiFont nf = ui_font(c, UI_TYPE_LABEL), pf = ui_font(c, UI_TYPE_CAPTION);
  return ui_px(c->scale, 8) + ui_px(c->scale, t->tile_art_h) + ui_px(c->scale, 2) + nf.line + pf.line + ui_px(c->scale, 8);
}

int ui_tile(UiCtx *c, uint32_t id, UiRect r, SDL_Texture *art, UiIcon icon, const char *name, const char *price, int flags,
            const char *tooltip) {
  const UiTokens *t = &c->tok;
  int sel = flags & UI_SELECTED, dis = flags & UI_DISABLED;
  float rad = ui_s(c, t->radius_tile);
  ui_hits_add(&c->cur, r, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
  float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
  UiRect b = pressed(c, id, r);
  float a = dis ? 0.45f : 1;
  if (sel) {
    float ring = ui_s(c, 2);
    uid_rrect(&c->d, ui_inset_all(b, -ring), rad + ring, t->accent);
    uid_rrect(&c->d, b, rad, t->tile_fill_on);
  } else {
    uid_rrect(&c->d, b, rad, uid_alpha(uid_mix(t->tile_fill, t->tile_fill_on, hov), a));
    uid_rrect_stroke(&c->d, b, rad, hair(c), uid_alpha(t->surface_inset_border, a));
  }
  float aw = ui_s(c, t->tile_art_w), ah = ui_s(c, t->tile_art_h);
  UiRect art_r = ui_rect(floorf(b.x + (b.w - aw) / 2), b.y + ui_s(c, 8), aw, ah);
  if (art) {
    int tw = 1, th = 1;
    SDL_QueryTexture(art, NULL, NULL, &tw, &th);
    float k = fminf(aw / tw, ah / th);
    float w = tw * k, h = th * k;
    UiRect d = ui_rect(floorf(art_r.x + (aw - w) / 2 + 0.5f), floorf(art_r.y + (ah - h) / 2 + 0.5f), floorf(w + 0.5f),
                       floorf(h + 0.5f));
    uid_image(&c->d, art, NULL, d, 0, (UiColor){255, 255, 255, (uint8_t)(255 * a)});
  } else if (icon) {
    float isz = ui_s(c, 36);
    UiRect ib = ui_center(art_r, isz + ui_s(c, 18), isz + ui_s(c, 18));
    uid_rrect(&c->d, ib, ib.w / 2, uid_alpha(t->accent_soft, a));
    ui_icon(c, icon, ib, isz, uid_alpha(t->accent_soft_text, a));
  }
  UiFont nf = ui_font(c, UI_TYPE_LABEL), pf = ui_font(c, UI_TYPE_CAPTION);
  float y = art_r.y + ah + ui_s(c, 2);
  if (name) ui_text_fit(c, ui_rect(b.x + ui_s(c, 4), y, b.w - ui_s(c, 8), nf.line), UI_TYPE_LABEL,
                        uid_alpha(t->text_primary, a), name, 0.5f);
  if (price && *price)
    ui_text_fit(c, ui_rect(b.x + ui_s(c, 4), y + nf.line, b.w - ui_s(c, 8), pf.line), UI_TYPE_CAPTION,
                dis ? t->danger : uid_alpha(t->text_secondary, 0.85f), price, 0.5f);
  if (tooltip) ui_tooltip(c, id, r, tooltip);
  return ui_clicked(c, id);
}

int ui_tile_row(UiCtx *c, uint32_t id, UiRect r, SDL_Texture *art, UiIcon icon, const char *name, const char *price,
                int flags, const char *tooltip) {
  const UiTokens *t = &c->tok;
  int sel = flags & UI_SELECTED, dis = flags & UI_DISABLED;
  float rad = ui_s(c, 12);
  ui_hits_add(&c->cur, r, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
  float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
  float a = dis ? 0.45f : 1;
  if (sel) {
    float ring = ui_s(c, 2);
    uid_rrect(&c->d, ui_inset_all(r, -ring), rad + ring, t->accent);
    uid_rrect(&c->d, r, rad, t->tile_fill_on);
  } else if (hov > 0) {
    uid_rrect(&c->d, r, rad, uid_alpha(t->tile_fill, hov));
  }
  float ah = r.h - ui_s(c, 8), aw = floorf(ah * 1.42f + 0.5f);
  UiRect ar = ui_rect(r.x + ui_s(c, 6), r.y + ui_s(c, 4), aw, ah);
  if (art) {
    int tw = 1, th = 1;
    SDL_QueryTexture(art, NULL, NULL, &tw, &th);
    float k = fminf(aw / tw, ah / th);
    UiRect d = ui_rect_round(ui_center(ar, tw * k, th * k));
    uid_image(&c->d, art, NULL, d, 0, (UiColor){255, 255, 255, (uint8_t)(255 * a)});
  } else if (icon) {
    ui_icon(c, icon, ar, floorf(ah * 0.6f), uid_alpha(t->accent_soft_text, a));
  }
  float x = ar.x + aw + ui_s(c, 8), w = r.x + r.w - x - ui_s(c, 8);
  float pw = price && *price ? ui_text_width(c, UI_TYPE_CAPTION, price) + ui_s(c, 6) : 0;
  ui_text_fit(c, ui_rect(x, r.y, w - pw, r.h), UI_TYPE_LABEL, uid_alpha(t->text_primary, a), name, 0);
  if (pw > 0) ui_text_in(c, ui_rect(r.x + r.w - ui_s(c, 8) - pw, r.y, pw, r.h), UI_TYPE_CAPTION,
                         dis ? t->danger : t->text_secondary, price, 1);
  if (tooltip) ui_tooltip(c, id, r, tooltip);
  return ui_clicked(c, id);
}

int ui_tile_mini(UiCtx *c, uint32_t id, UiRect r, SDL_Texture *art, UiIcon icon, int flags, const char *tooltip) {
  const UiTokens *t = &c->tok;
  int sel = flags & UI_SELECTED, dis = flags & UI_DISABLED;
  int cover = flags & UI_TILE_COVER;
  float rad = ui_s(c, 14);
  ui_hits_add(&c->cur, r, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
  float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
  UiRect b = pressed(c, id, r);
  float a = dis ? 0.45f : 1;
  if (sel) {
    float ring = ui_s(c, 2);
    uid_rrect(&c->d, ui_inset_all(b, -ring), rad + ring, t->accent);
    uid_rrect(&c->d, b, rad, t->tile_fill_on);
  } else {
    uid_rrect(&c->d, b, rad, uid_alpha(uid_mix(t->tile_fill, t->tile_fill_on, hov), a));
    UiColor border = cover ? uid_mix(t->surface_inset_border, t->accent, hov) : t->surface_inset_border;
    uid_rrect_stroke(&c->d, b, rad, hair(c), uid_alpha(border, a));
  }
  float inset = ui_s(c, cover ? 1 : 5);
  UiRect in = ui_inset_all(b, inset);
  if (art) {
    int tw = 1, th = 1;
    SDL_QueryTexture(art, NULL, NULL, &tw, &th);
    if (cover) {
      float k = fmaxf(in.w / tw, in.h / th);
      int sw = (int)fminf(tw, floorf(in.w / k + .5f));
      int sh = (int)fminf(th, floorf(in.h / k + .5f));
      SDL_Rect src = {(tw - sw) / 2, (th - sh) / 2, sw, sh};
      uid_image(&c->d, art, &src, ui_rect_round(in), fmaxf(0, rad - inset), (UiColor){255, 255, 255, (uint8_t)(255 * a)});
    } else {
      float k = fminf(in.w / tw, in.h / th);
      uid_image(&c->d, art, NULL, ui_rect_round(ui_center(in, tw * k, th * k)), 0, (UiColor){255, 255, 255, (uint8_t)(255 * a)});
    }
  } else if (icon) {
    ui_icon(c, icon, in, floorf(in.h * 0.55f), uid_alpha(t->accent_soft_text, a));
  }
  if (tooltip) ui_tooltip(c, id, r, tooltip);
  return ui_clicked(c, id);
}

// ui_tile_mini with a visible name under the icon (task-92: the pro's controls must not be icons alone).
int ui_tile_mini_label(UiCtx *c, uint32_t id, UiRect r, UiIcon icon, const char *label, int flags, const char *tooltip) {
  const UiTokens *t = &c->tok;
  int sel = flags & UI_SELECTED, dis = flags & UI_DISABLED;
  float rad = ui_s(c, 14);
  ui_hits_add(&c->cur, r, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
  float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
  UiRect b = pressed(c, id, r);
  float a = dis ? 0.45f : 1;
  if (sel) {
    float ring = ui_s(c, 2);
    uid_rrect(&c->d, ui_inset_all(b, -ring), rad + ring, t->accent);
    uid_rrect(&c->d, b, rad, t->tile_fill_on);
  } else {
    uid_rrect(&c->d, b, rad, uid_alpha(uid_mix(t->tile_fill, t->tile_fill_on, hov), a));
    uid_rrect_stroke(&c->d, b, rad, hair(c), uid_alpha(t->surface_inset_border, a));
  }
  UiFont nf = ui_font(c, UI_TYPE_CAPTION);
  float pad = ui_s(c, 4);
  UiRect in = ui_rect(b.x + pad, b.y + pad, b.w - 2 * pad, b.h - 2 * pad - nf.line);
  if (icon) ui_icon(c, icon, in, floorf(fminf(in.h, in.w) * 0.8f), uid_alpha(t->accent_soft_text, a));
  if (label) ui_text_fit(c, ui_rect(b.x + pad, b.y + b.h - pad - nf.line, b.w - 2 * pad, nf.line), UI_TYPE_CAPTION,
                         uid_alpha(t->text_primary, a), label, 0.5f);
  if (tooltip) ui_tooltip(c, id, r, tooltip);
  return ui_clicked(c, id);
}

void ui_divider_v(UiCtx *c, float x, float y0, float y1) {
  float w = hair(c);
  uid_rrect(&c->d, ui_rect(floorf(x), y0, w, y1 - y0), 0, c->tok.divider);
}

float ui_stat_width(UiCtx *c, UiIcon icon, const char *value, const char *unit) {
  float w = 0;
  if (icon) w += ui_s(c, 18) + ui_s(c, 7);
  w += ui_text_width(c, UI_TYPE_NUMBER, value);
  if (unit && *unit) w += ui_s(c, 6) + ui_text_width(c, UI_TYPE_CAPTION, unit);
  return w;
}

float ui_stat(UiCtx *c, float x, float cy, UiIcon icon, const char *value, const char *unit) {
  const UiTokens *t = &c->tok;
  float h = ui_s(c, 24);
  if (icon) {
    ui_icon(c, icon, ui_rect(x, cy - h / 2, ui_s(c, 18), h), ui_s(c, 18), t->accent_soft_text);
    x += ui_s(c, 18) + ui_s(c, 7);
  }
  UiFont nf = ui_font(c, UI_TYPE_NUMBER);
  float base = floorf(cy + nf.px * 0.36f + 0.5f);
  x = ui_text(c, x, base, UI_TYPE_NUMBER, t->text_primary, value);
  if (unit && *unit) x = ui_text(c, x + ui_s(c, 6), base, UI_TYPE_CAPTION, t->text_secondary, unit);
  return x;
}

void ui_tooltip(UiCtx *c, uint32_t id, UiRect anchor, const char *text) {
  if (!text || !*text || !ui_is_hot(c, id) || c->in.down) return;
  if (c->now - c->in.hot_since < c->tok.tooltip_delay_ms) return;
  c->tip_on = 1;
  c->tip_anchor = anchor;
  snprintf(c->tip, sizeof c->tip, "%.*s", ui_utf8_cut(text, (int)sizeof c->tip - 1), text);
}

static void draw_tooltip(UiCtx *c) {
  float h = ui_s(c, c->tok.tooltip_h), pad = ui_s(c, 12);
  float w = ui_text_width(c, UI_TYPE_LABEL, c->tip) + 2 * pad;
  float x = c->tip_anchor.x + c->tip_anchor.w / 2 - w / 2, y = c->tip_anchor.y - h - ui_s(c, 8);
  if (y < ui_s(c, 4)) y = c->tip_anchor.y + c->tip_anchor.h + ui_s(c, 8);
  if (x < ui_s(c, 4)) x = ui_s(c, 4);
  if (x + w > c->w - ui_s(c, 4)) x = c->w - ui_s(c, 4) - w;
  UiRect r = ui_rect_round(ui_rect(x, y, w, h));
  float a = ui_anim(c, 0x7001u, 1, c->tok.motion_hover);
  ui_panel(c, r, h / 2, UI_PANEL_STRONG | UI_PANEL_NOBLOCK);
  ui_text_in(c, r, UI_TYPE_LABEL, uid_alpha(c->tok.text_primary, a), c->tip, 0.5f);
}

static int wrap2(UiCtx *c, int style, const char *s, float w, char *l1, char *l2, int cap) {
  UiFont f = ui_font(c, style);
  int n = uit_fit(&c->t, f.weight, f.px, s, f.tracking, w);
  int len = (int)strlen(s);
  if (n >= len) {
    snprintf(l1, cap, "%s", s);
    l2[0] = 0;
    return 1;
  }
  snprintf(l1, cap, "%.*s", n, s);
  const char *rest = s + n;
  while (*rest == ' ') rest++;
  snprintf(l2, cap, "%s", rest);
  return 2;
}

float ui_toast_height(UiCtx *c, float w, const char *title, const char *line) {
  char a[256], b[256];
  float pad = ui_s(c, 10), por = ui_s(c, 44);
  float tw = w - pad - por - ui_s(c, 12) - ui_s(c, 20);
  int lines = line && *line ? wrap2(c, UI_TYPE_CAPTION, line, tw, a, b, sizeof a) : 0;
  float h = (title && *title ? ui_font(c, UI_TYPE_LABEL).line + ui_s(c, 2) : 0) + lines * ui_font(c, UI_TYPE_CAPTION).line;
  h += 2 * ui_s(c, 12);
  return fmaxf(h, por + 2 * pad);
}

void ui_toast(UiCtx *c, UiRect r, SDL_Texture *portrait, const char *title, const char *line) {
  const UiTokens *t = &c->tok;
  float rad = fminf(r.h / 2, ui_s(c, 34));
  ui_panel(c, r, rad, 0);
  float pad = ui_s(c, 10), por = ui_s(c, 44);
  float x = r.x + pad;
  if (portrait) {
    UiRect pr = ui_rect(x, r.y + (r.h - por) / 2, por, por);
    uid_rrect(&c->d, pr, por / 2, t->surface_inset);
    uid_image(&c->d, portrait, NULL, pr, por / 2, (UiColor){255, 255, 255, 255});
    x += por + ui_s(c, 12);
  } else {
    UiRect pr = ui_rect(x, r.y + (r.h - por) / 2, por, por);
    uid_rrect(&c->d, pr, por / 2, t->accent_soft);
    ui_icon(c, UI_ICON_BELL, pr, ui_s(c, 22), t->accent_soft_text);
    x += por + ui_s(c, 12);
  }
  float tw = r.x + r.w - ui_s(c, 20) - x;
  UiFont lf = ui_font(c, UI_TYPE_LABEL), cf = ui_font(c, UI_TYPE_CAPTION);
  char a[256], b[256];
  int lines = line && *line ? wrap2(c, UI_TYPE_CAPTION, line, tw, a, b, sizeof a) : 0;
  float th = (title && *title ? lf.line + ui_s(c, 2) : 0) + lines * cf.line;
  float y = r.y + (r.h - th) / 2;
  if (title && *title) {
    ui_text_fit(c, ui_rect(x, y, tw, lf.line), UI_TYPE_LABEL, t->text_primary, title, 0);
    y += lf.line + ui_s(c, 2);
  }
  if (lines >= 1) ui_text_fit(c, ui_rect(x, y, tw, cf.line), UI_TYPE_CAPTION, t->text_secondary, a, 0);
  if (lines == 2) ui_text_fit(c, ui_rect(x, y + cf.line, tw, cf.line), UI_TYPE_CAPTION, t->text_secondary, b, 0);
}

float ui_scroll_begin(UiCtx *c, uint32_t id, UiRect view, float content_w) {
  float *off = ui_state(c, id);
  float max = content_w - view.w;
  if (max < 0) max = 0;
  ui_hits_add(&c->cur, view, 0, id, UI_HIT_BLOCK | UI_HIT_WHEEL);
  float wheel = ui_wheel(c, id);
  if (wheel != 0) *off -= wheel * ui_s(c, c->tok.tile_w + c->tok.gap_tiles);
  if (*off > max) *off = max;
  if (*off < 0) *off = 0;
  c->scroll_on = 1;
  c->scroll_view = view;
  uid_set_clip(&c->d, &view);
  ui_hits_clip(&c->cur, view);
  return -floorf(*off + 0.5f);
}

void ui_scroll_end(UiCtx *c) {
  c->scroll_on = 0;
  uid_set_clip(&c->d, NULL);
  ui_hits_unclip(&c->cur);
}

// Vertical scroll area (task-32, long lists): the wheel scrolls by step px.
// Returns the y offset to add to children; call ui_vscroll_end after them.
// *max_out (optional) gets the largest offset.
float ui_vscroll_begin(UiCtx *c, uint32_t id, UiRect view, float content_h, float step) {
  float *off = ui_state(c, id);
  float max = content_h - view.h;
  if (max < 0) max = 0;
  ui_hits_add(&c->cur, view, 0, id, UI_HIT_BLOCK | UI_HIT_WHEEL);
  float wheel = ui_wheel(c, id);
  if (wheel != 0) *off -= wheel * step;
  if (*off > max) *off = max;
  if (*off < 0) *off = 0;
  c->scroll_on = 1;
  c->scroll_view = view;
  uid_set_clip(&c->d, &view);
  ui_hits_clip(&c->cur, view);
  return -floorf(*off + 0.5f);
}

void ui_vscroll_end(UiCtx *c) {
  c->scroll_on = 0;
  uid_set_clip(&c->d, NULL);
  ui_hits_unclip(&c->cur);
}

// ------------------------------------------------------------------ more components

void ui_card(UiCtx *c, UiRect r, int selected) {
  const UiTokens *t = &c->tok;
  float rad = ui_s(c, t->radius_card);
  if (selected) {
    float ring = ui_s(c, 2);
    uid_rrect(&c->d, ui_inset_all(r, -ring), rad + ring, t->accent);
  }
  uid_rrect(&c->d, r, rad, selected ? t->glass_fill_strong : t->surface_inset);
  if (!selected) uid_rrect_stroke(&c->d, r, rad, hair(c), t->surface_inset_border);
}

void ui_meter(UiCtx *c, UiRect r, float v) {
  const UiTokens *t = &c->tok;
  v = v < 0 ? 0 : v > 1 ? 1 : v;
  uid_rrect(&c->d, r, r.h / 2, t->meter_track);
  if (v > 0) {
    UiRect f = r;
    f.w = fmaxf(r.h, r.w * v);
    uid_rrect_grad(&c->d, f, r.h / 2, v < 0.4f ? t->warn_grad : t->meter_grad);
  }
}

int ui_slider(UiCtx *c, uint32_t id, UiRect r, float *value, const char *value_text) {
  const UiTokens *t = &c->tok;
  float vw = value_text ? ui_s(c, 56) : 0;
  UiRect track = ui_rect(r.x, r.y + (r.h - ui_s(c, t->slider_track_h)) / 2, r.w - vw, ui_s(c, t->slider_track_h));
  float th = ui_s(c, t->slider_thumb);
  ui_hits_add(&c->cur, ui_rect(r.x - th / 2, r.y, r.w - vw + th, r.h), 0, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
  int changed = 0;
  if (ui_is_active(c, id)) {
    float v = (c->in.x - track.x) / track.w;
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    if (v != *value) *value = v, changed = 1;
  }
  uid_rrect(&c->d, track, track.h / 2, t->meter_track);
  UiRect fill = track;
  fill.w = fmaxf(track.h, track.w * *value);
  uid_rrect_grad(&c->d, fill, track.h / 2, t->meter_grad);
  float cx = track.x + track.w * *value, cy = track.y + track.h / 2;
  UiRect thumb = ui_rect(cx - th / 2, cy - th / 2, th, th);
  shadow(c, thumb, th / 2, t->thumb_shadow);
  uid_rrect(&c->d, thumb, th / 2, (UiColor){255, 255, 255, 255});
  if (value_text) ui_text_in(c, ui_rect(r.x + r.w - vw, r.y, vw, r.h), UI_TYPE_NUMBER, t->text_primary, value_text, 1);
  return changed;
}

int ui_toggle(UiCtx *c, uint32_t id, UiRect r, int *on) {
  const UiTokens *t = &c->tok;
  float w = ui_s(c, t->toggle_w), h = ui_s(c, t->toggle_h);
  UiRect sw = ui_rect(r.x + r.w - w, r.y + (r.h - h) / 2, w, h);
  ui_hits_add(&c->cur, r, h / 2, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
  int changed = 0;
  if (ui_clicked(c, id)) *on = !*on, changed = 1;
  float k = ui_anim(c, id ^ 0x70661eu, *on ? 1.0f : 0.0f, t->motion_hover);
  uid_rrect(&c->d, sw, h / 2, uid_mix(t->meter_track, t->accent, k));
  if (k < 0.5f) uid_rrect_stroke(&c->d, sw, h / 2, hair(c), t->divider);
  float d = h - ui_s(c, 6);
  UiRect knob = ui_rect(sw.x + ui_s(c, 3) + (w - d - ui_s(c, 6)) * k, sw.y + ui_s(c, 3), d, d);
  shadow(c, knob, d / 2, t->thumb_shadow);
  uid_rrect(&c->d, knob, d / 2, (UiColor){255, 255, 255, 255});
  return changed;
}

int ui_list_row(UiCtx *c, uint32_t id, UiRect r, SDL_Texture *avatar, const char *name, const char *line,
                const char *right, int flags) {
  const UiTokens *t = &c->tok;
  float rad = ui_s(c, t->radius_tile);
  int sel = flags & UI_SELECTED, dis = flags & UI_DISABLED;
  ui_hits_add(&c->cur, r, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
  float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
  if (sel) {
    float ring = ui_s(c, 2);
    uid_rrect(&c->d, ui_inset_all(r, -ring), rad + ring, t->accent);
    uid_rrect(&c->d, r, rad, t->glass_fill_strong);
  } else if (hov > 0) {
    uid_rrect(&c->d, r, rad, uid_alpha(t->surface_inset, hov));
  }
  float a = dis ? 0.6f : 1;
  float av = ui_s(c, 52), pad = ui_s(c, 12);
  UiRect ar = ui_rect(r.x + pad, r.y + (r.h - av) / 2, av, av);
  uid_rrect(&c->d, ar, av / 2, t->surface_inset);
  if (avatar) uid_image(&c->d, avatar, NULL, ar, av / 2, (UiColor){255, 255, 255, (uint8_t)(255 * a)});
  float x = ar.x + av + ui_s(c, 14);
  float rw = right ? ui_text_width(c, UI_TYPE_NUMBER, right) + pad : 0;
  float tw = r.x + r.w - pad - rw - x;
  UiFont nf = ui_font(c, UI_TYPE_HEADING), cf = ui_font(c, UI_TYPE_CAPTION);
  float th = nf.line + (line ? cf.line : 0), y = r.y + (r.h - th) / 2;
  ui_text_fit(c, ui_rect(x, y, tw, nf.line), UI_TYPE_HEADING, uid_alpha(t->text_primary, a), name, 0);
  if (line) ui_text_fit(c, ui_rect(x, y + nf.line, tw, cf.line), UI_TYPE_CAPTION, uid_alpha(t->text_secondary, a), line, 0);
  if (right) ui_text_in(c, ui_rect(r.x + r.w - pad - rw, r.y, rw, r.h), UI_TYPE_NUMBER, uid_alpha(t->text_primary, a), right, 1);
  return ui_clicked(c, id);
}

UiRect ui_dialog_begin(UiCtx *c, uint32_t id, float w, float h, const char *title) {
  const UiTokens *t = &c->tok;
  (void)id;
  UiRect s = ui_screen(c);
  uid_rrect(&c->d, s, 0, t->scrim);
  ui_hits_add(&c->cur, s, 0, 0, UI_HIT_BLOCK | UI_HIT_MODAL);
  w = fminf(fmaxf(w, ui_s(c, t->dialog_min_w)), ui_s(c, t->dialog_max_w));
  UiRect r = ui_rect_round(ui_center(s, w, h));
  ui_panel(c, r, ui_s(c, t->radius_panel), UI_PANEL_STRONG);
  float pad = ui_s(c, t->pad_dialog);
  UiRect in = ui_inset_all(r, pad);
  if (title && *title) {
    UiFont f = ui_font(c, UI_TYPE_TITLE);
    ui_text_fit(c, ui_rect(in.x, in.y, in.w, f.line), UI_TYPE_TITLE, t->text_primary, title, 0);
    in = ui_inset(in, 0, f.line + ui_s(c, 12), 0, 0);
  }
  return in;
}

void ui_thought(UiCtx *c, float x, float y, SDL_Texture *icon, const char *text) {
  const UiTokens *t = &c->tok;
  float maxw = ui_s(c, t->bubble_max_w), isz = ui_s(c, 28);
  char a[256], b[256];
  float padl = ui_s(c, 9), padr = ui_s(c, 16), padv = ui_s(c, 9);
  int lines = wrap2(c, UI_TYPE_LABEL, text, maxw - padl - isz - ui_s(c, 8) - padr, a, b, sizeof a);
  UiFont f = ui_font(c, UI_TYPE_LABEL);
  float tw = fmaxf(ui_text_width(c, UI_TYPE_LABEL, a), lines > 1 ? ui_text_width(c, UI_TYPE_LABEL, b) : 0);
  float w = padl + isz + ui_s(c, 8) + tw + padr, h = fmaxf(isz, lines * f.line) + 2 * padv;
  float k = ui_anim(c, ui_id(text), 1, 160);
  float sc = 0.9f + 0.1f * k;
  UiRect r = ui_rect(x - w * sc / 2, y - ui_s(c, 14) - h * sc, w * sc, h * sc);
  shadow(c, r, r.h / 2, (UiShadow){0, 6, 16, uid_alpha(t->glass_shadow.color, 0.8f)});
  if (!uid_rrect_backdrop(&c->d, r, fminf(r.h / 2, ui_s(c, 22)))) {}
  uid_rrect(&c->d, r, fminf(r.h / 2, ui_s(c, 22)), (UiColor){255, 255, 255, 153});
  uid_rrect_stroke(&c->d, r, fminf(r.h / 2, ui_s(c, 22)), hair(c), (UiColor){255, 255, 255, 230});
  float dot = ui_s(c, 7);
  uid_rrect(&c->d, ui_rect(x - dot / 2, r.y + r.h + ui_s(c, 3), dot, dot), dot / 2, (UiColor){255, 255, 255, 200});
  UiRect ir = ui_rect(r.x + padl * sc, r.y + (r.h - isz) / 2, isz, isz);
  if (icon) uid_image(&c->d, icon, NULL, ir, 0, (UiColor){255, 255, 255, 255});
  else ui_icon(c, UI_ICON_SMILE, ir, isz, t->accent);
  float tx = ir.x + isz + ui_s(c, 8), ty = r.y + (r.h - lines * f.line) / 2;
  ui_text_in(c, ui_rect(tx, ty, tw + 1, f.line), UI_TYPE_LABEL, t->text_primary, a, 0);
  if (lines > 1) ui_text_in(c, ui_rect(tx, ty + f.line, tw + 1, f.line), UI_TYPE_LABEL, t->text_primary, b, 0);
}

UiRect ui_popover(UiCtx *c, float ax, float ay, float w, float h, int side) {
  const UiTokens *t = &c->tok;
  float arrow = ui_s(c, t->popover_arrow), gap = arrow / 2;
  UiRect r;
  if (side > 0) r = ui_rect(ax + gap + arrow / 2, ay - h / 2, w, h);   // to the right of the anchor
  else r = ui_rect(ax - gap - arrow / 2 - w, ay - h / 2, w, h);
  float m = ui_s(c, t->edge_margin);
    if (r.x + r.w > c->w - m) r.x = c->w - m - r.w;
    if (r.x < m) r.x = m;
  if (r.y < m) r.y = m;
  if (r.y + r.h > c->h - m) r.y = c->h - m - r.h;
  r = ui_rect_round(r);
  ui_panel(c, r, ui_s(c, t->radius_panel), UI_PANEL_STRONG);
  float ex = side > 0 ? r.x : r.x + r.w, tip = side > 0 ? ex - arrow / 2 : ex + arrow / 2;
  float cy = fminf(fmaxf(ay, r.y + arrow), r.y + r.h - arrow);
  uid_triangle(&c->d, ex, cy - arrow / 2, tip, cy, ex, cy + arrow / 2, t->glass_fill_strong);
  return r;
}

void ui_tag(UiCtx *c, float cx, float y, const char *text, int selected) {
  const UiTokens *t = &c->tok;
  float h = ui_s(c, 24), pad = ui_s(c, 10);
  float w = ui_text_width(c, UI_TYPE_CAPTION, text) + 2 * pad;
  UiRect r = ui_rect_round(ui_rect(cx - w / 2, y, w, h));
  if (selected) uid_rrect(&c->d, r, h / 2, t->accent);
  else {
    uid_rrect_backdrop(&c->d, r, h / 2);
    uid_rrect(&c->d, r, h / 2, t->glass_fill);
    uid_rrect_stroke(&c->d, r, h / 2, hair(c), t->glass_border);
  }
  ui_text_in(c, r, UI_TYPE_CAPTION, selected ? t->text_on_accent : t->text_primary, text, 0.5f);
}

// Discard only GPU resources. Fonts, input queues and active controls survive.
void ui_device_reset(SDL_Renderer *r) {
  for(UiCtx *c=g_contexts;c;c=c->next_context) {
    if(c->r!=r)continue;
    for(int k=0;k<c->nicons;k++)if(c->icons[k].tex)SDL_DestroyTexture(c->icons[k].tex);
    memset(c->icons,0,sizeof c->icons);c->nicons=0;
    uid_free(&c->d);uid_init(&c->d,r);
    uit_device_reset(&c->t);
  }
}

void ui_renderer_rebind(SDL_Renderer *old,SDL_Renderer *next) {
  for(UiCtx *c=g_contexts;c;c=c->next_context) {
    if(c->r!=old)continue;
    c->r=next;c->d.r=next;c->t.r=next;
  }
}
