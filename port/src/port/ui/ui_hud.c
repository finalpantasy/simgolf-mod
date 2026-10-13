// Frosted Mint course HUD screen, see ui_hud.h. Layout: docs/ui/DESIGN_SYSTEM.md
// section 4 (D5); what each control does in the game: docs/ui/HUD_DISPATCH.md.
#include "port/ui/ui_hud.h"
#include "port/ui/ui_rules.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Original 800x600 click points (HUD_DISPATCH.md 2.2, 2.3).
enum { W_COURSE, W_BUILD, W_PEOPLE, W_INFO, W_PAUSE, W_OPTIONS, W_ZOOMIN = 7, W_ZOOMOUT, W_ROTL, W_ROTR };
static const short k_wheel[11][2] = {{44, 474}, {117, 496}, {176, 537}, {72, 549}, {106, 563}, {130, 579},
                                     {77, 586}, {31, 542}, {31, 584}, {16, 564}, {45, 565}};

static const char *const k_terrain_default[16] = {"Tee", "Green", "Sand trap", "Rough", "Pot bunker", "Stream",
                                                  "Water", "Fairway", "Firm fairway", "Deep rough", "Waste bunker",
                                                  "Brush", "Rocks", "Tree", "Tree", "Tree"};
static const int k_terrain_id[16] = {0, 1, 7, 4, 9, 10, 17, 2, 3, 5, 8, 11, 12, 13, 14, 15};
// Course tray tabs -> terrain slots
static const int k_tab_holes[] = {0, 1, -1};
static const int k_tab_fairway[] = {7, 8, 3, 9, -1};
static const int k_tab_hazards[] = {2, 4, 10, 6, 5, 11, 12, -1};
static const int k_tab_trees[] = {13, 14, 15, -1};
static const int *const k_tabs[4] = {k_tab_holes, k_tab_fairway, k_tab_hazards, k_tab_trees};

typedef struct { const char *name; short x, y; int tool; UiIcon icon; } Ctl;
static const Ctl k_amenities[] = {
    {"Pathway", 333, 526, 0, UI_ICON_PATH},     {"Benches", 470, 575, 1, UI_ICON_HOUSE},
    {"Flower bed", 652, 575, 2, UI_ICON_SPARKLE}, {"Ball washer", 302, 563, 3, UI_ICON_CUP},
    {"Landmarks", 591, 575, 4, UI_ICON_STAR},    {"Home site", 363, 563, 5, UI_ICON_HOUSE},
    {"Scenic trees", 714, 575, 16, UI_ICON_TREE}, {"Scenic bridge", 531, 580, 19, UI_ICON_PATH},
};
static const Ctl k_elevation[] = {
    {"Raise or lower a point", 352, 547, 0, UI_ICON_MOUNTAIN}, {"Raise or lower a square", 467, 547, 1, UI_ICON_MOUNTAIN},
    {"Raise or lower an area", 587, 547, 2, UI_ICON_MOUNTAIN}, {"Analyze golf shot", 715, 547, 3, UI_ICON_EYE},
};
static const char *const k_elev_short[4] = {"Point", "Square", "Area", "Shot"};
static const char *const k_building_default[9] = {"Putting Green", "Snack Bar", "Pro Shop", "Swim Club", "Driving Range",
                                                  "Cart Garage", "Marina", "Resort Hotel", "Airstrip"};

void ui_hud_money(char *buf, int cap, int v) {
  char tmp[32];
  unsigned u = v < 0 ? (unsigned)(-(long long)v) : (unsigned)v;
  int n = snprintf(tmp, sizeof tmp, "%u", u), o = 0;
  char out[48];
  if (v < 0) out[o++] = '-';
  out[o++] = '$';
  for (int i = 0; i < n; i++) {
    out[o++] = tmp[i];
    if ((n - 1 - i) % 3 == 0 && i != n - 1) out[o++] = ',';
  }
  out[o] = 0;
  snprintf(buf, cap, "%s", out);
}

typedef struct { HudAction *out; int n, max; } Acts;
static void act(Acts *a, HudAction x) {
  if (a->n < a->max) a->out[a->n++] = x;
}
static void click(Acts *a, int x, int y) { act(a, (HudAction){HUD_ACT_CLICK, x, y, 0, 0}); }
static void wheel(Acts *a, int i) { click(a, k_wheel[i][0], k_wheel[i][1]); }
static void goal(Acts *a, int g, int x, int y) { act(a, (HudAction){HUD_ACT_GOAL, x, y, g, 0}); }

// Rotate what is being placed (a building, a tree, an amenity): the original's Tab
// key (shape counter), as a visible button in the tray header while a placement is
// active. The Buildings and Course tray builders set g_rotate for the tray() call
// that follows; tray() and panel_tray() take it.
static int g_rotate;
// task-115: which building lots golf turns with Tab. The placement ghost of the Putting Green (tool 6) and the
// Driving Range (tool 10) is drawn with facing 0 whatever the shape counter says (golf_01.c FUN_0040f5c0 cases 5
// and 9: FUN_0043d6f0(id, frame, 0)); every other lot, the clubhouse (15) and the trees use the counter as facing.
// tests/native/rotate_buildings.mjs proves it from the picture: the ghost of each tool at the four shapes.
static int building_rotates(int tool) { return tool >= 7 && tool <= 15 && tool != 10; }
static const char k_rot_label[] = "Rotate (Tab)";
static float rot_width(UiCtx *c) { return ui_s(c, 52) + ui_text_width(c, UI_TYPE_LABEL, k_rot_label); }
static void rot_button(UiCtx *c, float right, float cy, float h, Acts *a) {
  float w = rot_width(c);
  UiRect r = ui_rect(right - w, cy - h / 2, w, h);
  uint32_t id = ui_id("hud.rotate");
  if (ui_button(c, id, r, k_rot_label, UI_ICON_ROTR, UI_BTN_PILL, 0)) act(a, (HudAction){HUD_ACT_KEY, 0, 0, 0, 9});
  ui_tooltip(c, id, r, "Rotate what you are placing (the Tab key does the same)");
}

// ------------------------------------------------------------------ top capsule

// density 0 regular: emblem, course and date, four stats with their words, play/pause.
// 1 compact: 70% high, emblem (name and date in its tooltip), numbers only.
// 2 minimal: a small chip at the top left: money, date, play/pause; the other
//   stats slide out while the pointer is over it.
static void capsule(UiCtx *c, UiHud *hud, const HudSnap *s, SDL_Texture *const *art, Acts *a) {
  const UiTokens *t = ui_tokens(c);
  // the strip and fan variants share the slim capsule; the wheel (V2) uses the chip, centred at the top
  int center_chip = hud->density == HUD_WHEEL;
  int dens = hud->density == 3 || hud->density == HUD_FAN || hud->density == HUD_QUARTER || hud->density == HUD_STOCK ? 1
             : center_chip ? 2 : hud->density;
  char money[32], fun[16], skill[16], golfers[16], tip[160];
  if (s->sandbox) snprintf(money, sizeof money, "Unlimited");
  else ui_hud_money(money, sizeof money, s->money);
  if (s->cheat && !s->sandbox) strncat(money, "*", sizeof money - strlen(money) - 1);
  snprintf(fun, sizeof fun, "%d", s->fun);
  snprintf(skill, sizeof skill, "%d.%02d", s->skill_x100 / 100, (s->skill_x100 < 0 ? -s->skill_x100 : s->skill_x100) % 100);
  snprintf(golfers, sizeof golfers, "%d", s->golfers);
  snprintf(tip, sizeof tip, "%s, %s", s->course, s->date);
  if (dens == 1) ui_push_scale(c, 0.7f);
  if (dens == 2) ui_push_scale(c, 0.8f);
  float h = ui_s(c, t->capsule_h), pad = ui_s(c, t->capsule_pad), em = ui_s(c, t->emblem), gap = ui_s(c, dens ? 12 : 16);
  static const char *const segs_none[2] = {NULL, NULL};
  static const UiIcon seg_icons[2] = {UI_ICON_PLAY, UI_ICON_PAUSE};
  static const UiIcon icons[4] = {UI_ICON_COINS, UI_ICON_SMILE, UI_ICON_TROPHY, UI_ICON_USERS};
  static const char *const units_full[4] = {"Money", "Fun", "Skill", "Golfers"};
  static const char *const tips[4] = {"Cash on hand", "Fun rating", "Skill rating", "Golfers on the course"};
  const char *vals[4] = {money, fun, skill, golfers};
  const char *units[4];
  for (int i = 0; i < 4; i++) units[i] = dens ? NULL : units_full[i];
  int shown[4] = {1, 1, 1, 1};
  if (dens == 2 && !hud->chip_open) shown[1] = shown[2] = shown[3] = 0;
  float segw = ui_segmented_width(c, segs_none, seg_icons, 2);
  float name_w = 0;
  if (dens == 0) {
    name_w = fmaxf(ui_text_width(c, UI_TYPE_NUMBER, s->course), ui_text_width(c, UI_TYPE_CAPTION, s->date));
    if (name_w > ui_s(c, 300)) name_w = ui_s(c, 300);
  } else if (dens == 2) {
    name_w = ui_text_width(c, UI_TYPE_CAPTION, s->date);
  }
  float st[4];
  for (int i = 0; i < 4; i++) st[i] = ui_stat_width(c, icons[i], vals[i], units[i]);
  float w = pad;
  if (dens != 2) w += em + ui_s(c, 12);
  if (dens == 0) w += name_w + gap + 1 + gap;
  int first = 1;
  for (int i = 0; i < 4; i++)
    if (shown[i]) w += st[i] + (first ? 0 : gap), first = 0;
  if (dens == 2) w += gap + name_w;
  w += gap + 1 + gap + segw + ui_s(c, 6);
  UiRect scr = ui_screen(c);
  float m = ui_s(c, t->edge_margin);
  UiRect r = dens == 2 ? ui_rect_round(center_chip ? ui_anchor(scr, w, h, UI_TOP, 0, m) : ui_rect(m, m, w, h)) : ui_rect_round(ui_anchor(scr, w, h, UI_TOP, 0, dens == 1 ? ui_s(c, 14) : m));
  ui_panel(c, r, h / 2, 0);
  float cy = r.y + h / 2, x = r.x + pad;
  if (dens != 2) {
    UiRect er = ui_rect(x, cy - em / 2, em, em);
    uid_rrect(ui_drawer(c), er, em / 2, t->surface_inset);
    if (art && art[HUD_ART_EMBLEM]) uid_image(ui_drawer(c), art[HUD_ART_EMBLEM], NULL, ui_inset_all(er, ui_s(c, 2)), 0, (UiColor){255, 255, 255, 255});
    else ui_icon(c, UI_ICON_FLAG, er, ui_s(c, 22), t->accent);
    if (dens == 1) {
      uint32_t eid = ui_id("hud.emblem");
      ui_hit(c, er, em / 2, eid, UI_HIT_BLOCK | UI_HIT_INTERACT);
      ui_tooltip(c, eid, er, tip);
    }
    x += em + ui_s(c, 12);
  }
  UiFont nf = ui_font(c, UI_TYPE_NUMBER), cf = ui_font(c, UI_TYPE_CAPTION);
  if (dens == 0) {
    float th = nf.line + cf.line, ty = cy - th / 2;
    ui_text_fit(c, ui_rect(x, ty, name_w, nf.line), UI_TYPE_NUMBER, t->text_primary, s->course, 0);
    ui_text_fit(c, ui_rect(x, ty + nf.line, name_w, cf.line), UI_TYPE_CAPTION, t->text_secondary, s->date, 0);
    x += name_w + gap;
    ui_divider_v(c, x, r.y + ui_s(c, 14), r.y + h - ui_s(c, 14));
    x += 1 + gap;
  }
  first = 1;
  for (int i = 0; i < 4; i++) {
    if (!shown[i]) continue;
    if (!first) x += gap;
    first = 0;
    UiRect hit = ui_rect(x - ui_s(c, 4), r.y + pad, st[i] + ui_s(c, 8), h - 2 * pad);
    uint32_t sid = ui_idi(ui_id("hud.stat"), i);
    ui_hit(c, hit, ui_s(c, 10), sid, UI_HIT_BLOCK | UI_HIT_INTERACT);
    ui_tooltip(c, sid, hit, dens ? units_full[i] : tips[i]);
    if (i == 0 && s->money < 0 && !s->sandbox) {
      // negative cash: the number in danger red
      float ix = x + ui_s(c, 18) + ui_s(c, 7);
      ui_icon(c, icons[i], ui_rect(x, cy - ui_s(c, 12), ui_s(c, 18), ui_s(c, 24)), ui_s(c, 18), t->danger);
      float base = floorf(cy + nf.px * 0.36f + 0.5f);
      float e = ui_text(c, ix, base, UI_TYPE_NUMBER, t->danger, vals[i]);
      if (units[i]) ui_text(c, e + ui_s(c, 6), base, UI_TYPE_CAPTION, t->text_secondary, units[i]);
    } else {
      ui_stat(c, x, cy, icons[i], vals[i], units[i]);
    }
    x += st[i];
  }
  if (dens == 2) {
    x += gap;
    ui_text_in(c, ui_rect(x, r.y, name_w + 1, h), UI_TYPE_CAPTION, t->text_secondary, s->date, 0);
    x += name_w;
  }
  x += gap;
  ui_divider_v(c, x, r.y + ui_s(c, dens ? 10 : 14), r.y + h - ui_s(c, dens ? 10 : 14));
  x += 1 + gap;
  int sel = s->paused ? 1 : 0, want = sel;
  float sh = fminf(ui_s(c, 38), h - 2 * ui_s(c, 6));
  UiRect sr = ui_rect(x, cy - sh / 2, segw, sh);
  ui_segmented(c, ui_id("hud.speed"), sr, NULL, seg_icons, 2, &want);
  if (want != sel) wheel(a, W_PAUSE);
  // the minimal chip opens while the pointer is over it (with a little slack)
  if (dens == 2) {
    float px, py;
    int in = ui_pointer(c, &px, &py) && ui_rect_contains(ui_inset_all(r, -ui_s(c, 6)), px, py);
    hud->chip_open = in;
  }
  ui_pop_scale(c);
}

// ------------------------------------------------------------------ toast

static void toast(UiCtx *c, const UiHud *hud, const HudSnap *s) {
  if (!s->news_on || !s->news[0]) return;
  const UiTokens *t = ui_tokens(c);
  // first sentence as the title, the rest as the line
  char title[200], line[400];
  const char *p = s->news;
  const char *e = NULL;
  for (const char *q = p; *q; q++)
    if ((*q == '.' || *q == '!' || *q == '?') && (q[1] == ' ' || q[1] == 0)) {
      e = q + 1;
      break;
    }
  if (e && (size_t)(e - p) < sizeof title && *e) {
    snprintf(title, sizeof title, "%.*s", (int)(e - p), p);
    while (*e == ' ') e++;
    snprintf(line, sizeof line, "%s", e);
  } else {
    snprintf(title, sizeof title, "%s", "News");
    snprintf(line, sizeof line, "%s", p);
  }
  // as wide as the longer line needs, at most 420
  float need = fmaxf(ui_text_width(c, UI_TYPE_LABEL, title), ui_text_width(c, UI_TYPE_CAPTION, line)) +
               ui_s(c, 10 + 44 + 12 + 24);
  float w = fminf(fminf(ui_s(c, 420), need), ui_screen(c).w - 2 * ui_s(c, t->edge_margin));
  float h = ui_toast_height(c, w, title, line);
  float top = ui_s(c, t->edge_margin) + ui_s(c, t->capsule_h) + ui_s(c, 16);
  if (hud->density == HUD_JWE) top = ui_s(c, 14 + 40 + 12);   // just under the stats cluster
  UiRect r = ui_rect_round(ui_anchor(ui_screen(c), w, h, UI_TOP_RIGHT, ui_s(c, t->edge_margin), top));
  float k = ui_anim(c, ui_id(s->news), 1, t->motion_open);
  r.y -= (1 - k) * ui_s(c, 12);
  ui_toast(c, r, NULL, title, line);
}

// ------------------------------------------------------------------ camera capsule

static void camera(UiCtx *c, UiHud *hud, Acts *a) {
  const UiTokens *t = ui_tokens(c);
  if (hud->density >= 2) return;   // hub variants: V2 has zoom in its hub, V3 and V5 use the keys   // W/A/S/D, middle drag and the wheel move the camera (3: the column is there)
  if (hud->density == 1) ui_push_scale(c, 0.75f);
  float b = ui_s(c, t->cam_button), pad = ui_s(c, 8), gap = ui_s(c, 4);
  float h = 4 * b + 3 * gap + 2 * pad, w = b + 2 * pad;
  UiRect r = ui_rect_round(ui_anchor(ui_screen(c), w, h, UI_RIGHT, ui_s(c, 24), 0));
  ui_panel(c, r, ui_s(c, 22), 0);
  static const UiIcon ic[4] = {UI_ICON_PLUS, UI_ICON_MINUS, UI_ICON_ROTL, UI_ICON_ROTR};
  static const char *const tip[4] = {"Zoom in", "Zoom out", "Rotate left", "Rotate right"};
  static const int wh[4] = {W_ZOOMIN, W_ZOOMOUT, W_ROTL, W_ROTR};
  for (int i = 0; i < 4; i++) {
    UiRect br = ui_rect(r.x + pad, r.y + pad + i * (b + gap), b, b);
    if (ui_icon_button(c, ui_idi(ui_id("hud.cam"), i), br, ic[i], UI_COMPACT, tip[i])) wheel(a, wh[i]);
  }
  if (hud->density == 1) ui_pop_scale(c);
}

// ------------------------------------------------------------------ dock

enum { D_COURSE, D_BUILD, D_LAND, D_PEOPLE, D_REPORTS, D_MENU, D_N };

static int active_item(const HudSnap *s) {
  if (s->dock_mode == 0) return D_COURSE;
  if (s->dock_mode == 1) return s->elevation_on ? D_LAND : D_BUILD;
  if (s->dock_mode >= 2 && s->dock_mode <= 4) return D_PEOPLE;
  return -1;
}

static void dock_act(UiHud *hud, const HudSnap *s, Acts *a, int i, int on) {
  int hidden = hud->tray_hidden;
  hud->tray_hidden = 0;
  if (i == on && hidden) return;   // the tray was tucked away: the button brings it back
  switch (i) {
    case D_COURSE: goal(a, i == on ? HUD_GOAL_CLOSE : HUD_GOAL_COURSE, -1, -1); break;
    case D_BUILD: goal(a, i == on ? HUD_GOAL_CLOSE : HUD_GOAL_BUILDINGS, -1, -1); break;
    case D_LAND: goal(a, i == on ? HUD_GOAL_CLOSE : HUD_GOAL_LANDSCAPE, -1, -1); break;
    case D_PEOPLE: goal(a, i == on ? HUD_GOAL_CLOSE : HUD_GOAL_PEOPLE, -1, -1); break;
    case D_REPORTS: wheel(a, W_INFO); break;
    case D_MENU: wheel(a, W_OPTIONS); break;
  }
  (void)s;
}

// What Esc does on the course (task-112), one step per press: close a message or the history, else fold the
// jwe build bar, else (regular: close the open tray and cancel its tool; the folding layouts: tuck the tray),
// else cancel the tool that is still armed behind a tucked tray, else open the Menu. Returns the number of
// actions written; *what names the step for the log.
int ui_hud_escape(UiHud *h, const HudSnap *s, HudAction *out, int max_out, const char **what) {
  Acts a = {out, 0, max_out};
  const char *w = "menu";
  int on = active_item(s);
  if (h->msg_open || h->hist_open) {
    h->msg_open = 0;
    h->hist_open = 0;
    w = "message";
  } else if (h->density == HUD_JWE && h->bar_open) {
    h->bar_open = 0;
    h->tray_hidden = 1;
    w = "bar";
  } else if (on >= 0 && on <= D_PEOPLE && !h->tray_hidden) {
    if (h->density == HUD_REGULAR) {
      goal(&a, HUD_GOAL_CLOSE, -1, -1);
      w = "close";
    } else {
      h->tray_hidden = 1;
      w = "tray";
    }
  } else if (on >= 0 && on <= D_PEOPLE) {
    goal(&a, HUD_GOAL_CLOSE, -1, -1);
    w = "tool";
  } else {
    wheel(&a, W_OPTIONS);
  }
  if (what) *what = w;
  return a.n;
}

static const UiIcon k_dock_ic[D_N] = {UI_ICON_FLAG, UI_ICON_HOUSE, UI_ICON_MOUNTAIN, UI_ICON_USERS, UI_ICON_CHART, UI_ICON_GEAR};
static const char *const k_dock_lab[D_N] = {"Course", "Buildings", "Landscape", "People", "Reports", "Menu"};
static const char *const k_dock_tip[D_N] = {"Build the course (tees, greens, hazards, trees)", "Add buildings",
                                            "Shape the land", "Golfers and staff", "Reports and the world map",
                                            "Save, load, preferences"};

// Returns the dock's rectangle (trays keep clear of it).
static UiRect dock(UiCtx *c, UiHud *hud, const HudSnap *s, Acts *a) {
  const UiTokens *t = ui_tokens(c);
  int on = active_item(s), dens = hud->density;
  if (dens == 0) {
    float iw = ui_s(c, t->dock_item_w), gap = ui_s(c, 6), px = ui_s(c, t->dock_pad_x);
    UiFont mf = ui_font(c, UI_TYPE_MICRO);
    float ih = ui_s(c, t->dock_square_on) + ui_s(c, 4) + mf.line + ui_s(c, 3) + ui_s(c, 5);
    float divw = ui_s(c, 13);
    float w = 2 * px + D_N * iw + (D_N - 1) * gap + divw;
    float h = ui_s(c, t->dock_pad_top) + ih + ui_s(c, t->dock_pad_bottom);
    UiRect r = ui_rect_round(ui_anchor(ui_screen(c), w, h, UI_BOTTOM, 0, ui_s(c, t->dock_bottom)));
    ui_panel(c, r, ui_s(c, t->radius_panel), 0);
    float x = r.x + px, y = r.y + ui_s(c, t->dock_pad_top);
    for (int i = 0; i < D_N; i++) {
      if (i == D_MENU) {
        ui_divider_v(c, x + (divw - gap) / 2 - gap / 2, y + ui_s(c, 14), y + ih - ui_s(c, 14));
        x += divw;
      }
      UiRect slot = ui_rect(x, y, iw, ih);
      if (ui_dock_item(c, ui_idi(ui_id("hud.dock"), i), slot, k_dock_ic[i], k_dock_lab[i], i == on, k_dock_tip[i]))
        dock_act(hud, s, a, i, on);
      x += iw + gap;
    }
    return r;
  }
  // compact: a row of 60% squares, no labels, flush to the bottom edge;
  // minimal: the same squares as a slim column on the left edge
  ui_push_scale(c, 0.6f);
  float sq = ui_s(c, t->dock_square), gap = ui_s(c, 8), pad = ui_s(c, 8), divw = ui_s(c, 10);
  float len = D_N * sq + (D_N - 1) * gap + divw + 2 * pad, thick = sq + 2 * pad;
  UiRect r = dens == 1 ? ui_rect_round(ui_anchor(ui_screen(c), len, thick, UI_BOTTOM, 0, ui_s(c, 6)))
                       : ui_rect_round(ui_anchor(ui_screen(c), thick, len, UI_LEFT, ui_s(c, 6), 0));
  ui_panel(c, r, ui_s(c, 20), 0);
  float p = pad;
  for (int i = 0; i < D_N; i++) {
    if (i == D_MENU) {
      if (dens == 1) ui_divider_v(c, r.x + p + divw / 2 - gap / 2, r.y + pad + ui_s(c, 4), r.y + r.h - pad - ui_s(c, 4));
      else uid_rrect(ui_drawer(c), ui_rect(r.x + pad + ui_s(c, 4), r.y + p + divw / 2 - gap / 2, sq - ui_s(c, 8), 1), 0, t->divider);
      p += divw;
    }
    UiRect slot = dens == 1 ? ui_rect(r.x + p, r.y + pad, sq, sq) : ui_rect(r.x + pad, r.y + p, sq, sq);
    if (ui_dock_item(c, ui_idi(ui_id("hud.dock"), i), slot, k_dock_ic[i], NULL, i == on, k_dock_lab[i]))
      dock_act(hud, s, a, i, on);
    p += sq + gap;
  }
  ui_pop_scale(c);
  return r;
}

// ------------------------------------------------------------------ trays

static const HudSnap *g_s;   // the snapshot being drawn (the strip draws the dock with the tray)
static UiHud *g_h;

typedef struct {
  const char *name, *price, *tip;
  SDL_Texture *art;
  UiIcon icon;
  int selected, disabled;
  int labelled;   // task-92: the name shows under the icon (the pro's controls), and the tile is wider
  int rotates;    // task-115: golf turns this piece with Tab (a small turn badge on the tile)
  HudAction act;
} Tile;
static float g_tile_wmul = 1;   // width of the compact tiles (people_tray widens the labelled ones)
static void people_log(const char *what, UiRect r);
// task-115: the turn badge on a tile whose piece can be rotated with Tab (art with more than one facing)
static void rot_badge(UiCtx *c, UiRect r) {
  const UiTokens *t = ui_tokens(c);
  float d = ui_s(c, 18);
  UiRect b = ui_rect_round(ui_rect(r.x + r.w - d - ui_s(c, 5), r.y + ui_s(c, 5), d, d));
  UiDraw *dr = ui_drawer(c);
  uid_rrect(dr, b, d / 2, t->glass_fill_strong);
  uid_rrect_stroke(dr, b, d / 2, fmaxf(1.0f, ui_s(c, 1.0f)), uid_alpha(t->accent, 0.9f));
  float ic = d - ui_s(c, 5);
  ui_icon(c, UI_ICON_ROTR, ui_rect(b.x + (d - ic) / 2, b.y + (d - ic) / 2, ic, ic), ic, t->accent);
}
static int mini_tile(UiCtx *c, uint32_t id, UiRect r, const Tile *t, int flags, const char *tip) {
  if (t->labelled && g_s && (g_s->employee_overlay || g_s->dock_mode == 2) && (!strcmp(t->name, "Fire") || !strcmp(t->name, "Move") || !strcmp(t->name, "Rename") || !strcmp(t->name, "Hire")))
    people_log(t->name, r);
  else if (t->labelled && (!strcmp(t->name, "Skills") || !strcmp(t->name, "Edit pro") || !strcmp(t->name, "Profile") ||
                            !strcmp(t->name, "Practice")))   // task-102b: the Your pro tiles, for the tests
    people_log(t->name, r);
  int hit = t->labelled ? ui_tile_mini_label(c, id, r, t->icon, t->name, flags, tip) : ui_tile_mini(c, id, r, t->art, t->icon, flags, tip);
  if (t->rotates) rot_badge(c, r);
  return hit;
}

static void tray_column(UiCtx *c, const char *title, const char *const *tabs, int ntabs, int *tab, Tile *tiles, int n,
                        int undo_x, int undo_y, UiRect dock_r, Acts *a);

static void strip(UiCtx *c, const char *title, const char *const *tabs, int ntabs, int *tab, Tile *tiles, int n,
                  int undo_x, int undo_y, Acts *a);
static void row_tray(UiCtx *c, const char *title, const char *const *tabs, int ntabs, int *tab, Tile *tiles, int n,
                     int undo_x, int undo_y, Acts *a, float x0, int tabs_above);

static void stock_tray(UiCtx *c, const char *title, const char *const *tabs, int ntabs, int *tab, Tile *tiles, int n,
                       int undo_x, int undo_y, Acts *a);
static void panel_tray(UiCtx *c, const char *title, const char *const *tabs, int ntabs, int *tab, Tile *tiles, int n,
                       int undo_x, int undo_y, Acts *a);
static int g_col_mode;      // tray_column: 0 beside the left dock, 1 right edge, 2 above the anchor (left), 3 above it (right)
static UiRect g_col_anchor;

static void tray(UiCtx *c, UiHud *hud, const char *title, const char *const *tabs, int ntabs, int *tab, Tile *tiles, int n,
                 int max_n, int undo_x, int undo_y, UiRect dock_r, Acts *a) {
  int rot = g_rotate;
  if (hud->density < HUD_BARS) g_rotate = 0;   // only the regular tray and panel_tray draw the button
  if (hud->density == HUD_CARD || hud->density == HUD_CARD_RIGHT) {
    g_col_mode = hud->density == HUD_CARD ? 2 : 3;
    g_col_anchor = dock_r;
    tray_column(c, title, tabs, ntabs, tab, tiles, n, undo_x, undo_y, dock_r, a);
    g_col_mode = 0;
    return;
  }
  if (hud->density == HUD_STOCK) {
    stock_tray(c, title, tabs, ntabs, tab, tiles, n, undo_x, undo_y, a);
    return;
  }
  if (hud->density >= HUD_BARS) {
    panel_tray(c, title, tabs, ntabs, tab, tiles, n, undo_x, undo_y, a);
    return;
  }
  if (hud->density == 2 || hud->density == 3) {
    g_col_mode = hud->density == 3 ? 1 : 0;
    tray_column(c, title, tabs, ntabs, tab, tiles, n, undo_x, undo_y, hud->density == 3 ? ui_rect(0, 0, 0, 0) : dock_r, a);
    return;
  }
  if (hud->density == 1) {
    strip(c, title, tabs, ntabs, tab, tiles, n, undo_x, undo_y, a);
    return;
  }
  if (hud->density >= HUD_WHEEL) {
    row_tray(c, title, tabs, ntabs, tab, tiles, n, undo_x, undo_y, a, dock_r.x + dock_r.w,
             hud->density == HUD_FAN || hud->density == HUD_COLLAPSED);
    return;
  }
  float dock_top = dock_r.y;
  const UiTokens *t = ui_tokens(c);
  float pad_x = ui_s(c, 16), pad_t = ui_s(c, 14), pad_b = ui_s(c, 16);
  float tw = ui_s(c, t->tile_w), gap = ui_s(c, t->gap_tiles), th = ui_tile_height(c);
  float head = ui_s(c, 40);
  UiFont hf = ui_font(c, UI_TYPE_HEADING);
  float title_w = ui_text_width(c, UI_TYPE_HEADING, title) + ui_s(c, 6);
  float tabs_w = ntabs ? ui_segmented_width(c, tabs, NULL, ntabs) : 0;
  float head_w = title_w + ui_s(c, 14) + tabs_w + ui_s(c, 14) + ui_s(c, t->icon_button_sm);
  if (rot && hud->density == HUD_REGULAR) head_w += rot_width(c) + ui_s(c, 10);
  float row_w = ui_row_length(n, tw, gap);
  float wide_w = ui_row_length(max_n > n ? max_n : n, tw, gap);   // the same width for every tab
  float max_inner = ui_screen(c).w - 2 * ui_s(c, t->edge_margin) - 2 * pad_x;
  float inner = fminf(fmaxf(wide_w, head_w), max_inner);
  float w = inner + 2 * pad_x, h = pad_t + head + ui_s(c, 12) + th + pad_b;
  float bottom = ui_s(c, t->tray_bottom);
  UiRect scr = ui_screen(c);
  if (scr.h - bottom > dock_top - ui_s(c, 8)) bottom = scr.h - dock_top + ui_s(c, 8);   // never under the dock
  float k = ui_anim(c, ui_id(title), 1, t->motion_open);
  UiRect r = ui_rect_round(ui_anchor(scr, w, h, UI_BOTTOM, 0, bottom - (1 - k) * ui_s(c, 16)));
  ui_panel(c, r, ui_s(c, t->radius_tray), 0);
  float x = r.x + pad_x, y = r.y + pad_t;
  ui_text_in(c, ui_rect(x + ui_s(c, 6), y, title_w, head), UI_TYPE_HEADING, t->text_primary, title, 0);
  (void)hf;
  if (ntabs) {
    UiRect tr = ui_rect(x + title_w + ui_s(c, 14), y + (head - ui_s(c, 38)) / 2, tabs_w, ui_s(c, 38));
    ui_segmented(c, ui_id(title) ^ 0x7ab5u, tr, tabs, NULL, ntabs, tab);
  }
  if (rot && hud->density == HUD_REGULAR)
    rot_button(c, r.x + r.w - pad_x - (undo_x >= 0 ? ui_s(c, t->icon_button_sm) + ui_s(c, 10) : 0), y + head / 2, ui_s(c, 36), a);
  if (undo_x >= 0) {
    float b = ui_s(c, t->icon_button_sm);
    UiRect ur = ui_rect(r.x + r.w - pad_x - b, y + (head - b) / 2, b, b);
    if (ui_icon_button(c, ui_id(title) ^ 0x0d0u, ur, UI_ICON_UNDO, UI_COMPACT,
                       "Undo: right-click a piece of the course, then right-click it again"))
      click(a, undo_x, undo_y);
  }
  UiRect view = ui_rect(x, y + head + ui_s(c, 12), inner, th);
  if (n == 0) {   // nothing to pick here: say what to do instead
    const char *hint = ntabs && tab && *tab == 0 ? "Pick a golfer from the list, or click one on the course."
                                                 : "Pick a tab above.";
    ui_text_in(c, view, UI_TYPE_BODY, t->text_secondary, hint, 0.5f);
    return;
  }
  float off = ui_scroll_begin(c, ui_id(title) ^ 0x5c011u, view, row_w);
  (void)row_w;   // tiles start at the left, as in the mockups
  for (int i = 0; i < n; i++) {
    UiRect tr = ui_rect(view.x + off + i * (tw + gap), view.y, tw, th);
    if (tr.x + tr.w < view.x || tr.x > view.x + view.w) continue;
    int flags = (tiles[i].selected ? UI_SELECTED : 0) | (tiles[i].disabled ? UI_DISABLED : 0);
    if (ui_tile(c, ui_idi(ui_id(title), i + 100), tr, tiles[i].art, tiles[i].icon, tiles[i].name, tiles[i].price, flags,
                tiles[i].tip))
      act(a, tiles[i].act);
    if (tiles[i].rotates) rot_badge(c, tr);
  }
  ui_scroll_end(c);
}

// Minimal: a narrow column beside the left strip; tabs wrap to two rows,
// tools are rows (art, name, price).
static void tray_column(UiCtx *c, const char *title, const char *const *tabs, int ntabs, int *tab, Tile *tiles, int n,
                        int undo_x, int undo_y, UiRect dock_r, Acts *a) {
  const UiTokens *t = ui_tokens(c);
  ui_push_scale(c, 0.8f);
  float pad = ui_s(c, 12), w = ui_s(c, 260), rowh = ui_s(c, 46), gap = ui_s(c, 4);
  UiFont hf = ui_font(c, UI_TYPE_HEADING);
  float head = hf.line + ui_s(c, 8), segh = ui_s(c, 34);
  int tab_rows = ntabs > 3 ? 2 : ntabs ? 1 : 0;
  float body = n ? n * rowh + (n - 1) * gap : ui_s(c, 60);
  float h = pad + head + tab_rows * (segh + ui_s(c, 6)) + body + pad;
  UiRect scr = ui_screen(c);
  float maxh = scr.h - 2 * ui_s(c, t->edge_margin);
  if (h > maxh) h = maxh;
  float k = ui_anim(c, ui_id(title) ^ 0xc01u, 1, t->motion_open);
  int mode = g_col_mode;
  float x0, y0 = scr.y + (scr.h - h) / 2;
  if (mode >= 2) {   // rising from the corner card's top edge
    float top = g_col_anchor.y - ui_s(c, 10) - ui_s(c, t->edge_margin);
    if (h > top) h = top;
    w = fmaxf(w, g_col_anchor.w);
    x0 = mode == 2 ? g_col_anchor.x : g_col_anchor.x + g_col_anchor.w - w;
    y0 = g_col_anchor.y - ui_s(c, 10) - h + (1 - k) * ui_s(c, 16);
  } else if (mode == 1) {
    x0 = scr.x + scr.w - ui_s(c, t->edge_margin) - w + (1 - k) * ui_s(c, 16);
  } else {
    x0 = dock_r.x + dock_r.w + ui_s(c, 10) - (1 - k) * ui_s(c, 16);
  }
  UiRect r = ui_rect_round(ui_rect(x0, y0, w, h));
  ui_panel(c, r, ui_s(c, 22), 0);
  float x = r.x + pad, y = r.y + pad, iw = r.w - 2 * pad;
  ui_text_in(c, ui_rect(x + ui_s(c, 4), y, iw, head), UI_TYPE_HEADING, t->text_primary, title, 0);
  if (undo_x >= 0) {
    float b = ui_s(c, t->icon_button_sm);
    UiRect ur = ui_rect(r.x + r.w - pad - b, y + (head - b) / 2, b, b);
    if (ui_icon_button(c, ui_id(title) ^ 0x0d0u, ur, UI_ICON_UNDO, UI_COMPACT,
                       "Undo: right-click a piece of the course, then right-click it again"))
      click(a, undo_x, undo_y);
  }
  y += head;
  if (tab_rows) {
    int split = tab_rows == 2 ? (ntabs + 1) / 2 : ntabs;
    for (int row = 0; row < tab_rows; row++) {
      int from = row ? split : 0, cnt = row ? ntabs - split : split;
      int sel = *tab >= from && *tab < from + cnt ? *tab - from : -1, want = sel;
      ui_segmented(c, ui_idi(ui_id(title) ^ 0x7ab5u, row), ui_rect(x, y, iw, segh), tabs + from, NULL, cnt, &want);
      if (want != sel) *tab = from + want;
      y += segh + ui_s(c, 6);
    }
  }
  if (!n) {
    const char *hint = ntabs && tab && *tab == 0 ? "Pick a golfer from the list, or click one on the course." : "Pick a tab above.";
    ui_text_fit(c, ui_rect(x, y, iw, ui_s(c, 40)), UI_TYPE_CAPTION, t->text_secondary, hint, 0);
  }
  UiRect clip = ui_rect(r.x, y, r.w, r.y + r.h - pad - y);
  uid_set_clip(ui_drawer(c), &clip);
  for (int i = 0; i < n; i++) {
    UiRect rr = ui_rect(x, y + i * (rowh + gap), iw, rowh);
    if (rr.y + rr.h > clip.y + clip.h) break;
    int flags = (tiles[i].selected ? UI_SELECTED : 0) | (tiles[i].disabled ? UI_DISABLED : 0);
    if (ui_tile_row(c, ui_idi(ui_id(title), i + 100), rr, tiles[i].art, tiles[i].icon, tiles[i].name, tiles[i].price,
                    flags, tiles[i].tip))
      act(a, tiles[i].act);
  }
  uid_set_clip(ui_drawer(c), NULL);
  ui_pop_scale(c);
}

// Compact (ui.hud_density=compact): ONE glass strip at the bottom-left edge,
// 72 px high at 1080p: the dock icons, then (a category open) a divider, its
// tabs as a small segmented control and its tools as art-only tiles in the
// same row (names and prices in tooltips). It grows to the right when a
// category opens and shrinks back to the dock when it closes; tools that do
// not fit scroll (arrows at the end, or the wheel). n < 0: the dock alone.
static void strip(UiCtx *c, const char *title, const char *const *tabs, int ntabs, int *tab, Tile *tiles, int n,
                  int undo_x, int undo_y, Acts *a) {
  const UiTokens *t = ui_tokens(c);
  const HudSnap *s = g_s;
  UiHud *hud = g_h;
  UiRect scr = ui_screen(c);
  float H = ui_s(c, 72), pad = ui_s(c, 8), item = H - 2 * pad, gap = ui_s(c, 6), sep = ui_s(c, 12);
  float tw = ui_s(c, 64) * g_tile_wmul, arrow = ui_s(c, 30), undo_w = ui_s(c, 32);
  float m = ui_s(c, t->edge_margin), bottom = ui_s(c, 8);
  // the dock part
  float dock_w = D_N * item + (D_N - 1) * gap + sep;
  // the category part
  int open = n >= 0;
  float tabs_w = open && ntabs ? ui_segmented_width(c, tabs, NULL, ntabs) : 0;
  float tiles_w = open && n > 0 ? n * tw + (n - 1) * gap : 0;
  float hint_w = open && n == 0 ? ui_s(c, 300) : 0;
  float fixed = pad + dock_w + (open ? sep + 1 + sep + (tabs_w ? tabs_w + sep : 0) + (undo_x >= 0 ? sep + undo_w : 0) : 0) + pad;
  float avail = scr.w - 2 * m;
  float view_w = tiles_w + hint_w;
  int arrows = 0;
  if (fixed + view_w > avail && tiles_w > 0) {
    arrows = 1;
    view_w = avail - fixed - 2 * arrow - 2 * gap;
    if (view_w < tw) view_w = tw;
  }
  float full = fixed + view_w + (arrows ? 2 * arrow + 2 * gap : 0);
  float closed_w = 2 * pad + dock_w;
  float k = ui_anim(c, ui_id("hud.strip.open"), open ? 1.0f : 0.0f, open ? t->motion_open : t->motion_close);
  float w = open ? closed_w + (full - closed_w) * k : closed_w;
  UiRect r = ui_rect_round(ui_rect(scr.x + m, scr.y + scr.h - bottom - H, w, H));
  ui_panel(c, r, ui_s(c, 22), 0);
  uid_set_clip(ui_drawer(c), &r);
  float x = r.x + pad, cy = r.y + H / 2;
  int on = active_item(s);
  for (int i = 0; i < D_N; i++) {
    if (i == D_MENU) {
      ui_divider_v(c, x + sep / 2 - gap / 2, r.y + ui_s(c, 18), r.y + H - ui_s(c, 18));
      x += sep;
    }
    UiRect slot = ui_rect(x, r.y + pad, item, item);
    if (ui_dock_item(c, ui_idi(ui_id("hud.dock"), i), slot, k_dock_ic[i], NULL, i == on, k_dock_lab[i]))
      dock_act(hud, s, a, i, on);
    x += item + gap;
  }
  x += -gap;
  if (open) {
    x += sep;
    ui_divider_v(c, x, r.y + ui_s(c, 14), r.y + H - ui_s(c, 14));
    x += 1 + sep;
    if (ntabs) {
      UiRect tr = ui_rect(x, cy - ui_s(c, 18), tabs_w, ui_s(c, 36));
      ui_segmented(c, ui_id(title) ^ 0x7ab5u, tr, tabs, NULL, ntabs, tab);
      x += tabs_w + sep;
    }
    UiRect view = ui_rect(x, r.y + pad, view_w, item);
    if (n == 0) {
      const char *hint = ntabs && tab && *tab == 0 ? "Pick a golfer from the list, or click one on the course." : "Pick a tab.";
      ui_text_fit(c, view, UI_TYPE_CAPTION, t->text_secondary, hint, 0);
    } else {
      uint32_t sid = ui_id(title) ^ 0x5c011u;
      float *off = ui_state(c, sid);
      UiRect ar = ui_rect(view.x + view.w + gap, cy - arrow / 2, arrow, arrow);
      UiRect br = ui_rect(ar.x + arrow + gap, ar.y, arrow, arrow);
      if (arrows) {   // drawn first so the row's clip does not cut them; clicks shift by three tools
        if (ui_icon_button(c, sid ^ 1, ar, UI_ICON_BACK, UI_COMPACT | (*off <= 0 ? UI_DISABLED : 0), "More tools")) *off -= 3 * (tw + gap);
        if (ui_icon_button(c, sid ^ 2, br, UI_ICON_CHEV, UI_COMPACT | (*off >= tiles_w - view.w - 1 ? UI_DISABLED : 0), "More tools"))
          *off += 3 * (tw + gap);
      }
      float o = ui_scroll_begin(c, sid, view, tiles_w);
      for (int i = 0; i < n; i++) {
        UiRect tr = ui_rect(view.x + o + i * (tw + gap), view.y, tw, item);
        if (tr.x + tr.w < view.x || tr.x > view.x + view.w) continue;
        char tipbuf[96];
        if (tiles[i].tip) snprintf(tipbuf, sizeof tipbuf, "%s: %s", tiles[i].name, tiles[i].tip);
        else if (tiles[i].price && *tiles[i].price) snprintf(tipbuf, sizeof tipbuf, "%s, %s", tiles[i].name, tiles[i].price);
        else snprintf(tipbuf, sizeof tipbuf, "%s", tiles[i].name);
        int flags = (tiles[i].selected ? UI_SELECTED : 0) | (tiles[i].disabled ? UI_DISABLED : 0);
        if (mini_tile(c, ui_idi(ui_id(title), i + 100), tr, &tiles[i], flags, tipbuf)) act(a, tiles[i].act);
      }
      ui_scroll_end(c);
      uid_set_clip(ui_drawer(c), &r);
    }
    x = view.x + view.w + (arrows ? 2 * (arrow + gap) : 0);
    if (undo_x >= 0) {
      UiRect ur = ui_rect(x + sep, cy - undo_w / 2, undo_w, undo_w);
      if (ui_icon_button(c, ui_id(title) ^ 0x0d0u, ur, UI_ICON_UNDO, UI_COMPACT,
                         "Undo: right-click a piece of the course, then right-click it again"))
        click(a, undo_x, undo_y);
    }
  }
  uid_set_clip(ui_drawer(c), NULL);
}

// Hub variants (the maintainer: "similar to the stock UI in layout, kinda"): the
// original's wheel in the bottom-left corner as a round frosted hub with the
// categories around it; the tray slides out along the bottom edge from it.
//   V2 wheel:       hub with Reports, Menu and zoom; four categories on an arc;
//                   money and date in a small capsule at the top centre.
//   V3 fan:         smaller hub with the emblem, all six buttons on a quarter
//                   arc; tray with its tabs in a short row above the tiles.
//   V5 wheel_stats: like V2 with money, date and play/pause in the hub and
//                   no top capsule; Reports and Menu join the arc.
// Returns the rectangle the tray starts to the right of.
static UiRect hub(UiCtx *c, UiHud *hud, const HudSnap *s, SDL_Texture *const *art, Acts *a) {
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  int v = hud->density, on = active_item(s);
  float m = ui_s(c, 12);
  float R = ui_s(c, v == HUD_FAN ? 62 : v == HUD_WHEEL_STATS ? 104 : 96);
  float cx = scr.x + m + (v == HUD_WHEEL_STATS ? R : R * 0.72f), cy = scr.y + scr.h - m - (v == HUD_WHEEL_STATS ? R : R * 0.72f);
  UiRect hr = ui_rect_round(ui_rect(cx - R, cy - R, 2 * R, 2 * R));
  ui_panel(c, hr, R, 0);
  uint32_t base = ui_id("hud.hub");
  // the arc of category buttons
  int narc = v == HUD_WHEEL ? 4 : D_N;
  float arc = ui_s(c, v == HUD_FAN ? 150 : v == HUD_WHEEL_STATS ? 162 : 150);
  float right = 0;   // the tray starts right of the hub and of the arc's last button
  float a0 = v == HUD_FAN ? 90 : 108, a1 = v == HUD_FAN ? 8 : v == HUD_WHEEL_STATS ? 14 : 34;
  for (int i = 0; i < narc; i++) {
    float ang = (a0 + (a1 - a0) * i / (narc - 1)) * 3.14159265f / 180;
    float d = ui_s(c, v == HUD_FAN ? 46 : i >= 4 ? 44 : 58);
    float bx = cx + arc * cosf(ang), by = cy - arc * sinf(ang);
    if (by + d / 2 > scr.y + scr.h - ui_s(c, 100) && bx + d / 2 > right) right = bx + d / 2;
    if (ui_round_item(c, ui_idi(base, i), bx, by, d, k_dock_ic[i], i == on, k_dock_tip[i])) dock_act(hud, s, a, i, on);
  }
  if (v == HUD_WHEEL) {   // Reports, Menu, zoom in, zoom out in the visible quarter of the hub
    static const UiIcon ic[4] = {UI_ICON_CHART, UI_ICON_GEAR, UI_ICON_PLUS, UI_ICON_MINUS};
    static const char *const tip[4] = {"Reports and the world map", "Save, load, preferences", "Zoom in", "Zoom out"};
    float b = ui_s(c, 40), g = ui_s(c, 6);
    float x0 = cx - b / 2 + ui_s(c, 4), y0 = cy - b - g / 2 - ui_s(c, 10);
    for (int i = 0; i < 4; i++) {
      UiRect br = ui_rect(x0 + (i % 2) * (b + g), y0 + (i / 2) * (b + g), b, b);
      if (ui_icon_button(c, ui_idi(base, 20 + i), br, ic[i], UI_ROUND, tip[i]))
        i == 0 ? wheel(a, W_INFO) : i == 1 ? wheel(a, W_OPTIONS) : wheel(a, i == 2 ? W_ZOOMIN : W_ZOOMOUT);
    }
  } else if (v == HUD_FAN) {   // the course emblem
    float em = ui_s(c, 50);
    UiRect er = ui_rect_round(ui_rect(cx - em / 2 + ui_s(c, 6), cy - em / 2 - ui_s(c, 6), em, em));
    uid_rrect(ui_drawer(c), er, em / 2, t->surface_inset);
    if (art && art[HUD_ART_EMBLEM]) uid_image(ui_drawer(c), art[HUD_ART_EMBLEM], NULL, ui_inset_all(er, ui_s(c, 2)), 0, (UiColor){255, 255, 255, 255});
    else ui_icon(c, UI_ICON_FLAG, er, ui_s(c, 22), t->accent);
  } else {   // V5: money, date, play/pause
    char money[32];
    if (s->sandbox) snprintf(money, sizeof money, "Unlimited");
    else ui_hud_money(money, sizeof money, s->money);
    UiFont nf = ui_font(c, UI_TYPE_NUMBER), cf = ui_font(c, UI_TYPE_CAPTION);
    float y = cy - ui_s(c, 52);
    uint32_t mid = ui_idi(base, 30);
    UiRect mr = ui_rect(cx - R + ui_s(c, 14), y, 2 * R - ui_s(c, 28), nf.line);
    ui_hit(c, mr, 0, mid, UI_HIT_BLOCK | UI_HIT_INTERACT);
    char tip[160];
    snprintf(tip, sizeof tip, "%s. Fun %d, skill %d.%02d, %d golfers", s->course, s->fun, s->skill_x100 / 100,
             (s->skill_x100 < 0 ? -s->skill_x100 : s->skill_x100) % 100, s->golfers);
    ui_tooltip(c, mid, mr, tip);
    float sw = ui_stat_width(c, UI_ICON_COINS, money, NULL);
    ui_stat(c, cx - sw / 2, y + nf.line / 2, UI_ICON_COINS, money, NULL);
    y += nf.line + ui_s(c, 2);
    ui_text_in(c, ui_rect(cx - R, y, 2 * R, cf.line), UI_TYPE_CAPTION, t->text_secondary, s->date, 0.5f);
    y += cf.line + ui_s(c, 10);
    static const UiIcon seg_icons[2] = {UI_ICON_PLAY, UI_ICON_PAUSE};
    static const char *const none[2] = {NULL, NULL};
    float segw = ui_segmented_width(c, none, seg_icons, 2);
    int sel = s->paused ? 1 : 0, want = sel;
    ui_segmented(c, ui_idi(base, 31), ui_rect(cx - segw / 2, y, segw, ui_s(c, 36)), NULL, seg_icons, 2, &want);
    if (want != sel) wheel(a, W_PAUSE);
  }
  float x1 = fmaxf(hr.x + hr.w, right) + ui_s(c, 12);
  return ui_rect(hr.x, hr.y, x1 - hr.x, hr.h);
}

// Money, date and play/pause stacked in a box (corner looks).
static void stats_box(UiCtx *c, const HudSnap *s, UiRect box, Acts *a, int row) {
  const UiTokens *t = ui_tokens(c);
  char money[32];
  if (s->sandbox) snprintf(money, sizeof money, "Unlimited");
  else ui_hud_money(money, sizeof money, s->money);
  UiFont nf = ui_font(c, UI_TYPE_NUMBER), cf = ui_font(c, UI_TYPE_CAPTION);
  static const UiIcon seg_icons[2] = {UI_ICON_PLAY, UI_ICON_PAUSE};
  static const char *const none[2] = {NULL, NULL};
  float segw = ui_segmented_width(c, none, seg_icons, 2), segh = ui_s(c, 32);
  uint32_t mid = ui_id("hud.stats");
  char tip[160];
  snprintf(tip, sizeof tip, "%s. Fun %d, skill %d.%02d, %d golfers", s->course, s->fun, s->skill_x100 / 100,
           (s->skill_x100 < 0 ? -s->skill_x100 : s->skill_x100) % 100, s->golfers);
  int sel = s->paused ? 1 : 0, want = sel;
  if (row) {   // money and date at the left, play/pause at the right
    UiRect tr = ui_rect(box.x, box.y, box.w - segw - ui_s(c, 6), box.h);
    ui_hit(c, tr, 0, mid, UI_HIT_BLOCK | UI_HIT_INTERACT);
    ui_tooltip(c, mid, tr, tip);
    float th = nf.line + cf.line, y = box.y + (box.h - th) / 2;
    ui_stat(c, box.x, y + nf.line / 2, UI_ICON_COINS, money, NULL);
    ui_text_fit(c, ui_rect(box.x + ui_s(c, 25), y + nf.line, tr.w - ui_s(c, 25), cf.line), UI_TYPE_CAPTION, t->text_secondary, s->date, 0);
    ui_segmented(c, ui_idi(mid, 1), ui_rect(box.x + box.w - segw, box.y + (box.h - segh) / 2, segw, segh), NULL, seg_icons, 2, &want);
  } else {     // stacked, centred
    float th = nf.line + cf.line + ui_s(c, 8) + segh, y = box.y + (box.h - th) / 2;
    UiRect tr = ui_rect(box.x, y, box.w, nf.line + cf.line);
    ui_hit(c, tr, 0, mid, UI_HIT_BLOCK | UI_HIT_INTERACT);
    ui_tooltip(c, mid, tr, tip);
    float sw = ui_stat_width(c, UI_ICON_COINS, money, NULL);
    ui_stat(c, box.x + (box.w - sw) / 2, y + nf.line / 2, UI_ICON_COINS, money, NULL);
    ui_text_in(c, ui_rect(box.x, y + nf.line, box.w, cf.line), UI_TYPE_CAPTION, t->text_secondary, s->date, 0.5f);
    ui_segmented(c, ui_idi(mid, 1), ui_rect(box.x + (box.w - segw) / 2, y + nf.line + cf.line + ui_s(c, 8), segw, segh),
                 NULL, seg_icons, 2, &want);
  }
  if (want != sel) wheel(a, W_PAUSE);
}

// Corner looks, round two (the maintainer: "something with the corner but not these").
// Returns the rectangle the tray grows from.
static UiRect corner(UiCtx *c, UiHud *hud, const HudSnap *s, SDL_Texture *const *art, Acts *a) {
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  int v = hud->density, on = active_item(s);
  float m = ui_s(c, 14);
  uint32_t base = ui_id("hud.corner");
  (void)art;
  if (v == HUD_CARD || v == HUD_CARD_RIGHT) {
    // C1 / C6: a card in the corner: stats row on top, categories in a 2 x 3 grid
    float sq = ui_s(c, 58), g = ui_s(c, 8), pad = ui_s(c, 12), statsh = ui_s(c, 44);
    float w = 3 * sq + 2 * g + 2 * pad, h = pad + statsh + g + 2 * sq + g + pad;
    UiRect r = ui_rect_round(v == HUD_CARD ? ui_rect(scr.x + m, scr.y + scr.h - m - h, w, h)
                                           : ui_rect(scr.x + scr.w - m - w, scr.y + scr.h - m - h, w, h));
    ui_panel(c, r, ui_s(c, 24), 0);
    stats_box(c, s, ui_rect(r.x + pad, r.y + pad, w - 2 * pad, statsh), a, 1);
    for (int i = 0; i < D_N; i++) {
      UiRect slot = ui_rect(r.x + pad + (i % 3) * (sq + g), r.y + pad + statsh + g + (i / 3) * (sq + g), sq, sq);
      if (ui_dock_item(c, ui_idi(base, i), slot, k_dock_ic[i], NULL, i == on, k_dock_lab[i])) dock_act(hud, s, a, i, on);
    }
    return r;
  }
  if (v == HUD_QUARTER) {
    // C2: a quarter disc cut by the screen edges, the categories as buttons along its rim
    float R = ui_s(c, 236), cx = scr.x, cy = scr.y + scr.h;
    ui_panel(c, ui_rect_round(ui_rect(cx - R, cy - R, 2 * R, 2 * R)), R, 0);
    float rim = R - ui_s(c, 44), d = ui_s(c, 56);
    for (int i = 0; i < D_N; i++) {
      float ang = (82 - 74.0f * i / (D_N - 1)) * 3.14159265f / 180;
      if (ui_round_item(c, ui_idi(base, i), cx + rim * cosf(ang), cy - rim * sinf(ang), d, k_dock_ic[i], i == on, k_dock_lab[i]))
        dock_act(hud, s, a, i, on);
    }
    // the inner quarter: zoom
    float b = ui_s(c, 40);
    if (ui_icon_button(c, ui_idi(base, 20), ui_rect(cx + ui_s(c, 22), cy - ui_s(c, 22) - 2 * b - ui_s(c, 6), b, b), UI_ICON_PLUS, UI_ROUND, "Zoom in"))
      wheel(a, W_ZOOMIN);
    if (ui_icon_button(c, ui_idi(base, 21), ui_rect(cx + ui_s(c, 22), cy - ui_s(c, 22) - b, b, b), UI_ICON_MINUS, UI_ROUND, "Zoom out"))
      wheel(a, W_ZOOMOUT);
    if (ui_icon_button(c, ui_idi(base, 22), ui_rect(cx + ui_s(c, 22) + b + ui_s(c, 6), cy - ui_s(c, 22) - b, b, b), UI_ICON_ROTL, UI_ROUND, "Rotate"))
      wheel(a, W_ROTL);
    return ui_rect(cx, cy - R, R + ui_s(c, 6), R);
  }
  if (v == HUD_LSHAPE) {
    // C3: a corner square with the stats, three categories up the left edge, three along the bottom
    float sq = ui_s(c, 54), g = ui_s(c, 6), pad = ui_s(c, 8), cs = ui_s(c, 132);
    UiRect corner_r = ui_rect_round(ui_rect(scr.x + m, scr.y + scr.h - m - cs, cs, cs));
    UiRect up = ui_rect_round(ui_rect(corner_r.x + (cs - sq - 2 * pad) / 2, corner_r.y - g - (3 * sq + 2 * g + 2 * pad), sq + 2 * pad, 3 * sq + 2 * g + 2 * pad));
    UiRect along = ui_rect_round(ui_rect(corner_r.x + cs + g, corner_r.y + (cs - sq - 2 * pad) / 2, 3 * sq + 2 * g + 2 * pad, sq + 2 * pad));
    ui_panel(c, up, ui_s(c, 20), 0);
    ui_panel(c, along, ui_s(c, 20), 0);
    ui_panel(c, corner_r, ui_s(c, 26), 0);
    stats_box(c, s, ui_inset_all(corner_r, ui_s(c, 10)), a, 0);
    static const int up_items[3] = {D_LAND, D_BUILD, D_COURSE}, along_items[3] = {D_PEOPLE, D_REPORTS, D_MENU};
    for (int k = 0; k < 3; k++) {
      int i = up_items[k];
      UiRect slot = ui_rect(up.x + pad, up.y + pad + k * (sq + g), sq, sq);
      if (ui_dock_item(c, ui_idi(base, i), slot, k_dock_ic[i], NULL, i == on, k_dock_lab[i])) dock_act(hud, s, a, i, on);
      i = along_items[k];
      slot = ui_rect(along.x + pad + k * (sq + g), along.y + pad, sq, sq);
      if (ui_dock_item(c, ui_idi(base, i), slot, k_dock_ic[i], NULL, i == on, k_dock_lab[i])) dock_act(hud, s, a, i, on);
    }
    return ui_rect(corner_r.x, corner_r.y, along.x + along.w - corner_r.x, cs);
  }
  if (v == HUD_COLLAPSED) {
    // C4: one round button in the corner and a tiny money and date pill; the
    // button (or an open category) fans the categories out around the corner
    float d = ui_s(c, 60), cx = scr.x + m + d / 2, cy = scr.y + scr.h - m - d / 2;
    int open = hud->radial_open || on >= 0;
    if (ui_round_item(c, ui_idi(base, 40), cx, cy, d, open ? UI_ICON_X : UI_ICON_FLAG, 0, open ? "Close" : "Build and manage"))
      hud->radial_open = !open;
    char money[32];
    if (s->sandbox) snprintf(money, sizeof money, "Unlimited");
    else ui_hud_money(money, sizeof money, s->money);
    char pill[96];
    snprintf(pill, sizeof pill, "%s  \xC2\xB7  %s", money, s->date);
    float ph = ui_s(c, 30), pw = ui_text_width(c, UI_TYPE_LABEL, pill) + ui_s(c, 24);
    UiRect pr = ui_rect_round(ui_rect(cx + d / 2 + ui_s(c, 10), cy + d / 2 - ph, pw, ph));
    float rim = ui_s(c, 170), bd = ui_s(c, 48);
    if (open) pr = ui_rect_round(ui_rect(scr.x + m, cy - rim - bd / 2 - ui_s(c, 12) - ph, pw, ph));
    ui_panel(c, pr, ph / 2, 0);
    ui_text_in(c, pr, UI_TYPE_LABEL, t->text_primary, pill, 0.5f);
    float right = cx + d / 2;
    if (open) {
      for (int i = 0; i < D_N; i++) {
        float ang = (90 - 90.0f * i / (D_N - 1)) * 3.14159265f / 180;
        float bx = cx + rim * cosf(ang), by = cy - rim * sinf(ang);
        if (ui_round_item(c, ui_idi(base, i), bx, by, bd, k_dock_ic[i], i == on, k_dock_lab[i])) {
          dock_act(hud, s, a, i, on);
          hud->radial_open = 0;
        }
        if (bx + bd / 2 > right) right = bx + bd / 2;
      }
    }
    return ui_rect(scr.x + m, cy - d / 2, right + ui_s(c, 14) - scr.x - m, d);
  }
  // C5: the stock wheel, faithful: the original's base and buttons at their
  // own places (800x600 layout scaled like the game's own UI), in glass
  float k = ui_s(c, 150) / 100.0f;
  float ox = scr.x, oy = scr.y + scr.h - 600 * k;   // the original's 800x600 frame, bottom-anchored
#define SX(x) (ox + (x) * k)
#define SY(y) (oy + (y) * k)
  ui_panel(c, ui_rect_round(ui_rect(SX(-120), SY(380), 330 * k, 330 * k)), 165 * k, 0);   // the base, cut by the edges
  static const struct { short x, y, d; int item; } big[3] = {{44, 474, 74, D_COURSE}, {117, 496, 66, D_BUILD}, {176, 537, 60, D_PEOPLE}};
  for (int i = 0; i < 3; i++) {
    int it = big[i].item;
    if (ui_round_item(c, ui_idi(base, it), SX(big[i].x), SY(big[i].y), big[i].d * k, k_dock_ic[it], it == on, k_dock_lab[it]))
      dock_act(hud, s, a, it, on);
  }
  // the small ones: info (Reports), pause, wrench (Menu), zoom, rotate, and Landscape in the spare slot
  static const struct { short x, y; UiIcon ic; const char *tip; int w; } small[8] = {
      {72, 549, UI_ICON_CHART, "Reports and the world map", W_INFO}, {106, 563, UI_ICON_PAUSE, "Pause", W_PAUSE},
      {130, 579, UI_ICON_GEAR, "Save, load, preferences", W_OPTIONS}, {31, 542, UI_ICON_PLUS, "Zoom in", W_ZOOMIN},
      {31, 584, UI_ICON_MINUS, "Zoom out", W_ZOOMOUT}, {14, 564, UI_ICON_ROTL, "Rotate left", W_ROTL},
      {48, 565, UI_ICON_ROTR, "Rotate right", W_ROTR}, {84, 590, UI_ICON_MOUNTAIN, "Shape the land", -1}};
  for (int i = 0; i < 8; i++) {
    float d = 25 * k;
    UiRect br = ui_rect_round(ui_rect(SX(small[i].x) - d / 2, SY(small[i].y) - d / 2, d, d));
    int flags = UI_ROUND | UI_COMPACT | (i == 7 && on == D_LAND ? UI_SELECTED : 0) | (i == 1 && s->paused ? UI_SELECTED : 0);
    if (ui_icon_button(c, ui_idi(base, 50 + i), br, small[i].ic, flags, small[i].tip)) {
      if (small[i].w >= 0) wheel(a, small[i].w);
      else dock_act(hud, s, a, D_LAND, on);
    }
  }
#undef SX
#undef SY
  return ui_rect(ox, oy + 435 * k, 215 * k, 165 * k);
}

// C5's tray: where the original's opens (216,482)-(800,600) of the 800x600
// layout, two rows of tools like the original.
static void stock_tray(UiCtx *c, const char *title, const char *const *tabs, int ntabs, int *tab, Tile *tiles, int n,
                       int undo_x, int undo_y, Acts *a) {
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  float k = ui_s(c, 150) / 100.0f;
  float ox = scr.x, oy = scr.y + scr.h - 600 * k;
  UiRect r = ui_rect_round(ui_rect(ox + 222 * k, oy + 484 * k, 572 * k, 112 * k));
  float kk = ui_anim(c, ui_id(title) ^ 0x57c0u, 1, t->motion_open);
  r.y += (1 - kk) * ui_s(c, 16);
  ui_panel(c, r, ui_s(c, 22), 0);
  float pad = ui_s(c, 10), segh = ui_s(c, 30);
  float x = r.x + pad, y = r.y + pad;
  float title_w = ui_text_width(c, UI_TYPE_LABEL, title) + ui_s(c, 12);
  ui_text_in(c, ui_rect(x + ui_s(c, 4), y, title_w, segh), UI_TYPE_LABEL, t->text_primary, title, 0);
  if (ntabs) ui_segmented(c, ui_id(title) ^ 0x7ab5u, ui_rect(x + title_w, y, ui_segmented_width(c, tabs, NULL, ntabs), segh), tabs, NULL, ntabs, tab);
  if (undo_x >= 0) {
    float b = ui_s(c, 30);
    if (ui_icon_button(c, ui_id(title) ^ 0x0d0u, ui_rect(r.x + r.w - pad - b, y, b, b), UI_ICON_UNDO, UI_COMPACT,
                       "Undo: right-click a piece of the course, then right-click it again"))
      click(a, undo_x, undo_y);
  }
  float gy = y + segh + ui_s(c, 6), gh = r.y + r.h - pad - gy;
  int rows = 2;
  float th = (gh - ui_s(c, 6)) / rows, tw = th * 1.15f, g = ui_s(c, 6);
  int per = (int)((r.w - 2 * pad + g) / (tw + g));
  if (per > (n + 1) / 2) per = (n + 1) / 2;   // two rows, like the original's tray
  if (per < 1) per = 1;
  tw = fminf((r.w - 2 * pad - (per - 1) * g) / per, th * 1.8f);   // and as wide as the tray allows
  if (n == 0) ui_text_in(c, ui_rect(x, gy, r.w - 2 * pad, gh), UI_TYPE_CAPTION, t->text_secondary, "Pick a golfer from the list, or click one on the course.", 0.5f);
  for (int i = 0; i < n && i < 2 * per; i++) {
    UiRect tr = ui_rect(x + (i % per) * (tw + g), gy + (i / per) * (th + g), tw, th);
    char tipbuf[96];
    if (tiles[i].price && *tiles[i].price) snprintf(tipbuf, sizeof tipbuf, "%s, %s", tiles[i].name, tiles[i].price);
    else snprintf(tipbuf, sizeof tipbuf, "%s", tiles[i].name);
    int flags = (tiles[i].selected ? UI_SELECTED : 0) | (tiles[i].disabled ? UI_DISABLED : 0);
    if (mini_tile(c, ui_idi(ui_id(title), i + 100), tr, &tiles[i], flags, tipbuf)) act(a, tiles[i].act);
  }
}

// ------------------------------------------------------------------ bar looks (round three)
//
// the maintainer: "maybe a top/bottom full width bar? something like cities skylines 2?"
// Layout ideas from sim and tycoon games, in the Frosted Mint style:
//   R1 Cities: Skylines 2: thin top bar (club, date, speed left; stats right),
//      thin bottom bar with the categories centred, the panel rising centred.
//   R2 Parkitect: one top bar (categories left, stats right), panels drop
//      down under their button, nothing at the bottom.
//   R3 Anno 1800: a slim top ribbon with the stats spread across, a short
//      category pill at the bottom centre, the tray above it.
//   R4 Two Point Campus: one bottom bar (stats left, categories centred,
//      play/pause and menu right), the tray above the categories.
//   R5 Jurassic World Evolution 2: a stats cluster at the top right and a
//      build toolbar at the bottom left whose grid panel opens above it, so the
//      centre of the screen (the course) stays clear.

static struct { float cx, left, edge; int up, anchor_left; } g_pan;   // where panel_tray goes

static float bar_h(UiCtx *c) { return ui_s(c, 44); }

// A full-width frosted bar along the top or bottom edge.
static UiRect full_bar(UiCtx *c, int top, float h) {
  UiRect scr = ui_screen(c);
  UiRect r = ui_rect_round(ui_rect(scr.x, top ? scr.y : scr.y + scr.h - h, scr.w, h));
  ui_panel(c, ui_inset(r, -ui_s(c, 2), top ? -ui_s(c, 2) : 0, -ui_s(c, 2), top ? 0 : -ui_s(c, 2)), 0, UI_PANEL_NOSHADOW);
  uid_rrect(ui_drawer(c), ui_rect(r.x, top ? r.y + r.h - 1 : r.y, r.w, 1), 0, ui_tokens(c)->glass_border);
  return r;
}

static void stat_texts(const HudSnap *s, char *money, char *fun, char *skill, char *golfers) {
  if (s->sandbox) snprintf(money, 32, "Unlimited");
  else ui_hud_money(money, 32, s->money);
  snprintf(fun, 16, "%d", s->fun);
  snprintf(skill, 16, "%d.%02d", s->skill_x100 / 100, (s->skill_x100 < 0 ? -s->skill_x100 : s->skill_x100) % 100);
  snprintf(golfers, 16, "%d", s->golfers);
}

// The four stats in a row; spread: across width w with even gaps. Returns the end x.
static float stats_row(UiCtx *c, const HudSnap *s, float x, float cy, int words, float spread_w) {
  char v[4][32];
  stat_texts(s, v[0], v[1], v[2], v[3]);
  static const UiIcon icons[4] = {UI_ICON_COINS, UI_ICON_SMILE, UI_ICON_TROPHY, UI_ICON_USERS};
  static const char *const units[4] = {"Money", "Fun", "Skill", "Golfers"};
  float w[4], total = 0, gap = ui_s(c, 18);
  for (int i = 0; i < 4; i++) total += w[i] = ui_stat_width(c, icons[i], v[i], words ? units[i] : NULL);
  if (spread_w > 0) gap = (spread_w - total) / 3;
  for (int i = 0; i < 4; i++) {
    uint32_t sid = ui_idi(ui_id("hud.barstat"), i);
    UiRect hit = ui_rect(x - ui_s(c, 4), cy - ui_s(c, 14), w[i] + ui_s(c, 8), ui_s(c, 28));
    ui_hit(c, hit, ui_s(c, 8), sid, UI_HIT_BLOCK | UI_HIT_INTERACT);
    ui_tooltip(c, sid, hit, units[i]);
    ui_stat(c, x, cy, icons[i], v[i], words ? units[i] : NULL);
    x += w[i] + (i < 3 ? gap : 0);
  }
  return x;
}

static float stats_row_width(UiCtx *c, const HudSnap *s, int words) {
  char v[4][32];
  stat_texts(s, v[0], v[1], v[2], v[3]);
  static const UiIcon icons[4] = {UI_ICON_COINS, UI_ICON_SMILE, UI_ICON_TROPHY, UI_ICON_USERS};
  static const char *const units[4] = {"Money", "Fun", "Skill", "Golfers"};
  float t = 3 * ui_s(c, 18);
  for (int i = 0; i < 4; i++) t += ui_stat_width(c, icons[i], v[i], words ? units[i] : NULL);
  return t;
}

// Emblem, club name and date; returns the end x.
static float club(UiCtx *c, const HudSnap *s, SDL_Texture *const *art, float x, float cy, int name) {
  const UiTokens *t = ui_tokens(c);
  float em = ui_s(c, 30);
  UiRect er = ui_rect_round(ui_rect(x, cy - em / 2, em, em));
  uid_rrect(ui_drawer(c), er, em / 2, t->surface_inset);
  if (art && art[HUD_ART_EMBLEM]) uid_image(ui_drawer(c), art[HUD_ART_EMBLEM], NULL, ui_inset_all(er, ui_s(c, 1)), 0, (UiColor){255, 255, 255, 255});
  else ui_icon(c, UI_ICON_FLAG, er, ui_s(c, 16), t->accent);
  x += em + ui_s(c, 10);
  if (name) x = ui_text(c, x, floorf(cy + ui_font(c, UI_TYPE_LABEL).px * 0.36f + 0.5f), UI_TYPE_LABEL, t->text_primary, s->course) + ui_s(c, 14);
  x = ui_text(c, x, floorf(cy + ui_font(c, UI_TYPE_CAPTION).px * 0.36f + 0.5f), UI_TYPE_CAPTION, t->text_secondary, s->date);
  return x;
}

// Play/pause at x (vertically centred on cy); returns the end x.
static float speed(UiCtx *c, const HudSnap *s, float x, float cy, Acts *a) {
  static const UiIcon seg_icons[2] = {UI_ICON_PLAY, UI_ICON_PAUSE};
  static const char *const none[2] = {NULL, NULL};
  float segw = ui_segmented_width(c, none, seg_icons, 2), segh = ui_s(c, 30);
  int sel = s->paused ? 1 : 0, want = sel;
  ui_segmented(c, ui_id("hud.barspeed"), ui_rect(x, cy - segh / 2, segw, segh), NULL, seg_icons, 2, &want);
  if (want != sel) wheel(a, W_PAUSE);
  return x + segw;
}

// Category icons in a row from x (labels in tooltips); n of them from first.
// Records the active button's rectangle in *act_r. Returns the end x.
static float cat_row(UiCtx *c, UiHud *hud, const HudSnap *s, Acts *a, float x, float cy, float sq, int first, int n, UiRect *act_r) {
  int on = active_item(s);
  int lit = hud->density == HUD_JWE && hud->tray_hidden ? -1 : on;   // jwe: lit only while its tray is out
  float g = ui_s(c, 6);
  for (int i = first; i < first + n; i++) {
    UiRect slot = ui_rect(x, cy - sq / 2, sq, sq);
    if (i == on && act_r) *act_r = slot;
    if (ui_dock_item(c, ui_idi(ui_id("hud.barcat"), i), slot, k_dock_ic[i], NULL, i == lit, k_dock_lab[i])) dock_act(hud, s, a, i, on);
    x += sq + g;
  }
  return x - g;
}

static void bars(UiCtx *c, UiHud *hud, const HudSnap *s, SDL_Texture *const *art, Acts *a) {
  UiRect scr = ui_screen(c);
  int v = hud->density;
  float bh = bar_h(c), m = ui_s(c, 14), sq = ui_s(c, 34);
  UiRect act = ui_rect(0, 0, 0, 0);
  memset(&g_pan, 0, sizeof g_pan);
  if (v == HUD_BARS) {   // R1
    UiRect top = full_bar(c, 1, ui_s(c, 40));
    float cy = top.y + top.h / 2, x = club(c, s, art, top.x + m, cy, 1);
    speed(c, s, x + ui_s(c, 16), cy, a);
    float sw = stats_row_width(c, s, 1);
    stats_row(c, s, top.x + top.w - m - sw, cy, 1, 0);
    UiRect bot = full_bar(c, 0, bh);
    float bw = D_N * ui_s(c, 36) + (D_N - 1) * ui_s(c, 6);
    cat_row(c, hud, s, a, bot.x + (bot.w - bw) / 2, bot.y + bot.h / 2, ui_s(c, 36), 0, D_N, &act);
    g_pan.cx = scr.x + scr.w / 2;
    g_pan.edge = bot.y - ui_s(c, 10);
    g_pan.up = 1;
  } else if (v == HUD_TOPBAR) {   // R2
    UiRect top = full_bar(c, 1, bh);
    float cy = top.y + top.h / 2;
    cat_row(c, hud, s, a, top.x + m, cy, sq, 0, D_N, &act);
    float sw = stats_row_width(c, s, 0);
    float x = top.x + top.w - m - sw;
    stats_row(c, s, x, cy, 0, 0);
    float cw = ui_text_width(c, UI_TYPE_CAPTION, s->date) + ui_s(c, 16);
    static const char *const none[2] = {NULL, NULL};
    static const UiIcon seg_icons[2] = {UI_ICON_PLAY, UI_ICON_PAUSE};
    float segw = ui_segmented_width(c, none, seg_icons, 2);
    float mid = top.x + top.w / 2 - (cw + segw) / 2;
    ui_text(c, mid, floorf(cy + ui_font(c, UI_TYPE_CAPTION).px * 0.36f + 0.5f), UI_TYPE_CAPTION, ui_tokens(c)->text_secondary, s->date);
    speed(c, s, mid + cw, cy, a);
    g_pan.left = act.w > 0 ? act.x - ui_s(c, 10) : top.x + m;
    g_pan.anchor_left = 1;
    g_pan.edge = top.y + top.h + ui_s(c, 8);
    g_pan.up = 0;
  } else if (v == HUD_RIBBON) {   // R3
    UiRect top = full_bar(c, 1, ui_s(c, 38));
    float cy = top.y + top.h / 2, x = club(c, s, art, top.x + m, cy, 1);
    x = speed(c, s, x + ui_s(c, 16), cy, a);
    float sx = x + ui_s(c, 60), sw = top.x + top.w - m - sx;
    stats_row(c, s, sx, cy, 1, sw);
    float pw = D_N * ui_s(c, 40) + (D_N - 1) * ui_s(c, 6) + 2 * ui_s(c, 8), ph = ui_s(c, 56);
    UiRect pill = ui_rect_round(ui_anchor(scr, pw, ph, UI_BOTTOM, 0, ui_s(c, 10)));
    ui_panel(c, pill, ph / 2, 0);
    cat_row(c, hud, s, a, pill.x + ui_s(c, 8), pill.y + ph / 2, ui_s(c, 40), 0, D_N, &act);
    g_pan.cx = scr.x + scr.w / 2;
    g_pan.edge = pill.y - ui_s(c, 10);
    g_pan.up = 1;
  } else if (v == HUD_BOTTOMBAR) {   // R4
    UiRect bot = full_bar(c, 0, ui_s(c, 48));
    float cy = bot.y + bot.h / 2, x = bot.x + m;
    char money[32];
    if (s->sandbox) snprintf(money, sizeof money, "Unlimited");
    else ui_hud_money(money, sizeof money, s->money);
    x = stats_row(c, s, x, cy, 0, 0) + ui_s(c, 16);
    ui_text(c, x, floorf(cy + ui_font(c, UI_TYPE_CAPTION).px * 0.36f + 0.5f), UI_TYPE_CAPTION, ui_tokens(c)->text_secondary, s->date);
    float n = D_N - 1, bw = n * ui_s(c, 38) + (n - 1) * ui_s(c, 6);
    cat_row(c, hud, s, a, bot.x + (bot.w - bw) / 2, cy, ui_s(c, 38), 0, D_N - 1, &act);
    static const char *const none[2] = {NULL, NULL};
    static const UiIcon seg_icons[2] = {UI_ICON_PLAY, UI_ICON_PAUSE};
    float segw = ui_segmented_width(c, none, seg_icons, 2);
    float rx = bot.x + bot.w - m - ui_s(c, 38) - ui_s(c, 10) - segw;
    speed(c, s, rx, cy, a);
    cat_row(c, hud, s, a, bot.x + bot.w - m - ui_s(c, 38), cy, ui_s(c, 38), D_MENU, 1, NULL);
    g_pan.cx = scr.x + scr.w / 2;
    g_pan.edge = bot.y - ui_s(c, 10);
    g_pan.up = 1;
  } else {   // R5, Jurassic World Evolution 2 (task-57: slimmer, the toolbar collapsed to one button)
    // stats cluster at the top right: money, fun, skill, golfers, date, play / pause,
    // on the readable glass (7:1 text contrast over any course picture)
    static const char *const none[2] = {NULL, NULL};
    static const UiIcon seg_icons[2] = {UI_ICON_PLAY, UI_ICON_PAUSE};
    float cw = stats_row_width(c, s, 0), segw = ui_segmented_width(c, none, seg_icons, 2), pad = ui_s(c, 12);
    float dw = ui_text_width(c, UI_TYPE_CAPTION, s->date);
    float bell = ui_s(c, 30), bellw = bell + ui_s(c, 6);   // task-62: the message history's bell, at the left end
    float ch = ui_s(c, 40), w = bellw + pad + cw + pad + dw + ui_s(c, 12) + segw + ui_s(c, 8);
    UiRect cl = ui_rect_round(ui_anchor(scr, w, ch, UI_TOP_RIGHT, m, m));
    ui_panel(c, cl, ui_s(c, 14), UI_PANEL_READABLE);
    float cy = cl.y + ch / 2;
    {
      UiRect br = ui_rect(cl.x + ui_s(c, 6), cy - bell / 2, bell, bell);
      int newest = s->msg_n ? s->msgs[s->msg_n - 1].seq : 0;
      if (ui_icon_button(c, ui_id("hud.bell"), br, UI_ICON_BELL, UI_COMPACT | (hud->hist_open ? UI_SELECTED : 0),
                         newest ? "Messages: what the club has told you" : "Messages (none yet)")) {
        hud->hist_open = !hud->hist_open;
        hud->msg_open = 0;
      }
      if (newest > hud->seen_seq && !hud->hist_open)   // unread: a dot on the bell
        uid_rrect(ui_drawer(c), ui_rect(br.x + br.w - ui_s(c, 11), br.y + ui_s(c, 3), ui_s(c, 8), ui_s(c, 8)), ui_s(c, 4), ui_tokens(c)->danger);
    }
    float x = stats_row(c, s, cl.x + bellw + pad, cy, 0, 0) + pad;
    x = ui_text(c, x, floorf(cy + ui_font(c, UI_TYPE_CAPTION).px * 0.36f + 0.5f), UI_TYPE_CAPTION, ui_tokens(c)->text_secondary, s->date);
    speed(c, s, x + ui_s(c, 12), cy, a);
    // build toolbar at the bottom left: one small button; it opens the category row, a click on the
    // course or Esc folds it away again (hud_frosted.c), the first button folds it too
    float tsq = ui_s(c, 34), tpad = ui_s(c, 5), tg = ui_s(c, 6);
    int cells = hud->bar_open ? D_N + 1 : 1;
    float tw = cells * tsq + (cells - 1) * tg + 2 * tpad, th = tsq + 2 * tpad;
    UiRect tb = ui_rect_round(ui_rect(scr.x + m, scr.y + scr.h - m - th, tw, th));
    ui_panel(c, tb, ui_s(c, 16), 0);
    UiRect first = ui_rect(tb.x + tpad, tb.y + tpad, tsq, tsq);
    if (!hud->bar_open) {
      if (ui_dock_item(c, ui_id("hud.barmore"), first, UI_ICON_PLUS, NULL, 0, "Build: open the build bar")) {
        hud->bar_open = 1;
        hud->tray_hidden = 1;   // opens with the bar closed up, a category opens its tray
      }
    } else {
      if (ui_dock_item(c, ui_id("hud.barless"), first, UI_ICON_X, NULL, 0, "Close the build bar (Esc)")) {
        hud->bar_open = 0;
        hud->tray_hidden = 1;
      }
      cat_row(c, hud, s, a, first.x + tsq + tg, tb.y + th / 2, tsq, 0, D_N, &act);
      g_pan.left = tb.x;
      g_pan.anchor_left = 1;
      g_pan.edge = tb.y - ui_s(c, 8);
      g_pan.up = 1;
    }
    // camera pill in the bottom right corner (zoom in / out, rotate left / right),
    // small, so the rotate and zoom buttons stay on screen (the keys still work)
    static const UiIcon cic[4] = {UI_ICON_PLUS, UI_ICON_MINUS, UI_ICON_ROTL, UI_ICON_ROTR};
    static const char *const ctip[4] = {"Zoom in", "Zoom out", "Rotate left", "Rotate right"};
    static const int cwh[4] = {W_ZOOMIN, W_ZOOMOUT, W_ROTL, W_ROTR};
    float cb = ui_s(c, 28), cg = ui_s(c, 4), cp = ui_s(c, 5);
    UiRect cr = ui_rect_round(ui_anchor(scr, 4 * cb + 3 * cg + 2 * cp, cb + 2 * cp, UI_BOTTOM_RIGHT, m, m));
    ui_panel(c, cr, ui_s(c, 14), 0);
    for (int i = 0; i < 4; i++)
      if (ui_icon_button(c, ui_idi(ui_id("hud.cam"), i), ui_rect(cr.x + cp + i * (cb + cg), cr.y + cp, cb, cb), cic[i], UI_COMPACT, ctip[i]))
        wheel(a, cwh[i]);
  }
}

// The bar looks' tray: a panel with the title and tabs on top and the tools
// in one or two rows (names in tooltips), rising from or dropping under its
// bar, centred (g_pan.cx) or from a left edge (g_pan.left).
static void panel_tray(UiCtx *c, const char *title, const char *const *tabs, int ntabs, int *tab, Tile *tiles, int n,
                       int undo_x, int undo_y, Acts *a) {
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  int slim = g_h && g_h->density == HUD_JWE;   // task-57: smaller tiles, header and padding
  float pad = ui_s(c, slim ? 10 : 12), segh = ui_s(c, slim ? 28 : 32), tw = ui_s(c, slim ? 50 : 64) * g_tile_wmul, th = ui_s(c, slim ? 44 : 56);
  float g = ui_s(c, slim ? 5 : 6), hgap = ui_s(c, slim ? 7 : 10);
  int per = n > 9 ? (n + 1) / 2 : n < 1 ? 1 : n;
  int rows = n > 0 ? (n + per - 1) / per : 1;
  float title_w = ui_text_width(c, UI_TYPE_HEADING, title) + ui_s(c, 14);
  float tabs_w = ntabs ? ui_segmented_width(c, tabs, NULL, ntabs) : 0;
  float undo_w = undo_x >= 0 ? ui_s(c, slim ? 28 : 32) : 0;
  int rot = g_rotate;
  g_rotate = 0;
  float rot_w = rot ? rot_width(c) + ui_s(c, 10) : 0;
  float grid_w = n > 0 ? per * tw + (per - 1) * g : ui_s(c, 320);
  float inner = fmaxf(grid_w, title_w + tabs_w + rot_w + (undo_w ? ui_s(c, 12) + undo_w : 0));
  float h = pad + segh + hgap + rows * th + (rows - 1) * g + pad, w = inner + 2 * pad;
  float k = ui_anim(c, ui_id(title) ^ 0x9a9e1u, 1, t->motion_open);
  float x0 = g_pan.anchor_left ? g_pan.left : g_pan.cx - w / 2;
  if (x0 + w > scr.x + scr.w - ui_s(c, 12)) x0 = scr.x + scr.w - ui_s(c, 12) - w;
  float y0 = g_pan.up ? g_pan.edge - h + (1 - k) * ui_s(c, 16) : g_pan.edge - (1 - k) * ui_s(c, 16);
  UiRect r = ui_rect_round(ui_rect(x0, y0, w, h));
  ui_panel(c, r, ui_s(c, slim ? 18 : 22), 0);
  float x = r.x + pad, y = r.y + pad;
  ui_text_in(c, ui_rect(x + ui_s(c, 2), y, title_w, segh), UI_TYPE_HEADING, t->text_primary, title, 0);
  if (ntabs) ui_segmented(c, ui_id(title) ^ 0x7ab5u, ui_rect(x + title_w, y, tabs_w, segh), tabs, NULL, ntabs, tab);
  if (rot) rot_button(c, r.x + r.w - pad - (undo_w ? undo_w + ui_s(c, 10) : 0), y + segh / 2, segh, a);
  if (undo_x >= 0 && ui_icon_button(c, ui_id(title) ^ 0x0d0u, ui_rect(r.x + r.w - pad - undo_w, y, undo_w, undo_w), UI_ICON_UNDO,
                                     UI_COMPACT, "Undo: right-click a piece of the course, then right-click it again"))
    click(a, undo_x, undo_y);
  y += segh + hgap;
  if (n == 0) {
    const char *hint = ntabs && tab && *tab == 0 ? "Pick a golfer from the list, or click one on the course." : "Pick a tab.";
    ui_text_in(c, ui_rect(x, y, inner, th), UI_TYPE_CAPTION, t->text_secondary, hint, 0);
  }
  for (int i = 0; i < n; i++) {
    UiRect tr = ui_rect(x + (i % per) * (tw + g), y + (i / per) * (th + g), tw, th);
    char tipbuf[96];
    if (tiles[i].tip) snprintf(tipbuf, sizeof tipbuf, "%s: %s", tiles[i].name, tiles[i].tip);
    else if (tiles[i].price && *tiles[i].price) snprintf(tipbuf, sizeof tipbuf, "%s, %s", tiles[i].name, tiles[i].price);
    else snprintf(tipbuf, sizeof tipbuf, "%s", tiles[i].name);
    int flags = (tiles[i].selected ? UI_SELECTED : 0) | (tiles[i].disabled ? UI_DISABLED : 0);
    if (mini_tile(c, ui_idi(ui_id(title), i + 100), tr, &tiles[i], flags, tipbuf)) act(a, tiles[i].act);
  }
}

// The hub variants' tray: one row along the bottom edge from x0, tabs at its
// left (or, tabs_above, in a short row above the tools), art-only tiles with
// their names in tooltips, arrows when they do not fit.
static void row_tray(UiCtx *c, const char *title, const char *const *tabs, int ntabs, int *tab, Tile *tiles, int n,
                     int undo_x, int undo_y, Acts *a, float x0, int tabs_above) {
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  float pad = ui_s(c, 8), item = ui_s(c, 56), gap = ui_s(c, 6), sep = ui_s(c, 12), tw = ui_s(c, 64) * g_tile_wmul;
  float segh = ui_s(c, tabs_above ? 30 : 36), arrow = ui_s(c, 30), undo_w = ui_s(c, 32);
  float m = ui_s(c, 8), bottom = ui_s(c, 8);
  UiFont hf = ui_font(c, UI_TYPE_LABEL);
  float title_w = ui_text_width(c, UI_TYPE_HEADING, title) + sep;
  float tabs_w = ntabs ? ui_segmented_width(c, tabs, NULL, ntabs) : 0;
  float tiles_w = n > 0 ? n * tw + (n - 1) * gap : ui_s(c, 300);
  float H = tabs_above ? pad + segh + gap + item + pad : pad + item + pad;
  float lead = tabs_above ? 0 : title_w + (tabs_w ? tabs_w + sep : 0);
  float tail = (undo_x >= 0 ? sep + undo_w : 0);
  float avail = scr.x + scr.w - m - x0 - 2 * pad;
  float view_w = tiles_w;
  int arrows = 0;
  if (lead + view_w + tail > avail && n > 0) {
    arrows = 1;
    view_w = avail - lead - tail - 2 * (arrow + gap);
    if (view_w < tw) view_w = tw;
  }
  float inner = lead + view_w + (arrows ? 2 * (arrow + gap) : 0) + tail;
  if (tabs_above) inner = fmaxf(inner, title_w + tabs_w + tail);
  float k = ui_anim(c, ui_id(title) ^ 0x50a0u, 1, t->motion_open);
  UiRect r = ui_rect_round(ui_rect(x0 - (1 - k) * ui_s(c, 16), scr.y + scr.h - bottom - H, inner + 2 * pad, H));
  ui_panel(c, r, ui_s(c, 22), 0);
  float x = r.x + pad, rowy = r.y + r.h - pad - item;
  (void)hf;
  if (tabs_above) {
    float ty = r.y + pad;
    ui_text_in(c, ui_rect(x + ui_s(c, 4), ty, title_w, segh), UI_TYPE_LABEL, t->text_primary, title, 0);
    if (ntabs) ui_segmented(c, ui_id(title) ^ 0x7ab5u, ui_rect(x + title_w, ty, tabs_w, segh), tabs, NULL, ntabs, tab);
  } else {
    ui_text_in(c, ui_rect(x + ui_s(c, 4), r.y, title_w, r.h), UI_TYPE_HEADING, t->text_primary, title, 0);
    x += title_w;
    if (ntabs) {
      ui_segmented(c, ui_id(title) ^ 0x7ab5u, ui_rect(x, r.y + (r.h - segh) / 2, tabs_w, segh), tabs, NULL, ntabs, tab);
      x += tabs_w + sep;
    }
  }
  UiRect view = ui_rect(x, rowy, view_w, item);
  if (n == 0) {
    const char *hint = ntabs && tab && *tab == 0 ? "Pick a golfer from the list, or click one on the course." : "Pick a tab.";
    ui_text_fit(c, view, UI_TYPE_CAPTION, t->text_secondary, hint, 0);
  } else {
    uint32_t sid = ui_id(title) ^ 0x5c012u;
    float *off = ui_state(c, sid);
    float cyr = rowy + item / 2;
    if (arrows) {
      UiRect ar = ui_rect(view.x + view.w + gap, cyr - arrow / 2, arrow, arrow), br = ui_rect(ar.x + arrow + gap, ar.y, arrow, arrow);
      if (ui_icon_button(c, sid ^ 1, ar, UI_ICON_BACK, UI_COMPACT | (*off <= 0 ? UI_DISABLED : 0), "More tools")) *off -= 3 * (tw + gap);
      if (ui_icon_button(c, sid ^ 2, br, UI_ICON_CHEV, UI_COMPACT | (*off >= tiles_w - view.w - 1 ? UI_DISABLED : 0), "More tools"))
        *off += 3 * (tw + gap);
    }
    float o = ui_scroll_begin(c, sid, view, tiles_w);
    for (int i = 0; i < n; i++) {
      UiRect tr = ui_rect(view.x + o + i * (tw + gap), view.y, tw, item);
      if (tr.x + tr.w < view.x || tr.x > view.x + view.w) continue;
      char tipbuf[96];
      if (tiles[i].tip) snprintf(tipbuf, sizeof tipbuf, "%s: %s", tiles[i].name, tiles[i].tip);
      else if (tiles[i].price && *tiles[i].price) snprintf(tipbuf, sizeof tipbuf, "%s, %s", tiles[i].name, tiles[i].price);
      else snprintf(tipbuf, sizeof tipbuf, "%s", tiles[i].name);
      int flags = (tiles[i].selected ? UI_SELECTED : 0) | (tiles[i].disabled ? UI_DISABLED : 0);
      if (mini_tile(c, ui_idi(ui_id(title), i + 100), tr, &tiles[i], flags, tipbuf)) act(a, tiles[i].act);
    }
    ui_scroll_end(c);
  }
  if (undo_x >= 0) {
    UiRect ur = ui_rect(r.x + r.w - pad - undo_w, rowy + (item - undo_w) / 2, undo_w, undo_w);
    if (ui_icon_button(c, ui_id(title) ^ 0x0d0u, ur, UI_ICON_UNDO, UI_COMPACT,
                       "Undo: right-click a piece of the course, then right-click it again"))
      click(a, undo_x, undo_y);
  }
}

static void price(char *buf, int cap, int dollars) {
  if (dollars < 0) buf[0] = 0;
  else ui_hud_money(buf, cap, dollars);
}

static void course_tray(UiCtx *c, UiHud *h, const HudSnap *s, SDL_Texture *const *art, UiRect dock_r, Acts *a) {
  static const char *const tabs[5] = {"Holes", "Fairway", "Hazards", "Trees", "Extras"};
  if (s->terrain_tool != h->last_terrain) {   // a tool picked by hotkey: show its tab
    h->last_terrain = s->terrain_tool;
    for (int k = 0; k < 4; k++)
      for (const int *p = k_tabs[k]; *p >= 0; p++)
        if (k_terrain_id[*p] == s->terrain_tool) h->course_tab = k;
  }
  int tab = s->amenities_on ? 4 : h->course_tab;
  int before = tab;
  Tile tiles[16];
  char prices[16][24];
  int n = 0;
  if (tab < 4) {
    for (const int *p = k_tabs[tab]; *p >= 0; p++) {
      int slot = *p;
      Tile *tl = &tiles[n];
      memset(tl, 0, sizeof *tl);
      tl->name = s->terrain_name[slot][0] ? s->terrain_name[slot] : k_terrain_default[slot];
      price(prices[n], sizeof prices[n], s->terrain_cost[slot]);
      tl->price = prices[n];
      tl->art = art ? art[HUD_ART_TERRAIN + slot] : NULL;
      tl->icon = slot >= 13 ? UI_ICON_TREE : UI_ICON_FLAG;
      tl->selected = s->terrain_tool == k_terrain_id[slot] && !s->amenities_on;
      tl->act = (HudAction){HUD_ACT_GOAL, -2 - slot, 0, HUD_GOAL_COURSE, 0};   // x <= -2: terrain slot
      n++;
    }
  } else {
    for (int i = 0; i < 8; i++) {
      Tile *tl = &tiles[n];
      memset(tl, 0, sizeof *tl);
      tl->name = k_amenities[i].name;
      tl->price = "";
      tl->art = art ? art[HUD_ART_AMENITY + i] : NULL;
      tl->icon = k_amenities[i].icon;
      tl->selected = s->build_tool == k_amenities[i].tool && s->amenities_on;
      tl->disabled = k_amenities[i].tool == 5 && s->waiting < 1;
      tl->tip = tl->disabled ? "Home sites open when members are waiting" : NULL;
      tl->act = (HudAction){HUD_ACT_GOAL, k_amenities[i].x, k_amenities[i].y, HUD_GOAL_EXTRAS, 0};
      n++;
    }
  }
  for (int i = 0; i < n; i++)   // Trees: a tree armed; Extras: an amenity armed
    if (tiles[i].selected && (tab == 3 || tab == 4 || (tab == 0 && s->terrain_tool == 1))) g_rotate = 1;   // green: its shape
  tray(c, h, "Course", tabs, 5, &tab, tiles, n, 8, s->amenities_on ? 248 : 261, s->amenities_on ? 567 : 578, dock_r, a);
  if (tab != before) {
    if (tab == 4) goal(a, HUD_GOAL_EXTRAS, -1, -1);
    else {
      h->course_tab = tab;
      if (s->amenities_on) goal(a, HUD_GOAL_COURSE, -1, -1);
    }
  }
}

static void buildings_tray(UiCtx *c, UiHud *h, const HudSnap *s, SDL_Texture *const *art, UiRect dock_r, Acts *a) {
  Tile tiles[9];
  char prices[9][24];
  for (int i = 0; i < 9; i++) {
    Tile *tl = &tiles[i];
    memset(tl, 0, sizeof *tl);
    int id = i + 6, lvl = s->building_level[i];
    tl->name = s->building_name[i][0] ? s->building_name[i] : k_building_default[i];
    price(prices[i], sizeof prices[i], s->building_cost[i]);
    tl->price = prices[i];
    tl->art = art ? art[HUD_ART_BUILDING + i] : NULL;
    tl->icon = UI_ICON_HOUSE;
    tl->selected = s->build_tool == id;
    tl->disabled = !ui_lot_enabled(i, s->unlock, lvl, s->next_hole);
    tl->rotates = building_rotates(id) && !tl->disabled;
    tl->tip = tl->disabled ? (lvl >= 2 ? "Fully upgraded" : "Not open yet: open more holes")
            : lvl == 1 ? (tl->rotates ? "Upgrade (can be rotated, Tab)" : "Upgrade")
            : tl->rotates ? "Can be rotated (Tab)" : "One view only: does not rotate";
    int x = i < 5 ? 383 + 80 * i : 423 + 80 * (i - 5), y = i < 5 ? 531 : 571;
    tl->act = (HudAction){HUD_ACT_GOAL, x, y, HUD_GOAL_BUILDINGS, 0};
  }
  for (int i = 0; i < 9; i++)   // a lot armed that golf can turn
    if (tiles[i].selected && building_rotates(i + 6)) g_rotate = 1;
  tray(c, h, "Buildings", NULL, 0, NULL, tiles, 9, 9, 277, 567, dock_r, a);
}

static void landscape_tray(UiCtx *c, UiHud *h, const HudSnap *s, SDL_Texture *const *art, UiRect dock_r, Acts *a) {
  Tile tiles[4];
  for (int i = 0; i < 4; i++) {
    Tile *tl = &tiles[i];
    memset(tl, 0, sizeof *tl);
    tl->name = k_elev_short[i];
    tl->price = i < 3 ? "- and = keys" : "";
    tl->tip = k_elevation[i].name;
    tl->art = art ? art[HUD_ART_ELEVATION + i] : NULL;
    tl->icon = k_elevation[i].icon;
    tl->selected = s->elev_edit && s->elevation_tool == k_elevation[i].tool;
    tl->act = (HudAction){HUD_ACT_GOAL, k_elevation[i].x, k_elevation[i].y, HUD_GOAL_LANDSCAPE, 0};
  }
  tray(c, h, "Landscape", NULL, 0, NULL, tiles, 4, 4, 267, 561, dock_r, a);
}

// task-92: the "Your pro" page stands in for the original's player panel (FUN_004362f0, hidden with the old
// HUD): create the pro, the profile, the skills, Practice Round, Match, Begin Tournament and, while the pro
// waits for a shot, the five shot choices. The buttons click the original ones (800x600 points of
// FUN_00435f00) or run golf's own profile page and skill dialog (hud_game.c, HUD_ACT_PRO).
static int pro_tiles(const HudSnap *s, Tile *t) {
  static char skills_txt[24];
  int n = 0, total = 0;
  for (int i = 0; i < 10; i++) total += s->pro_skill[i];
  if (!s->pro_ready) {
    t[n].name = "Edit pro";   // task-102c: the pro exists from the start (as in 2002); this sets them up before the first round
    t[n].price = "look, skills";
    t[n].icon = UI_ICON_SPARKLE;
    t[n].selected = 1;
    t[n].tip = "Name and look, then 10 skill points (3 more for each accomplishment). Practice asks for the points too";
    t[n++].act = (HudAction){HUD_ACT_PRO, HUD_PRO_CREATE, 0, 0, 0};
  }
  t[n].name = "Profile";
  t[n].price = s->pro_name[0] ? s->pro_name : "name, look";
  t[n].icon = UI_ICON_PALETTE;
  t[n].tip = "Your pro's name, look and bio";
  t[n++].act = (HudAction){HUD_ACT_PRO, HUD_PRO_PROFILE, 0, 0, 0};
  snprintf(skills_txt, sizeof skills_txt, s->pro_ready ? "%d points" : "not set up", total);
  t[n].name = "Skills";
  t[n].price = skills_txt;
  t[n].icon = UI_ICON_CHART;
  t[n].tip = s->pro_ready ? "Your pro's ten skills; accomplishments add three points each"
                          : "Spend your pro's first skill points";
  t[n++].act = (HudAction){HUD_ACT_PRO, HUD_PRO_SKILLS, 0, 0, 0};
  if (s->pro_shots) {   // the pro waits on the course: the shot for the next stroke
    static const struct { const char *name; int x, v, fancy; } shot[5] = {
        {"Straight", 414, 0, 0}, {"Fade", 493, -1, 1}, {"Draw", 572, 1, 1}, {"High backspin", 651, 3, 1}, {"Low punch", 730, 4, 0}};
    for (int i = 0; i < 5; i++) {
      t[n].name = shot[i].name;
      t[n].price = "shot";
      t[n].icon = UI_ICON_FLAG;
      t[n].selected = s->pro_shot == shot[i].v;
      t[n].disabled = shot[i].fancy && s->pro_shots == 1;
      t[n].tip = t[n].disabled ? "Not from this lie" : "Your pro's next shot";
      t[n++].act = (HudAction){HUD_ACT_CLICK, shot[i].x, 505, 0, 0};
    }
    return n;
  }
  t[n].name = "Practice";
  t[n].price = s->pro_out ? "on the course" : "play";
  t[n].icon = UI_ICON_FLAG;
  t[n].disabled = !s->pro_practice;
  t[n].tip = s->pro_out ? "Your pro is out on the course" : s->pro_practice ? (s->pro_ready ? "Send your pro out for a round"
                                                                                               : "Your pro's skills come first, then the round")
                                                                            : "Open two holes first";
  t[n++].act = (HudAction){HUD_ACT_CLICK, 313, 519, 0, 0};
  if (s->pro_match) {
    t[n].name = "Match";
    t[n].price = "vs a pro";
    t[n].icon = UI_ICON_TROPHY;
    t[n].tip = "Accept the famous golfer's challenge";
    t[n++].act = (HudAction){HUD_ACT_CLICK, 313, 548, 0, 0};
  }
  if (s->pro_tourney) {
    t[n].name = "Tournament";
    t[n].price = "SGA";
    t[n].icon = UI_ICON_CUP;
    t[n].tip = "Begin the SGA tournament";
    t[n++].act = (HudAction){HUD_ACT_CLICK, 313, 580, 0, 0};
  }
  return n;
}

// ------------------------------------------------------------------ People lists (task-94)
//
// The Golfers and Staff pages of the People tray stand in for the original's golfer list (4x4 cards with a
// mood face, FUN_00435760) and employee strip (8 portraits, FUN_00436e50): a panel above the camera pill
// with one row per golfer or employee, scrolling when it is long. A golfer row jumps the camera to them and
// opens their card (HUD_ACT_SHOW); an employee row selects them, and Fire, Move and Rename appear in the tray.
static const char *const k_job[8] = {"Club Pro", "Celebrity", "Ranger", "Marshall", "Groundskeeper", "Technician", "Soda Vendor", "Refresher"};
static const char *const k_mood_word[5] = {"Furious", "Unhappy", "Okay", "Happy", "Delighted"};

// Tests read where the rows and buttons of the People lists are (window pixels): one line when one moves.
static void people_log(const char *what, UiRect r) {
  static struct { const char *what; int x, y, w, h; } last[12];
  for (int i = 0; i < 12; i++) {
    if (last[i].what && strcmp(last[i].what, what)) continue;
    int x = (int)r.x, y = (int)r.y, w = (int)r.w, h = (int)r.h;
    if (last[i].what && last[i].x == x && last[i].y == y && last[i].w == w && last[i].h == h) return;
    last[i].what = what;
    last[i].x = x; last[i].y = y; last[i].w = w; last[i].h = h;
    fprintf(stderr, "hud: people %s at %d,%d size %dx%d\n", what, x, y, w, h); fflush(stderr);
    return;
  }
}

// the first row not cut off at the top of the list (the list scrolled when it changes)
static void people_row_log(const char *kind, int i, UiRect rr) {
  static int first[2] = {-1, -1};
  int k = kind[0] == 's';
  if (first[k] == i) return;
  first[k] = i;
  fprintf(stderr, "hud: people %s first row %d at %d,%d size %dx%d\n", kind, i, (int)rr.x, (int)rr.y, (int)rr.w, (int)rr.h); fflush(stderr);
}

static UiRect people_panel_frame(UiCtx *c, const char *title, float w, float rows_h, float head_extra, UiRect *view_out) {
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  float pad = ui_s(c, 12), head = ui_s(c, 34) + head_extra;
  float maxh = fminf(scr.h - ui_s(c, 300), ui_s(c, 520));
  if (maxh < ui_s(c, 220)) maxh = ui_s(c, 220);
  float view_h = fminf(rows_h, maxh - head - 2 * pad);
  if (view_h < ui_s(c, 40)) view_h = ui_s(c, 40);
  float h = pad + head + view_h + pad;
  float k = ui_anim(c, ui_id(title) ^ 0x71a1u, 1, t->motion_open);
  UiRect r = ui_rect_round(ui_anchor(scr, w, h, UI_BOTTOM_RIGHT, ui_s(c, 12), ui_s(c, 84) - (1 - k) * ui_s(c, 12)));
  {   // tests wait for the panel: one line when it comes up (not drawn for half a second before)
    static Uint32 seen;
    static char kind;
    Uint32 now = SDL_GetTicks();
    if (now - seen > 500 || kind != title[0]) fprintf(stderr, "hud: people panel up: %s\n", title); fflush(stderr);
    seen = now;
    kind = title[0];
  }
  ui_panel(c, r, ui_s(c, 18), UI_PANEL_READABLE);
  ui_text_in(c, ui_rect(r.x + pad + ui_s(c, 2), r.y + pad, w - 2 * pad, ui_s(c, 30)), UI_TYPE_HEADING, t->text_primary, title, 0);
  *view_out = ui_rect(r.x + pad, r.y + pad + head, w - 2 * pad, view_h);
  return r;
}

static void golfers_panel(UiCtx *c, const HudSnap *s, Acts *a) {
  const UiTokens *t = ui_tokens(c);
  float w = ui_s(c, 320), rh = ui_s(c, 40), rg = ui_s(c, 4), pad = ui_s(c, 12);
  char title[48];
  snprintf(title, sizeof title, "Golfers  (%d)", s->golfer_n);
  float rows_h = s->golfer_n ? s->golfer_n * (rh + rg) - rg : ui_s(c, 48);
  UiRect view;
  UiRect r = people_panel_frame(c, title, w, rows_h, ui_s(c, 40) + (s->waiting > 0 ? ui_s(c, 20) : 0), &view);
  // mood counts: one face per band with its count, how the course feels at a glance
  float cx = r.x + pad, cy = r.y + pad + ui_s(c, 34);
  float cell = (w - 2 * pad) / 5;
  for (int m = 0; m < 5; m++) {
    char n[12];
    snprintf(n, sizeof n, "%d", s->mood_count[m]);
    UiRect fr = ui_rect(cx + m * cell, cy, ui_s(c, 28), ui_s(c, 28));
    ui_face(c, m * 2 + 2, fr, ui_s(c, 26), s->mood_count[m] ? 1.0f : 0.45f);
    ui_text_in(c, ui_rect(fr.x + ui_s(c, 30), cy, cell - ui_s(c, 32), ui_s(c, 28)), UI_TYPE_NUMBER,
               s->mood_count[m] ? t->text_primary : t->text_secondary, n, 0);
    uint32_t mid = ui_idi(ui_id("hud.people.mood"), m);
    UiRect mr = ui_rect(cx + m * cell, cy, cell, ui_s(c, 28));
    ui_hit(c, mr, ui_s(c, 8), mid, UI_HIT_INTERACT);
    ui_tooltip(c, mid, mr, k_mood_word[m]);
  }
  if (s->waiting > 0) {
    char wt[48];
    snprintf(wt, sizeof wt, "%d waiting to join the club", s->waiting);
    ui_text_in(c, ui_rect(r.x + pad + ui_s(c, 2), cy + ui_s(c, 32), w - 2 * pad, ui_s(c, 18)), UI_TYPE_CAPTION, t->text_secondary, wt, 0);
  }
  if (!s->golfer_n) {
    ui_text_in(c, view, UI_TYPE_CAPTION, t->text_secondary, "No golfers on the course yet.", 0);
    return;
  }
  float off = ui_vscroll_begin(c, ui_id("hud.people.golfers.scroll"), view, rows_h, rh + rg);
  int first_done = 0;
  for (int i = 0; i < s->golfer_n; i++) {
    UiRect rr = ui_rect(view.x, view.y + off + i * (rh + rg), view.w, rh);
    if (rr.y + rr.h < view.y || rr.y > view.y + view.h) continue;
    const char *nm = s->golfer[i].name[0] ? s->golfer[i].name : "Golfer";
    if (!first_done && rr.y >= view.y - 1) { first_done = 1; people_row_log("golfer", i, rr); }
    uint32_t id = ui_idi(ui_id("hud.people.golfer"), s->golfer[i].slot);
    float rad = ui_s(c, 12);
    ui_hit(c, rr, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
    float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
    uid_rrect(ui_drawer(c), rr, rad, uid_mix(t->surface_inset, t->tile_fill_on, hov));
    uid_rrect_stroke(ui_drawer(c), rr, rad, ui_s(c, 1), t->surface_inset_border);
    ui_face(c, s->golfer[i].mood, ui_rect(rr.x + ui_s(c, 6), rr.y + (rh - ui_s(c, 28)) / 2, ui_s(c, 28), ui_s(c, 28)), ui_s(c, 26), 1.0f);
    char where[24];
    if (s->golfer[i].hole > 18) snprintf(where, sizeof where, "leaving");
    else snprintf(where, sizeof where, "Hole %d", s->golfer[i].hole);
    float ww = ui_text_width(c, UI_TYPE_CAPTION, where) + ui_s(c, 8);
    ui_text_fit(c, ui_rect(rr.x + ui_s(c, 40), rr.y, rr.w - ui_s(c, 48) - ww, rh), UI_TYPE_LABEL, t->text_primary, nm, 0);
    ui_text_in(c, ui_rect(rr.x + rr.w - ww - ui_s(c, 6), rr.y, ww, rh), UI_TYPE_CAPTION, t->text_secondary, where, 1);
    char tip[96];
    snprintf(tip, sizeof tip, "%s, %s. Click to show the golfer", nm, k_mood_word[(s->golfer[i].mood - 1) / 2]);
    ui_tooltip(c, id, rr, tip);
    if (ui_clicked(c, id)) act(a, (HudAction){HUD_ACT_SHOW, HUD_SHOW_GOLFER, s->golfer[i].slot, 0, 0});
  }
  ui_vscroll_end(c);
}

static void staff_panel(UiCtx *c, const HudSnap *s, Acts *a) {
  const UiTokens *t = ui_tokens(c);
  float w = ui_s(c, 320), rh = ui_s(c, 48), rg = ui_s(c, 4);
  char title[48];
  snprintf(title, sizeof title, "Staff  (%d of %d)", s->staff_n, s->staff_limit);
  float rows_h = s->staff_n ? s->staff_n * (rh + rg) - rg : ui_s(c, 48);
  UiRect view;
  UiRect r = people_panel_frame(c, title, w, rows_h, s->staff_n ? ui_s(c, 22) : 0, &view);
  if (s->staff_moving >= 0)
    ui_text_in(c, ui_rect(r.x + ui_s(c, 14), r.y + ui_s(c, 44), w - ui_s(c, 28), ui_s(c, 20)), UI_TYPE_CAPTION, t->accent_soft_text,
               "Click a tile on the course to place them", 0);
  else if (s->staff_n)   // the hint sits above the list (a tooltip would cover the row below the pointer)
    ui_text_in(c, ui_rect(r.x + ui_s(c, 14), r.y + ui_s(c, 44), w - ui_s(c, 28), ui_s(c, 20)), UI_TYPE_CAPTION, t->text_secondary,
               "Pick someone, then Fire, Move or Rename", 0);
  if (!s->staff_n) {
    ui_text_in(c, view, UI_TYPE_CAPTION, t->text_secondary, "Nobody hired yet. Use Hire below.", 0);
    return;
  }
  float off = ui_vscroll_begin(c, ui_id("hud.people.staff.scroll"), view, rows_h, rh + rg);
  int first_done = 0;
  for (int i = 0; i < s->staff_n; i++) {
    UiRect rr = ui_rect(view.x, view.y + off + i * (rh + rg), view.w, rh);
    if (rr.y + rr.h < view.y || rr.y > view.y + view.h) continue;
    int sel = s->staff[i].rec == s->staff_sel;
    if (!first_done && rr.y >= view.y - 1) { first_done = 1; people_row_log("staff", i, rr); }
    uint32_t id = ui_idi(ui_id("hud.people.emp"), s->staff[i].rec);
    float rad = ui_s(c, 12);
    ui_hit(c, rr, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
    float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
    if (sel) uid_rrect(ui_drawer(c), ui_inset_all(rr, -ui_s(c, 2)), rad + ui_s(c, 2), t->accent);
    uid_rrect(ui_drawer(c), rr, rad, sel ? t->tile_fill_on : uid_mix(t->surface_inset, t->tile_fill_on, hov));
    if (!sel) uid_rrect_stroke(ui_drawer(c), rr, rad, ui_s(c, 1), t->surface_inset_border);
    ui_icon(c, UI_ICON_USERS, ui_rect(rr.x + ui_s(c, 8), rr.y + (rh - ui_s(c, 26)) / 2, ui_s(c, 26), ui_s(c, 26)), ui_s(c, 22), t->accent_soft_text);
    char wage[32], wm[24];
    ui_hud_money(wm, sizeof wm, s->staff[i].wage);
    snprintf(wage, sizeof wage, "%s/wk", wm);
    float ww = ui_text_width(c, UI_TYPE_CAPTION, wage) + ui_s(c, 8);
    float tx = rr.x + ui_s(c, 42), tw = rr.w - ui_s(c, 50) - ww;
    UiFont nf = ui_font(c, UI_TYPE_LABEL), cf = ui_font(c, UI_TYPE_CAPTION);
    float y0 = rr.y + (rh - nf.line - cf.line) / 2;
    ui_text_fit(c, ui_rect(tx, y0, tw, nf.line), UI_TYPE_LABEL, t->text_primary, s->staff[i].name[0] ? s->staff[i].name : "Employee", 0);
    ui_text_fit(c, ui_rect(tx, y0 + nf.line, tw, cf.line), UI_TYPE_CAPTION, t->text_secondary, k_job[s->staff[i].job & 7], 0);
    ui_text_in(c, ui_rect(rr.x + rr.w - ww - ui_s(c, 6), rr.y, ww, rh), UI_TYPE_CAPTION, t->text_secondary, wage, 1);
    if (ui_clicked(c, id)) act(a, (HudAction){HUD_ACT_STAFF, HUD_STAFF_SELECT, s->staff[i].rec, 0, 0});
  }
  ui_vscroll_end(c);
}

static void people_tray(UiCtx *c, UiHud *h, const HudSnap *s, UiRect dock_r, Acts *a) {
  static const char *const tabs[3] = {"Golfers", "Your pro", "Staff"};
  int tab = s->employee_overlay ? 2 : s->dock_mode == 2 ? 0 : 1, before = tab;
  Tile tiles[9];
  memset(tiles, 0, sizeof tiles);
  int n = 0;
  if (tab == 2) {
    tiles[0].name = "Hire";
    tiles[0].price = "staff";
    tiles[0].icon = UI_ICON_PLUS;
    tiles[0].tip = "Hire a groundskeeper, ranger, club pro or caddie";
    tiles[0].act = (HudAction){HUD_ACT_CLICK, 270, 558, 0, 0};
    tiles[0].labelled = 1;
    g_tile_wmul = 1.7f;
    n = 1;
    if (s->staff_sel >= 0) {   // task-94: the selected employee's three controls (golf's Fire, Move, Rename buttons)
      static char fire_txt[24];
      ui_hud_money(fire_txt, sizeof fire_txt, s->staff_fire_cost);
      tiles[0].labelled = 1;
      tiles[1] = (Tile){"Fire", fire_txt, "Let this employee go", NULL, UI_ICON_TRASH, 0, 0, 1, 0, {HUD_ACT_STAFF, HUD_STAFF_FIRE, 0, 0, 0}};
      tiles[2] = (Tile){"Move", "then click", "Pick them up, then click a tile on the course", NULL, UI_ICON_PIN, s->staff_moving >= 0, 0, 1, 0,
                        {HUD_ACT_STAFF, HUD_STAFF_MOVE, 0, 0, 0}};
      tiles[3] = (Tile){"Rename", "name", "Give this employee a new name", NULL, UI_ICON_PALETTE, 0, 0, 1, 0, {HUD_ACT_STAFF, HUD_STAFF_RENAME, 0, 0, 0}};
      n = 4;
    }
    staff_panel(c, s, a);
  } else if (tab == 0) {
    golfers_panel(c, s, a);
  } else if (tab == 1) {
    n = pro_tiles(s, tiles);
    for (int i = 0; i < n; i++) tiles[i].labelled = 1;
    g_tile_wmul = 1.7f;
  }
  tray(c, h, "People", tabs, 3, &tab, tiles, n, 4, -1, -1, dock_r, a);
  g_tile_wmul = 1;
  if (tab != before) {
    if (tab == 2) click(a, s->dock_mode == 2 ? 232 : 231, s->dock_mode == 2 ? 538 : 540);
    else if (s->employee_overlay) click(a, tab == 0 ? 286 : 256, tab == 0 ? 492 : 510);
    else if (tab == 0) click(a, 286, 490);
    else click(a, 256, 510);
  }
}

// ------------------------------------------------------------------ messages (task-62, jwe)
//
// The game's news strip shows one message for a few seconds and says nothing about it after. Here the
// card at the top right wraps it to three lines (the rest cut with "..." and a click reads it all),
// a click opens it in full, with a button that jumps the camera to the golfer or hole it is about
// when the game knows one (hud_game.c: HUD_ACT_SHOW), and the bell in the stats cluster opens the
// history of the last HUD_MSG_MAX messages with their time, scrollable. Esc or a click on the course
// closes both (hud_frosted.c).

// Word-wrap s into up to max lines of width w: start/len per line; *more set when text remains.
static int wrap_lines(UiCtx *c, int style, const char *s, float w, const char **start, int *len, int max, int *more) {
  UiFont f = ui_font(c, style);
  int n = 0;
  const char *p = s;
  while (*p == ' ') p++;
  while (*p && n < max) {
    int fit = uit_fit(ui_texter(c), f.weight, f.px, p, f.tracking, w);
    if (fit <= 0) fit = 1;
    int l = fit;
    while (l > 0 && p[l - 1] == ' ') l--;
    start[n] = p;
    len[n] = l;
    n++;
    p += fit;
    while (*p == ' ') p++;
  }
  *more = *p != 0;
  return n;
}

static void line_text(UiCtx *c, UiRect r, int style, UiColor col, const char *s, int len) {
  char buf[256];
  snprintf(buf, sizeof buf, "%.*s", len < 255 ? len : 255, s);
  ui_text_in(c, r, style, col, buf, 0);
}

// "March 2001 - 2 min ago"
static void msg_when(UiCtx *c, const HudMsg *m, char *out, int cap) {
  double sec = (ui_now(c) - m->t_ms) / 1000.0;
  char ago[24];
  if (sec < 10) snprintf(ago, sizeof ago, "just now");
  else if (sec < 60) snprintf(ago, sizeof ago, "%d s ago", (int)sec);
  else if (sec < 3600) snprintf(ago, sizeof ago, "%d min ago", (int)(sec / 60));
  else snprintf(ago, sizeof ago, "%d h ago", (int)(sec / 3600));
  if (m->date[0]) snprintf(out, cap, "%s  -  %s", m->date, ago);
  else snprintf(out, cap, "%s", ago);
}

static const char *show_label(const HudMsg *m) {
  return m->show == HUD_SHOW_GOLFER ? "Show the golfer" : m->show == HUD_SHOW_BUILDING ? "Show the building"
       : m->show == HUD_SHOW_STAFF ? "Open staff" : "Show the hole";
}

static const HudMsg *find_msg(const HudSnap *s, int seq) {
  for (int i = 0; i < s->msg_n; i++)
    if (s->msgs[i].seq == seq) return &s->msgs[i];
  return NULL;
}

// The card under the stats cluster: the strip's message, or the one the player clicked open.
static void news_card(UiCtx *c, UiHud *hud, const HudSnap *s, Acts *a) {
  const UiTokens *t = ui_tokens(c);
  if (s->news_seq != hud->last_news) {   // a new message replaces the one open in full
    if (hud->msg_open && s->news_seq) hud->msg_open = s->news_seq;
    hud->last_news = s->news_seq;
  }
  if (hud->hist_open) return;
  int want = hud->msg_open ? hud->msg_open : (s->news_on ? s->news_seq : 0);
  const HudMsg *m = want ? find_msg(s, want) : NULL;
  HudMsg tmp;
  if (!m && !hud->msg_open && s->news_on && s->news[0]) {   // no history entry (not seen arriving): the strip's text
    memset(&tmp, 0, sizeof tmp);
    snprintf(tmp.text, sizeof tmp.text, "%s", s->news);
    tmp.t_ms = ui_now(c);
    m = &tmp;
  }
  if (!m) {
    hud->msg_open = 0;
    return;
  }
  int open = hud->msg_open != 0;
  UiRect scr = ui_screen(c);
  float mg = ui_s(c, 14), top = ui_s(c, 14 + 40 + 12), pad = ui_s(c, 12), d = ui_s(c, 30), gap = ui_s(c, 10);
  float w = fminf(ui_s(c, open ? 440 : 400), scr.w - 2 * mg);
  float tw = w - 2 * pad - d - gap - (open ? ui_s(c, 28) : 0);
  UiFont cf = ui_font(c, UI_TYPE_CAPTION), lf = ui_font(c, UI_TYPE_LABEL);
  const char *st[16];
  int ln[16], more = 0;
  int n = wrap_lines(c, UI_TYPE_CAPTION, m->text, tw, st, ln, open ? 14 : 3, &more);
  int btn = (open || m->show == HUD_SHOW_STAFF) && m->show != HUD_SHOW_NONE;   // the weed card (task-112) has its button at once
  float btn_h = ui_s(c, 30);
  float h = 2 * pad + n * cf.line + (open ? lf.line + ui_s(c, 6) : 0) + (btn ? ui_s(c, 10) + btn_h : 0);
  h = fmaxf(h, d + 2 * pad);
  float k = ui_anim(c, ui_id("hud.newscard"), 1, t->motion_open);
  UiRect r = ui_rect_round(ui_anchor(scr, w, h, UI_TOP_RIGHT, mg, top - (1 - k) * ui_s(c, 12)));
  float rad = ui_s(c, 16);
  ui_panel(c, r, rad, UI_PANEL_READABLE);
  uint32_t cid = ui_id("hud.newscard.hit");
  if (!open) ui_hit(c, r, rad, cid, UI_HIT_BLOCK | UI_HIT_INTERACT);
  float x = r.x + pad, y = r.y + pad;
  UiRect disc = ui_rect(x, r.y + pad, d, d);
  uid_rrect(ui_drawer(c), disc, d / 2, t->accent_soft);
  ui_icon(c, UI_ICON_BELL, disc, ui_s(c, 18), t->accent_soft_text);
  x += d + gap;
  if (open) {
    char when[64];
    msg_when(c, m, when, sizeof when);
    ui_text_in(c, ui_rect(x, y, tw, lf.line), UI_TYPE_LABEL, t->text_secondary, when, 0);
    if (ui_icon_button(c, ui_id("hud.newscard.close"), ui_rect(r.x + r.w - pad - ui_s(c, 26), r.y + ui_s(c, 8), ui_s(c, 26), ui_s(c, 26)),
                       UI_ICON_X, UI_COMPACT, "Close"))
      hud->msg_open = 0;
    y += lf.line + ui_s(c, 6);
  }
  for (int i = 0; i < n; i++) {
    UiRect lr = ui_rect(x, y + i * cf.line, tw, cf.line);
    if (!open && i == n - 1 && more) {   // cut mid-text: end the last line with "..." inside the width
      char buf[256];
      int l = ln[i] < 240 ? ln[i] : 240;
      snprintf(buf, sizeof buf, "%.*s", l, st[i]);
      while (l > 0 && ui_text_width(c, UI_TYPE_CAPTION, buf) + ui_text_width(c, UI_TYPE_CAPTION, "...") > tw) {
        l--;
        while (l > 0 && buf[l - 1] == ' ') l--;
        buf[l] = 0;
      }
      strncat(buf, "...", sizeof buf - strlen(buf) - 1);
      ui_text_in(c, lr, UI_TYPE_CAPTION, t->text_primary, buf, 0);
    } else {
      line_text(c, lr, UI_TYPE_CAPTION, t->text_primary, st[i], ln[i]);
    }
  }
  if (btn) {
    UiRect br = ui_rect(x, r.y + r.h - pad - btn_h, ui_s(c, 150), btn_h);
    if (ui_button(c, ui_id("hud.newscard.show"), br, show_label(m), m->show == HUD_SHOW_STAFF ? UI_ICON_USERS : UI_ICON_EYE, UI_BTN_PRIMARY, UI_COMPACT)) {
      act(a, (HudAction){HUD_ACT_SHOW, m->show, m->target, 0, 0});
      hud->msg_open = 0;
      if (m->show == HUD_SHOW_STAFF) {   // the tray must show: the jwe build bar opens, a tucked tray comes back
        hud->bar_open = 1;
        hud->tray_hidden = 0;
      }
    }
  }
  if (!open) {
    ui_tooltip(c, cid, r, more ? "Click to read the whole message" : "Click to open the message");
    if (ui_clicked(c, cid)) hud->msg_open = m->seq ? m->seq : 0;
  }
}

// The bell's history: newest first, each with its time and its full text, the wheel scrolls.
static void history_panel(UiCtx *c, UiHud *hud, const HudSnap *s, Acts *a) {
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  float mg = ui_s(c, 14), top = ui_s(c, 14 + 40 + 10), pad = ui_s(c, 12), rp = ui_s(c, 10), rg = ui_s(c, 6);
  float w = fminf(ui_s(c, 430), scr.w - 2 * mg);
  float head = ui_s(c, 34);
  float maxh = fminf(scr.h - top - ui_s(c, 90), ui_s(c, 560));
  float tw = w - 2 * pad - 2 * rp - ui_s(c, 22);
  UiFont cf = ui_font(c, UI_TYPE_CAPTION);
  float line = cf.line, when_h = line + ui_s(c, 2);
  // row heights, newest first
  float rows_h = 0, rh[HUD_MSG_MAX];
  int nl[HUD_MSG_MAX];
  const char *st[HUD_MSG_MAX][12];
  int ln[HUD_MSG_MAX][12];
  for (int i = 0; i < s->msg_n; i++) {
    const HudMsg *m = &s->msgs[s->msg_n - 1 - i];
    int more;
    nl[i] = wrap_lines(c, UI_TYPE_CAPTION, m->text, tw, st[i], ln[i], 12, &more);
    rh[i] = 2 * rp + when_h + nl[i] * line;
    rows_h += rh[i] + (i ? rg : 0);
  }
  if (!s->msg_n) rows_h = ui_s(c, 48);
  float view_h = fminf(rows_h, maxh - head - 2 * pad);
  float h = pad + head + view_h + pad;
  float k = ui_anim(c, ui_id("hud.history"), 1, t->motion_open);
  UiRect r = ui_rect_round(ui_anchor(scr, w, h, UI_TOP_RIGHT, mg, top - (1 - k) * ui_s(c, 12)));
  ui_panel(c, r, ui_s(c, 18), UI_PANEL_READABLE);
  char cnt[40];
  snprintf(cnt, sizeof cnt, "Messages  (%d)", s->msg_n);
  ui_text_in(c, ui_rect(r.x + pad + ui_s(c, 2), r.y + pad, w - 2 * pad - ui_s(c, 40), head), UI_TYPE_HEADING, t->text_primary, cnt, 0);
  if (ui_icon_button(c, ui_id("hud.history.close"), ui_rect(r.x + r.w - pad - ui_s(c, 30), r.y + pad + (head - ui_s(c, 30)) / 2, ui_s(c, 30), ui_s(c, 30)),
                     UI_ICON_X, UI_COMPACT, "Close"))
    hud->hist_open = 0;
  UiRect view = ui_rect(r.x + pad, r.y + pad + head, w - 2 * pad, view_h);
  if (!s->msg_n) {
    ui_text_in(c, view, UI_TYPE_CAPTION, t->text_secondary, "No messages yet. News from the club shows up here.", 0);
    return;
  }
  float off = ui_vscroll_begin(c, ui_id("hud.history.scroll"), view, rows_h, ui_s(c, 48));
  {   // one line when the list scrolls (tests read it; the pixel difference of a scroll depends on the rows)
    static int last_off;
    if ((int)-off != last_off) {
      last_off = (int)-off;
      fprintf(stderr, "hud: history scroll %d of %d px\n", last_off, (int)fmaxf(0, rows_h - view_h));
    }
  }
  float y = view.y + off;
  for (int i = 0; i < s->msg_n; i++) {
    const HudMsg *m = &s->msgs[s->msg_n - 1 - i];
    UiRect rr = ui_rect(view.x, y, view.w, rh[i]);
    y += rh[i] + rg;
    if (rr.y + rr.h < view.y || rr.y > view.y + view.h) continue;
    uint32_t id = ui_idi(ui_id("hud.history.row"), m->seq);
    float rad = ui_s(c, 12);
    int target = m->show != HUD_SHOW_NONE;
    if (target) ui_hit(c, rr, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
    float hov = target ? ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover) : 0;
    uid_rrect(ui_drawer(c), rr, rad, uid_mix(t->surface_inset, t->tile_fill_on, hov));
    uid_rrect_stroke(ui_drawer(c), rr, rad, ui_s(c, 1), t->surface_inset_border);
    char when[64];
    msg_when(c, m, when, sizeof when);
    ui_text_in(c, ui_rect(rr.x + rp, rr.y + rp, tw, line), UI_TYPE_CAPTION, t->text_secondary, when, 0);
    for (int l = 0; l < nl[i]; l++)
      line_text(c, ui_rect(rr.x + rp, rr.y + rp + when_h + l * line, tw, line), UI_TYPE_CAPTION, t->text_primary, st[i][l], ln[i][l]);
    if (target) {
      ui_icon(c, UI_ICON_EYE, ui_rect(rr.x + rr.w - rp - ui_s(c, 18), rr.y + rp, ui_s(c, 18), ui_s(c, 18)), ui_s(c, 16), t->accent_soft_text);
      ui_tooltip(c, id, rr, show_label(m));
      if (ui_clicked(c, id)) {
        act(a, (HudAction){HUD_ACT_SHOW, m->show, m->target, 0, 0});
        hud->hist_open = 0;
      }
    }
  }
  ui_vscroll_end(c);
}

static void news_ui(UiCtx *c, UiHud *hud, const HudSnap *s, Acts *a) {
  if (s->msg_n && s->msgs[s->msg_n - 1].seq > hud->seen_seq && hud->hist_open) hud->seen_seq = s->msgs[s->msg_n - 1].seq;
  if (hud->hist_open) history_panel(c, hud, s, a);
  else news_card(c, hud, s, a);
}

// The whole HUD is hidden (task-57): one small pill that says how to bring it back, clickable too.
static void hidden_hint(UiCtx *c, UiHud *h) {
  const UiTokens *t = ui_tokens(c);
  char txt[64];
  if (h->hide_key[0]) snprintf(txt, sizeof txt, "UI hidden. Press %s to show", h->hide_key);
  else snprintf(txt, sizeof txt, "UI hidden. Click to show");
  float ph = ui_s(c, 26), pw = ui_text_width(c, UI_TYPE_CAPTION, txt) + 2 * ui_s(c, 12);
  UiRect r = ui_rect_round(ui_anchor(ui_screen(c), pw, ph, UI_BOTTOM_LEFT, ui_s(c, 14), ui_s(c, 14)));
  ui_panel(c, r, ph / 2, UI_PANEL_NOSHADOW);
  uint32_t id = ui_id("hud.showhint");
  ui_hit(c, r, ph / 2, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
  ui_text_in(c, r, UI_TYPE_CAPTION, t->text_secondary, txt, 0.5f);
  if (ui_clicked(c, id)) h->hidden = 0;
}

// ------------------------------------------------------------------ placement panel (task-96, task-97)

// Floating panel while a piece with a choice of designs is armed (bench, flower bed, landmark, scenic
// tree, scenic bridge) and while the clubhouse waits for its spot at the start of a course. The original
// picked the design by hovering a strip, which a parked pointer never does; here a chip click writes it
// (HUD_ACT_DESIGN). Rotate is the Tab key as a button. Independent of the tray, so it stays when the tray tucks away.
static int place_chip_flags(const HudSnap *s, int tool, int i) {
  int f = s->design == i ? UI_SELECTED : 0;
  if (tool == 4 && !((s->landmark_mask >> i) & 1u)) f |= UI_DISABLED;
  return f;
}

static void place_panel(UiCtx *c, UiHud *h, const HudSnap *s, SDL_Texture *const *art, Acts *a) {
  int club = s->clubhouse, n = club ? 0 : s->design_n, tool = s->build_tool;
  int lot = !club && n <= 0 && tool >= 6 && tool <= 14 && building_rotates(tool);   // task-115: a lot that turns
  if (!club && n <= 0 && !lot) return;
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  const char *title = club ? "Place your clubhouse"
                    : lot ? (s->building_name[tool - 6][0] ? s->building_name[tool - 6] : k_building_default[tool - 6])
                    : tool == 1 ? "Bench design" : tool == 2 ? "Flower bed design" : tool == 4 ? "Landmark"
                    : tool == 16 ? "Scenic tree" : "Scenic bridge";
  char hint[160] = "";
  if (club || lot) snprintf(hint, sizeof hint, "Click the course to set it down. Turn it with Rotate or the Tab key.");
  else if (tool == 4 && (s->design < 0 || s->design >= n)) snprintf(hint, sizeof hint, "Pick a landmark, then click the course.");
  else if (tool == 4) {
    char cost[24];
    if ((s->landmark_free >> s->design) & 1u) snprintf(cost, sizeof cost, "free");
    else ui_hud_money(cost, sizeof cost, (s->design * 5 + 25) * 200);
    snprintf(hint, sizeof hint, "%s, %s", s->landmark_name[s->design], cost);
  } else snprintf(hint, sizeof hint, "Pick a design, then click the course.");
  float pad = ui_s(c, 12), segh = ui_s(c, 34), g = ui_s(c, 6), cw = ui_s(c, 58), ch = ui_s(c, 58);
  float title_w = ui_text_width(c, UI_TYPE_HEADING, title) + ui_s(c, 14), rw = rot_width(c);
  float max_w = scr.w - 2 * ui_s(c, t->edge_margin);
  int per = n;
  if (per > 1 && per * cw + (per - 1) * g + 2 * pad > max_w) per = (n + 1) / 2;   // two rows on a narrow screen
  int rows = n > 0 ? (n + per - 1) / per : 0;
  float chips_w = n > 0 ? per * cw + (per - 1) * g : 0;
  float hint_w = ui_text_width(c, UI_TYPE_BODY, hint);
  float inner = fmaxf(fmaxf(chips_w, title_w + ui_s(c, 16) + rw), hint_w);
  float hh = pad + segh + ui_s(c, 8) + (rows ? rows * ch + (rows - 1) * g + ui_s(c, 8) : 0) +
             ui_font(c, UI_TYPE_BODY).line + pad;
  float w = inner + 2 * pad;
  float margin = h->density == HUD_JWE ? ui_s(c, 160) : ui_s(c, 250);
  float k = ui_anim(c, ui_id("hud.place") ^ 0x91ace1u, 1, t->motion_open);
  UiRect r = ui_rect_round(ui_anchor(scr, w, hh, UI_BOTTOM, 0, margin - (1 - k) * ui_s(c, 16)));
  ui_panel(c, r, ui_s(c, 20), 0);
  float x = r.x + pad, y = r.y + pad;
  ui_text_in(c, ui_rect(x + ui_s(c, 2), y, title_w, segh), UI_TYPE_HEADING, t->text_primary, title, 0);
  UiRect rr = ui_rect(r.x + r.w - pad - rw, y, rw, segh);
  uint32_t rid = ui_id("hud.place.rotate");
  if (club && getenv("SIMGOLF_FRONTEND_LOG")) {
    static int old_x = -1, old_y = -1;
    int rx = (int)(rr.x + rr.w / 2), ry = (int)(rr.y + rr.h / 2);
    if (rx != old_x || ry != old_y) {
      old_x = rx, old_y = ry;
      fprintf(stderr, "hud: clubhouse rotate at %d,%d target %.0fx%.0f\n", rx, ry, scr.w, scr.h);
    }
  }
  if (ui_button(c, rid, rr, k_rot_label, UI_ICON_ROTR, UI_BTN_PILL, 0)) act(a, (HudAction){HUD_ACT_KEY, 0, 0, 0, 9});
  ui_tooltip(c, rid, rr, "Rotate what you are placing (the Tab key does the same)");
  y += segh + ui_s(c, 8);
  for (int i = 0; i < n; i++) {
    UiRect cr = ui_rect(x + (i % per) * (cw + g), y + (i / per) * (ch + g), cw, ch);
    char lab[8], tip[80];
    snprintf(lab, sizeof lab, "%d", i + 1);
    int f = place_chip_flags(s, tool, i);
    if (tool == 4) {
      if (f & UI_DISABLED) snprintf(tip, sizeof tip, "%s: not earned yet", s->landmark_name[i]);
      else snprintf(tip, sizeof tip, "%s", s->landmark_name[i]);
    } else snprintf(tip, sizeof tip, "%s %d", title, i + 1);
    int slot = tool == 1 ? HUD_ART_BENCH : tool == 2 ? HUD_ART_FLOWER : tool == 4 ? HUD_ART_LANDMARK : tool == 16 ? HUD_ART_TREE : HUD_ART_BRIDGE;
    SDL_Texture *pic = art ? art[slot + i] : NULL;   // the design as the original strip drew it; a number if none
    int hit = pic ? ui_tile_mini(c, ui_idi(ui_id("hud.place"), i), cr, pic, UI_ICON_NONE, f, tip)
                  : ui_tile_mini_label(c, ui_idi(ui_id("hud.place"), i), cr, (f & UI_DISABLED) ? UI_ICON_LOCK : UI_ICON_NONE, lab, f, tip);
    if (hit && !(f & UI_DISABLED))
      act(a, (HudAction){HUD_ACT_DESIGN, tool, i, 0, 0});
  }
  y += rows ? rows * ch + (rows - 1) * g + ui_s(c, 8) : 0;
  ui_text_in(c, ui_rect(x + ui_s(c, 2), y, inner, ui_font(c, UI_TYPE_BODY).line), UI_TYPE_BODY, t->text_secondary, hint, 0);
}

static void shot_readout(UiCtx *c, const HudSnap *s) {
  if (!s->shot_readout) return;
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  float pad = ui_s(c, 16), row = ui_s(c, 26), head = ui_s(c, 30);
  float w = fminf(ui_s(c, 380), scr.w - 2 * pad), h = 2 * pad + head + SHOT_READOUT_LINES * row;
  UiRect r = ui_rect_round(ui_rect(scr.x + scr.w - pad - w, scr.y + scr.h - ui_s(c, 80) - h, w, h));
  ui_panel(c, r, ui_s(c, 20), UI_PANEL_STRONG);
  ui_hit(c, r, ui_s(c, 20), ui_id("hud.shot.readout"), UI_HIT_BLOCK);
  ui_text_in(c, ui_rect(r.x + pad, r.y + pad, w - 2 * pad, head), UI_TYPE_LABEL, t->text_secondary, "Next shot", 0);
  for (int i = 0; i < SHOT_READOUT_LINES; i++) {
    char line[SHOT_READOUT_TEXT + 2];
    const char *colon = strchr(s->shot_text[i], ':');
    if (colon && colon[1] && colon[1] != ' ')
      snprintf(line, sizeof line, "%.*s: %s", (int)(colon - s->shot_text[i]), s->shot_text[i], colon + 1);
    else snprintf(line, sizeof line, "%s", s->shot_text[i]);
    ui_text_fit(c, ui_rect(r.x + pad, r.y + pad + head + i * row, w - 2 * pad, row), UI_TYPE_BODY,
                t->text_primary, line, 0);
  }
  if (getenv("SIMGOLF_FRONTEND_LOG")) {
    static char last[SHOT_READOUT_LINES][SHOT_READOUT_TEXT];
    if (memcmp(last, s->shot_text, sizeof last)) {
      memcpy(last, s->shot_text, sizeof last);
      fprintf(stderr, "hud: shot readout target %.0fx%.0f at %.0f,%.0f size %.0fx%.0f [%s|%s|%s|%s]\n",
              scr.w, scr.h, r.x, r.y, r.w, r.h, s->shot_text[0], s->shot_text[1], s->shot_text[2], s->shot_text[3]);
    }
  }
}

int ui_hud_draw(UiCtx *c, UiHud *h, const HudSnap *s, SDL_Texture *const *art, HudAction *out, int max_out) {
  Acts a = {out, 0, max_out};
  if (!s->visible) return 0;
  if (h->hidden) {
    hidden_hint(c, h);
    return 0;
  }
  if (h->density < 0 || h->density > HUD_JWE) h->density = 0;
  g_s = s;
  g_h = h;
  if (h->density == 0) h->tray_hidden = 0;
  if (active_item(s) < 0) h->tray_hidden = 0;
  int own_stats = h->density == HUD_WHEEL_STATS || h->density == HUD_CARD || h->density == HUD_CARD_RIGHT ||
                  h->density == HUD_LSHAPE || h->density == HUD_COLLAPSED ||
                  h->density >= HUD_BARS;   // these carry money and date themselves
  if (s->capsule_visible && !own_stats) capsule(c, h, s, art, &a);
  if (h->density != HUD_JWE) toast(c, h, s);   // jwe: news_ui below (task-62)
  camera(c, h, &a);
  int tray_open = !h->tray_hidden && active_item(s) >= 0 && active_item(s) <= D_PEOPLE &&
                  (h->density != HUD_JWE || h->bar_open);   // jwe: the tray belongs to the open build bar
  UiRect dock_r = ui_rect(0, 0, 0, 0);
  if (h->density >= HUD_BARS) {
    bars(c, h, s, art, &a);
  } else if (h->density >= HUD_CARD) {
    dock_r = corner(c, h, s, art, &a);
  } else if (h->density >= HUD_WHEEL) {
    dock_r = hub(c, h, s, art, &a);
  } else if (h->density == 1 || h->density == 3) {
    // the strip: dock alone, or (compact) dock plus the open category
    if (h->density == 3 || !tray_open) strip(c, NULL, NULL, 0, NULL, NULL, -1, -1, -1, &a);
  } else {
    dock_r = dock(c, h, s, &a);
  }
  if (tray_open) switch (active_item(s)) {
    case D_COURSE: course_tray(c, h, s, art, dock_r, &a); break;
    case D_BUILD: buildings_tray(c, h, s, art, dock_r, &a); break;
    case D_LAND: landscape_tray(c, h, s, art, dock_r, &a); break;
    case D_PEOPLE: people_tray(c, h, s, dock_r, &a); break;
  }
  place_panel(c, h, s, art, &a);
  shot_readout(c, s);
  if (h->density == HUD_JWE) news_ui(c, h, s, &a);
  return a.n;
}
