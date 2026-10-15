// Frosted Mint reports panel (simgolf mod, task-28, UI-8): one centred glass panel with the report's
// title, a tab per report and a close button, and the report's own text re-set in the kit's type at the
// places golf put it (a table: rows striped, the header row quiet, negatives in the danger colour). A pure
// function of an RpSnap; reports_frosted.c calls it on the render thread, the gallery with a made-up one.
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "port/reports_frosted.h"
#include "port/ui/ui_draw.h"

static const char *const g_tab_label[] = {"Course", "Comments", "Histograph", "SGA", "Finances", "Roster", "Best scores"};
#define RP_TAB_KIND_VALUE(kind) kind,
static const int g_tab_kind[] = {RP_REPORT_TAB_TARGETS(RP_TAB_KIND_VALUE)};
#undef RP_TAB_KIND_VALUE
enum { TABS = (int)(sizeof g_tab_kind / sizeof g_tab_kind[0]) };

static const char *default_title(int kind) {
  switch (kind) {
    case RP_COURSE: return "Course Report";
    case RP_COMMENTS: return "Player Comments";
    case RP_HISTO: return "Histograph";
    case RP_KEYS: return "Keyboard Shortcuts";
    case RP_FINANCE: return "Financial Report";
    case RP_SGA: return "SGA Evaluation";
    case RP_ROSTER: return "Membership Roster";
    case RP_BEST: return "Best Scores";
    case RP_MONTH: return "End of Month";
    case RP_YEAR: return "End of Year";
    case RP_HOLE: return "Hole Stats";
    case RP_TOURNEY: return "Tournament Results";
    default: return "Report";
  }
}

// About how wide golf drew a string, in page pixels (its body font is about 6 px a character).
static float page_width(const RpItem *it) { return 6.0f * (float)strlen(it->s); }

void ui_reports_bounds(const RpSnap *s, float *x0, float *y0, float *x1, float *y1) {
  float ax = 1e9f, ay = 1e9f, bx = -1e9f, by = -1e9f;
  for (int i = 0; i < s->n; i++) {
    const RpItem *it = &s->it[i];
    if (it->type == RP_IT_LINE) {
      ax = fminf(ax, fminf(it->x, it->w)), bx = fmaxf(bx, fmaxf(it->x, it->w));
      ay = fminf(ay, fminf(it->y, it->h)), by = fmaxf(by, fmaxf(it->y, it->h));
      continue;
    }
    if (it->type != RP_IT_TEXT) {
      ax = fminf(ax, it->x), bx = fmaxf(bx, it->x + it->w);
      ay = fminf(ay, it->y), by = fmaxf(by, it->y + it->h);
      continue;
    }
    if (!it->s[0]) continue;
    float w = page_width(it), l = it->x, r = it->x + w;
    if (it->align == RP_AL_CENTRE) l = it->x - w / 2, r = it->x + w / 2;
    else if (it->align == RP_AL_RIGHT) l = it->x - w, r = it->x;
    ax = fminf(ax, l), bx = fmaxf(bx, r);
    ay = fminf(ay, it->y), by = fmaxf(by, it->y + 14);
  }
  if (bx < ax) ax = 0, bx = 800, ay = 0, by = 600;
  *x0 = ax, *y0 = ay, *x1 = bx, *y1 = by;
}

static int same_text(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    char x = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a, y = *b >= 'a' && *b <= 'z' ? (char)(*b - 32) : *b;
    if (x != y) return 0;
  }
  return *a == *b;
}

static int numeric(const char *s) {
  int digits = 0;
  for (; *s; s++) {
    if (*s >= '0' && *s <= '9') digits++;
    else if (!strchr(" $-+.,%()/:", *s)) return 0;
  }
  return digits > 0;
}

// golf's 565 colours: black and dark text stay the primary text colour, reds become danger, greens accent
static UiColor text_colour(const UiTokens *t, uint16_t c) {
  int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  r = r * 255 / 31, g = g * 255 / 63, b = b * 255 / 31;
  if (r > 90 && r > g + 40 && r > b + 40) return t->danger;
  if (g > 90 && g > r + 40 && g > b + 40) return t->accent_soft_text;
  return t->text_primary;
}

static UiColor rgb565(uint16_t c, float lift) {
  float r = ((c >> 11) & 31) / 31.0f, g = ((c >> 5) & 63) / 63.0f, b = (c & 31) / 31.0f;
  float m = fmaxf(r, fmaxf(g, b));
  float k = m > 0.01f && m * lift > 1 ? 1 / m : lift;   // golf's dark curve colours, brightened for glass
  return (UiColor){(uint8_t)fminf(255, r * k * 255), (uint8_t)fminf(255, g * k * 255), (uint8_t)fminf(255, b * k * 255), 255};
}

// golf's money sign is its font's character 0xA7; the kit's font has a real dollar sign
static void fix_text(char *dst, int cap, const char *src) {
  int n = 0;
  for (; *src && n < cap - 1; src++) dst[n++] = (unsigned char)*src == 0xA7 ? '$' : *src;
  dst[n] = 0;
}

// "PLAYER COMMENTS REPORT" -> "Player Comments Report"; words with lower case letters stay as they are
static void title_case(char *dst, int cap, const char *src) {
  fix_text(dst, cap, src);
  for (char *w = dst; *w;) {
    while (*w == ' ') w++;
    char *e = w;
    int lower = 0, len = 0;
    while (*e && *e != ' ') lower |= (*e >= 'a' && *e <= 'z'), e++, len++;
    if (!lower && len > 1 && strncmp(w, "SGA", 3) != 0)
      for (char *q = w + 1; q < e; q++)
        if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);
    if (w == dst && *w >= 'a' && *w <= 'z') *w = (char)(*w - 32);
    w = e;
  }
}

#define ROWS 96

static RpLayout g_layout;
void ui_reports_layout(RpLayout *out) { *out = g_layout; }

int ui_pages_draw(UiCtx *c, const RpSnap *s, SDL_Texture *const *art, RpAction *acts, int max);   // ui_pages.c (task-73)

int ui_reports_draw(UiCtx *c, const RpSnap *s, SDL_Texture *const *art, RpAction *acts, int max) {
  if (s->kind >= RP_PROFILE) return ui_pages_draw(c, s, art, acts, max);
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  int na = 0;
  UiRect scr = ui_screen(c);

  // scrim: a click beside the panel closes the report, as a click anywhere did in the original
  uid_rrect(d, scr, 0, t->scrim);
  uint32_t scrim_id = ui_id("reports.scrim");
  ui_hit(c, scr, 0, scrim_id, UI_HIT_BLOCK | UI_HIT_MODAL | UI_HIT_INTERACT);

  float x0, y0, x1, y1;
  ui_reports_bounds(s, &x0, &y0, &x1, &y1);
  float bw = fmaxf(x1 - x0, 100), bh = fmaxf(y1 - y0, 40);

  float margin = ui_s(c, t->edge_margin), pad = ui_s(c, t->pad_dialog);
  float W = fminf(ui_s(c, 1400), scr.w - 2 * margin);
  float cw = W - 2 * pad;
  float kx = cw / bw, ky = kx * 0.9f;
  UiFont tf = ui_font(c, UI_TYPE_TITLE);
  float head = tf.line + ui_s(c, 20);
  float maxh = scr.h - 2 * margin - head - 2 * pad;
  if (bh * ky > maxh) ky = maxh / bh;
  float H = head + bh * ky + 2 * pad + ui_s(c, 8);
  UiRect pr = ui_rect_round(ui_center(scr, W, H));
  ui_panel(c, pr, ui_s(c, t->radius_panel), UI_PANEL_STRONG);
  UiRect in = ui_inset_all(pr, pad);

  // title row: title, tabs, close
  char title[80];
  title_case(title, sizeof title, s->title[0] ? s->title : default_title(s->kind));
  float cb = ui_s(c, 40);
  UiRect close_r = ui_rect(in.x + in.w - cb, in.y + (tf.line - cb) / 2, cb, cb);
  if (ui_icon_button(c, ui_id("reports.close"), close_r, UI_ICON_X, 0, "Close (Esc)") && na < max)
    acts[na++] = (RpAction){RP_A_CLOSE, 0};
  float segw = ui_segmented_width(c, g_tab_label, NULL, TABS);
  float tw = ui_text_width(c, UI_TYPE_TITLE, title);
  float seg_x = in.x + in.w - cb - ui_s(c, 12) - segw;
  int sel = -1;
  for (int i = 0; i < TABS; i++)
    if (g_tab_kind[i] == s->kind) sel = i;
  g_layout.panel = pr;
  g_layout.close_x = close_r.x + close_r.w / 2, g_layout.close_y = close_r.y + close_r.h / 2;
  g_layout.ntabs = 0;
  if (seg_x > in.x + tw + ui_s(c, 16)) {
    UiRect sr = ui_rect(seg_x, in.y + (tf.line - ui_s(c, 38)) / 2, segw, ui_s(c, 38));
    float x = sr.x + ui_s(c, 3);
    for (int i = 0; i < TABS; i++) {
      float w1 = ui_segmented_width(c, &g_tab_label[i], NULL, 1) - ui_s(c, 6);
      g_layout.tab_x[i] = x + w1 / 2;
      x += w1 + ui_s(c, 2);
    }
    g_layout.ntabs = TABS;
    g_layout.tab_y = sr.y + sr.h / 2;
    int was = sel;
    if (ui_segmented(c, ui_id("reports.tabs"), sr, g_tab_label, NULL, TABS, &sel) && sel != was && sel >= 0 && na < max)
      acts[na++] = (RpAction){RP_A_TAB, g_tab_kind[sel]};
  }
  ui_text_fit(c, ui_rect(in.x, in.y, fmaxf(seg_x - in.x - ui_s(c, 16), tw * 0.5f), tf.line), UI_TYPE_TITLE,
              t->text_primary, title, 0);

  // body: golf's text at its places
  UiRect body = ui_rect(in.x, in.y + head, in.w, bh * ky + ui_s(c, 8));
  float cx0 = body.x, cy0 = body.y + ui_s(c, 4);
  float row_h = fmaxf(ui_font(c, UI_TYPE_BODY).line, 14 * ky);

  // rows: distinct y values; rows with three or more strings are table rows
  int ry[ROWS], rn[ROWS], nr = 0;
  for (int i = 0; i < s->n; i++) {
    const RpItem *it = &s->it[i];
    if (it->type != RP_IT_TEXT || !it->s[0]) continue;
    int j = 0;
    while (j < nr && abs(ry[j] - it->y) > 3) j++;
    if (j == nr) {
      if (nr == ROWS) continue;
      ry[nr] = it->y, rn[nr] = 0, nr++;
    }
    rn[j]++;
  }
  for (int a = 1; a < nr; a++)   // sort by y
    for (int b = a; b > 0 && ry[b - 1] > ry[b]; b--) {
      int ty = ry[b], tn = rn[b];
      ry[b] = ry[b - 1], rn[b] = rn[b - 1], ry[b - 1] = ty, rn[b - 1] = tn;
    }
  // the header: the first table row when a wider gap than the rows' own spacing follows it
  int tr[ROWS], nt = 0;
  for (int j = 0; j < nr; j++)
    if (rn[j] >= 3) tr[nt++] = j;
  int header_row = -1, stripe = 0;
  if (nt >= 3) {
    int gap0 = ry[tr[1]] - ry[tr[0]], pitch = ry[tr[2]] - ry[tr[1]];
    for (int k = 3; k < nt; k++) pitch = abs(ry[tr[k]] - ry[tr[k - 1]]) < pitch ? abs(ry[tr[k]] - ry[tr[k - 1]]) : pitch;
    if (gap0 * 4 > pitch * 5) header_row = tr[0];
  }
  for (int k = 0; k < nt; k++) {
    int j = tr[k];
    float y = cy0 + (ry[j] - y0) * ky + (14 * ky - row_h) / 2;
    UiRect band = ui_rect(body.x - ui_s(c, 8), y - ui_s(c, 3), body.w + ui_s(c, 16), row_h + ui_s(c, 6));
    if (j == header_row) uid_rrect(d, band, ui_s(c, 10), t->surface_inset);
    else if (stripe++ & 1) uid_rrect(d, band, ui_s(c, 10), uid_alpha(t->surface_inset, 0.45f));
  }

  // golf's shapes: cells, boxes, curves
  for (int i = 0; i < s->n; i++) {
    const RpItem *it = &s->it[i];
    float x = cx0 + (it->x - x0) * kx, y = cy0 + (it->y - y0) * ky;
    if (it->type == RP_IT_CHIP && it->big) {   // golf's flat highlight box: a soft tinted chip
      UiRect r = ui_rect(x - ui_s(c, 2), y + ui_s(c, 1), it->w * kx + ui_s(c, 4), it->h * ky - ui_s(c, 2));
      UiColor tc = rgb565(it->col, 1), tint = uid_alpha(tc, 0.3f);
      if (tc.r > tc.g + 60) tint = uid_alpha(t->danger, 0.24f);
      else if (tc.g > tc.r + 60) tint = uid_alpha(t->accent, 0.24f);
      uid_rrect(d, r, ui_s(c, 8), tint);
    } else if (it->type == RP_IT_CHIP) {   // golf's own pictogram, kept to its aspect
      SDL_Texture *tx = art && it->art ? art[it->art - 1] : NULL;
      if (!tx) continue;
      float k = fminf(kx, ky), w = it->w * k, h = it->h * k;
      UiRect r = ui_rect(x + (it->w * kx - w) / 2, y + (it->h * ky - h) / 2, w, h);
      uid_image(d, tx, NULL, r, 0, (UiColor){255, 255, 255, 255});
    } else if (it->type == RP_IT_RECT) {
      UiRect r = ui_rect(x, y, fmaxf(it->w * kx, ui_s(c, 3)), fmaxf(it->h * ky, ui_s(c, 3)));
      float rad = fminf(r.w, r.h) / 2;
      if (it->w <= 3 && it->h <= 3) {   // a dot on a curve: a round marker
        float dd = ui_s(c, 7);
        r = ui_rect(x + it->w * kx / 2 - dd / 2, y + it->h * ky / 2 - dd / 2, dd, dd), rad = dd / 2;
      }
      uid_rrect(d, r, rad, rgb565(it->col, 1.6f));
    } else if (it->type == RP_IT_LINE) {
      float x1 = cx0 + (it->w - x0) * kx, y1 = cy0 + (it->h - y0) * ky;
      uid_line(d, x, y, x1, y1, ui_s(c, 2.5f), rgb565(it->col, 1.6f));
    }
  }

  for (int i = 0; i < s->n; i++) {
    const RpItem *it = &s->it[i];
    if (it->type != RP_IT_TEXT || !it->s[0]) continue;
    char txt[RP_TEXT];
    fix_text(txt, sizeof txt, it->s);
    if (s->title[0] && same_text(it->s, s->title)) continue;   // the board repeats its title: the panel has it
    int hdr = header_row >= 0 && abs(it->y - ry[header_row]) <= 3;
    int style = hdr ? UI_TYPE_LABEL : UI_TYPE_BODY;
    UiColor col = hdr ? t->text_secondary : text_colour(t, it->col);
    float w = ui_text_width(c, style, txt);
    float x = cx0 + (it->x - x0) * kx;
    float y = cy0 + (it->y - y0) * ky + (14 * ky - row_h) / 2;
    UiRect r = ui_rect(x, y, w + 2, row_h);
    float al = 0;
    if (it->align == RP_AL_CENTRE) r.x = x - w / 2 - 1, al = 0.5f;
    else if (it->align == RP_AL_RIGHT) r.x = x - w - 2, al = 1;
    ui_text_in(c, r, style, col, txt, al);
  }

  // the roster's own controls (scroll arrows, sort) take clicks at their place in golf's page
  if (s->kind == RP_ROSTER) {
    uint32_t bid = ui_id("reports.body");
    ui_hit(c, body, 0, bid, UI_HIT_BLOCK | UI_HIT_INTERACT | UI_HIT_WHEEL);
    float px, py;
    if (ui_clicked(c, bid) && ui_pointer(c, &px, &py) && na < max) {
      int ox = (int)lroundf(x0 + (px - cx0) / kx), oy = (int)lroundf(y0 + (py - cy0) / ky);
      acts[na++] = (RpAction){RP_A_CLICK, (ox & 0xffff) | (oy << 16)};
    }
    // task-94: golf's scroll hot spots (0x2ff, 0x50..0x74 up, 0x2ff, 0x1ea..0x20e down) as visible arrows, and the wheel
    static const int spot_y[2] = {0x62, 0x1fc};
    for (int k = 0; k < 2; k++) {
      float bs = ui_s(c, 34);
      float ax = cx0 + (0x2ff + 8 - x0) * kx - bs / 2, ay = cy0 + (spot_y[k] - y0) * ky - bs / 2;
      ax = fminf(fmaxf(ax, body.x), body.x + body.w - bs);
      ay = fminf(fmaxf(ay, body.y), body.y + body.h - bs);
      UiRect ar = ui_rect(ax, ay, bs, bs);
      if (ui_icon_button(c, ui_idi(ui_id("reports.roster.arrow"), k), ar, k ? UI_ICON_DOWN : UI_ICON_UP, UI_COMPACT,
                         k ? "Scroll down (or the mouse wheel)" : "Scroll up (or the mouse wheel)") && na < max)
        acts[na++] = (RpAction){RP_A_CLICK, (0x30c & 0xffff) | (spot_y[k] << 16)};
    }
    float wh = ui_wheel(c, bid);
    if (wh != 0 && na < max) {
      int k = wh > 0 ? 0 : 1;
      acts[na++] = (RpAction){RP_A_CLICK, (0x30c & 0xffff) | (spot_y[k] << 16)};
    }
  }
  if (ui_clicked(c, scrim_id) && na < max) acts[na++] = (RpAction){RP_A_CLOSE, 0};
  return na;
}
