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

static int draw_ranking(UiCtx *c, const RpSnap *s, SDL_Texture *const *art, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  UiRect scr = ui_screen(c);
  float margin = ui_s(c, t->edge_margin);
  float fit = fminf(1.0f, fminf((scr.w - 2 * margin) / ui_s(c, 1180), (scr.h - 2 * margin) / ui_s(c, 800)));
  if (fit < 1) ui_push_scale(c, fit);
  UiRect pr = ui_rect_round(ui_center(ui_screen(c), ui_s(c, 1180), ui_s(c, 800)));
  uid_rrect(d, scr, 0, t->scrim);
  ui_panel(c, pr, ui_s(c, t->radius_panel), UI_PANEL_STRONG);
  UiRect in = ui_inset_all(pr, ui_s(c, 28));
  memset(&g_layout, 0, sizeof g_layout);
  g_layout.panel = pr;
  int na = 0;
  float cb = ui_s(c, 40);
  UiRect close = ui_rect(in.x + in.w - cb, in.y, cb, cb);
  g_layout.close_x = close.x + cb / 2;
  g_layout.close_y = close.y + cb / 2;
  if (ui_icon_button(c, ui_id("ranking.close"), close, UI_ICON_X, 0, "Close (Esc)") && na < max)
    acts[na++] = (RpAction){RP_A_CLOSE, 0};
  ui_text_fit(c, ui_rect(in.x, in.y, in.w - cb, cb), UI_TYPE_TITLE, t->text_primary, "Top 10 Golf Courses", 0);
  ui_text_fit(c, ui_rect(in.x, in.y + ui_s(c, 48), in.w, ui_s(c, 24)), UI_TYPE_LABEL, t->accent_soft_text,
              "COURSE RANKINGS", 0);
  float row_y = in.y + ui_s(c, 140), row_h = ui_s(c, 48);
  float name_x = in.x + ui_s(c, 64), values_x = in.x + ui_s(c, 510), cell_w = ui_s(c, 100);
  ui_text_in(c, ui_rect(name_x, row_y - ui_s(c, 42), ui_s(c, 420), ui_s(c, 30)), UI_TYPE_LABEL, t->text_tertiary, "COURSE", 0);
  for (int col = 0; col < 5; col++) {
    SDL_Texture *tx = NULL;
    for (int i = 0; art && i < s->n && !tx; i++) {
      const RpItem *it = &s->it[i];
      if (it->type == RP_IT_CHIP && it->group == col && it->art) tx = art[it->art - 1];
    }
    UiRect cell = ui_rect(values_x + col * cell_w, row_y - ui_s(c, 42), cell_w, ui_s(c, 30));
    if (col == 4) {
      ui_text_fit(c, cell, UI_TYPE_LABEL, t->text_tertiary, "STORIES", 0.5f);
    } else if (tx) {
      float side = ui_s(c, 24);
      UiRect icon = ui_center(cell, side, side);
      uid_rrect(d, ui_inset_all(icon, -ui_s(c, 4)), ui_s(c, 6), uid_alpha(t->text_primary, 0.65f));
      uid_image(d, tx, NULL, icon, 0, (UiColor){255, 255, 255, 255});
    }
  }
  ui_text_fit(c, ui_rect(values_x + 5 * cell_w, row_y - ui_s(c, 42), cell_w, ui_s(c, 30)),
              UI_TYPE_LABEL, t->text_tertiary, "TOTAL", 0.5f);
  for (int row = 0; row < 10; row++) {
    const RpItem *name = NULL;
    for (int i = 0; i < s->n; i++)
      if (s->it[i].type == RP_IT_TEXT && s->it[i].align == RP_AL_LEFT && s->it[i].y == 50 + 14 * row) name = &s->it[i];
    if (!name) continue;
    int current = name->col == 0x7c1f;
    UiRect rr = ui_rect(in.x, row_y + row * row_h, in.w, row_h - ui_s(c, 3));
    uid_rrect(d, rr, ui_s(c, t->radius_control), uid_alpha(current ? t->accent_soft : t->surface_inset, current ? 0.72f : 0.35f));
    char text[RP_TEXT];
    snprintf(text, sizeof text, "%d", row + 1);
    ui_text_fit(c, ui_rect(in.x, rr.y, ui_s(c, 52), rr.h), UI_TYPE_HEADING, t->text_secondary, text, 0.5f);
    fix_text(text, sizeof text, name->s);
    ui_text_fit(c, ui_rect(name_x, rr.y, values_x - name_x - ui_s(c, 20), rr.h), UI_TYPE_HEADING,
                current ? t->accent_soft_text : t->text_primary, text, 0);
    int64_t total = 0;
    for (int col = 0; col < 6; col++) {
      int64_t count = col < 5 ? s->ranking_counts[row][col] : total;
      if (col < 5) total += count;
      snprintf(text, sizeof text, "%lld", (long long)count);
      ui_text_fit(c, ui_rect(values_x + col * cell_w, rr.y, cell_w, rr.h), col == 5 ? UI_TYPE_HEADING : UI_TYPE_BODY,
                  col == 5 ? t->accent_soft_text : t->text_secondary, text, 0.5f);
    }
  }
  int footer = 0;
  for (int i = 0; i < s->n && footer < 2; i++) {
    const RpItem *it = &s->it[i];
    if (it->type != RP_IT_TEXT || it->align != RP_AL_CENTRE || it->y <= 190) continue;
    char text[RP_TEXT];
    fix_text(text, sizeof text, it->s);
    ui_text_fit(c, ui_rect(in.x, row_y + 10 * row_h + ui_s(c, 18 + footer++ * 26), in.w, ui_s(c, 26)),
                UI_TYPE_BODY, t->text_secondary, text, 0.5f);
  }
  if (fit < 1) ui_pop_scale(c);
  return na;
}

static int draw_leaderboard(UiCtx *c, const RpSnap *s) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  UiRect scr = ui_screen(c);
  float margin = ui_s(c, t->edge_margin), width = ui_s(c, 380), row_h = ui_s(c, 34);
  float top = margin + ui_s(c, 110);
  const RpItem *rows[48];
  int n = 0;
  for (int i = 0; i < s->n && n < 48; i++)
    if (s->it[i].type == RP_IT_TEXT && s->it[i].align == RP_AL_LEFT && s->it[i].y >= 45) rows[n++] = &s->it[i];
  float header = ui_s(c, 140);
  float height = fminf(header + n * row_h + ui_s(c, 24), fminf(ui_s(c, 650), scr.h - top - margin));
  UiRect panel = ui_rect_round(ui_rect(scr.x + margin, top, width, height));
  memset(&g_layout, 0, sizeof g_layout);
  g_layout.panel = panel;
  ui_panel(c, panel, ui_s(c, t->radius_panel), UI_PANEL_STRONG);
  ui_hit(c, panel, ui_s(c, t->radius_panel), ui_id("leaderboard.panel"), UI_HIT_BLOCK);
  UiRect in = ui_inset_all(panel, ui_s(c, 18));
  ui_text_fit(c, ui_rect(in.x, in.y, in.w, ui_s(c, 32)), UI_TYPE_HEADING, t->text_primary, "Tournament leaderboard", 0);
  int line = 0;
  for (int i = 0; i < s->n && line < 2; i++) {
    const RpItem *it = &s->it[i];
    if (it->type != RP_IT_TEXT || it->align != RP_AL_CENTRE || it->y < 20 || it->y > 33) continue;
    char text[RP_TEXT]; fix_text(text, sizeof text, it->s);
    ui_text_fit(c, ui_rect(in.x, in.y + ui_s(c, 36 + 24 * line++), in.w, ui_s(c, 24)), UI_TYPE_CAPTION,
                t->text_secondary, text, 0);
  }
  UiRect view = ui_rect(in.x, panel.y + header, in.w, panel.h - header - ui_s(c, 18));
  float off = ui_vscroll_begin(c, ui_id("leaderboard.scroll"), view, n * row_h, row_h * 3);
  for (int i = 0; i < n; i++) {
    UiRect r = ui_rect(view.x, view.y + off + i * row_h, view.w, row_h - ui_s(c, 2));
    if (r.y + r.h <= view.y || r.y >= view.y + view.h) continue;
    uid_rrect(d, r, ui_s(c, t->radius_control), uid_alpha(t->surface_inset, i & 1 ? 0.2f : 0.4f));
    char text[RP_TEXT]; fix_text(text, sizeof text, rows[i]->s);
    ui_text_fit(c, ui_inset_all(r, ui_s(c, 4)), UI_TYPE_BODY,
                rows[i]->col == 0x23e8 ? t->accent_soft_text : t->text_primary, text, 0);
  }
  ui_vscroll_end(c);
  return 0;
}

#include "ui_report_layouts.inc"

int ui_reports_draw(UiCtx *c, const RpSnap *s, SDL_Texture *const *art, RpAction *acts, int max) {
  if (s->kind == RP_LEADERBOARD) return draw_leaderboard(c, s);
  if (s->kind == RP_MONTH) return draw_ranking(c, s, art, acts, max);
  if (s->kind >= RP_PROFILE) return ui_pages_draw(c, s, art, acts, max);
  if (s->kind != RP_NONE) return draw_organized_report(c, s, art, acts, max);
  return 0;
}
