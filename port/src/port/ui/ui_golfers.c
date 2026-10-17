// Frosted Mint golfers in the world (task-31): see ui_golfers.h.
#include "port/ui/ui_golfers.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI_F 3.14159265f

static GwLayout g_layout;
const GwLayout *ui_golfers_layout(void) { return &g_layout; }

const char *ui_golfers_tone_name(int tone) {
  switch (tone) {
    case -2: return "eagle";
    case -1: return "under par";
    case 0: return "par";
    case 1: return "over par";
    default: return "double bogey or worse";
  }
}

// Greedy wrap of s into at most max lines no wider than w; the last line is
// cut with an ellipsis. Returns the number of lines.
static int wrap_lines(UiCtx *c, int style, const char *s, float w, char (*out)[160], int max) {
  int n = 0;
  char line[160] = "";
  const char *p = s;
  while (*p && n < max) {
    const char *e = p;
    while (*e && *e != ' ') e++;
    char word[120];
    int wl = (int)(e - p);
    if (wl > 119) wl = 119;
    memcpy(word, p, (size_t)wl);
    word[wl] = 0;
    char trial[300];
    snprintf(trial, sizeof trial, "%s%s%s", line, line[0] ? " " : "", word);
    if (line[0] && ui_text_width(c, style, trial) > w) {
      snprintf(out[n++], 160, "%s", line);
      snprintf(line, sizeof line, "%s", word);
    } else {
      snprintf(line, sizeof line, "%s", trial);
    }
    p = *e ? e + 1 : e;
  }
  if (n < max && line[0]) snprintf(out[n++], 160, "%s", line);
  else if (line[0] && n == max) {
    // text left over: ellipsis on the last line
    char t[170];
    snprintf(t, sizeof t, "%.*s...", ui_utf8_cut(out[max - 1], 150), out[max - 1]);
    snprintf(out[max - 1], 160, "%s", t);
  }
  return n;
}

static UiColor tone_colour(const UiTokens *t, int tone) {
  switch (tone) {
    case -2: return (UiColor){214, 140, 20, 255};   // eagle: gold
    case -1: return t->accent;                       // under par: mint
    case 1: return t->text_secondary;                // over par: quiet
    case 2: return t->danger;
    default: return t->text_primary;
  }
}

static void ring(UiCtx *c, float cx, float cy, float rx, float ry, float width, UiColor col) {
  UiDraw *d = ui_drawer(c);
  const int N = 40;
  float px = cx + rx, py = cy;
  for (int i = 1; i <= N; i++) {
    float a = 2 * PI_F * i / N;
    float x = cx + rx * cosf(a), y = cy + ry * sinf(a);
    uid_line(d, px, py, x, y, width, col);
    px = x;
    py = y;
  }
}

// ------------------------------------------------------------------ options and remembered state (task-60)

static GwOpts g_o;
static int g_o_set;

void ui_golfers_opts_default(GwOpts *o) {
  memset(o, 0, sizeof *o);
  o->tags = GW_TAGS_HOVER;
  o->bubbles = GW_BUB_BRIEF;
  o->tag_style = GW_STYLE_PLAIN;
  o->tag_scale = 0.7f;
  o->tag_opacity = 0.85f;
  o->brief_ms = 4000;
}

void ui_golfers_set_opts(const GwOpts *o) {
  g_o = *o;
  g_o_set = 1;
}

static const GwOpts *opts(void) {
  if (!g_o_set) ui_golfers_opts_default(&g_o), g_o_set = 1;
  return &g_o;
}

// task-56: while a zoom glide runs, a pinned point p of the note is at zoom_s * p + zoom_xy (canvas px).
static float g_zs = 1, g_zx, g_zy;

#define GW_SLOT_G 152
#define GW_SLOT_ALL 256

static int slot_idx(int slot) {
  if (slot < 0) return -1;
  if (slot == 999) return GW_SLOT_ALL - 1;   // tutorial guidance has no golfer owner
  if (slot < GW_SLOT_G) return slot;
  if (slot >= 1000) return GW_SLOT_G + (slot - 1000) % (GW_SLOT_ALL - GW_SLOT_G - 1);
  return -1;
}

typedef struct {
  float a;            // 0..1 now
  int live;           // seen this frame
  int x, y, selected, staff;   // canvas px of the feet
  char name[40];
} TagSt;
typedef struct {
  uint32_t h;
  double t0;
  int live;
} BubSt;
static TagSt g_tag[GW_SLOT_ALL];
static BubSt g_bub[GW_SLOT_ALL];
static double g_last_ms, g_clock;
static float g_dt = 16;

void ui_golfers_reset(void) {
  memset(g_tag, 0, sizeof g_tag);
  memset(g_bub, 0, sizeof g_bub);
  g_last_ms = g_clock = 0;
  g_dt = 16;
}

static void tick(const GwNote *n) {
  const GwOpts *o = opts();
  double dt = g_last_ms > 0 ? o->now_ms - g_last_ms : 16;
  g_last_ms = o->now_ms;
  g_dt = (float)(dt < 0 ? 0 : dt > 100 ? 100 : dt);
  if (!n->paused) g_clock += g_dt;
}

static uint32_t hash_str(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
  return h ? h : 1;
}

// a eased towards target: up_ms to rise from 0 to 1, down_ms to fall
static float step_to(float a, float target, float up_ms, float down_ms) {
  if (opts()->still) return target;
  if (a < target) a = fminf(target, a + g_dt / up_ms);
  else a = fmaxf(target, a - g_dt / down_ms);
  return a;
}

// The golfer's body as a box around the feet (x, y), target px: is the pointer in it?
static int over_body(const GwNote *n, float tx, float ty, float k, float *score) {
  const GwOpts *o = opts();
  if (!o->have_mouse) return 0;
  float zoom = n->zoom > 0 ? (float)n->zoom : 4;
  float hw = (zoom * 2.5f + 3) * k * g_zs, top = zoom * 10 * k * g_zs, bot = 4 * k * g_zs;
  if (o->mx < tx - hw || o->mx > tx + hw || o->my < ty - top || o->my > ty + bot) return 0;
  if (score) *score = fabsf(o->my - (ty - top / 2)) + fabsf(o->mx - tx) * 0.5f;
  return 1;
}

static float hair_px(UiCtx *c) { return ui_scale(c) < 1.5f ? 1.0f : floorf(ui_scale(c) + 0.25f); }

// Zoomed out the names and bubbles shrink and fade: golfers are small there.
static float zoom_k(const GwNote *n) {
  float zoom = n->zoom > 0 ? (float)n->zoom : 4;
  return zoom >= 4 ? 1.0f : zoom <= 1 ? 0.5f : 0.5f + (zoom - 1) / 3 * 0.5f;
}

// Text with a thin dark outline and a soft shadow, for names without a pill.
static void outlined_text(UiCtx *c, UiRect r, int style, UiColor col, UiColor edge, const char *s, float a) {
  float h = hair_px(c);
  static const float off[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  UiRect sr = r;
  sr.y += h * 1.5f;
  ui_text_in(c, sr, style, uid_alpha(edge, 0.35f * a), s, 0.5f);
  UiColor e = uid_alpha(edge, 0.62f * a);
  for (int i = 0; i < 4; i++) {
    UiRect q = r;
    q.x += off[i][0] * h;
    q.y += off[i][1] * h;
    ui_text_in(c, q, style, e, s, 0.5f);
  }
  ui_text_in(c, r, style, uid_alpha(col, a), s, 0.5f);
}

// One name: plain text, or a tight glass pill (hover, selection, ui.tag_style=pill).
static void draw_name(UiCtx *c, float cx, float top, float th, const char *text, float a, int selected, int hover) {
  const UiTokens *t = ui_tokens(c);
  const GwOpts *o = opts();
  UiDraw *d = ui_drawer(c);
  float pad = ui_s(c, 10);
  float w = ui_text_width(c, o->tag_style == GW_STYLE_PILL || selected || hover ? UI_TYPE_CAPTION : UI_TYPE_LABEL, text) + 2 * pad;
  UiRect r = ui_rect_round(ui_rect(cx - w / 2, top, w, th));
  int pill = selected || hover || o->tag_style == GW_STYLE_PILL;
  if (!pill) {
    outlined_text(c, r, UI_TYPE_LABEL, (UiColor){255, 255, 255, 255}, (UiColor){8, 30, 22, 255}, text, a);
    return;
  }
  if (selected) {
    uid_rrect(d, r, th / 2, uid_alpha(t->accent, a));
    ui_text_in(c, r, UI_TYPE_CAPTION, uid_alpha(t->text_on_accent, a), text, 0.5f);
    return;
  }
  uid_rrect_shadow(d, r, th / 2, 0, ui_s(c, 2), ui_s(c, 6), uid_alpha(t->glass_shadow.color, 0.5f * a));
  uid_rrect_backdrop(d, r, th / 2);
  UiColor fill = t->glass_fill;
  fill.a = (uint8_t)fminf(255, fill.a * o->tag_opacity * a + 0.5f);
  uid_rrect(d, r, th / 2, fill);
  uid_rrect_stroke(d, r, th / 2, hair_px(c), uid_alpha(t->glass_border, a));
  ui_text_in(c, r, UI_TYPE_CAPTION, uid_alpha(t->text_primary, a), text, 0.5f);
}

// ------------------------------------------------------------------ tags and bubbles

void ui_golfers_set_zoom(float s, float x, float y) {
  g_zs = s > 0 ? s : 1;
  g_zx = x;
  g_zy = y;
}

static void draw_tags(UiCtx *c, const GwNote *n, float vx, float vy, float k, float sx, float sy) {
  const UiTokens *t = ui_tokens(c);
  const GwOpts *o = opts();
  // the selected golfer's ellipse first (under every tag)
  for (int i = 0; i < n->n_tags; i++) {
    const GwTag *g = &n->tags[i];
    if (!g->selected) continue;
    float x = vx + (g->x * g_zs + g_zx + sx) * k, y = vy + (g->y * g_zs + g_zy + sy) * k;
    ring(c, x, y, 17 * k * g_zs, 8.5f * k * g_zs, fmaxf(2.5f, ui_s(c, 2.5f)), t->accent);
  }
  float ts = o->tag_scale < 0.4f ? 0.4f : o->tag_scale > 1.6f ? 1.6f : o->tag_scale;
  float zk = zoom_k(n);
  // who is under the pointer: the nearest golfer or staff member
  int hov = -1;
  float best = 1e9f;
  for (int i = 0; i < n->n_tags && i < GW_TAGS; i++) {
    const GwTag *g = &n->tags[i];
    float tx = vx + (g->x * g_zs + g_zx + sx) * k, ty = vy + (g->y * g_zs + g_zy + sy) * k, sc;
    if (over_body(n, tx, ty, k, &sc) && sc < best) best = sc, hov = i;
  }
  int hov_idx = hov >= 0 ? slot_idx(n->tags[hov].slot) : -1;
  // who wants to be seen
  for (int i = 0; i < GW_SLOT_ALL; i++) g_tag[i].live = 0;
  for (int i = 0; i < n->n_tags && i < GW_TAGS; i++) {
    const GwTag *g = &n->tags[i];
    int s = slot_idx(g->slot);
    if (s < 0 || !g->name[0]) continue;
    TagSt *st = &g_tag[s];
    st->live = 1;
    st->x = g->x;
    st->y = g->y;
    st->selected = g->selected;
    st->staff = g->slot >= 1000;
    snprintf(st->name, sizeof st->name, "%s", g->name);
    int want = 0;
    if (o->tags == GW_TAGS_OFF) want = 0;
    else if (g->selected || s == hov_idx || o->tags == GW_TAGS_ALWAYS) want = 1;
    else if (o->show_all && !st->staff) want = 1;
    st->a = step_to(st->a, (float)want, 140, 260);
  }
  int order[GW_SLOT_ALL], m = 0;
  for (int i = 0; i < GW_SLOT_ALL; i++) {
    // gone from the frame (zoomed out, a thought is showing): fade out where it was
    if (!g_tag[i].live) g_tag[i].a = step_to(g_tag[i].a, 0, 140, 160);
    if (g_tag[i].a > 0.01f && g_tag[i].name[0]) order[m++] = i;
  }
  for (int a = 1; a < m; a++) {
    int v = order[a], b = a - 1;
    while (b >= 0 && g_tag[order[b]].y > g_tag[v].y) order[b + 1] = order[b], b--;
    order[b + 1] = v;
  }
  ui_push_scale(c, ts * zk);
  struct { float x0, y0, x1, y1; } placed[GW_SLOT_ALL];
  int np = 0;
  float th = ui_s(c, 24), gap2 = ui_s(c, 2);
  for (int a = 0; a < m; a++) {
    TagSt *st = &g_tag[order[a]];
    float x = vx + (st->x * g_zs + g_zx + sx) * k, y = vy + (st->y * g_zs + g_zy + sy) * k;
    float top = y + ui_s(c, 4);
    float tw = ui_text_width(c, UI_TYPE_CAPTION, st->name) + 2 * ui_s(c, 10);
    for (int tries = 0; tries < 8 && !st->selected; tries++) {
      int hit = 0;
      for (int i = 0; i < np; i++)
        if (x - tw / 2 < placed[i].x1 + gap2 && x + tw / 2 > placed[i].x0 - gap2 && top < placed[i].y1 + gap2 && top + th > placed[i].y0 - gap2) {
          top = placed[i].y1 + gap2;
          hit = 1;
        }
      if (!hit) break;
    }
    placed[np].x0 = x - tw / 2;
    placed[np].x1 = x + tw / 2;
    placed[np].y0 = top;
    placed[np].y1 = top + th;
    np++;
    draw_name(c, x, top, th, st->name, st->a * (0.6f + 0.4f * zk), st->selected, order[a] == hov_idx);
  }
  // the people tab (staff have no mood): golf drew a mood bar under each name, with or without the name
  if (n->dock_mode == 2) {
    UiDraw *dd = ui_drawer(c);
    for (int i = 0; i < n->n_tags && i < GW_TAGS; i++) {
      const GwTag *g = &n->tags[i];
      int s = slot_idx(g->slot);
      if (s < 0 || g->slot >= GW_SLOT_G) continue;
      float x = vx + (g->x * g_zs + g_zx + sx) * k, y = vy + (g->y * g_zs + g_zy + sy) * k;
      float by = y + ui_s(c, 3) + (g_tag[s].a > 0.5f ? th + ui_s(c, 2) : ui_s(c, 3));
      float w = ui_s(c, 34), h = ui_s(c, 4);
      float v = (g->mood - 4) / 8.0f;
      v = v < 0 ? 0 : v > 1 ? 1 : v;
      UiRect r = ui_rect_round(ui_rect(x - w / 2, by, w, h));
      uid_rrect(dd, r, h / 2, t->meter_track);
      UiRect f = r;
      f.w = fmaxf(r.w * v, h);
      uid_rrect_grad(dd, f, h / 2, v < 0.4f ? t->warn_grad : t->meter_grad);
    }
  }
  ui_pop_scale(c);
}

// A big thought: the glass bubble with the mood face (or the speaker's) and the words.
static void draw_bubble_full(UiCtx *c, float x, float bottom, SDL_Texture *tex, SDL_Texture *mood_tex, int mood, const char *text, float a, int tutorial) {
  const UiTokens *t = ui_tokens(c);
  const GwOpts *o = opts();
  UiDraw *d = ui_drawer(c);
  UiRect scr = ui_screen(c);
  float maxw = tutorial ? fminf(ui_s(c, 560), scr.w - ui_s(c, 40)) : ui_s(c, t->bubble_max_w), isz = ui_s(c, 28);
  char ln[10][160];
  float padl = ui_s(c, 9), padr = ui_s(c, 16), padv = ui_s(c, 9);
  int lines = wrap_lines(c, UI_TYPE_LABEL, text, maxw - padl - isz - ui_s(c, 8) - padr, ln, tutorial ? 10 : 2);
  UiFont f = ui_font(c, UI_TYPE_LABEL);
  float tw = ln[0][0] ? ui_text_width(c, UI_TYPE_LABEL, ln[0]) : 0;
  for (int i = 1; i < lines; i++) tw = fmaxf(tw, ui_text_width(c, UI_TYPE_LABEL, ln[i]));
  float w = padl + isz + ui_s(c, 8) + tw + padr, h = fmaxf(isz, lines * f.line) + 2 * padv;
  UiRect r = ui_rect(x - w / 2, bottom - h, w, h);
  if (tutorial) {
    r.x = fmaxf(scr.x + ui_s(c, 12), fminf(r.x, scr.x + scr.w - ui_s(c, 12) - w));
    r.y = fmaxf(scr.y + ui_s(c, 12), r.y);
  }
  float rad = fminf(h / 2, ui_s(c, 22));
  float op = o->tag_opacity;
  uid_rrect_shadow(d, r, rad, 0, ui_s(c, 6), ui_s(c, 16), uid_alpha(t->glass_shadow.color, 0.8f * a));
  uid_rrect_backdrop(d, r, rad);
  uid_rrect(d, r, rad, (UiColor){255, 255, 255, (uint8_t)(153 * op * a + 0.5f)});
  uid_rrect_stroke(d, r, rad, hair_px(c), (UiColor){255, 255, 255, (uint8_t)(200 * a + 0.5f)});
  float dot = ui_s(c, 7);
  uid_rrect(d, ui_rect(x - dot / 2, r.y + r.h + ui_s(c, 3), dot, dot), dot / 2, (UiColor){255, 255, 255, (uint8_t)(170 * op * a + 0.5f)});
  UiRect ir = ui_rect(r.x + padl, r.y + (r.h - isz) / 2, isz, isz);
  if (tex) uid_image(d, tex, NULL, ir, 0, (UiColor){255, 255, 255, (uint8_t)(255 * a + 0.5f)});
  else if (o->faces_original && mood_tex) uid_image(d, mood_tex, NULL, ir, 0, (UiColor){255, 255, 255, (uint8_t)(255 * a + 0.5f)});
  else ui_face(c, mood, ir, isz, a);
  float tx = ir.x + isz + ui_s(c, 8), ty = r.y + (r.h - lines * f.line) / 2;
  g_layout.bubble_text = ui_rect(tx, ty, tw, lines * f.line);
  for (int i = 0; i < lines; i++)
    ui_text_in(c, ui_rect(tx, ty + i * f.line, tw + 1, f.line), UI_TYPE_LABEL, uid_alpha(t->text_primary, a), ln[i], 0);
}

// A minor thought: just the face over the head.
static void draw_bubble_icon(UiCtx *c, float x, float bottom, SDL_Texture *tex, SDL_Texture *mood_tex, int mood, float a) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  float isz = ui_s(c, 30);
  UiRect ir = ui_rect(x - isz / 2, bottom - isz, isz, isz);
  uid_rrect_shadow(d, ir, isz / 2, 0, ui_s(c, 3), ui_s(c, 8), uid_alpha(t->glass_shadow.color, 0.6f * a));
  if (tex) uid_image(d, tex, NULL, ir, isz / 2, (UiColor){255, 255, 255, (uint8_t)(255 * a + 0.5f)});
  else if (opts()->faces_original && mood_tex) uid_image(d, mood_tex, NULL, ir, 0, (UiColor){255, 255, 255, (uint8_t)(255 * a + 0.5f)});
  else ui_face(c, mood, ir, isz, a);
}

static int mood_of_art(int art) { return art >= GW_ART_MOOD && art <= GW_ART_MOOD + 10 ? art - GW_ART_MOOD : 5; }

static void draw_bubbles(UiCtx *c, const GwNote *n, SDL_Texture *const *art, float vx, float vy, float k, float sx, float sy) {
  const GwOpts *o = opts();
  for (int i = 0; i < GW_SLOT_ALL; i++) g_bub[i].live = 0;
  float ts = o->tag_scale < 0.4f ? 0.4f : o->tag_scale > 1.6f ? 1.6f : o->tag_scale;
  float zk = zoom_k(n);
  int order[GW_BUBBLES], m = 0;
  for (int i = 0; i < n->n_bubbles && i < GW_BUBBLES; i++) order[m++] = i;
  for (int a = 1; a < m; a++) {   // far ones first so the near ones lie over them
    int v = order[a], b = a - 1;
    while (b >= 0 && n->bubbles[order[b]].y > n->bubbles[v].y) order[b + 1] = order[b], b--;
    order[b + 1] = v;
  }
  ui_push_scale(c, ts * zk);
  struct { float x0, y0, x1, y1; } placed[GW_BUBBLES];
  int np = 0;
  UiFont lf = ui_font(c, UI_TYPE_LABEL);
  float maxw = ui_s(c, ui_tokens(c)->bubble_max_w), chrome = ui_s(c, 61), gap2 = ui_s(c, 3);
  float zoom = n->zoom > 0 ? (float)n->zoom : 4;
  for (int a = 0; a < m; a++) {
    const GwBubble *b = &n->bubbles[order[a]];
    if (o->bubbles == GW_BUB_OFF && b->kind != 2) continue;
    if (!b->text[0]) continue;
    int si = slot_idx(b->slot);
    if (si < 0) continue;
    float x = vx + (b->x * g_zs + g_zx + sx) * k, y = vy + (b->y * g_zs + g_zy + sy) * k;
    // under the pointer: the golfer (from the head down) or the bubble itself
    int hov = 0;
    if (o->have_mouse) {
      float hw = (zoom * 2.5f + 6) * k;
      hov = o->mx > x - hw && o->mx < x + hw && o->my > y - ui_s(c, 40) && o->my < y + zoom * 9 * k;
    }
    uint32_t hs = hash_str(b->text);
    BubSt *st = &g_bub[si];
    if (st->h != hs) {
      st->h = hs;
      st->t0 = g_clock;
    }
    st->live = 1;
    float life = (float)(g_clock - st->t0);
    float brief = (float)o->brief_ms;
    if (o->bubbles != GW_BUB_FULL && hov && life > brief - 700) {   // the pointer keeps it from fading
      st->t0 = g_clock - (brief - 700);
      life = brief - 700;
    }
    float al = 1;
    if (!o->still) {
      float ramp = life / 140;
      al = ramp > 1 ? 1 : ramp;
      if (b->kind != 2 && o->bubbles != GW_BUB_FULL && life > brief && !(n->paused && life < brief + 700)) al *= fmaxf(0, 1 - (life - brief) / 700);   // tutorial guidance stays while the game supplies it
    }
    if (al <= 0.01f) continue;
    int big = b->kind == 2 || o->bubbles == GW_BUB_FULL || (o->bubbles == GW_BUB_BRIEF && (b->tone != 0 || b->slot >= 1000)) || hov;
    int mood = mood_of_art(b->art);
    SDL_Texture *tex = b->kind == 1 && b->art >= 0 && b->art < GW_ART_MAX ? art[b->art] : NULL;
    float bottom = y - ui_s(c, 14);
    float w, h;
    if (big) {
      float tw = ui_text_width(c, UI_TYPE_LABEL, b->text);
      int lines = tw > maxw - chrome ? 2 : 1;
      w = fminf(maxw, tw + chrome);
      h = fmaxf(ui_s(c, 28), lines * lf.line) + ui_s(c, 18);
    } else {
      w = h = ui_s(c, 30);
    }
    for (int tries = 0; tries < 8; tries++) {
      int hit = 0;
      for (int i = 0; i < np; i++)
        if (x - w / 2 < placed[i].x1 + gap2 && x + w / 2 > placed[i].x0 - gap2 && bottom - h < placed[i].y1 + gap2 && bottom > placed[i].y0 - gap2) {
          bottom = placed[i].y0 - gap2;
          hit = 1;
        }
      if (!hit) break;
    }
    placed[np].x0 = x - w / 2;
    placed[np].x1 = x + w / 2;
    placed[np].y0 = bottom - h;
    placed[np].y1 = bottom;
    np++;
    float rise = o->still ? 0 : (1 - al) * ui_s(c, 4);   // a little lift as it fades in and out
    float aa = al * (0.6f + 0.4f * zk);
    SDL_Texture *mt = mood >= 0 && GW_ART_MOOD + mood < GW_ART_MAX ? art[GW_ART_MOOD + mood] : NULL;
    if (big) draw_bubble_full(c, x, bottom - rise, tex, mt, mood, b->text, aa, b->kind == 2);
    else draw_bubble_icon(c, x, bottom - rise, tex, mt, mood, aa);
  }
  for (int i = 0; i < GW_SLOT_ALL; i++)
    if (!g_bub[i].live) g_bub[i].h = 0;
  ui_pop_scale(c);
}

// ------------------------------------------------------------------ the card

static void tone_text(UiCtx *c, UiRect r, const char *s, int tone) {
  const UiTokens *t = ui_tokens(c);
  ui_text_in(c, r, UI_TYPE_NUMBER, tone_colour(t, tone), s, 0.5f);
}

static int draw_card(UiCtx *c, const GwNote *n, SDL_Texture *const *art, float vx, float vy, float k, float sx, float sy,
                     GwAction *acts, int max_acts) {
  const GwCard *g = &n->card;
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  int na = 0;
  UiRect scr = ui_screen(c);
  float w = ui_s(c, t->popover_w);
  float pad = ui_s(c, 20);
  float in_w = w - 2 * pad;

  // measure the body first (the panel is as tall as its content)
  UiFont tf = ui_font(c, UI_TYPE_TITLE), cf = ui_font(c, UI_TYPE_CAPTION), bf = ui_font(c, UI_TYPE_BODY),
         lf = ui_font(c, UI_TYPE_LABEL);
  float portrait = ui_s(c, 76), head_gap = ui_s(c, 14);
  float head_w = in_w - portrait - head_gap;
  char l2[3][160];
  int n_l2 = g->line2[0] ? wrap_lines(c, UI_TYPE_CAPTION, g->line2, head_w, l2, 3) : 0;
  float head_h = fmaxf(portrait, tf.line + cf.line + n_l2 * cf.line + ui_s(c, 6));

  int story = g->layout != 0;
  char q[4][160];
  int n_q = g->quote[0] && !story ? wrap_lines(c, UI_TYPE_BODY, g->quote, in_w - ui_s(c, 28), q, 4) : 0;
  float quote_h = n_q ? n_q * bf.line + ui_s(c, 24) : 0;
  char wl[2][160];
  int n_w = g->skills[0] && !story ? wrap_lines(c, UI_TYPE_CAPTION, g->skills, in_w - ui_s(c, 28) - ui_text_width(c, UI_TYPE_CAPTION, "Has  "), wl, 2) : 0;
  float wants_h = n_w ? n_w * cf.line + ui_s(c, 10) : 0;
  char club[3][160];
  int n_club = g->club_story[0] ? wrap_lines(c, UI_TYPE_CAPTION, g->club_story, in_w, club, 3) : 0;
  float club_h = n_club ? n_club * cf.line + ui_s(c, 12) + ui_s(c, t->pill_h) + ui_s(c, 8) : 0;

  float meters_h = story ? 0 : GW_METERS * ui_s(c, 24);
  float card_rows_h = 0;
  int rows = 0;
  if (!story && g->has_scores) {
    rows = g->cur_hole > 9 ? 2 : 1;   // the back nine only once it is being played
    for (int i = 9; i < GW_HOLES; i++)
      if (g->score[i] > 0 || (g->has_opponent && (g->opponent_score[i] > 0 || g->opponent_hole == i + 1))) rows = 2;
    card_rows_h = rows * (cf.line + bf.line * (g->has_opponent ? 2 : 1) + ui_s(c, 6)) + ui_s(c, 6);
    if (g->has_opponent) card_rows_h += cf.line + ui_s(c, 6);
  }
  char sl[10][160];
  int8_t sl_tone[10];
  int n_sl = 0;
  float story_h = 0;
  {
    // golf's comment lines: the story page lists them all, the card its newest few under the scorecard
    int most = story ? 5 : 3;
    for (int i = 0; i < g->n_story && i < most; i++) {
      char two[2][160];
      int m = wrap_lines(c, story ? UI_TYPE_BODY : UI_TYPE_CAPTION, g->story[i], story ? in_w : 4000, two, story ? 2 : 1);
      for (int j = 0; j < m && n_sl < 10; j++) {
        sl_tone[n_sl] = g->story_tone[i];
        snprintf(sl[n_sl++], 160, "%s", two[j]);
      }
    }
    story_h = n_sl ? n_sl * ((story ? bf.line : cf.line) + ui_s(c, 4)) + ui_s(c, 8) : 0;
  }
  float btn_h = ui_s(c, t->pill_h);
  float h = pad + head_h + ui_s(c, 14) + quote_h + (quote_h ? ui_s(c, 8) : 0) + wants_h + (wants_h ? ui_s(c, 10) : 0) + meters_h +
            (meters_h ? ui_s(c, 6) : 0) + card_rows_h + story_h + club_h + btn_h + pad;

  // beside the golfer, on the side with room; the arrow points at the body
  float body = (n->zoom > 0 ? n->zoom : 4) * 5.0f;   // canvas px from the feet to the middle of the golfer
  float ax = vx + (g->ax * g_zs + g_zx + sx) * k, ay = vy + (g->ay * g_zs + g_zy + sy) * k - body * k;
  if (g->ax < 0) {
    ax = scr.w / 2;
    ay = scr.h / 2;
  }
  float gap = ui_s(c, t->popover_arrow);
  // clear of the golfer's own tag (the widest thing at its feet) and the ring under it
  float clear = 22 * k;
  for (int i = 0; i < n->n_tags; i++)
    if (n->tags[i].selected) clear = fmaxf(clear, ui_text_width(c, UI_TYPE_CAPTION, n->tags[i].name) / 2 + ui_s(c, 14));
  // (the camera buttons sit at the right edge: the card keeps clear of them)
  int side = (ax + clear + gap + w + ui_s(c, t->edge_margin) < scr.w - ui_s(c, 100)) ? 1 : -1;
  if (g->ax >= 0) ax += side * clear;
  float edge = ui_s(c, 24);
  if (h > scr.h - 2 * edge) h = scr.h - 2 * edge;
  UiRect r = ui_popover(c, ax, ay, w, h, side);
  memset(&g_layout, 0, sizeof g_layout);
  g_layout.card = r;

  float x = r.x + pad, y = r.y + pad;
  // header: portrait, name, status, traits
  UiRect pr = ui_rect(x, y, portrait, portrait);
  uid_rrect(d, pr, portrait / 2, t->surface_inset);
  uid_rrect_stroke(d, pr, portrait / 2, 1, t->surface_inset_border);
  SDL_Texture *ptex = art[GW_ART_PORTRAIT];
  if (ptex) {
    int tw = 0, th = 0;
    SDL_QueryTexture(ptex, NULL, NULL, &tw, &th);
    float s = fminf(portrait * 0.96f / tw, portrait * 0.96f / th);
    // Direct source portraits can exceed the card. Fit the entire face.
    float iw = tw * s, ih = th * s;
    UiRect clip = pr;
    uid_set_clip(d, &clip);
    uid_image(d, ptex, NULL, ui_rect(pr.x + (portrait - iw) / 2, pr.y + (portrait - ih) / 2 + ui_s(c, 4), iw, ih), 0,
              (UiColor){255, 255, 255, 255});
    uid_set_clip(d, NULL);
  } else {
    ui_icon(c, UI_ICON_USERS, pr, portrait * 0.5f, t->icon_muted);
  }
  {   // the golfer's mood as a face on the portrait's corner (golf's mood strip face, redrawn)
    int mood = 5, found = 0;
    for (int i = 0; i < n->n_tags && !found; i++)
      if (n->tags[i].slot == g->slot) mood = n->tags[i].mood + 2, found = 1;
    if (found) {
      mood = mood < 1 ? 1 : mood > 10 ? 10 : mood;
      float fs = ui_s(c, 28);
      UiRect fr = ui_rect(pr.x + portrait - fs * 0.85f, pr.y + portrait - fs * 0.85f, fs, fs);
      uid_rrect_shadow(d, fr, fs / 2, 0, ui_s(c, 2), ui_s(c, 6), (UiColor){10, 40, 30, 70});
      ui_face(c, mood, fr, fs, 1.0f);
    }
  }
  float hx = x + portrait + head_gap;
  float close = ui_s(c, t->icon_button_sm);
  UiRect cr = ui_rect(r.x + r.w - pad - close + ui_s(c, 6), r.y + ui_s(c, 10), close, close);
  g_layout.close = cr;
  if (ui_icon_button(c, ui_id("gw.close"), cr, UI_ICON_X, UI_COMPACT, "Close") && na < max_acts) acts[na++] = (GwAction){GW_ACT_CLOSE, 4};
  ui_text_fit(c, ui_rect(hx, y, head_w - close * 0.6f, tf.line), UI_TYPE_TITLE, t->text_primary, g->name, 0);
  ui_text_fit(c, ui_rect(hx, y + tf.line, head_w, cf.line), UI_TYPE_CAPTION, t->text_secondary, g->line1, 0);
  for (int i = 0; i < n_l2; i++)
    ui_text_in(c, ui_rect(hx, y + tf.line + cf.line + ui_s(c, 4) + i * cf.line, head_w, cf.line), UI_TYPE_CAPTION,
               t->text_tertiary, l2[i], 0);
  y += head_h + ui_s(c, 14);

  if (n_q) {
    UiRect qb = ui_rect(x, y, in_w, quote_h);
    uid_rrect(d, qb, ui_s(c, 18), t->surface_inset);
    uid_rrect_stroke(d, qb, ui_s(c, 18), 1, t->surface_inset_border);
    for (int i = 0; i < n_q; i++)
      ui_text_in(c, ui_rect(qb.x + ui_s(c, 14), qb.y + ui_s(c, 12) + i * bf.line, qb.w - ui_s(c, 28), bf.line), UI_TYPE_QUOTE,
                 t->text_primary, q[i], 0);
    y += quote_h + ui_s(c, 8);
  }
  if (n_w) {
    float lx = x + ui_s(c, 14);
    float lw = ui_text_width(c, UI_TYPE_CAPTION, "Has  ");
    ui_text_in(c, ui_rect(lx, y, lw, cf.line), UI_TYPE_CAPTION, t->accent_soft_text, "Has", 0);
    for (int i = 0; i < n_w; i++)
      ui_text_in(c, ui_rect(lx + lw, y + i * cf.line, in_w - ui_s(c, 28) - lw, cf.line), UI_TYPE_CAPTION, t->text_secondary, wl[i], 0);
    y += wants_h + ui_s(c, 10);
  }
  if (n_club) {
    for (int i = 0; i < n_club; i++)
      ui_text_in(c, ui_rect(x, y + i * cf.line, in_w, cf.line), UI_TYPE_CAPTION, t->accent_soft_text, club[i], 0);
    y += n_club * cf.line + ui_s(c, 6);
    const char *label = g->layout == 2 ? "Back to golfer" : "Club diary";
    UiRect diary = ui_rect(x, y, ui_text_width(c, UI_TYPE_LABEL, label) + ui_s(c, 40), ui_s(c, t->pill_h));
    g_layout.diary = diary;
    if (ui_button(c, ui_id("club.diary"), diary, label, UI_ICON_FLAG, UI_BTN_PILL, 0) && na < max_acts)
      acts[na++] = (GwAction){GW_ACT_DIARY, 0};
    y += ui_s(c, t->pill_h) + ui_s(c, 14);
  }
  if (!story) {
    for (int i = 0; i < GW_METERS; i++) {
      float rowh = ui_s(c, 24);
      ui_text_in(c, ui_rect(x, y, ui_s(c, 74), rowh), UI_TYPE_CAPTION, t->text_secondary, g->label[i], 0);
      ui_meter(c, ui_rect(x + ui_s(c, 80), y + (rowh - ui_s(c, t->meter_h)) / 2, in_w - ui_s(c, 80), ui_s(c, t->meter_h)),
               fmaxf(g->meter[i] / 1000.0f, 0.05f));   // an empty need still shows its warning stub
      y += rowh;
    }
    y += ui_s(c, 6);
    if (g->has_scores) {
      if (g->has_opponent) {
        char against[96]; snprintf(against, sizeof against, "Against %s", g->opponent_name);
        ui_text_fit(c, ui_rect(x, y, in_w, cf.line), UI_TYPE_CAPTION, t->accent_soft_text, against, 0);
        y += cf.line + ui_s(c, 6);
      }
      // a row of nine holes (two once the back nine is played); the total sits at the end of the last row
      float colw = in_w / (g->has_opponent ? 11.0f : 10.0f);
      float score_x = x + (g->has_opponent ? colw : 0);
      for (int row = 0; row < rows; row++) {
        if (g->has_opponent) {
          ui_text_fit(c, ui_rect(x, y + cf.line, colw, bf.line), UI_TYPE_MICRO, t->text_secondary, g->has_opponent == 1 ? "You" : "Rival", 0);
          ui_text_fit(c, ui_rect(x, y + cf.line + bf.line, colw, bf.line), UI_TYPE_MICRO, t->text_secondary, g->has_opponent == 1 ? "Rival" : "You", 0);
        }
        char hn[8];
        for (int col = 0; col < 9; col++) {
          int hole = row * 9 + col;
          UiRect cell = ui_rect(score_x + col * colw, y, colw, cf.line);
          snprintf(hn, sizeof hn, "%d", hole + 1);
          ui_text_in(c, cell, UI_TYPE_MICRO, g->cur_hole == hole + 1 ? t->accent : t->text_tertiary, hn, 0.5f);
          if (g->score[hole] > 0 || g->cur_hole == hole + 1) {
            char sn[8];
            snprintf(sn, sizeof sn, "%d", g->score[hole]);
            UiRect sc = ui_rect(score_x + col * colw, y + cf.line, colw, bf.line);
            if (g->score[hole] > 0) tone_text(c, sc, sn, g->tone[hole]);
          }
          if (g->has_opponent && g->opponent_score[hole] > 0) {
            char sn[8]; snprintf(sn, sizeof sn, "%d", g->opponent_score[hole]);
            ui_text_in(c, ui_rect(score_x + col * colw, y + cf.line + bf.line, colw, bf.line),
                       UI_TYPE_NUMBER, g->opponent_hole == hole + 1 ? t->accent : t->text_primary, sn, .5f);
          }
        }
        if (row == rows - 1) {
          char tn[8];
          snprintf(tn, sizeof tn, "%d", g->total);
          ui_text_in(c, ui_rect(score_x + 9 * colw, y, colw, cf.line), UI_TYPE_MICRO, t->text_tertiary, "Total", 0.5f);
          ui_text_in(c, ui_rect(score_x + 9 * colw, y + cf.line, colw, bf.line), UI_TYPE_NUMBER, t->text_primary, tn, 0.5f);
          if (g->has_opponent) {
            snprintf(tn, sizeof tn, "%d", g->opponent_total);
            ui_text_in(c, ui_rect(score_x + 9 * colw, y + cf.line + bf.line, colw, bf.line), UI_TYPE_NUMBER, t->text_primary, tn, .5f);
          }
        }
        y += cf.line + bf.line * (g->has_opponent ? 2 : 1) + ui_s(c, 6);
      }
      y += ui_s(c, 6);
    }
    for (int i = 0; i < n_sl; i++) {
      UiColor col = sl_tone[i] > 0 ? t->accent : sl_tone[i] < 0 ? t->danger : t->text_secondary;
      ui_text_fit(c, ui_rect(x, y, in_w, cf.line), UI_TYPE_CAPTION, col, sl[i], 0);
      y += cf.line + ui_s(c, 4);
    }
    y += n_sl ? ui_s(c, 8) : 0;
  } else {
    for (int i = 0; i < n_sl; i++) {
      UiColor col = sl_tone[i] > 0 ? t->accent : sl_tone[i] < 0 ? t->danger : t->text_primary;
      ui_text_fit(c, ui_rect(x, y, in_w, bf.line), UI_TYPE_BODY, col, sl[i], 0);
      y += bf.line + ui_s(c, 4);
    }
    y += ui_s(c, 8);
  }

  // buttons: golf's own controls, in golf's own words
  struct { int id; UiIcon icon; int ghost; } B[4] = {{0, UI_ICON_USERS, 0}, {2, UI_ICON_CAMERA, 0}, {3, UI_ICON_FLAG, 0},
                                                      {1, UI_ICON_DOOR, 1}};
  float bx = x, by = r.y + r.h - pad - btn_h;
  (void)lf;
  for (int i = 0; i < 4; i++) {
    int id = B[i].id;
    if (!g->btn[id][0]) continue;
    if (id == 3 && !g->has_story) continue;
    const char *label = id == 0 ? "Profile" : id == 2 ? "Snapshot" : id == 3 ? (g->layout == 1 ? "Comments" : "Story") : "Eject";
    float bw = ui_text_width(c, UI_TYPE_LABEL, label) + ui_s(c, 44);
    UiRect b = ui_rect(bx, by, bw, btn_h);
    if (bx + bw > x + in_w) break;
    g_layout.btn[id] = b;
    if (ui_button(c, ui_id(label) ^ 0x4757u, b, label, B[i].icon, UI_BTN_PILL, 0) && na < max_acts)
      acts[na++] = (GwAction){GW_ACT_BUTTON, id};
    bx += bw + ui_s(c, 8);
  }
  return na;
}

// Course help and object prices use the same screen-resolution font as the UI.
// Keep their anchors with the course, but never shrink the font with world zoom.
static void draw_popovers(UiCtx *c, const GwNote *n, float vx, float vy, float k, float sx, float sy) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  UiRect screen = ui_screen(c);
  float edge = ui_s(c, 8), pad = ui_s(c, 6);
  int used[GW_POPOVERS] = {0};
  for (int i = 0; i < n->n_popovers && i < GW_POPOVERS; ++i) {
    if (used[i]) continue;
    used[i] = 1;
    const GwPopover *p = &n->popovers[i];
    int style = (p->fn & 7) == 4 ? UI_TYPE_BODY : UI_TYPE_CAPTION;
    UiFont font = ui_font(c, style);
    float maxw = fminf(ui_s(c, t->bubble_max_w), screen.w - 2 * edge - 2 * pad);
    if (maxw <= 0) continue;
    char lines[32][160] = {{0}};
    UiColor colours[32];
    int count = 0, members[GW_POPOVERS] = {i}, nmembers = 1, last_y = p->y;
    // GameTick emits the side cost panel between an item's name and its price.
    // Group by anchor and vertical adjacency, including those interleaved runs.
    for (;;) {
      int next = -1;
      for (int j = i + 1; j < n->n_popovers && j < GW_POPOVERS; ++j) {
        const GwPopover *q = &n->popovers[j];
        if (used[j] || q->fn != p->fn || q->x != p->x || q->y <= last_y || q->y - last_y > 24) continue;
        if (next < 0 || q->y < n->popovers[next].y) next = j;
      }
      if (next < 0) break;
      used[next] = 1;
      members[nmembers++] = next;
      last_y = n->popovers[next].y;
    }
    for (int member = 0; member < nmembers && count < 32; ++member) {
      const GwPopover *part = &n->popovers[members[member]];
      unsigned raw = part->colour & 0x7fff;
      int red = (raw >> 10) & 31, green = (raw >> 5) & 31;
      UiColor colour = red > green * 2 && red > 15 ? t->danger : t->text_primary;
      int added = wrap_lines(c, style, part->text, maxw, lines + count, 32 - count);
      for (int j = 0; j < added; ++j) colours[count + j] = colour;
      count += added;
    }
    float w = 0;
    for (int j = 0; j < count; ++j) w = fmaxf(w, ui_text_width(c, style, lines[j]));
    float x = vx + ((p->fn & 8) ? p->x : p->x * g_zs + g_zx + sx) * k;
    float y = vy + ((p->fn & 8) ? p->y : p->y * g_zs + g_zy + sy) * k;
    float align = (p->fn & 7) == 2 || (p->fn & 7) == 3 ? 0 : 0.5f;
    UiRect r = ui_rect(x - w * align - pad, y - pad, w + 2 * pad, count * font.line + 2 * pad);
    r.x = fmaxf(screen.x + edge, fminf(r.x, screen.x + screen.w - edge - r.w));
    r.y = fmaxf(screen.y + edge, fminf(r.y, screen.y + screen.h - edge - r.h));
    float radius = fminf(r.h / 2, ui_s(c, 14));
    uid_rrect_shadow(d, r, radius, 0, ui_s(c, 2), ui_s(c, 6), t->glass_shadow.color);
    uid_rrect_backdrop(d, r, radius);
    uid_rrect(d, r, radius, t->glass_fill_strong);
    uid_rrect_stroke(d, r, radius, hair_px(c), t->glass_border);
    for (int j = 0; j < count; ++j)
      ui_text_in(c, ui_rect(r.x + pad, r.y + pad + j * font.line, w, font.line), style, colours[j], lines[j], align);
  }
}

void ui_golfers_draw_world(UiCtx *c, const GwNote *n, SDL_Texture *const *art, float view_x, float view_y, float px,
                           float slide_x, float slide_y) {
  if (!n || n->magic != GW_NOTE_MAGIC) return;
  tick(n);
  draw_tags(c, n, view_x, view_y, px, slide_x, slide_y);
  draw_bubbles(c, n, art, view_x, view_y, px, slide_x, slide_y);
  draw_popovers(c, n, view_x, view_y, px, slide_x, slide_y);
}

int ui_golfers_draw_card(UiCtx *c, const GwNote *n, SDL_Texture *const *art, float view_x, float view_y, float px,
                         float slide_x, float slide_y, GwAction *acts, int max_acts) {
  if (!n || n->magic != GW_NOTE_MAGIC || !n->card.on) return 0;
  return draw_card(c, n, art, view_x, view_y, px, slide_x, slide_y, acts, max_acts);
}

int ui_golfers_draw(UiCtx *c, const GwNote *n, SDL_Texture *const *art, float view_x, float view_y, float px, float slide_x,
                    float slide_y, GwAction *acts, int max_acts) {
  ui_golfers_draw_world(c, n, art, view_x, view_y, px, slide_x, slide_y);
  return ui_golfers_draw_card(c, n, art, view_x, view_y, px, slide_x, slide_y, acts, max_acts);
}
