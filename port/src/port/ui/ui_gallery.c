// ui_gallery: every Frosted Mint kit component rendered to PNGs (task-26).
//
//   ui_gallery.exe [--out DIR] [--fonts DIR] [--tokens FILE] [--backdrop FILE.bmp]
//                  [--sizes 1920x1080,3840x2160] [--renderer software|direct3d11]
//
// Pages: hud (the course HUD over a course picture), components (every other
// component), dialog (a dialog over its scrim). Default renderer: SDL's
// software renderer on an off-screen surface: no window at all. direct3d11
// opens a hidden window (never shown) to check the GPU path the game uses.
// The backdrop is a 24-bit BMP (an hdshot of the course) or, without one, a
// drawn stand-in (grass, fairways, water, trees) so the frost has detail.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/ui/ui_dialogs.h"
#include "port/ui/ui_frontend.h"
#include "port/ui/ui_golfers.h"
#include "port/ui/ui_hud.h"
#include "port/ui/ui_kit.h"
#include "port/ui/ui_mods.h"
#include "port/reports_frosted.h"
#include "port/ui/ui_png.h"

static SDL_Surface *load_bmp(const char *path) {
  SDL_Surface *s = path ? SDL_LoadBMP(path) : NULL;
  if (path && !s) fprintf(stderr, "gallery: %s: %s\n", path, SDL_GetError());
  return s;
}

// A stand-in course: rolling grass, two fairways, a pond, sand, trees.
static void draw_course(UiDraw *d, int w, int h) {
  uid_rect_vgrad3(d, ui_rect(0, 0, (float)w, (float)h), (UiColor){92, 160, 70, 255}, (UiColor){108, 176, 78, 255},
                  (UiColor){84, 150, 64, 255});
  float s = h / 1080.0f;
  unsigned seed = 7;
#define RND() ((seed = seed * 1103515245u + 12345u) >> 8 & 0xffff) / 65535.0f
  for (int i = 0; i < 180; i++) {   // mottled grass
    float x = RND() * w, y = RND() * h, r = (20 + RND() * 80) * s;
    uid_rrect(d, ui_rect(x - r, y - r * 0.6f, 2 * r, 1.2f * r), r, (UiColor){70, 140, 56, (uint8_t)(40 + RND() * 50)});
  }
  for (int k = 0; k < 2; k++)   // fairways
    for (int i = 0; i < 14; i++) {
      float t = i / 13.0f, x = (k ? 0.25f : 0.62f) * w + sinf(t * 3 + k) * 160 * s + t * 300 * s,
            y = (0.12f + t * 0.7f) * h, r = 70 * s;
      uid_rrect(d, ui_rect(x - r * 1.4f, y - r, 2.8f * r, 2 * r), r, (UiColor){150, 205, 96, 255});
    }
  uid_rrect(d, ui_rect(0.40f * w, 0.30f * h, 360 * s, 220 * s), 110 * s, (UiColor){52, 120, 170, 255});   // pond
  uid_rrect(d, ui_rect(0.43f * w, 0.34f * h, 250 * s, 120 * s), 60 * s, (UiColor){70, 145, 196, 255});
  uid_rrect(d, ui_rect(0.70f * w, 0.55f * h, 140 * s, 80 * s), 40 * s, (UiColor){236, 222, 170, 255});   // sand
  for (int i = 0; i < 90; i++) {   // trees
    float x = RND() * w, y = RND() * h, r = (14 + RND() * 16) * s;
    uid_rrect_shadow(d, ui_rect(x - r, y - r, 2 * r, 2 * r), r, 6 * s, 8 * s, 10 * s, (UiColor){0, 30, 0, 90});
    uid_rrect(d, ui_rect(x - r, y - r, 2 * r, 2 * r), r, (UiColor){(uint8_t)(30 + RND() * 40), (uint8_t)(90 + RND() * 40), 40, 255});
    uid_rrect(d, ui_rect(x - r * 0.6f, y - r * 0.7f, r, r * 0.8f), r * 0.5f, (UiColor){120, 180, 90, 90});
  }
#undef RND
}

// Hex tile art stand-ins for the gallery (the game supplies its own art).
static SDL_Texture *tile_art(SDL_Renderer *r, UiColor top, UiColor side) {
  const int W = 88, H = 62;
  SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, W, H);
  if (!t) return NULL;
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  SDL_Texture *old = SDL_GetRenderTarget(r);
  SDL_SetRenderTarget(r, t);
  SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
  SDL_RenderClear(r);
  SDL_Vertex v[6];
  float cx = W / 2.0f, cy = 26;
  SDL_Color cs = {side.r, side.g, side.b, 255}, ct = {top.r, top.g, top.b, 255};
  // side band then the top diamond
  float pts[4][2] = {{cx, cy - 22}, {cx + 40, cy}, {cx, cy + 22}, {cx - 40, cy}};
  int idx[6] = {0, 1, 2, 0, 2, 3};
  for (int k = 0; k < 4; k++) v[k] = (SDL_Vertex){{pts[k][0], pts[k][1] + 10}, cs, {0, 0}};
  SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
  for (int k = 0; k < 4; k++) v[k] = (SDL_Vertex){{pts[k][0], pts[k][1]}, ct, {0, 0}};
  SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
  SDL_SetRenderTarget(r, old);
  return t;
}

static HudSnap demo_snap(int w, int h) {
  HudSnap s;
  memset(&s, 0, sizeof s);
  s.valid = s.visible = s.capsule_visible = 1;
  s.canvas_w = w;
  s.canvas_h = h;
  s.money = 50000;   // (the game scene below is a sandbox; the gallery shows a normal course's money)
  s.fun = 32;
  s.skill_x100 = 240;
  s.golfers = 12;
  snprintf(s.course, sizeof s.course, "Dolphin Coast Golf Club");
  snprintf(s.date, sizeof s.date, "March 2001");
  s.dock_mode = 0;
  s.terrain_tool = 0;
  s.build_tool = -1;
  s.unlock = 9;
  s.next_hole = 4;
  s.news_on = 1;
  snprintf(s.news, sizeof s.news, "Let's build our first hole. Pick Tees, then click the map.");
  static const int costs[16] = {500, 1000, 60, 20, 80, 120, 80, 40, 40, 20, 50, 30, 40, 60, 60, 60};
  for (int i = 0; i < 16; i++) s.terrain_cost[i] = costs[i];
  for (int i = 0; i < 9; i++) s.building_cost[i] = 2000 + 1500 * i;
  return s;
}

typedef struct {
  SDL_Renderer *r;
  SDL_Texture *canvas;   // the page is drawn here, then read back
  SDL_Texture *course;   // backdrop picture at the page size
  UiCtx *ui;
  const char *fonts;
  SDL_Texture *art[HUD_ART_MAX];
  int w, h;
  float scale;
} Page;

static void begin_page(Page *p, double t) {
  SDL_SetRenderTarget(p->r, p->canvas);
  SDL_SetTextureBlendMode(p->course, SDL_BLENDMODE_NONE);
  SDL_RenderCopy(p->r, p->course, NULL, NULL);
  ui_begin(p->ui, p->canvas, p->w, p->h, p->scale, p->course, t);
}

static void paint_backdrop(Page *p) {
  SDL_SetRenderTarget(p->r, p->canvas);
  SDL_SetTextureBlendMode(p->course, SDL_BLENDMODE_NONE);
  SDL_RenderCopy(p->r, p->course, NULL, NULL);
}

static int save_page(Page *p, const char *out, const char *name) {
  SDL_SetRenderTarget(p->r, p->canvas);
  uint32_t *px = malloc((size_t)p->w * p->h * 4);
  int ok = px && !SDL_RenderReadPixels(p->r, NULL, SDL_PIXELFORMAT_ARGB8888, px, p->w * 4);
  char path[1024];
  snprintf(path, sizeof path, "%s/%s-%dx%d.png", out, name, p->w, p->h);
  ok = ok && ui_png_write(path, px, p->w, p->h, p->w);
  printf("%s %s\n", ok ? "wrote" : "FAILED", path);
  free(px);
  return ok;
}

// Feeds the kit pointer events as a player would (target pixels).
static void pointer(Page *p, float x, float y, int press) {
  UiEvent e = {UI_EV_MOVE, x, y};
  ui_route_event(p->ui, &e);
  if (press) {
    UiEvent d = {UI_EV_DOWN, x, y, 1};
    ui_route_event(p->ui, &d);
  }
}

// The course HUD in the default layout (jwe, ui.hud_density) or the regular D5 one.
static void page_hud(Page *p, const char *out, int density, const char *suffix) {
  UiHud hud = UI_HUD_INIT;
  hud.density = density;
  hud.bar_open = 1;   // jwe: the build bar out (it starts collapsed in game)
  HudSnap s = demo_snap(p->w, p->h);
  HudAction acts[16];
  // three frames: settle the animations, then hover the Course dock button long enough for its tooltip
  for (int f = 0; f < 4; f++) {
    begin_page(p, 1000.0 + f * 400);
    ui_hud_draw(p->ui, &hud, &s, p->art, acts, 16);
    ui_end(p->ui);
    if (f == 1) {
      // the Water tile of the Hazards tab: hover it (find the tile row: bottom tray)
      float x = p->w / 2.0f, y = p->h - ui_s(p->ui, 128) - ui_s(p->ui, 70);
      pointer(p, x, y, 0);
    }
  }
  char name[32];
  snprintf(name, sizeof name, "hud%s", suffix);
  save_page(p, out, name);
  // the Buildings tray, paused, a negative balance
  s.dock_mode = 1;
  s.paused = 1;
  s.money = -1250;
  s.news_on = 0;
  s.build_tool = 8;
  for (int f = 0; f < 3; f++) {
    begin_page(p, 3000.0 + f * 400);
    ui_hud_draw(p->ui, &hud, &s, p->art, acts, 16);
    ui_end(p->ui);
  }
  snprintf(name, sizeof name, "hud-buildings%s", suffix);
  save_page(p, out, name);
}

// task-62: the jwe HUD's message card (three lines, cut with "..."), the same message read in full with
// its camera button, and the bell's history (newest first, with time).
static void page_messages(Page *p, const char *out) {
  static const struct { const char *text; int show, target; } msgs[] = {
      {"Your club has 12 members on the waiting list. Build more home sites to welcome them.", 0, 0},
      {"Hole 3 needs a green. Golfers cannot finish the hole.", HUD_SHOW_HOLE, 3},
      {"Pro shop sales are up. Consider hiring a second club pro so the queue at the counter stays short during the busy summer weekends.", 0, 0},
      {"Tony Birdie is having a great time. He says the greens are the best he has played this year.", HUD_SHOW_GOLFER, 4},
      {"A heavy rain is forecast for the weekend. Expect fewer golfers and soggy fairways.", 0, 0},
      {"Hole 5 has been closed for repair. Fix it soon, golfers are starting to complain.", HUD_SHOW_HOLE, 5},
      {"Your members are asking for a driving range. They say practice is the quickest way to cut strokes.", 0, 0},
      {"Carmen has been rated as one of the Top 18 holes in the country by Great Golf Holes Magazine. Increase greens fees by $100 to cash in on the new popularity, and watch the tee sheet fill up all summer.", HUD_SHOW_HOLE, 7},
  };
  UiHud hud = UI_HUD_INIT;
  hud.density = HUD_JWE;
  HudSnap s = demo_snap(p->w, p->h);
  s.news_on = 1;
  s.dock_mode = 5;
  int n = (int)(sizeof msgs / sizeof msgs[0]);
  for (int i = 0; i < n; i++) {
    HudMsg *m = &s.msgs[s.msg_n++];
    snprintf(m->text, sizeof m->text, "%s", msgs[i].text);
    snprintf(m->date, sizeof m->date, "%s", i < 4 ? "February 2001" : "March 2001");
    m->t_ms = 1000.0 - (n - i) * 95000.0;
    m->seq = i + 1;
    m->show = msgs[i].show;
    m->target = msgs[i].target;
  }
  s.news_seq = n;
  snprintf(s.news, sizeof s.news, "%s", msgs[n - 1].text);
  HudAction acts[16];
  static const char *const names[3] = {"hud-message-card", "hud-message-open", "hud-message-history"};
  for (int st = 0; st < 3; st++) {
    hud.msg_open = st == 1 ? n : 0;
    hud.hist_open = st == 2;
    for (int f = 0; f < 4; f++) {
      begin_page(p, 40000.0 + st * 3000 + f * 400);
      ui_hud_draw(p->ui, &hud, &s, p->art, acts, 16);
      ui_end(p->ui);
    }
    save_page(p, out, names[st]);
  }
}

// ui.hud_density: the same scene in regular, compact and minimal, the Course
// tray open on Hazards (sand trap picked) and closed; prints the share of the
// screen the HUD covers in each.
// The HUD layouts the maintainer compares, same scene, three states each: closed, the
// Course tray on Hazards (sand trap picked), the Buildings tray. Prints the
// share of the screen each covers (golf.log prints the same in game).
static void page_density(Page *p, const char *out) {
  static const struct { const char *name; int density; } v[] = {
      {"V0-regular", HUD_REGULAR},   {"V1-strip", HUD_COMPACT},       {"V2-wheel", HUD_WHEEL},
      {"V3-fan", HUD_FAN},           {"V4-column", HUD_COMPACT_COLUMN}, {"V5-wheel-stats", HUD_WHEEL_STATS},
      {"minimal", HUD_MINIMAL},
      {"C1-card", HUD_CARD},         {"C2-quarter", HUD_QUARTER},     {"C3-lshape", HUD_LSHAPE},
      {"C4-collapsed", HUD_COLLAPSED}, {"C5-stock", HUD_STOCK},        {"C6-card-right", HUD_CARD_RIGHT},
      {"R1-skylines", HUD_BARS},     {"R2-parkitect", HUD_TOPBAR},    {"R3-anno", HUD_RIBBON},
      {"R4-two-point", HUD_BOTTOMBAR}, {"R5-jwe2", HUD_JWE},
  };
  static const char *const states[3] = {"closed", "hazards", "buildings"};
  HudAction acts[16];
  for (int d = 0; d < (int)(sizeof v / sizeof v[0]); d++)
    for (int st = 0; st < 3; st++) {
      UiHud hud = UI_HUD_INIT;
      hud.density = v[d].density;
      hud.bar_open = 1;
      HudSnap s = demo_snap(p->w, p->h);
      s.news_on = 0;
      s.dock_mode = st == 0 ? 5 : st == 1 ? 0 : 1;
      s.terrain_tool = st == 1 ? 7 : -1;   // sand trap: the Hazards tab
      s.build_tool = st == 2 ? 8 : -1;
      for (int f = 0; f < 4; f++) {
        begin_page(p, 20000.0 + d * 7000 + st * 2000 + f * 400);
        ui_hud_draw(p->ui, &hud, &s, p->art, acts, 16);
        ui_end(p->ui);
      }
      char name[64];
      snprintf(name, sizeof name, "variant-%s-%s", v[d].name, states[st]);
      save_page(p, out, name);
      printf("coverage %dx%d %s %s: %.1f%%\n", p->w, p->h, v[d].name, states[st], ui_coverage(p->ui, 2) * 100);
    }
}

static void page_components(Page *p, const char *out) {
  UiCtx *c = p->ui;
  const UiTokens *t = ui_tokens(c);
  for (int f = 0; f < 3; f++) {
    begin_page(p, 5000.0 + f * 400);
    float m = ui_s(c, 40), col = ui_s(c, 560), gap = ui_s(c, 24);
    // column 1: panel with buttons, tabs, slider, toggle, meters
    UiRect a = ui_rect(m, m, col, ui_s(c, 600));
    ui_panel(c, a, ui_s(c, t->radius_panel), 0);
    UiRect in = ui_inset_all(a, ui_s(c, t->pad_panel));
    UiFont hf = ui_font(c, UI_TYPE_HEADING);
    ui_text_in(c, ui_rect(in.x, in.y, in.w, hf.line), UI_TYPE_HEADING, t->text_primary, "Panel, buttons, tabs", 0);
    float y = in.y + hf.line + ui_s(c, 14);
    ui_button(c, ui_id("g.prim"), ui_rect(in.x, y, ui_s(c, 170), ui_s(c, t->button_h)), "Build here", UI_ICON_CHEV, UI_BTN_PRIMARY, 0);
    ui_button(c, ui_id("g.pill"), ui_rect(in.x + ui_s(c, 184), y + 2, ui_s(c, 150), ui_s(c, t->pill_h)), "Follow", UI_ICON_EYE, UI_BTN_PILL, 0);
    ui_button(c, ui_id("g.dis"), ui_rect(in.x + ui_s(c, 348), y, ui_s(c, 150), ui_s(c, t->button_h)), "Disabled", 0, UI_BTN_PRIMARY, UI_DISABLED);
    y += ui_s(c, t->button_h) + ui_s(c, 16);
    static const UiIcon ib[5] = {UI_ICON_UNDO, UI_ICON_REDO, UI_ICON_PLUS, UI_ICON_MINUS, UI_ICON_X};
    for (int i = 0; i < 5; i++)
      ui_icon_button(c, ui_idi(ui_id("g.ib"), i), ui_rect(in.x + i * ui_s(c, 48), y, ui_s(c, 40), ui_s(c, 40)), ib[i],
                     i == 0 ? UI_SELECTED : 0, NULL);
    ui_icon_button(c, ui_id("g.back"), ui_rect(in.x + 5 * ui_s(c, 48) + ui_s(c, 8), y - ui_s(c, 4), ui_s(c, 48), ui_s(c, 48)),
                   UI_ICON_BACK, UI_ROUND | UI_SELECTED, NULL);
    y += ui_s(c, 56);
    static const char *const tabs[4] = {"Holes", "Fairway", "Hazards", "Trees"};
    static int sel = 1;
    ui_segmented(c, ui_id("g.tabs"), ui_rect(in.x, y, ui_segmented_width(c, tabs, NULL, 4), ui_s(c, 38)), tabs, NULL, 4, &sel);
    y += ui_s(c, 54);
    static float vol = 0.62f;
    ui_text_in(c, ui_rect(in.x, y, ui_s(c, 150), ui_s(c, 30)), UI_TYPE_BODY, t->text_primary, "Camera speed", 0);
    ui_slider(c, ui_id("g.slider"), ui_rect(in.x + ui_s(c, 160), y, in.w - ui_s(c, 160), ui_s(c, 30)), &vol, "62%");
    y += ui_s(c, 44);
    static int on1 = 1, on2 = 0;
    ui_text_in(c, ui_rect(in.x, y, ui_s(c, 300), ui_s(c, 30)), UI_TYPE_BODY, t->text_primary, "Show golfer names", 0);
    ui_toggle(c, ui_id("g.t1"), ui_rect(in.x, y, in.w, ui_s(c, 30)), &on1);
    y += ui_s(c, 40);
    ui_text_in(c, ui_rect(in.x, y, ui_s(c, 300), ui_s(c, 30)), UI_TYPE_BODY, t->text_primary, "Golfer thoughts", 0);
    ui_toggle(c, ui_id("g.t2"), ui_rect(in.x, y, in.w, ui_s(c, 30)), &on2);
    y += ui_s(c, 48);
    static const char *const needs[5] = {"Fun", "Attitude", "Energy", "Hunger", "Thirst"};
    static const float nv[5] = {0.82f, 0.64f, 0.35f, 0.55f, 0.18f};
    for (int i = 0; i < 5 && y < a.y + a.h - ui_s(c, 30); i++) {
      ui_text_in(c, ui_rect(in.x, y, ui_s(c, 74), ui_s(c, 18)), UI_TYPE_CAPTION, t->text_secondary, needs[i], 0);
      ui_meter(c, ui_rect(in.x + ui_s(c, 80), y + ui_s(c, 5), in.w - ui_s(c, 80), ui_s(c, t->meter_h)), nv[i]);
      y += ui_s(c, 24);
    }
    // column 2: list, cards, tags, stats
    UiRect b = ui_rect(m + col + gap, m, col, ui_s(c, 600));
    ui_panel(c, b, ui_s(c, t->radius_panel), UI_PANEL_STRONG);
    in = ui_inset_all(b, ui_s(c, 18));
    ui_text_in(c, ui_rect(in.x, in.y, in.w, hf.line), UI_TYPE_HEADING, t->text_primary, "List, cards, stats", 0);
    y = in.y + hf.line + ui_s(c, 10);
    ui_list_row(c, ui_id("g.r1"), ui_rect(in.x, y, in.w, ui_s(c, 68)), NULL, "Dolphin Coast", "Parkland, ocean views, 190 acres", "$40,000", UI_SELECTED);
    y += ui_s(c, 76);
    ui_list_row(c, ui_id("g.r2"), ui_rect(in.x, y, in.w, ui_s(c, 68)), NULL, "Sunset Mesa", "Desert, canyons", "$65,000", 0);
    y += ui_s(c, 76);
    ui_list_row(c, ui_id("g.r3"), ui_rect(in.x, y, in.w, ui_s(c, 68)), NULL, "Emerald Isle", "Links, locked", "$120,000", UI_DISABLED);
    y += ui_s(c, 84);
    float cw = (in.w - ui_s(c, 16)) / 2;
    ui_card(c, ui_rect(in.x, y, cw, ui_s(c, 120)), 1);
    ui_card(c, ui_rect(in.x + cw + ui_s(c, 16), y, cw, ui_s(c, 120)), 0);
    ui_text_in(c, ui_rect(in.x + ui_s(c, 20), y + ui_s(c, 16), cw, ui_s(c, 26)), UI_TYPE_HEADING, t->text_primary, "Card, selected", 0);
    ui_text_in(c, ui_rect(in.x + cw + ui_s(c, 36), y + ui_s(c, 16), cw, ui_s(c, 26)), UI_TYPE_HEADING, t->text_primary, "Card", 0);
    ui_stat(c, in.x + ui_s(c, 20), y + ui_s(c, 80), UI_ICON_COINS, "$50,000", "Money");
    ui_stat(c, in.x + cw + ui_s(c, 36), y + ui_s(c, 80), UI_ICON_TROPHY, "2.40", "Skill");
    y += ui_s(c, 140);
    ui_tag(c, in.x + ui_s(c, 70), y, "Gary Golf", 0);
    ui_tag(c, in.x + ui_s(c, 200), y, "Ann Birdie", 1);
    // type scale
    UiRect ty = ui_rect(m, m + ui_s(c, 600) + gap, 2 * col + gap, ui_s(c, 300));
    ui_panel(c, ty, ui_s(c, t->radius_panel), 0);
    in = ui_inset_all(ty, ui_s(c, 20));
    static const char *const sn[8] = {"Display 34", "Title 24", "Heading 20", "Number 17 $12,345", "Body 15 descriptions and list rows",
                                      "Label 13 buttons and tabs", "Caption 12 units and hints", "Micro 11 dock labels"};
    y = in.y;
    for (int i = 0; i < 8; i++) {
      UiFont f = ui_font(c, i);
      ui_text(c, in.x, y + f.px, i, i >= UI_TYPE_CAPTION ? t->text_secondary : t->text_primary, sn[i]);
      y += f.line + ui_s(c, 4);
    }
    // column 3: thought bubble, popover (golfer card), toast, tooltip
    float x3 = m + 2 * (col + gap);
    ui_thought(c, x3 + ui_s(c, 220), m + ui_s(c, 90), NULL, "This hole is a real challenge!");
    ui_tag(c, x3 + ui_s(c, 220), m + ui_s(c, 112), "Gary Golf", 0);
    UiRect pop = ui_popover(c, x3 + ui_s(c, 4), m + ui_s(c, 330), ui_s(c, t->popover_w) - ui_s(c, 40), ui_s(c, 330), 1);
    in = ui_inset_all(pop, ui_s(c, 20));
    ui_text_in(c, ui_rect(in.x, in.y, in.w, ui_s(c, 30)), UI_TYPE_TITLE, t->text_primary, "Gary Golf", 0);
    ui_text_in(c, ui_rect(in.x, in.y + ui_s(c, 30), in.w, ui_s(c, 18)), UI_TYPE_CAPTION, t->text_secondary, "Pro \xC2\xB7 playing hole 3, age 41", 0);
    for (int i = 0; i < 5; i++) {
      float yy = in.y + ui_s(c, 64) + i * ui_s(c, 24);
      ui_text_in(c, ui_rect(in.x, yy, ui_s(c, 74), ui_s(c, 18)), UI_TYPE_CAPTION, t->text_secondary, needs[i], 0);
      ui_meter(c, ui_rect(in.x + ui_s(c, 80), yy + ui_s(c, 5), in.w - ui_s(c, 80), ui_s(c, 8)), nv[i]);
    }
    ui_button(c, ui_id("g.prof"), ui_rect(in.x, in.y + in.h - ui_s(c, 40), ui_s(c, 110), ui_s(c, 40)), "Profile", UI_ICON_USERS, UI_BTN_PILL, 0);
    ui_button(c, ui_id("g.snap"), ui_rect(in.x + ui_s(c, 120), in.y + in.h - ui_s(c, 40), ui_s(c, 130), ui_s(c, 40)), "Snapshot", UI_ICON_CAMERA, UI_BTN_PILL, 0);
    float tw = ui_s(c, 400);
    float th = ui_toast_height(c, tw, "Hole 1 is open for play", "Golfers will start arriving soon.");
    ui_toast(c, ui_rect(x3, m + ui_s(c, 700), tw, th), NULL, "Hole 1 is open for play", "Golfers will start arriving soon.");
    UiRect tipb = ui_rect(x3 + ui_s(c, 40), m + ui_s(c, 860), ui_s(c, 40), ui_s(c, 40));
    ui_icon_button(c, ui_id("g.tipbtn"), tipb, UI_ICON_INFO, 0, "Information (I)");
    ui_end(c);
    if (f == 0) pointer(p, tipb.x + tipb.w / 2, tipb.y + tipb.h / 2, 0);
  }
  save_page(p, out, "components");
}

// Golfers in the world (task-31): name tags, thought bubbles and the golfer
// card beside a selected golfer, on a course backdrop. Positions are canvas
// pixels of the 720-line canvas the game uses at 1080p (scaled to the page).
static SDL_Texture *fake_head(SDL_Renderer *r, UiColor skin, UiColor hair) {
  const int W = 80, H = 96;
  SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, W, H);
  if (!t) return NULL;
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  SDL_Texture *old = SDL_GetRenderTarget(r);
  SDL_SetRenderTarget(r, t);
  SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
  SDL_RenderClear(r);
  UiDraw d;
  uid_init(&d, r);
  uid_begin(&d, t, W, H);
  uid_rrect(&d, ui_rect(14, 70, 52, 40), 20, (UiColor){60, 90, 160, 255});     // shoulders
  uid_rrect(&d, ui_rect(18, 14, 44, 60), 22, hair);
  uid_rrect(&d, ui_rect(20, 22, 40, 52), 20, skin);
  uid_rrect(&d, ui_rect(29, 42, 6, 6), 3, (UiColor){40, 30, 30, 255});
  uid_rrect(&d, ui_rect(45, 42, 6, 6), 3, (UiColor){40, 30, 30, 255});
  uid_rrect(&d, ui_rect(32, 58, 16, 5), 2.5f, (UiColor){160, 70, 70, 255});
  uid_free(&d);
  SDL_SetRenderTarget(r, old);
  return t;
}

static int page_golfers(Page *p, const char *out) {
  static GwNote n;
  memset(&n, 0, sizeof n);
  float k = p->h / 720.0f;   // canvas px -> page px
  n.magic = GW_NOTE_MAGIC;
  n.zoom = 4;
  struct { const char *name; int x, y, sel; } tags[] = {
      {"Ann Birdie", 330, 330, 1}, {"Gary Golf", 520, 300, 0}, {"Biff", 600, 340, 0}, {"I.M. Picky", 612, 346, 0},
      {"Mike Marshall", 200, 250, 0}, {"Maharishi", 800, 220, 0}, {"Zeke", 760, 430, 0}};
  for (unsigned i = 0; i < sizeof tags / sizeof tags[0]; i++) {
    GwTag *t = &n.tags[n.n_tags++];
    t->slot = (int16_t)(20 + i);
    t->selected = (int16_t)tags[i].sel;
    t->x = tags[i].x;
    t->y = tags[i].y;
    t->mood = 8;
    snprintf(t->name, sizeof t->name, "%s", tags[i].name);
  }
  struct { const char *text; int x, y, tone, kind; } bub[] = {
      {"This hole is a real challenge!", 520, 250, 0, 0}, {"I'm getting a little thirsty.", 760, 380, -1, 0},
      {"Wow, what a great shot!", 600, 290, 1, 0}, {"What's the matter, Zeke? You look sad.", 200, 190, 0, 1}};
  for (unsigned i = 0; i < sizeof bub / sizeof bub[0]; i++) {
    GwBubble *b = &n.bubbles[n.n_bubbles++];
    b->slot = (int16_t)(20 + i);
    b->tone = (int16_t)bub[i].tone;
    b->x = bub[i].x;
    b->y = bub[i].y;
    b->kind = (int16_t)bub[i].kind;
    b->art = (int16_t)(GW_ART_MOOD + 6 + bub[i].tone * 3);
    snprintf(b->text, sizeof b->text, "%s", bub[i].text);
  }
  GwCard *g = &n.card;
  g->on = 1;
  g->slot = 20;
  g->ax = 330;
  g->ay = 330;
  snprintf(g->name, sizeof g->name, "Ann Birdie (Surfer)");
  snprintf(g->line1, sizeof g->line1, "Single, age 29");
  snprintf(g->line2, sizeof g->line2, "Outgoing, Active and Nice");
  snprintf(g->line2, sizeof g->line2, "Outgoing, Active and Nice | The Social Butterfly | Visit 12 | Confidence 72");
  snprintf(g->club_story, sizeof g->club_story, "A familiar pairing: progress. Visit 12: We are building a collection of golfing memories.");
  snprintf(g->skills, sizeof g->skills, "length, accuracy, imagination+");
  snprintf(g->quote, sizeof g->quote, "'This is the best course I've ever played.'");
  static const char *const lab[GW_METERS] = {"Fun", "Attitude", "Energy", "Hunger", "Thirst"};
  static const int mv[GW_METERS] = {880, 640, 920, 520, 150};
  for (int i = 0; i < GW_METERS; i++) {
    snprintf(g->label[i], sizeof g->label[i], "%s", lab[i]);
    g->meter[i] = (int16_t)mv[i];
    g->warn[i] = mv[i] < 400;
  }
  g->has_scores = 1;
  static const int sc[9] = {4, 3, 5, 4, 6, 3, 4, 7, 2};
  static const int tn[9] = {0, -1, 0, 0, 1, 0, 0, 2, -2};
  for (int i = 0; i < 9; i++) {
    g->score[i] = (int16_t)sc[i];
    g->tone[i] = (int8_t)tn[i];
    g->total += (int16_t)sc[i];
  }
  g->cur_hole = 10;
  g->has_story = 1;
  snprintf(g->story[0], sizeof g->story[0], "10th hole, 1st shot- Great, thanks for asking.");
  snprintf(g->story[1], sizeof g->story[1], "9th hole, 3rd shot- Ahhh, a cool foamy beverage.");
  snprintf(g->story[2], sizeof g->story[2], "9th hole, 1st shot- Hmmm, a tricky uphill shot.");
  g->story_tone[0] = 1;
  g->story_tone[1] = 1;
  g->story_tone[2] = -1;
  g->n_story = 3;
  g->btn[0][0] = g->btn[1][0] = g->btn[2][0] = g->btn[3][0] = 'x';
  SDL_Texture *art[GW_ART_MAX] = {0};
  art[GW_ART_PORTRAIT] = fake_head(p->r, (UiColor){232, 188, 150, 255}, (UiColor){70, 40, 24, 255});
  GwAction acts[4];
  {
    GwOpts o;
    ui_golfers_opts_default(&o);
    o.still = 1;
    o.have_mouse = 1;
    o.mx = 520 * k;
    o.my = (300 - 14) * k;   // the pointer is on Gary Golf
    ui_golfers_set_opts(&o);
    ui_golfers_reset();
  }
  // the game runs the world layer and the card layer in two contexts (the card is the only one that takes the pointer)
  UiCtx *wc = ui_create(p->r, ui_tokens(p->ui), p->fonts);
  for (int f = 0; f < 4; f++) {
    paint_backdrop(p);
    ui_begin(wc, p->canvas, p->w, p->h, p->scale, p->course, 30000.0 + f * 400);
    ui_golfers_draw_world(wc, &n, art, 0, 0, k, 0, 0);
    ui_end(wc);
    ui_begin(p->ui, p->canvas, p->w, p->h, p->scale, p->canvas, 30000.0 + f * 400 + 100);
    ui_golfers_draw_card(p->ui, &n, art, 0, 0, k, 0, 0, acts, 4);
    ui_end(p->ui);
  }
  save_page(p, out, "golfers");
  // click-through: a press and release on Snapshot, then on the close button, must come back as actions
  int fails = 0;
  const GwLayout *L = ui_golfers_layout();
  struct { UiRect r; int kind, id; const char *what; } clicks[3] = {
      {L->btn[2], GW_ACT_BUTTON, 2, "Snapshot"}, {L->close, GW_ACT_CLOSE, 4, "close"},
      {L->diary, GW_ACT_DIARY, 0, "Club diary"}};
  for (int i = 0; i < 3; i++) {
    UiRect r = clicks[i].r;
    if (r.w <= 0) {
      printf("FAIL golfer card: no %s button\n", clicks[i].what);
      fails++;
      continue;
    }
    float x = r.x + r.w / 2, y = r.y + r.h / 2;
    UiEvent mv = {UI_EV_MOVE, x, y};
    UiEvent dn = {UI_EV_DOWN, x, y, 1}, up = {UI_EV_UP, x, y, 1};
    int got = 0, kind = 0, id = -1;
    for (int f = 0; f < 5; f++) {
      if (f == 1) ui_route_event(p->ui, &mv);
      if (f == 2) ui_route_event(p->ui, &dn);
      if (f == 3) ui_route_event(p->ui, &up);
      paint_backdrop(p);
      ui_begin(wc, p->canvas, p->w, p->h, p->scale, p->course, 40000.0 + i * 5000 + f * 100);
      ui_golfers_draw_world(wc, &n, art, 0, 0, k, 0, 0);
      ui_end(wc);
      ui_begin(p->ui, p->canvas, p->w, p->h, p->scale, p->canvas, 40000.0 + i * 5000 + f * 100 + 50);
      int na = ui_golfers_draw_card(p->ui, &n, art, 0, 0, k, 0, 0, acts, 4);
      ui_end(p->ui);
      for (int a = 0; a < na; a++) got++, kind = acts[a].kind, id = acts[a].id;
    }
    int ok = got == 1 && kind == clicks[i].kind && id == clicks[i].id;
    printf("%s golfer card: click on %s -> %d action(s), kind %d id %d\n", ok ? "ok  " : "FAIL", clicks[i].what, got, kind, id);
    fails += !ok;
  }
  g->layout = 2;
  g->n_story = 5;
  for (int i = 0; i < 5; i++)
    snprintf(g->story[i], sizeof g->story[i], "Visit %d: Another good moment to share with a familiar playing partner.", 12 - i);
  for (int f = 0; f < 4; f++) {
    paint_backdrop(p);
    ui_begin(p->ui, p->canvas, p->w, p->h, p->scale, p->canvas, 60000.0 + f * 400);
    ui_golfers_draw_card(p->ui, &n, art, 0, 0, k, 0, 0, acts, 4);
    ui_end(p->ui);
  }
  save_page(p, out, "living-club-diary");
  ui_destroy(wc);
  if (art[GW_ART_PORTRAIT]) SDL_DestroyTexture(art[GW_ART_PORTRAIT]);
  return fails;
}

// ------------------------------------------------------------------ task-60: faces, names

static const char *g_old_faces;   // --faces DIR: golf's own mood sprites, mood_<i>.bmp (SIMGOLF_FACES_DUMP)

static SDL_Texture *old_face_tex(SDL_Renderer *r, int i) {
  if (!g_old_faces) return NULL;
  char path[1024];
  snprintf(path, sizeof path, "%s/mood_%d.bmp", g_old_faces, i);
  SDL_Surface *s = SDL_LoadBMP(path);
  if (!s) return NULL;
  SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
  SDL_FreeSurface(s);
  if (t) {
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
  }
  return t;
}

// The ten mood faces, golf's 2002 sprites against the Frosted Mint ones, at the sizes they are used.
static void page_faces(Page *p, const char *out) {
  static const char *const word[11] = {"", "furious", "sad", "unhappy", "meh", "flat", "pleased", "content", "happy", "grinning", "delighted"};
  SDL_Texture *old[11] = {0};
  for (int i = 1; i <= 10; i++) old[i] = old_face_tex(p->r, i);
  begin_page(p, 60000);
  UiCtx *c = p->ui;
  const UiTokens *t = ui_tokens(c);
  float m = ui_s(c, 36);
  UiRect panel = ui_rect(m, m, p->w - 2 * m, p->h * 0.64f);
  ui_panel(c, panel, ui_s(c, t->radius_panel), UI_PANEL_STRONG);
  UiRect in = ui_inset_all(panel, ui_s(c, 28));
  UiFont hf = ui_font(c, UI_TYPE_TITLE), cf = ui_font(c, UI_TYPE_CAPTION);
  ui_text_in(c, ui_rect(in.x, in.y, in.w, hf.line), UI_TYPE_TITLE, t->text_primary, "Golfer mood faces: 2002 sprites and the Frosted Mint set", 0);
  float labw = ui_s(c, 190), cw = (in.w - labw) / 10;
  float y = in.y + hf.line + ui_s(c, 24);
  float big = fminf(cw * 0.78f, ui_s(c, 110));
  struct { const char *label; float size; int newer; } rows[] = {
      {"Before, 2002 sprite", 0, 0}, {"After, Frosted Mint", 0, 1}, {"Before, at bubble size", 28, 0},
      {"After, at bubble size", 28, 1}, {"Before, minor icon size", 21, 0}, {"After, minor icon size", 21, 1}};
  for (unsigned r = 0; r < sizeof rows / sizeof rows[0]; r++) {
    float sz = rows[r].size ? ui_s(c, rows[r].size) : big;
    float rh = fmaxf(sz, cf.line) + ui_s(c, r < 2 ? 22 : 12);
    ui_text_in(c, ui_rect(in.x, y, labw - ui_s(c, 10), rh - ui_s(c, 6)), UI_TYPE_CAPTION, t->text_secondary, rows[r].label, 0);
    for (int i = 1; i <= 10; i++) {
      UiRect cell = ui_rect(in.x + labw + (i - 1) * cw, y, cw, rh - ui_s(c, 6));
      float cx = cell.x + cell.w / 2, cy = y + sz / 2;
      UiRect slot = ui_rect(cx - sz / 2, cy - sz / 2, sz, sz);
      if (rows[r].newer) ui_face(c, i, slot, sz, 1.0f);
      else if (old[i]) uid_image(ui_drawer(c), old[i], NULL, slot, 0, (UiColor){255, 255, 255, 255});
      else ui_text_in(c, slot, UI_TYPE_MICRO, t->text_tertiary, "n/a", 0.5f);
    }
    y += rh;
    if (r == 1) y += ui_s(c, 8);
  }
  // numbers and meaning under the big rows
  y += ui_s(c, 4);
  for (int i = 1; i <= 10; i++) {
    char cap[40];
    snprintf(cap, sizeof cap, "%d %s", i, word[i]);
    ui_text_in(c, ui_rect(in.x + labw + (i - 1) * cw, y, cw, cf.line), UI_TYPE_CAPTION, t->text_primary, cap, 0.5f);
  }
  y += cf.line + ui_s(c, 22);
  // the faces in use: a thought bubble and the golfer card's corner badge
  float bw = ui_s(c, 420);
  ui_text_in(c, ui_rect(in.x, y, labw, cf.line), UI_TYPE_CAPTION, t->text_secondary, "In context", 0);
  float bx = in.x + labw + ui_s(c, 20);
  static const char *const say[3] = {"What a great shot!", "Hmm, a tricky uphill shot.", "Argh, in the water again."};
  static const int md[3] = {10, 5, 1};
  GwOpts o;
  ui_golfers_opts_default(&o);
  o.still = 1;
  o.tag_scale = 1.0f;
  o.tag_opacity = 1.0f;
  o.bubbles = GW_BUB_FULL;
  ui_golfers_set_opts(&o);
  (void)bw;
  for (int i = 0; i < 3; i++) {
    UiRect rr = ui_rect(bx + i * ui_s(c, 400), y, ui_s(c, 380), ui_s(c, 60));
    uid_rrect(ui_drawer(c), rr, ui_s(c, 14), (UiColor){90, 150, 80, 255});
    // the bubble as the world layer draws it (face, words)
    static GwNote n;
    memset(&n, 0, sizeof n);
    n.magic = GW_NOTE_MAGIC;
    n.zoom = 4;
    GwBubble *b = &n.bubbles[n.n_bubbles++];
    b->slot = 1;
    b->tone = md[i] == 5 ? 0 : md[i] > 5 ? 1 : -1;
    b->art = (int16_t)(GW_ART_MOOD + md[i]);
    snprintf(b->text, sizeof b->text, "%s", say[i]);
    ui_golfers_reset();
    SDL_Texture *none[GW_ART_MAX] = {0};
    float kk = p->h / 720.0f;
    // anchor so the bubble sits inside rr: canvas px of the head
    b->x = (int)((rr.x + rr.w / 2) / kk);
    b->y = (int)((rr.y + rr.h + ui_s(c, 14)) / kk);
    ui_golfers_draw_world(c, &n, none, 0, 0, kk, 0, 0);
  }
  ui_end(c);
  save_page(p, out, "faces");
  for (int i = 1; i <= 10; i++)
    if (old[i]) SDL_DestroyTexture(old[i]);
}

// Names, plain text against a tight pill, side by side on the same golfers (the maintainer's question).
static void page_names(Page *p, const char *out) {
  GwOpts o;
  float k = p->h / 720.0f;
  static const struct { const char *name; int x, y; int sel; } g[] = {
      {"Ann Birdie", 150, 300, 1}, {"Gary Golf", 330, 250, 0}, {"Biff", 440, 330, 0}, {"I.M. Picky", 452, 338, 0}, {"Mike Marshall", 120, 450, 0}};
  paint_backdrop(p);
  for (int side = 0; side < 3; side++) {
    static GwNote n;
    memset(&n, 0, sizeof n);
    n.magic = GW_NOTE_MAGIC;
    n.zoom = 4;
    int dx = side * 410;
    for (unsigned i = 0; i < sizeof g / sizeof g[0]; i++) {
      GwTag *t = &n.tags[n.n_tags++];
      t->slot = (int16_t)(30 + i);
      t->selected = (int16_t)(g[i].sel && side < 2);
      t->x = g[i].x + dx;
      t->y = g[i].y;
      t->mood = 8;
      snprintf(t->name, sizeof t->name, "%s", g[i].name);
    }
    ui_golfers_opts_default(&o);
    o.still = 1;
    o.show_all = 1;
    o.have_mouse = 1;
    o.mx = (g[1].x + dx) * k;
    o.my = (g[1].y - 14) * k;   // the pointer is on Gary
    o.tag_style = side == 1 ? GW_STYLE_PILL : GW_STYLE_PLAIN;
    if (side == 2) o.tag_scale = 1.0f, o.tag_style = GW_STYLE_PILL, o.tag_opacity = 1.0f;   // the old look
    ui_golfers_set_opts(&o);
    ui_golfers_reset();
    SDL_Texture *none[GW_ART_MAX] = {0};
    UiCtx *wc = ui_create(p->r, ui_tokens(p->ui), p->fonts);
    ui_begin(wc, p->canvas, p->w, p->h, p->scale, p->course, 70000.0 + side);
    ui_golfers_draw_world(wc, &n, none, 0, 0, k, 0, 0);
    static const char *const cap[3] = {"New default: plain text, pill on hover or selection", "ui.tag_style=pill: tight pill on every name",
                                       "Before (task-31): pill on every name, 100%"};
    ui_text_in(wc, ui_rect(dx * k + ui_s(wc, 30), ui_s(wc, 30), 420 * k - ui_s(wc, 40), ui_s(wc, 24)), UI_TYPE_LABEL, (UiColor){255, 255, 255, 255}, cap[side], 0);
    ui_end(wc);
    ui_destroy(wc);
  }
  save_page(p, out, "names");
}

// Contrast of the thought bubble's text over the picture (task-60): the default bubble (70 percent, glass at
// ui.tag_opacity) is drawn at a grid of places over the backdrop (pass --backdrop with a course screenshot);
// at each, the darkest glass pixel under the text row against the text colour (WCAG contrast).
static float lum8(uint32_t px) {
  float v[3] = {((px >> 16) & 255) / 255.0f, ((px >> 8) & 255) / 255.0f, (px & 255) / 255.0f};
  for (int i = 0; i < 3; i++) v[i] = v[i] <= 0.03928f ? v[i] / 12.92f : powf((v[i] + 0.055f) / 1.055f, 2.4f);
  return 0.2126f * v[0] + 0.7152f * v[1] + 0.0722f * v[2];
}

static int cmp_f(const void *a, const void *b) { return *(const float *)a < *(const float *)b ? -1 : *(const float *)a > *(const float *)b; }

static int page_contrast(Page *p, const char *out, float opacity, const char *tag) {
  enum { GX = 10, GY = 6 };
  float res[GX * GY];
  int n = 0;
  GwOpts o;
  ui_golfers_opts_default(&o);
  o.still = 1;
  o.bubbles = GW_BUB_FULL;
  o.tag_opacity = opacity;
  ui_golfers_set_opts(&o);
  float k = p->h / 720.0f;
  UiCtx *wc = ui_create(p->r, ui_tokens(p->ui), p->fonts);
  const UiTokens *t = ui_tokens(p->ui);
  float lt = lum8((uint32_t)t->text_primary.r << 16 | (uint32_t)t->text_primary.g << 8 | t->text_primary.b);
  for (int gy = 0; gy < GY; gy++)
    for (int gx = 0; gx < GX; gx++) {
      static GwNote note;
      memset(&note, 0, sizeof note);
      note.magic = GW_NOTE_MAGIC;
      note.zoom = 4;
      GwBubble *b = &note.bubbles[note.n_bubbles++];
      b->slot = 3;
      b->tone = 1;
      b->art = GW_ART_MOOD + 9;
      b->x = (int)((p->w * (gx + 0.5f) / GX) / k);
      b->y = (int)((p->h * (gy + 0.5f) / GY + 40) / k);
      snprintf(b->text, sizeof b->text, "Wow, what a great shot!");
      ui_golfers_reset();
      paint_backdrop(p);
      SDL_Texture *none[GW_ART_MAX] = {0};
      ui_begin(wc, p->canvas, p->w, p->h, p->scale, p->course, 80000.0 + n);
      ui_golfers_draw_world(wc, &note, none, 0, 0, k, 0, 0);
      ui_end(wc);
      UiRect tr = ui_golfers_layout()->bubble_text;
      SDL_Rect rc = {(int)tr.x, (int)tr.y, (int)tr.w, (int)tr.h};
      if (rc.w < 4 || rc.h < 4 || rc.x < 0 || rc.y < 0 || rc.x + rc.w > p->w || rc.y + rc.h > p->h) continue;
      uint32_t *px = malloc((size_t)rc.w * rc.h * 4);
      if (!px || SDL_RenderReadPixels(p->r, &rc, SDL_PIXELFORMAT_ARGB8888, px, rc.w * 4)) {
        free(px);
        continue;
      }
      float lmin = 1e9f, lmax = -1e9f;
      for (int i = 0; i < rc.w * rc.h; i++) {
        float l = lum8(px[i]);
        lmin = fminf(lmin, l);
        lmax = fmaxf(lmax, l);
      }
      float mid = (lmin + lmax) / 2, worst = 1e9f;   // the darkest of the glass pixels (the brighter half)
      for (int i = 0; i < rc.w * rc.h; i++) {
        float l = lum8(px[i]);
        if (l > mid && l < worst) worst = l;
      }
      free(px);
      res[n++] = (worst + 0.05f) / (lt + 0.05f);
    }
  ui_destroy(wc);
  qsort(res, (size_t)n, sizeof res[0], cmp_f);
  int ge45 = 0, ge3 = 0;
  for (int i = 0; i < n; i++) ge45 += res[i] >= 4.5f, ge3 += res[i] >= 3.0f;
  char path[1024];
  snprintf(path, sizeof path, "%s/contrast-%s-%dx%d.txt", out, tag, p->w, p->h);
  FILE *f = fopen(path, "w");
  char line[400];
  snprintf(line, sizeof line,
           "bubble text contrast, opacity %.2f, %d places over the backdrop: min %.1f, 10th percentile %.1f, median %.1f, max %.1f; %d of %d at least 4.5:1, %d at least 3:1\n",
           opacity, n, n ? res[0] : 0, n ? res[n / 10] : 0, n ? res[n / 2] : 0, n ? res[n - 1] : 0, ge45, n, ge3);
  printf("%s", line);
  if (f) {
    fputs(line, f);
    fclose(f);
  }
  return 0;
}

// Names and bubbles as the camera zooms out (zoom 4 closest): they shrink and fade, like the golfers.
static void page_zoomout(Page *p, const char *out) {
  float k = p->h / 720.0f;
  paint_backdrop(p);
  for (int col = 0; col < 4; col++) {
    int zoom = 4 - col;
    static GwNote n;
    memset(&n, 0, sizeof n);
    n.magic = GW_NOTE_MAGIC;
    n.zoom = zoom;
    int cx = 160 + col * 320;
    GwTag *t = &n.tags[n.n_tags++];
    t->slot = 40;
    t->x = cx;
    t->y = 360;
    t->mood = 8;
    snprintf(t->name, sizeof t->name, "Gary Golf");
    GwBubble *b = &n.bubbles[n.n_bubbles++];
    b->slot = 40;
    b->tone = 1;
    b->x = cx;
    b->y = 360 - zoom * 10;
    b->art = GW_ART_MOOD + 9;
    snprintf(b->text, sizeof b->text, "What a great shot!");
    GwOpts o;
    ui_golfers_opts_default(&o);
    o.still = 1;
    o.tags = GW_TAGS_ALWAYS;
    ui_golfers_set_opts(&o);
    ui_golfers_reset();
    SDL_Texture *none[GW_ART_MAX] = {0};
    UiCtx *wc = ui_create(p->r, ui_tokens(p->ui), p->fonts);
    ui_begin(wc, p->canvas, p->w, p->h, p->scale, p->course, 90000.0 + col);
    ui_golfers_draw_world(wc, &n, none, 0, 0, k, 0, 0);
    char cap[40];
    snprintf(cap, sizeof cap, "zoom %d", zoom);
    ui_text_in(wc, ui_rect(cx * k - ui_s(wc, 60), 40 * k, ui_s(wc, 120), ui_s(wc, 24)), UI_TYPE_LABEL, (UiColor){255, 255, 255, 255}, cap, 0.5f);
    // a stand-in for the golfer: a pin at the feet, tall as the game draws them
    uid_rrect(ui_drawer(wc), ui_rect(cx * k - zoom * 1.2f * k, (360 - zoom * 9) * k, zoom * 2.4f * k, zoom * 9 * k), zoom * 1.2f * k, (UiColor){250, 250, 250, 255});
    ui_end(wc);
    ui_destroy(wc);
  }
  save_page(p, out, "zoomout");
}

// ------------------------------------------------------------------ the front end (task-32)

static void fe_demo(FeSnap *s) {
  memset(s, 0, sizeof *s);
  s->magic = FE_MAGIC;
  snprintf(s->theme_name, sizeof s->theme_name, "Standard");
  for (int i = 0; i < 4; i++) {
    s->wages[i] = 2 + i;
    s->fatigue[i] = 1 + i;
  }
  FeWorld *W = &s->world;
  static const char *const names[FE_TAGS] = {"Monterey", "San Diego", "Rocky Mtns.", "Las Vegas", "Phoenix", "Hawaii", "Oahu", "Nova Scotia",
                                             "Northeast", "Carolina", "Ireland", "Scotland", "Wales", "Spain", "Florida", "Jamaica"};
  static const char *const feat[FE_TAGS] = {"Scenic Cypress", "Dolphins", "Free Hotel", "Fun, Fun, Fun", "Free Spa", "Scenic Waterfall", "Japanese Garden", "Scenic Lighthouse",
                                            "Civil War Battlefield", "Free Putting Green", "Leprechauns", "Free Castle", "Stonehenge", "Scenic Vineyards", "Free Pro Shop", "Scenic Statues"};
  W->n = FE_TAGS;
  W->hover = -1;
  W->difficulty = 0;
  W->can_refresh = 1;
  W->gcx = 201;
  W->gcy = 308;
  W->gr = 196;
  snprintf(W->budget, sizeof W->budget, "$100,000");
  snprintf(W->heading, sizeof W->heading, "Where will you build your golf course?");
  for (int i = 0; i < FE_TAGS; i++) {
    FeTag *T = &W->tag[i];
    snprintf(T->name, sizeof T->name, "%s", names[i]);
    snprintf(T->feature, sizeof T->feature, "%s", feat[i]);
    T->price = 50000 + i * i * 4000 + i * 30000;
    T->acres = 40 + (i * 37) % 150;
    T->state = T->price <= 100000 ? 1 : 0;
    T->theme = (int8_t)(i % 4);
    T->art = -1;
    T->px = (int16_t)(60 + (i * 53) % 300);
    T->py = (int16_t)(180 + (i * 71) % 260);
    if (i == 0) {
      T->n_detail = 1;
      snprintf(T->detail[0], sizeof T->detail[0], "Buy 40 acres of flat coastal parkland property for only $50,000.");
    }
  }
  FeLoad *L = &s->load;
  snprintf(L->list.title, sizeof L->list.title, "Load Previous Game");
  static const char *const saves[9] = {"autosave: (-Flamingo Shores, 16 May 2009)", "autosave: (-Flamingo Shores, 1 May 2009)",
                                       "Ocean's Edge 12 June 2001.sve", "Ocean's Edge 19 September 2001.sve", "Showcase Resort v5 (4 stars).sve",
                                       "Showcase Resort.sve", "Showcase Sandbox v4.sve", "Showcase Sandbox.sve", "While_Browsing.sve"};
  L->list.n_rows = 9;
  L->list.total = 9;
  for (int i = 0; i < 9; i++) snprintf(L->list.row[i], sizeof L->list.row[i], "%s", saves[i]);
  L->list.sel = 5;
  L->has_info = 1;
  snprintf(L->line1, sizeof L->line1, "Flamingo Shores  (Theme: Standard)");
  snprintf(L->line2, sizeof L->line2, "Designed by Gary Golf");
  snprintf(L->line3, sizeof L->line3, "Course Record: 66, by Arnie");
  snprintf(L->holes, sizeof L->holes, "18");
  snprintf(L->par, sizeof L->par, "72");
  snprintf(L->yards, sizeof L->yards, "6688");
  snprintf(L->cash, sizeof L->cash, "47,816,700");
  static const char *const sl[4] = {"Fun Rating", "Length Skill", "Accuracy Skill", "Imagination"};
  static const char *const sv[4] = {"1530", "9.01", "5.29", "4.23"};
  for (int i = 0; i < 4; i++) {
    snprintf(L->stat_label[i], sizeof L->stat_label[i], "%s", sl[i]);
    snprintf(L->stat_value[i], sizeof L->stat_value[i], "%s", sv[i]);
  }
  L->n_holes = 18;
  for (int i = 0; i < 18; i++) {
    snprintf(L->hole_no[i], sizeof L->hole_no[i], "%d", i + 1);
    snprintf(L->hole_par[i], sizeof L->hole_par[i], "%d", 3 + (i * 7) % 3);
    snprintf(L->hole_yards[i], sizeof L->hole_yards[i], "%d", 220 + (i * 61) % 340);
  }
  FePro *P = &s->pro;
  snprintf(P->list.title, sizeof P->list.title, "Pick A Pro");
  P->list.n_rows = 1;
  P->list.total = 1;
  snprintf(P->list.row[0], sizeof P->list.row[0], "Gary Golf.pro");
  P->list.sel = 0;
  P->has_info = 1;
  snprintf(P->name, sizeof P->name, "Gary Golf");
  snprintf(P->saying, sizeof P->saying, "'Gary Golf, keep your head down - swing easy.'");
  static const char *const sk[10] = {"Power Hitter", "Long Driver", "Accurate Driver", "Accurate Irons", "Accurate Putter",
                                     "Draw Shot (R to L)", "Fade Shot (L to R)", "High Backspin Shot", "Recovery Skills", "Luck"};
  P->n_skills = 10;
  for (int i = 0; i < 10; i++) {
    snprintf(P->skill[i], sizeof P->skill[i], "%s", sk[i]);
    snprintf(P->skill_value[i], sizeof P->skill_value[i], "+0%%");
  }
  FeTheme *T = &s->theme;
  T->n = 4;
  T->sel = 2;
  static const char *const tn[4] = {"Firaxis", "More Stories", "Standard", "The Sims"};
  static const uint8_t tm[4] = {3, 1, 15, 2};
  for (int i = 0; i < 4; i++) {
    snprintf(T->name[i], sizeof T->name[i], "%s", tn[i]);
    T->mask[i] = tm[i];
  }
}

static void fe_frame_draw(Page *p, const FeSnap *snap, SDL_Texture *const *art, FeAction *acts, int *na, double t) {
  SDL_SetRenderTarget(p->r, p->canvas);
  ui_frontend_background(p->r, ui_tokens(p->ui), p->w, p->h);
  ui_begin(p->ui, p->canvas, p->w, p->h, p->scale, p->canvas, t);
  *na = ui_frontend_draw(p->ui, snap, art, acts, 8);
  ui_end(p->ui);
}

// task-80: the Updates button, panel and "is out" notice on the start menu
static int upd_click(Page *p, const FeSnap *snap, SDL_Texture *const *art, UiRect r, double t, int *kind, int *arg) {
  float x = r.x + r.w / 2, y = r.y + r.h / 2;
  UiEvent mv = {UI_EV_MOVE, x, y}, dn = {UI_EV_DOWN, x, y, 1}, up = {UI_EV_UP, x, y, 1};
  FeAction acts[8];
  int got = 0, na = 0;
  *kind = 0;
  *arg = -1;
  for (int f = 0; f < 5; f++) {
    if (f == 1) ui_route_event(p->ui, &mv);
    if (f == 2) ui_route_event(p->ui, &dn);
    if (f == 3) ui_route_event(p->ui, &up);
    fe_frame_draw(p, snap, art, acts, &na, t + f * 100);
    for (int a = 0; a < na; a++) got++, *kind = acts[a].kind, *arg = acts[a].arg;
  }
  return got;
}

// Scroll through ordinary routed wheel input until the whole row is visible.
static int upd_row_visible(const FeLayout *ly, int row) {
  UiRect r = ly->upd_row[row], v = ly->upd_view;
  return r.w > 0 && r.h > 0 && v.w > 0 && v.h > 0 &&
         r.y >= v.y && r.y + r.h <= v.y + v.h;
}

static int upd_scroll_to_row(Page *p, const FeSnap *snap, SDL_Texture *const *art, int row, double t) {
  FeAction acts[8];
  int na = 0, steps = 0;
  for (; steps < 24 && !upd_row_visible(ui_frontend_layout(), row); steps++) {
    const FeLayout *ly = ui_frontend_layout();
    UiRect v = ly->upd_view, r = ly->upd_row[row];
    float x = v.x + v.w / 2, y = v.y + v.h / 2;
    UiEvent move = {UI_EV_MOVE, x, y};
    UiEvent wheel = {UI_EV_WHEEL, x, y, 0, r.y < v.y ? 1.0f : -1.0f};
    ui_route_event(p->ui, &move);
    ui_route_event(p->ui, &wheel);
    fe_frame_draw(p, snap, art, acts, &na, t + steps * 100);
  }
  return steps;
}

static int page_updates(Page *p, const char *out, FeSnap *snap, SDL_Texture *const *art) {
  static UpdState u;
  char err[160] = "";
  int fails = 0, kind, arg;
  if (!upd_load_news(&u, "data/updates.json", err, sizeof err)) {
    printf("FAIL updates: %s\n", err);
    return 1;
  }
  u.expanded = -1;
  u.show_dot = 1;
  snprintf(u.latest, sizeof u.latest, "0.9.9");
  u.avail = 1;
  snap->upd = &u;
  snap->screen = FE_TITLE;
  FeAction acts[8];
  int na = 0;
  for (int f = 0; f < 4; f++) fe_frame_draw(p, snap, art, acts, &na, 200000.0 + f * 400);
  save_page(p, out, "frontend-update-notice");
  const FeLayout *Ly = ui_frontend_layout();
  UiRect later = Ly->upd_later, now = Ly->upd_now, btn = Ly->upd_btn;
  // Update now and Later report themselves
  int got = upd_click(p, snap, art, now, 201000.0, &kind, &arg);
  int ok = got == 1 && kind == FE_A_UPDATES && arg == 2;
  printf("%s updates click-through: Update now -> %d action(s), kind %d arg %d\n", ok ? "ok  " : "FAIL", got, kind, arg);
  fails += !ok;
  got = upd_click(p, snap, art, later, 202000.0, &kind, &arg);
  ok = got == 1 && kind == FE_A_UPDATES && arg == 1;
  printf("%s updates click-through: Later -> %d action(s), kind %d arg %d\n", ok ? "ok  " : "FAIL", got, kind, arg);
  fails += !ok;
  // the Updates button opens the panel (and reports it, so the presenter clears the dot)
  got = upd_click(p, snap, art, btn, 203000.0, &kind, &arg);
  ok = got == 1 && kind == FE_A_UPDATES && arg == 0 && u.open && u.expanded == -1;
  printf("%s updates click-through: Updates button opens the panel -> %d action(s), open %d\n", ok ? "ok  " : "FAIL", got, u.open);
  fails += !ok;
  u.show_dot = 0;
  for (int f = 0; f < 4; f++) fe_frame_draw(p, snap, art, acts, &na, 204000.0 + f * 400);
  save_page(p, out, "frontend-updates");
  // a row opens, then closes
  Ly = ui_frontend_layout();
  upd_click(p, snap, art, Ly->upd_row[0], 205000.0, &kind, &arg);
  ok = u.expanded == 0;
  printf("%s updates click-through: row opens (expanded %d)\n", ok ? "ok  " : "FAIL", u.expanded);
  fails += !ok;
  for (int f = 0; f < 4; f++) fe_frame_draw(p, snap, art, acts, &na, 206000.0 + f * 400);
  save_page(p, out, "frontend-updates-open");
  Ly = ui_frontend_layout();
  upd_click(p, snap, art, Ly->upd_row[0], 207000.0, &kind, &arg);
  ok = u.expanded == -1;
  printf("%s updates click-through: the same row closes (expanded %d)\n", ok ? "ok  " : "FAIL", u.expanded);
  fails += !ok;
  for (int f = 0; f < 2; f++) fe_frame_draw(p, snap, art, acts, &na, 207500.0 + f * 100);
  Ly = ui_frontend_layout();
  upd_click(p, snap, art, Ly->upd_row[2], 208000.0, &kind, &arg);
  ok = u.expanded == 2;
  printf("%s updates click-through: another row opens (expanded %d)\n", ok ? "ok  " : "FAIL", u.expanded);
  fails += !ok;
  for (int f = 0; f < 2; f++) fe_frame_draw(p, snap, art, acts, &na, 208500.0 + f * 100);
  Ly = ui_frontend_layout();
  UiRect before = Ly->upd_row[3], view = Ly->upd_view;
  int needs_scroll = !upd_row_visible(Ly, 3);
  printf("updates geometry: row3 y=%.1f h=%.1f, viewport y=%.1f h=%.1f, needs scroll %d\n",
         before.y, before.h, view.y, view.h, needs_scroll);
  save_page(p, out, "frontend-updates-before-scroll");
  if (before.y >= view.y + view.h) {
    upd_click(p, snap, art, before, 208800.0, &kind, &arg);
    ok = u.expanded == 2;
    printf("%s updates click-through: off-screen row is clipped (expanded %d)\n", ok ? "ok  " : "FAIL", u.expanded);
    fails += !ok;
  }
  int steps = upd_scroll_to_row(p, snap, art, 3, 209400.0);
  Ly = ui_frontend_layout();
  ok = upd_row_visible(Ly, 3) && (!needs_scroll || (steps > 0 && Ly->upd_row[3].y < before.y));
  printf("%s updates wheel: row3 visible after %d step(s), y %.1f -> %.1f\n",
         ok ? "ok  " : "FAIL", steps, before.y, Ly->upd_row[3].y);
  fails += !ok;
  save_page(p, out, "frontend-updates-scrolled");
  upd_click(p, snap, art, Ly->upd_row[3], 213000.0, &kind, &arg);
  ok = u.expanded == 3;
  printf("%s updates click-through: a third row switches (expanded %d)\n", ok ? "ok  " : "FAIL", u.expanded);
  fails += !ok;
  // Esc (close_req) and the X both close the panel
  u.close_req = 1;
  for (int f = 0; f < 2; f++) fe_frame_draw(p, snap, art, acts, &na, 214000.0 + f * 100);
  ok = !u.open;
  printf("%s updates: Esc closes the panel (open %d)\n", ok ? "ok  " : "FAIL", u.open);
  fails += !ok;
  u.open = 1;
  for (int f = 0; f < 2; f++) fe_frame_draw(p, snap, art, acts, &na, 214500.0 + f * 100);
  Ly = ui_frontend_layout();
  upd_click(p, snap, art, Ly->upd_close, 215000.0, &kind, &arg);
  ok = !u.open;
  printf("%s updates click-through: X closes the panel (open %d)\n", ok ? "ok  " : "FAIL", u.open);
  fails += !ok;
  snap->upd = NULL;
  return fails;
}

static int page_frontend(Page *p, const char *out) {
  static FeSnap snap;
  fe_demo(&snap);
  static const struct { int screen; const char *name; } pages[6] = {{FE_TITLE, "frontend-title"}, {FE_DIFFICULTY, "frontend-difficulty"},
                                                                  {FE_WORLD, "frontend-world"}, {FE_LOAD, "frontend-load"},
                                                                  {FE_PRO, "frontend-pro"}, {FE_THEME, "frontend-themes"}};
  SDL_Texture *art[FE_ART_MAX] = {0};
  FeAction acts[8];
  int fails = 0, na = 0;
  for (int pi = 0; pi < 6; pi++) {
    snap.screen = pages[pi].screen;
    for (int f = 0; f < 4; f++) fe_frame_draw(p, &snap, art, acts, &na, 50000.0 + pi * 5000 + f * 400);
    save_page(p, out, pages[pi].name);
  }
  // click-through: a press and release on the title's New game tile, a difficulty card, a property row and a save row
  struct { int screen; int pick; int kind, arg; const char *what; } clicks[4] = {
      {FE_TITLE, 1, FE_A_TITLE, 1, "title New game"}, {FE_DIFFICULTY, 2, FE_A_DIFFICULTY, 2, "difficulty Difficult"},
      {FE_WORLD, 0, FE_A_HOVER_TAG, 0, "property row"}, {FE_LOAD, 2, FE_A_ROW, 2, "save row"}};
  for (int i = 0; i < 4; i++) {
    snap.screen = clicks[i].screen;
    for (int f = 0; f < 2; f++) fe_frame_draw(p, &snap, art, acts, &na, 90000.0 + i * 5000 + f * 100);
    const FeLayout *Ly = ui_frontend_layout();
    UiRect r = clicks[i].screen == FE_TITLE || clicks[i].screen == FE_DIFFICULTY ? Ly->btn[clicks[i].pick] : Ly->row[clicks[i].pick];
    if (r.w <= 0) {
      printf("FAIL front end: no %s\n", clicks[i].what);
      fails++;
      continue;
    }
    float x = r.x + r.w / 2, y = r.y + r.h / 2;
    UiEvent mv = {UI_EV_MOVE, x, y}, dn = {UI_EV_DOWN, x, y, 1}, up = {UI_EV_UP, x, y, 1};
    int got = 0, kind = 0, arg = -1;
    for (int f = 0; f < 5; f++) {
      if (f == 1) ui_route_event(p->ui, &mv);
      if (f == 2) ui_route_event(p->ui, &dn);
      if (f == 3) ui_route_event(p->ui, &up);
      fe_frame_draw(p, &snap, art, acts, &na, 91000.0 + i * 5000 + f * 100);
      for (int a = 0; a < na; a++) got++, kind = acts[a].kind, arg = acts[a].arg;
    }
    int ok = got == 1 && kind == clicks[i].kind && arg == clicks[i].arg;
    printf("%s front end click-through: %s -> %s (%d action(s), kind %d arg %d)\n", ok ? "ok  " : "FAIL", clicks[i].what,
           ok ? "the right action" : "wrong", got, kind, arg);
    fails += !ok;
  }
  fails += page_updates(p, out, &snap, art);
  return fails;
}

// task-27: the dialogs of the original (popup menu, preferences, text entry, hire panel, message with a speaker)
static void dlg_item(DlgSnap *s, const char *text, int item) {
  snprintf(s->line[s->n_lines].text, DLG_TEXT, "%s", text);
  s->line[s->n_lines].item = item;
  s->n_lines++;
}

static void dlg_demo(DlgSnap *s, int kind) {
  memset(s, 0, sizeof *s);
  s->magic = DLG_MAGIC;
  s->kind = kind;
  s->sel = -1;
  s->hover = -1;
  if (kind == DLG_MENU) {
    static const char *const it[8] = {"Save the Current Game", "Load a Previous Game", "Save Course for Championship", "Save Gary Golf for Championship",
                                      "Rename Your Course", "Edit Preferences", "Cancel match/tournament", "Exit the Game"};
    for (int i = 0; i < 8; i++) dlg_item(s, it[i], i), s->n_items++;
  } else if (kind == DLG_CHECKS) {
    static const char *const it[6] = {"Display golfer names on screen.", "Show snapshots of great shots.", "Full explanation of golfer comments.",
                                      "Display message for new members.", "Display golfer thoughts on screen.", "Include animals and habitats."};
    dlg_item(s, "Preferences...", -1);
    for (int i = 0; i < 6; i++) dlg_item(s, it[i], i), s->n_items++;
    s->mask = 0x3b;
  } else if (kind == DLG_ENTRY) {
    snprintf(s->prompt, sizeof s->prompt, "Rename Course...");
    snprintf(s->field, sizeof s->field, "Ocean's Edge");
    s->caret = 12;
    s->max_chars = 32;
  } else if (kind == DLG_HIRE) {
    static const char *const gr[4] = {"Greeters...", "Speed up play...", "Weed Killers...", "Thirst quenchers..."};
    static const char *const jb[8] = {"Club Pro", "Celebrity", "Ranger", "Marshall", "Groundskeeper", "Technician", "Soda Vendor", "Refresher"};
    static const int wg[8] = {300, 700, 200, 300, 200, 400, 200, 500};
    for (int i = 0; i < 4; i++) snprintf(s->group[i], sizeof s->group[i], "%s", gr[i]);
    for (int i = 0; i < 8; i++) snprintf(s->job[i], sizeof s->job[i], "%s", jb[i]), s->wage[i] = wg[i];
  } else if (kind == DLG_HINT) {
    snprintf(s->text, sizeof s->text, "To UNDO a previous action, right click on the map to display the effect of the undo. Right click again to effect the change. Your money will be refunded!");
    s->speaker = -2;
  }
}

static int dlg_frame(Page *p, const DlgSnap *s, SDL_Texture *const *art, DlgAction *acts, double t) {
  begin_page(p, t);
  int n = ui_dialogs_draw(p->ui, s, art, acts, 8);
  ui_end(p->ui);
  return n;
}

static int page_dialogs(Page *p, const char *out) {
  static const struct { int kind; const char *name; } pages[5] = {{DLG_MENU, "dialogs-menu"}, {DLG_CHECKS, "dialogs-checks"}, {DLG_ENTRY, "dialogs-entry"},
                                                                {DLG_HIRE, "dialogs-hire"}, {DLG_HINT, "dialogs-hint"}};
  static DlgSnap snap;
  SDL_Texture *art[DLG_ART_MAX] = {0};
  DlgAction acts[8];
  int fails = 0;
  for (int pi = 0; pi < 5; pi++) {
    dlg_demo(&snap, pages[pi].kind);
    for (int f = 0; f < 4; f++) dlg_frame(p, &snap, art, acts, 60000.0 + pi * 5000 + f * 400);
    save_page(p, out, pages[pi].name);
  }
  // click-through: a press and release on a menu choice (the 3rd), a preferences toggle (the 2nd) and a hire job (the 5th)
  struct { int kind; float fx, fy; int act, arg; const char *what; } clicks[3] = {
      {DLG_MENU, 0.5f, 0, DLG_A_ITEM, 2, "menu choice"}, {DLG_CHECKS, 0.5f, 0, DLG_A_ITEM, 1, "preferences toggle"}, {DLG_HIRE, 0.5f, 0, DLG_A_ITEM, 4, "hire job"}};
  for (int i = 0; i < 3; i++) {
    dlg_demo(&snap, clicks[i].kind);
    for (int f = 0; f < 3; f++) dlg_frame(p, &snap, art, acts, 80000.0 + i * 5000 + f * 100);
    // find the row by clicking down the middle of the screen until the wanted action comes: the rows are the only controls there
    int found = 0, got = 0, kind = 0, arg = -1;
    float cx = p->w / 2.0f;
    for (float y = p->h * 0.2f; y < p->h * 0.8f && !found; y += p->h / 90.0f) {
      UiEvent mv = {UI_EV_MOVE, cx, y}, dn = {UI_EV_DOWN, cx, y, 1}, up = {UI_EV_UP, cx, y, 1};
      got = 0;
      for (int f = 0; f < 5; f++) {
        if (f == 1) ui_route_event(p->ui, &mv);
        if (f == 2) ui_route_event(p->ui, &dn);
        if (f == 3) ui_route_event(p->ui, &up);
        int n = dlg_frame(p, &snap, art, acts, 81000.0 + f * 100);
        for (int a = 0; a < n; a++) got++, kind = acts[a].kind, arg = acts[a].arg;
      }
      if (got == 1 && kind == clicks[i].act && arg == clicks[i].arg) found = 1;
    }
    printf("%s dialogs click-through: %s -> %s\n", found ? "ok  " : "FAIL", clicks[i].what, found ? "the right action" : "never reached");
    fails += !found;
  }
  return fails;
}

// task-73 pages: the golfer profile and the land purchase map, from made-up snapshots with golf's places
static void rp_demo_text(RpSnap *s, int x, int y, int align, int col, const char *text) {
  if (s->n >= RP_ITEMS) return;
  RpItem *it = &s->it[s->n++];
  memset(it, 0, sizeof *it);
  it->type = RP_IT_TEXT, it->x = (int16_t)x, it->y = (int16_t)y, it->align = (uint8_t)align, it->col = (uint16_t)col;
  snprintf(it->s, sizeof it->s, "%s", text);
}

static void rp_demo_group_text(RpSnap *s, int group, const char *text) {
  rp_demo_text(s, 0, 0, RP_AL_LEFT, 0, text);
  if (s->n) s->it[s->n - 1].group = (uint8_t)group;
}

static void pages_demo(RpSnap *s, int kind) {
  if (kind == RP_TOP_DESIGNERS) {   // issue-13: preserve the legacy score components and difficulty-weighted total
    static const struct { const char *designer, *course; int fun, skill_x100, cash, multiplier; } rows[10] = {
      {"B. L. Dye", "Harbour Lights GC", 949, 877, 7450, 4}, {"H. X. Dye", "Pine Valley", 835, 722, 6440, 3},
      {"L. F. Dye", "Dolphin Coast", 790, 639, 6030, 3}, {"O. G. Dye", "Cypress Point", 628, 580, 5380, 3},
      {"L. E. Dye", "Red Rock Golf Club", 533, 505, 4020, 2}, {"I. J. Dye", "Lakeside", 434, 405, 3750, 2},
      {"Y. K. Dye", "Meadow Run", 319, 345, 2850, 2}, {"Z. N. Dye", "Tidewater", 214, 265, 2000, 1},
      {"K. E. Dye", "Hilltop Links", 197, 111, 910, 1}, {"Gary Golf", "Starter Course", 136, 119, 180, 1},
    };
    memset(s, 0, sizeof *s);
    s->magic = RP_MAGIC, s->kind = kind, s->ext[0] = 0, s->ext[1] = 10;
    snprintf(s->title, sizeof s->title, "Top Ten Designers");
    for (int i = 0; i < 10; i++) {
      char value[RP_TEXT];
      rp_demo_text(s, 0, i, RP_AL_LEFT, 0, rows[i].designer);
      rp_demo_text(s, 1, i, RP_AL_LEFT, 0, rows[i].course);
      snprintf(value, sizeof value, "%d", rows[i].fun);
      rp_demo_text(s, 2, i, RP_AL_LEFT, 0, value);
      snprintf(value, sizeof value, "%d.%02d", rows[i].skill_x100 / 100, rows[i].skill_x100 % 100);
      rp_demo_text(s, 3, i, RP_AL_LEFT, 0, value);
      snprintf(value, sizeof value, "%d", rows[i].cash / 10);
      rp_demo_text(s, 4, i, RP_AL_LEFT, 0, value);
      snprintf(value, sizeof value, "%d", (rows[i].cash / 10 + rows[i].skill_x100 + rows[i].fun) * rows[i].multiplier);
      rp_demo_text(s, 5, i, RP_AL_LEFT, 0, value);
      snprintf(value, sizeof value, "x%d", rows[i].multiplier);
      rp_demo_text(s, 6, i, RP_AL_LEFT, 0, value);
    }
    return;
  }
  if (kind == RP_PHOTO) {   // task-105: the accomplishment board, two photos, the newest just won with points to follow
    memset(s, 0, sizeof *s);
    s->magic = RP_MAGIC, s->kind = kind;
    s->ext[1] = 3, s->ext[2] = 1, s->ext[3] = 2, s->ext[5] = 1;
    static const struct { int id, x, y; const char *name, *cap; } ph[2] = {{0, 20, 0x1bb - 0xa6, "1st Challenge Hole", "Dolphin Coast MC 2 March 2001"},
                                                                          {3, 535, 0x1ad - 0xa6, "1st Tournament", "Dolphin Coast MC 6 March 2001"}};
    for (int i = 0; i < 2; i++) {
      RpItem *it = &s->it[s->n++];
      it->type = RP_IT_RECT, it->x = (int16_t)ph[i].x, it->y = (int16_t)ph[i].y, it->w = 200, it->h = 160, it->art = (uint8_t)ph[i].id;
      snprintf(it->s, sizeof it->s, "%s", ph[i].name);
      rp_demo_text(s, ph[i].x, ph[i].y + 0xa6, RP_AL_LEFT, 0x2108, ph[i].cap);
    }
    return;
  }
  if (kind == RP_END_YEAR) {
    memset(s, 0, sizeof *s);
    s->magic = RP_MAGIC, s->kind = kind;
    snprintf(s->title, sizeof s->title, "End of Year 2001");
    rp_demo_text(s, 0x196, 0x37, RP_AL_LEFT, 0, "End of Year 2001");
    s->it[s->n - 1].big = 1;
    rp_demo_text(s, 0x1d1, 0x5d, RP_AL_LEFT, 0, "This Year");
    rp_demo_text(s, 0x236, 0x5d, RP_AL_LEFT, 0, "Last Year");
    rp_demo_text(s, 0x194, 200, RP_AL_LEFT, 0, "Highlights");
    rp_demo_text(s, 0x12d, 0x73, RP_AL_LEFT, 0, "Rounds played");
    rp_demo_text(s, 0x236, 0x73, RP_AL_RIGHT, 0, "1,248");
    rp_demo_text(s, 0x12d, 0x87, RP_AL_LEFT, 0, "Average score");
    rp_demo_text(s, 0x236, 0x87, RP_AL_RIGHT, 0, "82.4");
    rp_demo_text(s, 0x12d, 0x9b, RP_AL_LEFT, 0, "Course rating");
    rp_demo_text(s, 0x236, 0x9b, RP_AL_RIGHT, 0, "4 stars");
    rp_demo_text(s, 0x12d, 0xaf, RP_AL_LEFT, 0, "Membership grew by 18 percent this year.");
    rp_demo_text(s, 0x12d, 0xc3, RP_AL_LEFT, 0, "Three golfers earned a place on the honor roll.");
    return;
  }
  if (kind == RP_PAIRING) {
    static const struct { const char *name, *bio, *status, *age; } golfers[8] = {
        {"Mack Putter", "(Surfer)", "Single", "27 years old"},
        {"Tina Fairway", "(Bookworm)", "Married", "32 years old"},
        {"Owen Driver", "(Jock)", "Single", "24 years old"},
        {"Nina Green", "(Artist)", "Widowed", "46 years old"},
        {"Cal Birdie", "(Prankster)", "Divorced", "38 years old"},
        {"Lena Links", "(Nature Lover)", "Married", "41 years old"},
        {"Ray Bunker", "(Workaholic)", "Single", "29 years old"},
        {"Mia Eagle", "(Socialite)", "Married", "35 years old"},
    };
    memset(s, 0, sizeof *s);
    s->magic = RP_MAGIC, s->kind = kind;
    s->ext[0] = (1u << 1) | (1u << 2), s->ext[1] = 2, s->ext[2] = 8, s->ext[3] = 2, s->ext[4] = 1, s->ext[5] = 1;
    rp_demo_group_text(s, 0xff, "SELECT THE NEXT PAIR OF GOLFERS");
    for (int i = 0; i < 8; i++) {
      rp_demo_group_text(s, i, golfers[i].name);
      rp_demo_group_text(s, i, golfers[i].bio);
      rp_demo_group_text(s, i, golfers[i].age);
      rp_demo_group_text(s, i, golfers[i].status);
    }
    return;
  }
  if (kind == RP_SKILLS) {   // task-102: the pro's ten skill points, three spent
    static const char *const nm[10] = {"Power Hitter", "Long Driver", "Accurate Driver", "Accurate Irons", "Accurate Putter",
                                       "Draw Shot (R to L)", "Fade Shot (L to R)", "High Backspin Shot", "Recovery Skills", "Luck"};
    memset(s, 0, sizeof *s);
    s->magic = RP_MAGIC, s->kind = kind;
    snprintf(s->title, sizeof s->title, "Gary Golf");
    snprintf(s->bio, sizeof s->bio, "Before you play your course you may customize your character by improving his or her golf skills. You can also win additional skill points for each accomplishment added to your trophy.");
    s->ext[1] = 10, s->ext[2] = 7, s->ext[3] = 200, s->ext[4] = -1;
    for (int i = 0; i < 10; i++) {
      RpItem *it = &s->it[s->n++];
      it->type = RP_IT_TEXT, it->x = (int16_t)i, it->y = (int16_t)(i == 0 ? 2 : i == 4 ? 1 : 0);
      snprintf(it->s, sizeof it->s, "%s", nm[i]);
    }
    return;
  }
  int pro = kind == -RP_PROFILE;   // task-102: the player's pro as a new career opens it (first step of the creation)
  if (pro) kind = RP_PROFILE;
  memset(s, 0, sizeof *s);
  s->magic = RP_MAGIC, s->kind = kind;
  if (pro) {
    static const char *const lev[10] = {"Power Hitter", "Driving Accuracy", "Long Irons", "Short Irons", "Chipping",
                                        "Sand Master", "Putting", "Fade", "Draw", "Recovery"};
    s->ext[1] = 0, s->ext[2] = 1, s->ext[3] = 10;
    rp_demo_text(s, 585, 30, RP_AL_LEFT, 0, "Golf Skill Levels");
    for (int i = 0; i < 10; i++) {
      rp_demo_text(s, 501, 61 + 16 * i, RP_AL_LEFT, 0, "0");
      rp_demo_text(s, 546, 61 + 16 * i, RP_AL_LEFT, 0, lev[i]);
    }
  }
  if (kind == RP_PROFILE) {
    static const char *const trait[5] = {"Neat", "Outgoing", "Active", "Playful", "Nice"};
    static const char *const skill[3] = {"Length", "Accuracy", "Imagination"};
    rp_demo_text(s, 154, 24, RP_AL_LEFT, 0, "Biff");
    rp_demo_text(s, 154, 48, RP_AL_LEFT, 0, "(Surfer)");
    rp_demo_text(s, 154, 69, RP_AL_LEFT, 0, "Single");
    rp_demo_text(s, 154, 90, RP_AL_LEFT, 0, "Young");
    for (int i = 0; i < 5; i++) rp_demo_text(s, 93, 122 + 28 * i, RP_AL_CENTRE, i == 1 || i == 2 ? 0x0848 : 0, trait[i]);
    for (int i = 0; i < 3; i++) rp_demo_text(s, 212, 142 + 35 * i, RP_AL_CENTRE, i != 1 ? 0x0848 : 0, skill[i]);
    if (!pro) {
      rp_demo_text(s, 580, 30, RP_AL_LEFT, 0, "Visitor");
      rp_demo_text(s, 580, 50, RP_AL_LEFT, 0, "Low round: -");
      rp_demo_text(s, 580, 64, RP_AL_LEFT, 0, "Handicap: -");
      rp_demo_text(s, 580, 78, RP_AL_LEFT, 0, "Rounds played: 1");
    }
    static const char *const lab[6] = {"Signature saying...", "Made good shot...", "Too easy shot...", "Hard shot...", "Hit bad shot...", "Hit into the water..."};
    static const char *const val[6] = {"Surf's up, dude.", "How'd you like that shot, Mary!", "This shot looks pretty easy.", "Eeek, I gotta stay away from the DATA(tees).", "Darn, I'm DATA(in the tees)!", "Nooooo, I'm in the drink!"};
    for (int i = 0; i < 6; i++) {
      rp_demo_text(s, 52, 271 + 16 * i, RP_AL_LEFT, i == 2 || i == 4 ? 0x7d08 : i == 1 ? 0x23e8 : 0, lab[i]);
      rp_demo_text(s, 412, 271 + 16 * i, RP_AL_LEFT, 0x6318, val[i]);
    }
    snprintf(s->bio, sizeof s->bio, "Biff likes milkshakes, Super Bowl parties, and broccoli.");
  } else if (kind == RP_TRACTS) {
    static const int cx[3] = {78, 334, 600};
    static const char *const what[9] = {"4 acres of brush, water, and tropical bushes", "", "4 acres of brush, water, and water", "4 acres of brush, tropical trees, and palm trees", "",
                                        "4 acres of brush, tropical trees, and palm trees", "8 acres of brush, brush, and brush", "4 acres of brush, tropical bushes, and tropical trees", "8 acres of brush, tropical trees, and rocks"};
    static const char *const price[9] = {"Price: \xA7" "4,000", "", "Price: \xA7" "4,000", "Price: \xA7" "3,000", "", "Price: \xA7" "3,000", "Price: \xA7" "7,000", "Price: \xA7" "3,000", "Price: \xA7" "8,000"};
    for (int n = 0; n < 9; n++) {
      int x = cx[n / 3], y = 62 + 68 * (n % 3);
      if (!what[n][0]) {
        rp_demo_text(s, x + 4, y + 2, RP_AL_LEFT, 0, "Already purchased.");
        continue;
      }
      char line[RP_TEXT];
      snprintf(line, sizeof line, "Buy tract #%d, %s", n + 1, what[n]);
      rp_demo_text(s, x, y, RP_AL_LEFT, 0, line);
      rp_demo_text(s, x + 0xa5, y + 30, RP_AL_RIGHT, 0, price[n]);
    }
    rp_demo_text(s, 548, 263, RP_AL_LEFT, 0, "Cash Reserve");
    rp_demo_text(s, 720, 264, RP_AL_CENTRE, 0, "\xA7" "47,816,700");
  }
}

int ui_pages_raw_data(void);

static int page_pages(Page *p, const char *out) {
  static const struct { int kind; const char *name; } pages[8] = {{RP_PROFILE, "pages-profile"}, {RP_TRACTS, "pages-tracts"}, {-RP_PROFILE, "pages-pro-create"}, {RP_SKILLS, "pages-skills"}, {RP_PHOTO, "pages-board"}, {RP_END_YEAR, "pages-year"}, {RP_TOP_DESIGNERS, "pages-top-designers"}, {RP_PAIRING, "pages-pairing"}};
  static RpSnap snap;
  RpAction acts[8];
  for (int pi = 0; pi < 8; pi++) {
    pages_demo(&snap, pages[pi].kind);
    for (int f = 0; f < 4; f++) {
      begin_page(p, 90000.0 + pi * 5000 + f * 400);
      ui_reports_draw(p->ui, &snap, NULL, acts, 8);
      ui_end(p->ui);
    }
    save_page(p, out, pages[pi].name);
  }
  int raw = ui_pages_raw_data();
  printf("%s pages: no visible profile text contains DATA( (%d found)\n", raw ? "FAIL" : "ok  ", raw);
  return raw ? 1 : 0;
}

static void page_dialog(Page *p, const char *out) {
  UiCtx *c = p->ui;
  const UiTokens *t = ui_tokens(c);
  for (int f = 0; f < 2; f++) {
    begin_page(p, 8000.0 + f * 400);
    UiRect in = ui_dialog_begin(c, ui_id("g.dlg"), ui_s(c, 600), ui_s(c, 300), "Save the current game?");
    UiFont bf = ui_font(c, UI_TYPE_BODY);
    ui_text(c, in.x, in.y + bf.px, UI_TYPE_BODY, t->text_secondary, "Your course and golfers are kept in Saved Games.");
    ui_text(c, in.x, in.y + bf.px + bf.line, UI_TYPE_BODY, t->text_secondary, "You can keep playing afterwards.");
    float bw = ui_s(c, 140), bh = ui_s(c, t->button_h);
    ui_button(c, ui_id("g.ok"), ui_rect(in.x + in.w - bw, in.y + in.h - bh, bw, bh), "Save", 0, UI_BTN_PRIMARY, 0);
    ui_button(c, ui_id("g.cancel"), ui_rect(in.x + in.w - 2 * bw - ui_s(c, 12), in.y + in.h - bh + ui_s(c, 2), bw, ui_s(c, t->pill_h)),
              "Cancel", 0, UI_BTN_PILL, 0);
    ui_end(c);
  }
  save_page(p, out, "dialog");
}

// task-22: the Mods screen over the repo's mods folder (--mods DIR), with a draft that turns the sample mods on
// so a conflict, a dependency and a missing dependency show.
static const char *g_mods_dir = "mods";
static int g_only_mods;   // --only mods: just the Mods pages
static int g_only_updates;   // --only updates: focused wheel and click checks
static int g_only_pages;     // --only pages: focused modal-page rendering
static int g_only_core;      // --only core: the normal gallery without pages or mods
static int g_only_course;    // --only course: HUD, components and golfer controls
static int g_only_frontend;  // --only frontend: title, setup, load and update screens
static int g_only_dialogs;   // --only dialogs: popup, entry and hire dialogs
static int g_only_golfers;   // --only golfers: native card, diary and click checks

static int page_mods(Page *p, const char *out) {
  const char *running[16];
  int nrun = 0;
  {   // what the game "loaded": the default profile's mods
    ModMgr *probe = modmgr_open(g_mods_dir, NULL, -1);
    const char *ids[64];
    nrun = modmgr_list(probe, ids, 16);
    for (int i = 0; i < nrun; i++) running[i] = strdup(ids[i]);
    modmgr_close(probe);
  }
  ModMgr *m = modmgr_open(g_mods_dir, running, nrun);
  if (!m || modmgr_count(m) == 0) {
    printf("FAIL mods page: no mods in %s\n", g_mods_dir);
    return 1;
  }
  char err[160];
  modmgr_profile_create(m, "my_setup", 1, err, sizeof err);   // a copy of the default, to edit
  const char *turn_on[] = {"sample_pricey_bar", "sample_cheap_bar", "sample_bar_tips", "sample_fancy_menu"};
  for (int i = 0; i < 4; i++) {
    int k = modmgr_find(m, turn_on[i]);
    if (k >= 0) modmgr_set_enabled(m, k, 1);
  }
  ModsUi ui;
  ui_mods_init(&ui, m);
  ui.selected = modmgr_find(m, "sample_pricey_bar");
  ui.view.sort = MM_SORT_RECENT;   // the sample mods are the newest
  for (int f = 0; f < 4; f++) {
    begin_page(p, 120000.0 + f * 400);
    ui_mods_draw(p->ui, &ui, 120000.0 + f * 400);
    ui_end(p->ui);
  }
  save_page(p, out, "mods-list");
  // filtered: the conflicts chip and a search
  ui.view.only_conflicts = 1;
  ui.selected = modmgr_find(m, "sample_fancy_menu");
  for (int f = 0; f < 4; f++) {
    begin_page(p, 121000.0 + f * 400);
    ui_mods_draw(p->ui, &ui, 121000.0 + f * 400);
    ui_end(p->ui);
  }
  save_page(p, out, "mods-conflicts");
  ui.view.only_conflicts = 0;
  ui.view.sort = MM_SORT_NAME;
  snprintf(ui.view.search, sizeof ui.view.search, "bar");
  ui.selected = modmgr_find(m, "sample_bar_tips");
  for (int f = 0; f < 4; f++) {
    begin_page(p, 122000.0 + f * 400);
    ui_mods_draw(p->ui, &ui, 122000.0 + f * 400);
    ui_end(p->ui);
  }
  save_page(p, out, "mods-search");
  ui.view.search[0] = 0;
  ui.dd_open = 1;
  for (int f = 0; f < 4; f++) {
    begin_page(p, 123000.0 + f * 400);
    ui_mods_draw(p->ui, &ui, 123000.0 + f * 400);
    ui_end(p->ui);
  }
  save_page(p, out, "mods-profiles");
  ui.dd_open = 0;
  ui.dialog = MUI_DLG_NEW;
  snprintf(ui.field, sizeof ui.field, "weekend");
  ui.dlg_copy = 1;
  for (int f = 0; f < 4; f++) {
    begin_page(p, 124000.0 + f * 400);
    ui_mods_draw(p->ui, &ui, 124000.0 + f * 400);
    ui_end(p->ui);
  }
  save_page(p, out, "mods-newprofile");
  modmgr_close(m);
  return 0;
}

int main(int argc, char **argv) {
  const char *out = "evidence/ui-kit", *fonts = "build/native/fonts/plus-jakarta-sans", *tokens = "data/ui/frosted_mint.toml";
  const char *backdrop = NULL, *sizes = "1920x1080,3840x2160", *renderer = "software", *artdir = NULL;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--out")) out = argv[i + 1];
    else if (!strcmp(argv[i], "--fonts")) fonts = argv[i + 1];
    else if (!strcmp(argv[i], "--tokens")) tokens = argv[i + 1];
    else if (!strcmp(argv[i], "--backdrop")) backdrop = argv[i + 1];
    else if (!strcmp(argv[i], "--sizes")) sizes = argv[i + 1];
    else if (!strcmp(argv[i], "--renderer")) renderer = argv[i + 1];
    else if (!strcmp(argv[i], "--faces")) g_old_faces = argv[i + 1];   // mood_<i>.bmp from SIMGOLF_FACES_DUMP
    else if (!strcmp(argv[i], "--mods")) g_mods_dir = argv[i + 1];
    else if (!strcmp(argv[i], "--only")) {
      g_only_mods = !strcmp(argv[i + 1], "mods");
      g_only_updates = !strcmp(argv[i + 1], "updates");
      g_only_pages = !strcmp(argv[i + 1], "pages");
      g_only_core = !strcmp(argv[i + 1], "core");
      g_only_course = !strcmp(argv[i + 1], "course");
      g_only_frontend = !strcmp(argv[i + 1], "frontend");
      g_only_dialogs = !strcmp(argv[i + 1], "dialogs");
      g_only_golfers = !strcmp(argv[i + 1], "golfers");
    }
    else if (!strcmp(argv[i], "--art")) artdir = argv[i + 1];   // art_<slot>.bmp from SIMGOLF_UI_DUMP_ART
  }
  SDL_SetMainReady();
  SDL_SetHint(SDL_HINT_VIDEODRIVER, strcmp(renderer, "software") ? "windows" : "dummy");
  if (SDL_Init(strcmp(renderer, "software") ? SDL_INIT_VIDEO : 0)) {
    fprintf(stderr, "gallery: SDL_Init: %s\n", SDL_GetError());
    return 3;
  }
  UiTokens tok;
  ui_tokens_load_all(&tok, tokens, NULL, 0);
  SDL_Surface *bmp = load_bmp(backdrop);
  int fails = 0;
  char list[256];
  snprintf(list, sizeof list, "%s", sizes);
  for (char *sz = strtok(list, ","); sz; sz = strtok(NULL, ",")) {
    int w = 0, h = 0;
    if (sscanf(sz, "%dx%d", &w, &h) != 2 || w < 320 || h < 240) continue;
    SDL_Surface *surf = NULL;
    SDL_Window *win = NULL;
    SDL_Renderer *r = NULL;
    if (!strcmp(renderer, "software")) {
      surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
      r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    } else {
      win = SDL_CreateWindow("ui_gallery", 0, 0, 64, 64, SDL_WINDOW_HIDDEN);
      SDL_SetHint(SDL_HINT_RENDER_DRIVER, renderer);
      r = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE) : NULL;
    }
    if (!r) {
      fprintf(stderr, "gallery: no %s renderer: %s\n", renderer, SDL_GetError());
      return 3;
    }
    SDL_RendererInfo info;
    SDL_GetRendererInfo(r, &info);
    Page p = {0};
    p.r = r;
    p.w = w;
    p.h = h;
    p.scale = ui_scale_for_height(h);
    p.canvas = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    p.course = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_SetTextureScaleMode(p.course, SDL_ScaleModeLinear);
    // backdrop
    UiDraw d;
    uid_init(&d, r);
    uid_begin(&d, p.course, w, h);
    if (bmp) {
      SDL_Texture *bt = SDL_CreateTextureFromSurface(r, bmp);
      SDL_SetTextureScaleMode(bt, SDL_ScaleModeLinear);
      SDL_RenderCopy(r, bt, NULL, NULL);
      SDL_DestroyTexture(bt);
    } else {
      draw_course(&d, w, h);
    }
    uid_free(&d);
    p.ui = ui_create(r, &tok, fonts);
    p.fonts = fonts;
    static const UiColor tops[16][2] = {
        {{70, 150, 60, 255}, {60, 70, 120, 255}}, {{120, 200, 80, 255}, {60, 70, 120, 255}}, {{236, 222, 170, 255}, {60, 70, 120, 255}},
        {{80, 140, 60, 255}, {60, 70, 120, 255}}, {{220, 205, 160, 255}, {60, 70, 120, 255}}, {{70, 140, 190, 255}, {60, 70, 120, 255}},
        {{60, 120, 170, 255}, {60, 70, 120, 255}}, {{140, 200, 90, 255}, {60, 70, 120, 255}}, {{150, 210, 100, 255}, {60, 70, 120, 255}},
        {{50, 110, 50, 255}, {60, 70, 120, 255}}, {{210, 200, 170, 255}, {60, 70, 120, 255}}, {{110, 120, 60, 255}, {60, 70, 120, 255}},
        {{150, 150, 140, 255}, {60, 70, 120, 255}}, {{40, 100, 50, 255}, {60, 70, 120, 255}}, {{50, 120, 60, 255}, {60, 70, 120, 255}},
        {{30, 90, 50, 255}, {60, 70, 120, 255}}};
    for (int i = 0; i < 16; i++) p.art[HUD_ART_TERRAIN + i] = tile_art(r, tops[i][0], tops[i][1]);
    for (int i = 0; artdir && i < HUD_ART_MAX; i++) {   // the game's own art, when given
      char ap[1024];
      snprintf(ap, sizeof ap, "%s/art_%d.bmp", artdir, i);
      SDL_Surface *as = SDL_LoadBMP(ap);
      if (!as) continue;
      SDL_Texture *at = SDL_CreateTextureFromSurface(r, as);
      SDL_FreeSurface(as);
      if (!at) continue;
      SDL_SetTextureBlendMode(at, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(at, SDL_ScaleModeLinear);
      if (p.art[i]) SDL_DestroyTexture(p.art[i]);
      p.art[i] = at;
    }
    printf("gallery %dx%d on %s, UI scale %.2f\n", w, h, info.name, p.scale);
    if (g_only_golfers) {
      fails += page_golfers(&p, out);
    } else if (g_only_pages) {
      fails += page_pages(&p, out);
    } else if (g_only_updates) {
      static FeSnap snap;
      SDL_Texture *art[FE_ART_MAX] = {0};
      fe_demo(&snap);
      fails += page_updates(&p, out, &snap, art);
    } else if (g_only_frontend) {
      fails += page_frontend(&p, out);
    } else if (g_only_dialogs) {
      fails += page_dialogs(&p, out);
    } else if (!g_only_mods) {
      page_hud(&p, out, HUD_JWE, "");
      page_hud(&p, out, HUD_REGULAR, "-regular");
      page_messages(&p, out);
      page_density(&p, out);
      page_components(&p, out);
      page_dialog(&p, out);
      fails += page_golfers(&p, out);
      page_names(&p, out);
      page_faces(&p, out);
      page_zoomout(&p, out);
      page_contrast(&p, out, 0.75f, "o075");
      page_contrast(&p, out, 0.85f, "default");
      page_contrast(&p, out, 1.0f, "task31");
      if (!g_only_course) {
        fails += page_frontend(&p, out);
        fails += page_dialogs(&p, out);
        if (!g_only_core) fails += page_pages(&p, out);
      }
    }
    if (!g_only_updates && !g_only_pages && !g_only_core &&
        !g_only_course && !g_only_frontend && !g_only_dialogs && !g_only_golfers) fails += page_mods(&p, out);
    ui_destroy(p.ui);
    for (int i = 0; i < HUD_ART_MAX; i++)
      if (p.art[i]) SDL_DestroyTexture(p.art[i]);
    SDL_DestroyTexture(p.canvas);
    SDL_DestroyTexture(p.course);
    ui_course_map_clear(r);
    SDL_DestroyRenderer(r);
    if (surf) SDL_FreeSurface(surf);
    if (win) SDL_DestroyWindow(win);
  }
  if (bmp) SDL_FreeSurface(bmp);
  SDL_Quit();
  return fails ? 1 : 0;
}
