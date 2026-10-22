// Frosted Mint cart path help (task-53): see ui_paths.h.
#include "port/ui/ui_paths.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "port/ui/ui_draw.h"

static float pin_x(const PvMap *m, float x) {
  return m->view_x + ((m->zoom_on ? m->zoom_s * x + m->zoom_x : x + m->slide_x)) * m->px;
}
static float pin_y(const PvMap *m, float y) {
  return m->view_y + ((m->zoom_on ? m->zoom_s * y + m->zoom_y : y + m->slide_y)) * m->px;
}

static void quad(UiDraw *d, const PvMap *m, const PvQuad *q, UiColor col) {
  float x[4], y[4];
  for (int i = 0; i < 4; i++) {
    x[i] = pin_x(m, q->p[i * 2]);
    y[i] = pin_y(m, q->p[i * 2 + 1]);
  }
  uid_triangle(d, x[0], y[0], x[1], y[1], x[2], y[2], col);
  uid_triangle(d, x[0], y[0], x[2], y[2], x[3], y[3], col);
}

static void path_tint(UiDraw *d, const PvMap *m, const PvQuad *q, const PvOpts *o, UiColor col) {
  if (!o->subtle || q->path) quad(d, m, q, col);
}

void ui_paths_draw_view(UiCtx *c, const PvNote *n, const PvMap *m, UiRect view_rect, const PvOpts *o) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  float alpha = o->opacity;
  if (alpha < 0.2f) alpha = 0.2f;
  if (alpha > 1.5f) alpha = 1.5f;
  if (!o->subtle) {
    UiColor veil = {18, 30, 26, (uint8_t)fminf(255, 118 * alpha)};
    uid_rrect(d, view_rect, 0, veil);
  } else alpha *= o->tint;
  UiColor amber = {255, 178, 66, (uint8_t)fminf(255, 130 * alpha)};
  for (int i = 0; i < n->n_off && n->n_net + i < PV_QUADS; i++) path_tint(d, m, &n->q[n->n_net + i], o, amber);
  UiColor mint = t->accent;
  mint.a = (uint8_t)fminf(255, 118 * alpha);
  for (int i = 0; i < n->n_net && i < PV_QUADS; i++) path_tint(d, m, &n->q[i], o, mint);
}

// ------------------------------------------------------------------ markers

static const char k_marker[] = "No path";

typedef struct { float cx, cy, w, h; } Mark;

static Mark marker_of(UiCtx *c, const PvNote *n, const PvMap *m, const PvBuilding *b) {
  float h = ui_s(c, 24), pad = ui_s(c, 9), ic = ui_s(c, 16);
  float w = pad + ic + ui_s(c, 4) + ui_text_width(c, UI_TYPE_CAPTION, k_marker) + pad;
  float lift = (5.0f + 1.6f * (float)n->zoom) * m->px;   // above the ground point of the building, a little more when zoomed in
  Mark k = {pin_x(m, b->x), pin_y(m, b->y) - lift - h / 2, w, h};
  if (m->zoom_on) k.cy -= (m->zoom_s - 1.0f) * 4 * m->px;
  return k;
}

void ui_paths_draw_markers(UiCtx *c, const PvNote *n, const PvMap *m, const PvOpts *o, int hover_idx) {
  if (!o->markers || n->not_needed) return;
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  for (int i = 0; i < n->n_bld && i < PV_BUILDINGS; i++) {
    const PvBuilding *b = &n->bld[i];
    if (b->connected) continue;
    Mark k = marker_of(c, n, m, b);
    UiRect r = ui_rect_round(ui_rect(k.cx - k.w / 2, k.cy - k.h / 2, k.w, k.h));
    int hov = i == hover_idx;
    UiColor fill = t->glass_fill_strong;
    fill.a = (uint8_t)fminf(255, fill.a * (o->opacity > 0 ? o->opacity : 1) + (hov ? 20 : 0));
    // the stalk down to the building
    uid_triangle(d, k.cx - ui_s(c, 4), r.y + r.h - 1, k.cx + ui_s(c, 4), r.y + r.h - 1, k.cx, r.y + r.h + ui_s(c, 5), fill);
    uid_rrect_shadow(d, r, r.h / 2, 0, ui_s(c, 2), ui_s(c, 6), (UiColor){0, 0, 0, 70});
    uid_rrect(d, r, r.h / 2, fill);
    uid_rrect_stroke(d, r, r.h / 2, fmaxf(1.0f, ui_s(c, 1.2f)), uid_alpha(t->danger, hov ? 1.0f : 0.8f));
    float ic = ui_s(c, 16);
    ui_icon(c, UI_ICON_PATH, ui_rect(r.x + ui_s(c, 9), r.y + (r.h - ic) / 2, ic, ic), ic, t->danger);
    float tx = r.x + ui_s(c, 9) + ic + ui_s(c, 4);
    ui_text_in(c, ui_rect(tx, r.y, r.x + r.w - tx, r.h), UI_TYPE_CAPTION, t->text_primary, k_marker, 0);
  }
}

int ui_paths_marker_at(UiCtx *c, const PvNote *n, const PvMap *m, float px, float py) {
  if (n->not_needed) return -1;
  int best = -1;
  for (int i = 0; i < n->n_bld && i < PV_BUILDINGS; i++) {
    const PvBuilding *b = &n->bld[i];
    if (b->connected) continue;
    Mark k = marker_of(c, n, m, b);
    float pad = ui_s(c, 4);
    if (px >= k.cx - k.w / 2 - pad && px <= k.cx + k.w / 2 + pad && py >= k.cy - k.h / 2 - pad && py <= k.cy + k.h / 2 + k.h) best = i;
  }
  return best;
}

void ui_paths_draw_card(UiCtx *c, const PvNote *n, const PvMap *m, int i, const char *key) {
  if (i < 0 || i >= n->n_bld || i >= PV_BUILDINGS) return;
  const PvBuilding *b = &n->bld[i];
  const UiTokens *t = ui_tokens(c);
  Mark k = marker_of(c, n, m, b);
  char title[64], last[64];
  snprintf(title, sizeof title, "%s has no cart path", b->name[0] ? b->name : "This building");
  if (key && key[0]) snprintf(last, sizeof last, "Press %s to see which paths are joined.", key);
  else snprintf(last, sizeof last, "The path view shows which paths are joined.");
  const char *lines[] = {
      "No cart path joins it to the clubhouse, so it stays",
      "closed and golfers do not use it. Lay path tiles from",
      "the clubhouse (or a joined path) to the building, edge",
      "to edge. A corner, a fairway or a green does not join.",
      last,
  };
  const int nl = (int)(sizeof lines / sizeof lines[0]);
  float pad = ui_s(c, 14), lh = ui_s(c, 18), th = ui_s(c, 24);
  float w = ui_text_width(c, UI_TYPE_HEADING, title);
  for (int j = 0; j < nl; j++) w = fmaxf(w, ui_text_width(c, UI_TYPE_CAPTION, lines[j]));
  w += 2 * pad;
  float h = pad + th + ui_s(c, 4) + nl * lh + pad;
  UiRect scr = ui_screen(c);
  float x = k.cx - w / 2, y = k.cy - k.h / 2 - ui_s(c, 12) - h;
  if (y < ui_s(c, 8)) y = k.cy + k.h / 2 + ui_s(c, 22);
  if (x < ui_s(c, 8)) x = ui_s(c, 8);
  if (x + w > scr.w - ui_s(c, 8)) x = scr.w - ui_s(c, 8) - w;
  UiRect r = ui_rect_round(ui_rect(x, y, w, h));
  ui_panel(c, r, ui_s(c, 16), UI_PANEL_STRONG | UI_PANEL_NOBLOCK);
  ui_text_in(c, ui_rect(r.x + pad, r.y + pad, w - 2 * pad, th), UI_TYPE_HEADING, t->text_primary, title, 0);
  for (int j = 0; j < nl; j++)
    ui_text_in(c, ui_rect(r.x + pad, r.y + pad + th + ui_s(c, 4) + (float)j * lh, w - 2 * pad, lh), UI_TYPE_CAPTION, t->text_secondary, lines[j], 0);
}

void ui_paths_draw_legend(UiCtx *c, const PvNote *n, int forced) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  char text[160];
  snprintf(text, sizeof text, "%s", n->not_needed ? "Green paths join the clubhouse, orange ones do not (paths are not needed on this difficulty)"
                                                  : "Green paths join the clubhouse, orange ones do not");
  const char *mode = forced ? "Shown automatically while building"
                            : "Kept on. Use the path-view control to return to automatic.";
  float pad = ui_s(c, 16), line = ui_s(c, 34), h = ui_s(c, 54), sw = ui_s(c, 12), gap = ui_s(c, 8);
  float text_w = fmaxf(ui_text_width(c, UI_TYPE_LABEL, text), ui_text_width(c, UI_TYPE_CAPTION, mode));
  float w = pad + sw + gap + text_w + pad + ui_s(c, 6);
  UiRect scr = ui_screen(c);
  UiRect r = ui_rect_round(ui_rect((scr.w - w) / 2, ui_s(c, 74), w, h));
  ui_panel(c, r, h / 2, UI_PANEL_STRONG | UI_PANEL_NOBLOCK);
  UiRect s = ui_rect_round(ui_rect(r.x + pad, r.y + (line - sw) / 2, sw, sw));
  uid_rrect(d, s, ui_s(c, 3), t->accent);
  ui_text_in(c, ui_rect(s.x + sw + gap, r.y, r.w - pad - sw - gap, line), UI_TYPE_LABEL, t->text_primary, text, 0);
  ui_text_in(c, ui_rect(s.x + sw + gap, r.y + line, r.w - pad - sw - gap, h - line), UI_TYPE_CAPTION, t->text_secondary, mode, 0);
}
