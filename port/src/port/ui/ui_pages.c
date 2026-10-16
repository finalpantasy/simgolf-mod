#pragma clang diagnostic error "-Wimplicit-function-declaration"
// Frosted Mint pages (simgolf mod, task-73): the last old modal screens of the original as native panels, a pure
// function of an RpSnap (port/reports_frosted.h) like the reports. The golfer profile page (the editor) lives here;
// the snapshot popup and the land purchase map follow in the same file. reports_frosted.c calls ui_pages_draw on the
// render thread, the gallery with a made-up snapshot.
//
// Like the dialogs, a page only shows what golf's page shows (golf's text calls were recorded with their places and
// colours by reports_game.c) and says what the player chose as a click at the ORIGINAL control's place (RP_A_CLICK in
// golf's 800x600 page), so golf's own handlers run. docs/ui/KIT.md "Pages".
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "port/reports_frosted.h"
#include "port/ui/ui_draw.h"

#define CLICK(x, y) ((x) | ((y) << 16))

// ------------------------------------------------------------------ shared pieces

// golf's money sign is its font's character 0xA7; the kit's font has a real dollar sign
static void fix_text(char *dst, int cap, const char *src) {
  int n = 0;
  for (; *src && n < cap - 1; src++) dst[n++] = (unsigned char)*src == 0xA7 ? '$' : (*src == '^' ? ' ' : *src);
  dst[n] = 0;
}

static const RpItem *txt(const RpSnap *s, int x, int y) {
  for (int i = 0; i < s->n; i++)
    if (s->it[i].type == RP_IT_TEXT && s->it[i].x == x && s->it[i].y == y && s->it[i].s[0]) return &s->it[i];
  return NULL;
}

// golf's text colours are 15 bit (0x7fff is white): reds are the bad things, greens the good things
static UiColor text_colour(const UiTokens *t, uint16_t c) {
  int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
  r = r * 255 / 31, g = g * 255 / 31, b = b * 255 / 31;
  if (r > 90 && r > g + 40 && r > b + 40) return t->danger;
  if (g > 90 && g > r + 40 && g > b + 40) return t->accent_soft_text;
  return t->text_primary;
}

// Greedy word wrap of s to width w into lines of at most 120 characters; returns the number of lines.
enum { WRAP_LINES = 12, WRAP_LEN = 120 };
static int wrap(UiCtx *c, int style, const char *s, float w, char out[WRAP_LINES][WRAP_LEN]) {
  int n = 0;
  char cur[WRAP_LEN * 2] = "";
  const char *p = s;
  while (*p && n < WRAP_LINES) {
    char word[WRAP_LEN];
    int k = 0;
    while (*p == ' ') p++;
    while (*p && *p != ' ' && k < WRAP_LEN - 1) word[k++] = *p++;
    word[k] = 0;
    if (!k) break;
    char test[WRAP_LEN * 3];
    snprintf(test, sizeof test, "%s%s%s", cur, cur[0] ? " " : "", word);
    if (cur[0] && ui_text_width(c, style, test) > w) {
      snprintf(out[n++], WRAP_LEN, "%s", cur);
      snprintf(cur, sizeof cur, "%s", word);
    } else {
      snprintf(cur, sizeof cur, "%.*s", WRAP_LEN - 1, test);
    }
  }
  if (cur[0] && n < WRAP_LINES) snprintf(out[n++], WRAP_LEN, "%s", cur);
  return n;
}

// A pill button of the pages: soft inset, accent when on. Returns 1 on click.
static int chip(UiCtx *c, uint32_t id, UiRect r, const char *label, int on, int disabled, int style) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  float rad = fminf(r.h / 2, ui_s(c, t->radius_control));
  ui_hit(c, r, rad, id, disabled ? UI_HIT_BLOCK : UI_HIT_BLOCK | UI_HIT_INTERACT);
  float hov = ui_anim(c, id, !disabled && ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
  if (on) {
    uid_rrect(d, r, rad, uid_mix(t->accent_soft, t->accent, 0.18f * hov));
    uid_rrect_stroke(d, r, rad, ui_s(c, 1.5f), uid_alpha(t->accent, 0.7f));
  } else {
    uid_rrect(d, r, rad, uid_alpha(t->surface_inset, 0.55f + 0.45f * hov));
    uid_rrect_stroke(d, r, rad, ui_s(c, 1), uid_alpha(t->divider, 0.5f + 0.5f * hov));
  }
  UiColor tc = disabled ? t->text_tertiary : (on ? t->accent_soft_text : t->text_primary);
  ui_text_fit(c, ui_inset(r, ui_s(c, 10), 0, ui_s(c, 10), 0), style, tc, label, 0.5f);
  return !disabled && ui_clicked(c, id);
}

// A row of a card that is a control: label at the left, value at the right, a chevron.
static int field_row(UiCtx *c, uint32_t id, UiRect r, const char *label, const char *value, int disabled) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  float rad = ui_s(c, t->radius_control);
  ui_hit(c, r, rad, id, disabled ? UI_HIT_BLOCK : UI_HIT_BLOCK | UI_HIT_INTERACT);
  float hov = ui_anim(c, id, !disabled && ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
  uid_rrect(d, r, rad, uid_alpha(t->surface_inset, 0.55f + 0.45f * hov));
  uid_rrect_stroke(d, r, rad, ui_s(c, 1), uid_alpha(t->divider, 0.5f + 0.5f * hov));
  ui_text_in(c, ui_rect(r.x + ui_s(c, 14), r.y, ui_s(c, 96), r.h), UI_TYPE_LABEL, t->text_tertiary, label, 0);
  ui_text_fit(c, ui_rect(r.x + ui_s(c, 110), r.y, r.w - ui_s(c, 110) - ui_s(c, 38), r.h), UI_TYPE_HEADING, disabled ? t->text_tertiary : t->text_primary, value, 0);
  ui_icon(c, UI_ICON_CHEV, ui_rect(r.x + r.w - ui_s(c, 34), r.y, ui_s(c, 24), r.h), ui_s(c, 14), uid_alpha(t->text_secondary, 0.7f));
  return !disabled && ui_clicked(c, id);
}

static void section_label(UiCtx *c, float x, float y, float w, const char *s) {
  const UiTokens *t = ui_tokens(c);
  ui_text_in(c, ui_rect(x, y, w, ui_font(c, UI_TYPE_LABEL).line), UI_TYPE_LABEL, t->text_secondary, s, 0);
}

static void scrim_and_panel(UiCtx *c, UiRect pr) {
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  uid_rrect(ui_drawer(c), scr, 0, t->scrim);
  ui_hit(c, scr, 0, ui_id("pages.scrim"), UI_HIT_BLOCK | UI_HIT_MODAL);
  ui_panel(c, pr, ui_s(c, t->radius_panel), UI_PANEL_STRONG);
}

// ------------------------------------------------------------------ golfer profile (FUN_004385d0)

// Golf's hit centres of the page's controls (the table at 0x4c7b38, in golf's 800x600 page) and the places the
// original reads the pointer at for the rows that are no table entries.
static const int PF_SKILL[3][2] = {{210, 150}, {210, 185}, {210, 220}};
static const int PF_TRAIT[5][2] = {{93, 127}, {93, 156}, {93, 182}, {93, 211}, {93, 239}};
enum { PF_LOAD = 8, PF_SAVE, PF_BODY, PF_ADULT, PF_FACE, PF_SHIRT, PF_PANTS, PF_HAIR, PF_SKIN, PF_GENDER, PF_CANCEL, PF_OK, PF_BIO };
static const int PF_BTN[13][2] = {{48, 22}, {258, 22}, {454, 30}, {454, 80}, {454, 130}, {454, 180}, {454, 230}, {328, 56}, {328, 107}, {328, 157},
                                  {762, 33}, {760, 220}, {580, 98}};   // indexed by control number - 8

typedef struct {
  char name[RP_TEXT], nick[RP_TEXT], marital[RP_TEXT], age[RP_TEXT], member[RP_TEXT], low[RP_TEXT], hcp[RP_TEXT], rounds[RP_TEXT];
  char trait[5][RP_TEXT], skill[3][RP_TEXT];
  int trait_on[5], skill_on[3];
  int pro, n_lev;
  char lev_name[10][RP_TEXT];
  int lev[10];
  int n_say;
  struct {
    char label[RP_TEXT], text[RP_TEXT];
    uint16_t col, tcol;
  } say[24];
  int pic;   // art slot + 1 of the portrait picture
  int pic_w, pic_h;
  int prev, prev_w, prev_h;   // task-102c: golf's little golfers on their disc (the shirt, pants and skin colours show there)
} Prof;

static void pf_copy(char *dst, const RpItem *it) {
  if (!it) {
    dst[0] = 0;
    return;
  }
  fix_text(dst, RP_TEXT, it->s);
}

static void pf_read(const RpSnap *s, Prof *p) {
  memset(p, 0, sizeof *p);
  pf_copy(p->name, txt(s, 154, 24));
  pf_copy(p->nick, txt(s, 154, 48));
  pf_copy(p->marital, txt(s, 154, 69));
  pf_copy(p->age, txt(s, 154, 90));
  size_t L = strlen(p->nick);   // "(Surfer)": the parentheses are golf's way of showing a profession
  if (L > 1 && p->nick[0] == '(' && p->nick[L - 1] == ')') {
    memmove(p->nick, p->nick + 1, L - 2);
    p->nick[L - 2] = 0;
  }
  for (int i = 0; i < 5; i++) {
    const RpItem *it = txt(s, 93, 122 + 28 * i);
    pf_copy(p->trait[i], it);
    p->trait_on[i] = it && it->col == 0x0848;
  }
  for (int i = 0; i < 3; i++) {
    const RpItem *it = txt(s, 212, 142 + 35 * i);
    pf_copy(p->skill[i], it);
    p->skill_on[i] = it && it->col == 0x0848;
  }
  const RpItem *lv = txt(s, 585, 30);   // "Golf Skill Levels": a pro's page
  p->pro = lv != NULL;
  if (p->pro) {
    for (int i = 0; i < 10; i++) {
      const RpItem *num = txt(s, 501, 61 + 16 * i), *nm = txt(s, 546, 61 + 16 * i);
      if (!nm) continue;
      pf_copy(p->lev_name[p->n_lev], nm);
      p->lev[p->n_lev] = num ? atoi(num->s) : 0;
      p->n_lev++;
    }
  } else {
    pf_copy(p->member, txt(s, 580, 30));
    pf_copy(p->low, txt(s, 580, 50));
    pf_copy(p->hcp, txt(s, 580, 64));
    pf_copy(p->rounds, txt(s, 580, 78));
  }
  for (int i = 0; i < 24; i++) {
    const RpItem *lab = txt(s, 52, 271 + 16 * i), *val = txt(s, 412, 271 + 16 * i);
    if (!lab) {
      if (i > 0 && !txt(s, 52, 271 + 16 * (i + 1))) break;
      continue;
    }
    pf_copy(p->say[p->n_say].label, lab);
    pf_copy(p->say[p->n_say].text, val);
    p->say[p->n_say].col = lab->col;
    p->say[p->n_say].tcol = val ? val->col : 0x4210;
    p->n_say++;
  }
  for (int i = 0; i < s->n; i++)
    if (s->it[i].type == RP_IT_PIC && s->it[i].x == 298) p->prev = s->it[i].art, p->prev_w = s->it[i].w, p->prev_h = s->it[i].h;
    else if (s->it[i].type == RP_IT_PIC) p->pic = s->it[i].art, p->pic_w = s->it[i].w, p->pic_h = s->it[i].h;
}

// design height of the panel at scale 1 (used to fit it on small screens)
#define PF_W 1240.0f
#define PF_TOP 344.0f
#define PF_SAY_ROW 40.0f
#define PF_H (28 + 44 + 14 + PF_TOP + 16 + 22 + 11 * PF_SAY_ROW + 14 + 44 + 28)

static RpLayout g_pf_layout;
// task-102: a chip shows its new state on the frame it is clicked; golf's page (about ten turns a second) confirms it a
// moment later. 0..4 personality, 5..7 skills; -1 nothing pending.
static int g_pend[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
static double g_pend_t[8];
static int pend_show(UiCtx *c, int i, int golf_on) {
  if (g_pend[i] < 0) return golf_on;
  if (g_pend[i] == golf_on || ui_now(c) - g_pend_t[i] > 1500) {   // golf has it (or never took it: show golf's)
    g_pend[i] = -1;
    return golf_on;
  }
  return g_pend[i];
}
static void pend_set(UiCtx *c, int i, int shown) {
  g_pend[i] = !shown;
  g_pend_t[i] = ui_now(c);
}
static void pf_ctl(const char *name, UiRect r) {   // remember a control's centre for the log and the tests
  if (g_pf_layout.nctl >= 48) return;
  snprintf(g_pf_layout.ctl_name[g_pf_layout.nctl], sizeof g_pf_layout.ctl_name[0], "%s", name);
  g_pf_layout.ctl_x[g_pf_layout.nctl] = r.x + r.w / 2, g_pf_layout.ctl_y[g_pf_layout.nctl] = r.y + r.h / 2;
  g_pf_layout.nctl++;
}
static int g_raw_data;   // visible profile texts that still contain golf's DATA( placeholder (the gallery checks it is 0)
int ui_pages_raw_data(void) { return g_raw_data; }

static int draw_profile(UiCtx *c, const RpSnap *s, SDL_Texture *const *art, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  int na = 0;
  Prof p;
  pf_read(s, &p);
  int editing = s->ext[0] != 0;
  // task-102: the player's own pro (golf's record 0) keeps its skill levels to golf's skill screen (golf ignores the
  // page's level clicks for it); the first step of a new pro says so and leads on to the skill points
  int own_pro = p.pro && s->ext[1] == 0, create = own_pro && s->ext[2] != 0, points = s->ext[3];
  UiRect scr = ui_screen(c);
  float margin = ui_s(c, t->edge_margin);
  float k = fminf(1.0f, fminf((scr.w - 2 * margin) / ui_s(c, PF_W), (scr.h - 2 * margin) / ui_s(c, PF_H)));
  if (k < 1.0f) ui_push_scale(c, k);
  float pad = ui_s(c, 28);
  float W = ui_s(c, PF_W), H = ui_s(c, PF_H);
  UiRect scr2 = ui_screen(c);
  UiRect pr = ui_rect_round(ui_center(scr2, W, H));
  scrim_and_panel(c, pr);
  UiRect in = ui_inset_all(pr, pad);
  g_pf_layout.panel = pr;
  g_pf_layout.nctl = 0;

  // header: title, close
  UiFont tf = ui_font(c, UI_TYPE_TITLE);
  float cb = ui_s(c, 40);
  UiRect close_r = ui_rect(in.x + in.w - cb, in.y, cb, cb);
  if (ui_icon_button(c, ui_id("pages.close"), close_r, UI_ICON_X, editing ? UI_DISABLED : 0, "Done (Esc)") && na < max) acts[na++] = (RpAction){RP_A_CLOSE, 0};
  g_pf_layout.close_x = close_r.x + close_r.w / 2, g_pf_layout.close_y = close_r.y + close_r.h / 2;
  const char *title = create ? "Edit your pro" : p.pro ? "Pro profile" : "Golfer profile";
  float title_w = fminf(ui_text_width(c, UI_TYPE_TITLE, title) + ui_s(c, 4), in.w - cb - ui_s(c, 12));
  ui_text_fit(c, ui_rect(in.x, in.y + (cb - tf.line) / 2, title_w, tf.line), UI_TYPE_TITLE, t->text_primary, title, 0);
  if (create || own_pro) {   // one line that says what to do here
    char sub[160];
    if (create) snprintf(sub, sizeof sub, "Pick a look and a personality, then press Next to spend your %d skill points.", points);
    else snprintf(sub, sizeof sub, "Skill levels go up with the skill points golf gives you (the Skills tile of Your pro).");
    UiFont bf0 = ui_font(c, UI_TYPE_BODY);
    float sx = in.x + title_w + ui_s(c, 24);
    ui_text_fit(c, ui_rect(sx, in.y + (cb - bf0.line) / 2, in.x + in.w - cb - ui_s(c, 16) - sx, bf0.line), UI_TYPE_BODY, t->text_secondary, sub, 0);
  }

  float top_y = in.y + ui_s(c, 44 + 14), top_h = ui_s(c, PF_TOP);
  float gap = ui_s(c, 16);
  float colA = ui_s(c, 332), colB = ui_s(c, 190);
  float colC = in.w - colA - colB - 2 * gap;

  // ---- column A: identity, personality, skills
  UiRect A = ui_rect(in.x, top_y, colA, top_h);
  uid_rrect(d, A, ui_s(c, t->radius_card), uid_alpha(t->surface_inset, 0.35f));
  float ax = A.x + ui_s(c, 14), aw = A.w - ui_s(c, 28), ay = A.y + ui_s(c, 14);
  {
    // the name: a control (golf asks for a new one)
    UiRect nr = ui_rect(ax, ay, aw, ui_s(c, 40));
    uint32_t id = ui_id("pf.name");
    pf_ctl("name", nr);
    ui_hit(c, nr, ui_s(c, 10), id, UI_HIT_BLOCK | (editing ? 0 : UI_HIT_INTERACT));
    float hov = ui_anim(c, id, !editing && ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
    if (hov > 0.01f) uid_rrect(d, nr, ui_s(c, 10), uid_alpha(t->surface_inset, 0.7f * hov));
    ui_text_fit(c, ui_inset(nr, ui_s(c, 8), 0, ui_s(c, 8), 0), UI_TYPE_DISPLAY, t->text_primary, p.name[0] ? p.name : "Golfer", 0);
    ui_text_in(c, ui_rect(nr.x + nr.w - ui_s(c, 90), nr.y, ui_s(c, 82), nr.h), UI_TYPE_CAPTION, uid_alpha(t->text_secondary, 0.9f * hov), "Rename", 1);
    if (!editing && ui_clicked(c, id) && na < max) acts[na++] = (RpAction){RP_A_CLICK, CLICK(154, 34)};
    ay += nr.h + ui_s(c, 8);
    float rh = ui_s(c, 32);
    const char *lab[3] = {"Profession", "Status", "Age"};
    const char *val[3] = {p.nick, p.marital, p.age};
    for (int i = 0; i < 3; i++) {
      char key[16];
      snprintf(key, sizeof key, "pf.row%d", i);
      pf_ctl(key + 3, ui_rect(ax, ay, aw, rh));
      if (field_row(c, ui_id(key), ui_rect(ax, ay, aw, rh), lab[i], val[i][0] ? val[i] : "-", editing) && na < max)
        acts[na++] = (RpAction){RP_A_CLICK, CLICK(154, 34 + 20 * (i + 1))};
      ay += rh + ui_s(c, 4);
    }
    ay += ui_s(c, 6);
    // personality: five chips that flip golf's personality bits
    section_label(c, ax, ay, aw, "Personality (click to switch on or off)");
    ay += ui_font(c, UI_TYPE_LABEL).line + ui_s(c, 4);
    float cx = ax, ch = ui_s(c, 34);   // task-102: bigger chips, easier to hit
    for (int i = 0; i < 5; i++) {
      float w1 = ui_text_width(c, UI_TYPE_LABEL, p.trait[i]) + ui_s(c, 34);
      if (cx + w1 > ax + aw + 1) cx = ax, ay += ch + ui_s(c, 5);
      char key[16];
      snprintf(key, sizeof key, "pf.trait%d", i);
      pf_ctl(key + 3, ui_rect(cx, ay, w1, ch));
      int on = pend_show(c, i, p.trait_on[i]);
      if (chip(c, ui_id(key), ui_rect(cx, ay, w1, ch), p.trait[i], on, editing, UI_TYPE_LABEL) && na < max) {
        acts[na++] = (RpAction){RP_A_CLICK, CLICK(PF_TRAIT[i][0], PF_TRAIT[i][1])};
        pend_set(c, i, on);
      }
      cx += w1 + ui_s(c, 6);
    }
    ay += ch + ui_s(c, 10);
    section_label(c, ax, ay, aw, "Skills");
    ay += ui_font(c, UI_TYPE_LABEL).line + ui_s(c, 4);
    cx = ax;
    for (int i = 0; i < 3; i++) {
      float w1 = ui_text_width(c, UI_TYPE_LABEL, p.skill[i]) + ui_s(c, 34);
      if (cx + w1 > ax + aw + 1) cx = ax, ay += ch + ui_s(c, 5);
      char key[16];
      snprintf(key, sizeof key, "pf.skill%d", i);
      pf_ctl(key + 3, ui_rect(cx, ay, w1, ch));
      int on = pend_show(c, 5 + i, p.skill_on[i]);
      if (chip(c, ui_id(key), ui_rect(cx, ay, w1, ch), p.skill[i], on, editing, UI_TYPE_LABEL) && na < max) {
        acts[na++] = (RpAction){RP_A_CLICK, CLICK(PF_SKILL[i][0], PF_SKILL[i][1])};
        pend_set(c, 5 + i, on);
      }
      cx += w1 + ui_s(c, 6);
    }
  }

  // ---- column B: the portrait (golf's own art, kept to its aspect)
  UiRect B = ui_rect(A.x + A.w + gap, top_y, colB, top_h);
  uid_rrect(d, B, ui_s(c, t->radius_card), uid_alpha(t->surface_inset, 0.35f));
  SDL_Texture *ptex = art && p.pic > 0 ? art[p.pic - 1] : NULL;
  SDL_Texture *vtex = art && p.prev > 0 ? art[p.prev - 1] : NULL;
  UiRect PB = B;   // the portrait's part of the column: the little golfers take the bottom when they show
  if (vtex && p.prev_w > 0 && p.prev_h > 0) {
    float vs = ui_s(c, 2.0f), vw = p.prev_w * vs, vh = p.prev_h * vs;
    PB.h -= vh + ui_s(c, 10);
    UiRect vr = ui_rect_round(ui_rect(B.x + (B.w - vw) / 2, B.y + B.h - vh - ui_s(c, 8), vw, vh));
    uid_image(d, vtex, NULL, vr, 0, (UiColor){255, 255, 255, 255});
  }
  if (ptex && p.pic_w > 0 && p.pic_h > 0) {
    float kk = fminf((PB.w - ui_s(c, 20)) / p.pic_w, (PB.h - ui_s(c, 20)) / p.pic_h);
    UiRect ir = ui_rect_round(ui_center(PB, p.pic_w * kk, p.pic_h * kk));
    uid_image(d, ptex, NULL, ir, 0, (UiColor){255, 255, 255, 255});
    uid_rrect_stroke(d, ir, 0, ui_s(c, 1), uid_alpha(t->divider, 0.8f));
  } else {
    ui_icon(c, UI_ICON_SMILE, B, ui_s(c, 64), uid_alpha(t->text_tertiary, 0.6f));
  }

  // ---- column C: membership and results (or a pro's skill levels), bio, looks
  UiRect C = ui_rect(B.x + B.w + gap, top_y, colC, top_h);
  float cy = C.y;
  if (!p.pro) {
    float th = ui_s(c, 64);
    float tw = (C.w - 3 * ui_s(c, 10)) / 4;
    UiRect tiles[4];
    for (int i = 0; i < 4; i++) tiles[i] = ui_rect(C.x + i * (tw + ui_s(c, 10)), cy, tw, th);
    const char *tl[4] = {"Membership", "Low round", "Handicap", "Rounds played"};
    const char *tv[4] = {p.member, p.low, p.hcp, p.rounds};
    for (int i = 0; i < 4; i++) {
      uid_rrect(d, tiles[i], ui_s(c, t->radius_tile), uid_alpha(t->surface_inset, 0.55f));
      char v[RP_TEXT];
      const char *src = tv[i];
      if (i >= 1 && strchr(src, ':')) src = strrchr(src, ':') + 1;   // golf draws "Low round: -"
      while (*src == ' ') src++;
      snprintf(v, sizeof v, "%s", src[0] ? src : "-");
      ui_text_in(c, ui_rect(tiles[i].x + ui_s(c, 12), tiles[i].y + ui_s(c, 8), tiles[i].w - ui_s(c, 24), ui_font(c, UI_TYPE_CAPTION).line),
                 UI_TYPE_CAPTION, t->text_tertiary, tl[i], 0);
      ui_text_fit(c, ui_rect(tiles[i].x + ui_s(c, 12), tiles[i].y + ui_s(c, 28), tiles[i].w - ui_s(c, 24), ui_font(c, UI_TYPE_HEADING).line),
                  UI_TYPE_HEADING, t->text_primary, v, 0);
    }
    cy += th + ui_s(c, 12);
    // the bio: a control (golf opens its text editor), live while it is typed
    float bh = ui_s(c, 118);
    UiRect br = ui_rect(C.x, cy, C.w, bh);
    uint32_t bid = ui_id("pf.bio");
    ui_hit(c, br, ui_s(c, t->radius_tile), bid, UI_HIT_BLOCK | (editing ? 0 : UI_HIT_INTERACT));
    float hov = ui_anim(c, bid, !editing && ui_is_hot(c, bid) ? 1.0f : 0.0f, t->motion_hover);
    if (!editing && ui_clicked(c, bid) && na < max) acts[na++] = (RpAction){RP_A_CLICK, CLICK(580, 105)};   // golf's bio text editor opens
    uid_rrect(d, br, ui_s(c, t->radius_tile), uid_alpha(t->surface_inset, 0.55f + 0.3f * hov));
    if (editing) uid_rrect_stroke(d, br, ui_s(c, t->radius_tile), ui_s(c, 2), t->accent);
    else uid_rrect_stroke(d, br, ui_s(c, t->radius_tile), ui_s(c, 1), uid_alpha(t->divider, 0.5f + 0.5f * hov));
    section_label(c, br.x + ui_s(c, 14), br.y + ui_s(c, 8), br.w - ui_s(c, 28), editing ? "Bio (typing)" : "Bio");
    char bio[RP_TEXT * 5];
    fix_text(bio, sizeof bio, s->bio);
    char lines[WRAP_LINES][WRAP_LEN];
    int nl = wrap(c, UI_TYPE_BODY, bio, br.w - ui_s(c, 28), lines);
    UiFont bf = ui_font(c, UI_TYPE_BODY);
    float ty = br.y + ui_s(c, 8) + ui_font(c, UI_TYPE_LABEL).line + ui_s(c, 6);
    int maxl = (int)((br.y + br.h - ui_s(c, 8) - ty) / bf.line);
    for (int i = 0; i < nl && i < maxl; i++, ty += bf.line)
      ui_text_in(c, ui_rect(br.x + ui_s(c, 14), ty, br.w - ui_s(c, 28), bf.line), UI_TYPE_BODY, t->text_primary, lines[i], 0);
    if (editing && ((int)(ui_now(c) / 500.0)) % 2 == 0) {
      int li = nl > 0 ? (nl - 1 < maxl - 1 ? nl - 1 : maxl - 1) : 0;
      float cxp = br.x + ui_s(c, 14) + (nl > 0 ? ui_text_width(c, UI_TYPE_BODY, lines[li]) : 0) + ui_s(c, 2);
      uid_rrect(d, ui_rect(cxp, br.y + ui_s(c, 8) + ui_font(c, UI_TYPE_LABEL).line + ui_s(c, 6) + li * bf.line + ui_s(c, 2), ui_s(c, 2), bf.line - ui_s(c, 4)), 1,
                t->accent);
    }
    if (editing) {
      float bw = ui_s(c, 120), bhh = ui_s(c, 34);
      if (ui_button(c, ui_id("pf.bio.ok"), ui_rect(br.x + br.w - bw - ui_s(c, 10), br.y + br.h - bhh - ui_s(c, 8), bw, bhh), "Done typing", UI_ICON_NONE, UI_BTN_PRIMARY, 0) &&
          na < max)
        acts[na++] = (RpAction){RP_A_KEY, SDLK_RETURN};
    }
    cy += bh + ui_s(c, 12);
  } else {
    section_label(c, C.x, cy, C.w, create ? "Golf skill levels (you set them next)" : "Golf skill levels");
    cy += ui_font(c, UI_TYPE_LABEL).line + ui_s(c, 4);
    float half = (C.w - ui_s(c, 14)) / 2;
    // two columns of five: name, a level meter and - / + (a left click adds a level in golf, a right click takes one)
    float row = ui_s(c, 34);
    for (int i = 0; i < p.n_lev; i++) {
      int colx = i / 5, rowy = i % 5;
      UiRect rr = ui_rect(C.x + colx * (half + ui_s(c, 14)), cy + rowy * (row + ui_s(c, 4)), half, row);
      uid_rrect(d, rr, ui_s(c, t->radius_tile), uid_alpha(t->surface_inset, 0.55f));
      float bw = ui_s(c, 28), bws = own_pro ? ui_s(c, 4) : 2 * bw;   // the player's pro: no buttons, the meter moves right
      ui_text_fit(c, ui_rect(rr.x + ui_s(c, 12), rr.y, rr.w - bws - ui_s(c, 100), rr.h), UI_TYPE_LABEL, t->text_primary, p.lev_name[i], 0);
      UiRect mr = ui_rect(rr.x + rr.w - bws - ui_s(c, 94), rr.y + (rr.h - ui_s(c, 8)) / 2, ui_s(c, 60), ui_s(c, 8));
      ui_meter(c, mr, p.lev[i] / 10.0f);
      char num[16];
      snprintf(num, sizeof num, "%d", p.lev[i]);
      ui_text_in(c, ui_rect(mr.x + mr.w + ui_s(c, 2), rr.y, ui_s(c, 24), rr.h), UI_TYPE_LABEL, t->text_secondary, num, 0.5f);
      char key[24];
      int oy = 0x48 + 16 * i;
      if (own_pro) continue;   // no dead buttons: golf changes these only through its skill screen
      snprintf(key, sizeof key, "lev%d-", i);
      pf_ctl(key, ui_rect(rr.x + rr.w - 2 * bw - ui_s(c, 4), rr.y + ui_s(c, 3), bw, row - ui_s(c, 6)));
      snprintf(key, sizeof key, "lev%d+", i);
      pf_ctl(key, ui_rect(rr.x + rr.w - bw - ui_s(c, 2), rr.y + ui_s(c, 3), bw, row - ui_s(c, 6)));
      snprintf(key, sizeof key, "pf.lev%d.minus", i);
      if (ui_icon_button(c, ui_id(key), ui_rect(rr.x + rr.w - 2 * bw - ui_s(c, 4), rr.y + ui_s(c, 3), bw, row - ui_s(c, 6)), UI_ICON_MINUS, UI_COMPACT, NULL) && na < max)
        acts[na++] = (RpAction){RP_A_RCLICK, CLICK(520, oy)};
      snprintf(key, sizeof key, "pf.lev%d.plus", i);
      if (ui_icon_button(c, ui_id(key), ui_rect(rr.x + rr.w - bw - ui_s(c, 2), rr.y + ui_s(c, 3), bw, row - ui_s(c, 6)), UI_ICON_PLUS, UI_COMPACT, NULL) && na < max)
        acts[na++] = (RpAction){RP_A_CLICK, CLICK(520, oy)};
    }
    cy += 5 * (row + ui_s(c, 4)) + ui_s(c, 8);
  }
  // looks: the editing buttons around the portrait in golf's page
  {
    // shirt and pants: golf keeps them for the pro out on the course and does not redraw the page's golfers with them
    // (FUN_004385d0 copies them to the golfer's look only as the page closes, as in 2002): the chips say the colour
    section_label(c, C.x, cy, C.w, p.pro ? "Look (shirt and pants show on the course)" : "Look");
    cy += ui_font(c, UI_TYPE_LABEL).line + ui_s(c, 4);
    static const struct {
      const char *label;
      int btn;
    } lk[8] = {{"Face", PF_FACE}, {"Body type", PF_BODY}, {"Adult / child", PF_ADULT}, {"Gender", PF_GENDER},
               {"Skin tone", PF_SKIN}, {"Hair color", PF_HAIR}, {"Shirt color", PF_SHIRT}, {"Pants color", PF_PANTS}};
    float cw4 = (C.w - 3 * ui_s(c, 8)) / 4, chh = ui_s(c, 34);
    for (int i = 0; i < 8; i++) {
      UiRect cr = ui_rect(C.x + (i % 4) * (cw4 + ui_s(c, 8)), cy + (i / 4) * (chh + ui_s(c, 8)), cw4, chh);
      char key[16];
      snprintf(key, sizeof key, "pf.look%d", i);
      pf_ctl(key + 3, cr);
      // golf takes a hair colour only for a woman (FUN_004385d0 drops the click when 0x80 of the look byte is clear), as in 2002
      int off = editing || (lk[i].btn == PF_HAIR && !(s->ext[4] & 0x80));
      char lab[40];
      int val = -1, of = 0;   // task-102c: the colour golf has now, from the snapshot (reports_game.c, ext[5..7])
      if (lk[i].btn == PF_SHIRT) val = s->ext[5], of = 10;
      if (lk[i].btn == PF_PANTS) val = s->ext[6], of = 10;
      if (lk[i].btn == PF_SKIN) val = s->ext[7] & 0xff, of = 4;
      if (lk[i].btn == PF_HAIR && !off) val = (s->ext[7] >> 8) & 0xff, of = 5;
      if (val >= 0 && val < of) snprintf(lab, sizeof lab, "%s %d/%d", lk[i].label, val + 1, of);
      else snprintf(lab, sizeof lab, "%s", lk[i].label);
      // task-119 (tester F27): the toggles say what they are now. Adult / child is golf's child flag (a child is
      // drawn at about three quarters size); without it a click made the pro small with nothing on the page saying so
      if (lk[i].btn == PF_ADULT) snprintf(lab, sizeof lab, "%s", (s->ext[7] >> 18) & 1 ? "Child (small)" : "Adult");
      if (lk[i].btn == PF_BODY) snprintf(lab, sizeof lab, "Body type %d/4", ((s->ext[7] >> 16) & 3) + 1);
      if (lk[i].btn == PF_GENDER) snprintf(lab, sizeof lab, "%s", (s->ext[4] & 0x80) ? "Woman" : "Man");
      if (chip(c, ui_id(key), cr, lab, 0, off, UI_TYPE_LABEL) && na < max)
        acts[na++] = (RpAction){RP_A_CLICK, CLICK(PF_BTN[lk[i].btn - 8][0], PF_BTN[lk[i].btn - 8][1])};
    }
  }

  // ---- sayings: what the golfer says, in two columns (a click asks golf for new words)
  float sy = top_y + top_h + ui_s(c, 16);
  section_label(c, in.x, sy, in.w, "What this golfer says");
  ui_text_fit(c, ui_rect(in.x + in.w - ui_s(c, 260), sy, ui_s(c, 260), ui_font(c, UI_TYPE_CAPTION).line), UI_TYPE_CAPTION,
              t->text_tertiary, "Click the saying text to edit", 1);
  sy += ui_s(c, 22);
  float rw = (in.w - ui_s(c, 12)) / 2, rrow = ui_s(c, PF_SAY_ROW);
  int per_col = (p.n_say + 1) / 2;
  if (per_col > 11) per_col = 11;
  for (int i = 0; i < p.n_say && i < 22; i++) {
    int col = i / per_col, row = i % per_col;
    UiRect rr = ui_rect(in.x + col * (rw + ui_s(c, 12)), sy + row * rrow, rw, rrow - ui_s(c, 4));
    uint32_t id = ui_idi(ui_id("pf.say"), i);
    UiRect lr = ui_rect(rr.x + ui_s(c, 12), rr.y + ui_s(c, 3), rr.w - ui_s(c, 24), ui_font(c, UI_TYPE_CAPTION).line);
    char lab[RP_TEXT];
    snprintf(lab, sizeof lab, "%s", p.say[i].label);
    size_t L = strlen(lab);
    while (L && (lab[L - 1] == '.' || lab[L - 1] == ' ')) lab[--L] = 0;   // golf's "Hit a tree..." is the situation
    UiColor lc = text_colour(t, p.say[i].col);
    if (lc.r == t->text_primary.r && lc.g == t->text_primary.g && lc.b == t->text_primary.b) lc = t->text_secondary;
    UiColor tc = p.say[i].tcol == 0x4210 || !p.say[i].text[0] ? t->text_tertiary : t->text_primary;
    UiRect tr = ui_rect(rr.x + ui_s(c, 12), rr.y + ui_s(c, 3) + ui_font(c, UI_TYPE_CAPTION).line, rr.w - ui_s(c, 24), ui_font(c, UI_TYPE_BODY).line);
    // The event category is descriptive; only the golfer's actual saying opens the text editor.
    ui_hit(c, tr, ui_s(c, 8), id, UI_HIT_BLOCK | (editing ? 0 : UI_HIT_INTERACT));
    float hov = ui_anim(c, id, !editing && ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
    uid_rrect(d, rr, ui_s(c, 10), uid_alpha(t->surface_inset, (i % 2 ? 0.35f : 0.55f) + 0.4f * hov));
    ui_text_in(c, lr, UI_TYPE_CAPTION, lc, lab, 0);
    if (i == 0) {
      pf_ctl("saylabel0", lr);
      pf_ctl("saytext0", tr);
    }
    const char *dp = strstr(p.say[i].text, "DATA(");
    if (dp && !strchr(dp, ')')) g_raw_data++;   // an unclosed placeholder would show as it is
    const char *dq = dp ? strchr(dp, ')') : NULL;
    if (dp && dq) {   // golf's example word: "DATA(fountain)" is shown as the word, in the accent colour
      char pre[RP_TEXT], word[RP_TEXT], post[RP_TEXT];
      snprintf(pre, sizeof pre, "%.*s", (int)(dp - p.say[i].text), p.say[i].text);
      snprintf(word, sizeof word, "%.*s", (int)(dq - dp - 5), dp + 5);
      snprintf(post, sizeof post, "%s", dq + 1);
      char pre_x[RP_TEXT + 2];
      snprintf(pre_x, sizeof pre_x, "%sx", pre);   // a trailing space counts
      float x = tr.x, w1 = ui_text_width(c, UI_TYPE_BODY, pre_x) - ui_text_width(c, UI_TYPE_BODY, "x"), w2 = ui_text_width(c, UI_TYPE_BODY, word);
      ui_text_in(c, ui_rect(x, tr.y, w1 + 1, tr.h), UI_TYPE_BODY, tc, pre, 0);
      ui_text_in(c, ui_rect(x + w1, tr.y, w2 + 1, tr.h), UI_TYPE_BODY, t->accent, word, 0);
      ui_text_in(c, ui_rect(x + w1 + w2, tr.y, tr.w - w1 - w2, tr.h), UI_TYPE_BODY, tc, post, 0);
    } else {
      if (strstr(p.say[i].text, "DATA(")) g_raw_data++;
      ui_text_fit(c, tr, UI_TYPE_BODY, tc, p.say[i].text[0] ? p.say[i].text : "(nothing yet)", 0);
    }
    if (!editing && ui_clicked(c, id) && na < max) acts[na++] = (RpAction){RP_A_CLICK, CLICK(0x242, 0x10e + 16 * i + 8)};
  }

  // ---- footer
  float fy = in.y + in.h - ui_s(c, 44);
  float bw = ui_s(c, 140), bh = ui_s(c, 44), dw = create ? ui_s(c, 250) : bw;
  pf_ctl("done", ui_rect(in.x + in.w - dw, fy, dw, bh));
  if (ui_button(c, ui_id("pf.done"), ui_rect(in.x + in.w - dw, fy, dw, bh), create ? "Next: skill points" : "Done", create ? UI_ICON_CHEV : UI_ICON_NONE,
                UI_BTN_PRIMARY, editing ? UI_DISABLED : 0) && na < max)
    acts[na++] = (RpAction){RP_A_CLOSE, 0};
  if (ui_button(c, ui_id("pf.cancel"), ui_rect(in.x + in.w - dw - bw - ui_s(c, 12) - ui_s(c, 30), fy, bw + ui_s(c, 30), bh), "Undo changes", UI_ICON_NONE, UI_BTN_PILL, editing ? UI_DISABLED : 0) && na < max)
    acts[na++] = (RpAction){RP_A_CLICK, CLICK(PF_BTN[PF_CANCEL - 8][0], PF_BTN[PF_CANCEL - 8][1])};
  float lw = ui_s(c, 190);
  if (ui_button(c, ui_id("pf.load"), ui_rect(in.x, fy, lw, bh), "Load character", UI_ICON_FOLDER, UI_BTN_PILL, editing ? UI_DISABLED : 0) && na < max)
    acts[na++] = (RpAction){RP_A_CLICK, CLICK(PF_BTN[PF_LOAD - 8][0], PF_BTN[PF_LOAD - 8][1])};
  if (ui_button(c, ui_id("pf.save"), ui_rect(in.x + lw + ui_s(c, 12), fy, lw, bh), "Save character", UI_ICON_PIN, UI_BTN_PILL, editing ? UI_DISABLED : 0) && na < max)
    acts[na++] = (RpAction){RP_A_CLICK, CLICK(PF_BTN[PF_SAVE - 8][0], PF_BTN[PF_SAVE - 8][1])};
  ui_text_fit(c, ui_rect(in.x + 2 * lw + ui_s(c, 36), fy, in.w - 2 * lw - dw - bw - ui_s(c, 90), bh), UI_TYPE_CAPTION, t->text_tertiary,
              editing ? "Type the bio, Enter finishes" : create ? "Esc also keeps the changes and goes on" : "Esc keeps the changes and closes", 0);

  if (k < 1.0f) ui_pop_scale(c);
  return na;
}

// ------------------------------------------------------------------ skill points (FUN_0045f0f0, task-102)

// The snapshot (reports_game.c skills_snap): title = the pro's name, bio = what golf said before the screen, ext[1] the
// points this screen gives, ext[2] the points left, ext[3] golf's x of the screen, ext[4] whose (-1 the first setup); it[i] (i < 10): s = the skill's name,
// y = its level (0..10), w = the level the screen opened with (golf never takes a level below it).
static int g_sk_target[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static double g_sk_t;

#define SK_W 820.0f
#define SK_ROW 50.0f

static int draw_skills(UiCtx *c, const RpSnap *s, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  int na = 0;
  int points = s->ext[1], left = s->ext[2], x0 = s->ext[3];
  int n = s->n < 10 ? s->n : 10;
  char text[RP_TEXT * 5];
  fix_text(text, sizeof text, s->bio);
  UiRect scr = ui_screen(c);
  float margin = ui_s(c, t->edge_margin);
  // a click shows on the frame it is made; golf's screen confirms it a moment later (or the old value comes back)
  if (ui_now(c) - g_sk_t > 1500)
    for (int i = 0; i < 10; i++) g_sk_target[i] = -1;
  int lv[10], pend = 0;
  for (int i = 0; i < n; i++) {
    lv[i] = s->it[i].y;
    if (g_sk_target[i] >= 0 && g_sk_target[i] == lv[i]) g_sk_target[i] = -1;
    if (g_sk_target[i] >= 0) pend += g_sk_target[i] - lv[i], lv[i] = g_sk_target[i];
  }
  int left_shown = left - pend;
  char lines[WRAP_LINES][WRAP_LEN];
  float tw = ui_s(c, SK_W) - 2 * ui_s(c, 28);
  int nl = text[0] ? wrap(c, UI_TYPE_BODY, text, tw, lines) : 0;
  if (nl > 4) nl = 4;
  UiFont bf = ui_font(c, UI_TYPE_BODY);
  float design_h = 28 + 44 + 12 + nl * bf.line / ui_s(c, 1) + 12 + 64 + 12 + 10 * SK_ROW + 16 + 44 + 28;
  float k = fminf(1.0f, fminf((scr.w - 2 * margin) / ui_s(c, SK_W), (scr.h - 2 * margin) / ui_s(c, design_h)));
  if (k < 1.0f) ui_push_scale(c, k);
  float pad = ui_s(c, 28);
  UiRect scr2 = ui_screen(c);
  bf = ui_font(c, UI_TYPE_BODY);
  float H = ui_s(c, 28 + 44 + 12) + nl * bf.line + ui_s(c, 12 + 64 + 12 + 10 * SK_ROW + 16 + 44 + 28);
  UiRect pr = ui_rect_round(ui_center(scr2, ui_s(c, SK_W), H));
  scrim_and_panel(c, pr);
  UiRect in = ui_inset_all(pr, pad);
  g_pf_layout.panel = pr;
  g_pf_layout.nctl = 0;

  // header
  UiFont tf = ui_font(c, UI_TYPE_TITLE);
  float cb = ui_s(c, 40);
  UiRect close_r = ui_rect(in.x + in.w - cb, in.y, cb, cb);
  if (ui_icon_button(c, ui_id("sk.close"), close_r, UI_ICON_X, 0, "Done") && na < max) acts[na++] = (RpAction){RP_A_CLOSE, 0};
  g_pf_layout.close_x = close_r.x + close_r.w / 2, g_pf_layout.close_y = close_r.y + close_r.h / 2;
  char title[RP_TEXT + 32];
  char name[64];
  fix_text(name, sizeof name, s->title);
  if (points > 0 && s->ext[4] != -1)   // task-102b: an accomplishment's points
    snprintf(title, sizeof title, "%d new skill points for %s", points, name[0] ? name : "your pro");
  else if (points > 0) snprintf(title, sizeof title, "Skill points for %s", name[0] ? name : "your pro");
  else snprintf(title, sizeof title, "%s's golf skills", name[0] ? name : "Your pro");
  ui_text_fit(c, ui_rect(in.x, in.y + (cb - tf.line) / 2, in.w - cb - ui_s(c, 12), tf.line), UI_TYPE_TITLE, t->text_primary, title, 0);
  float y = in.y + ui_s(c, 44 + 12);
  for (int i = 0; i < nl; i++, y += bf.line) ui_text_in(c, ui_rect(in.x, y, in.w, bf.line), UI_TYPE_BODY, t->text_secondary, lines[i], 0);
  y += ui_s(c, 12);

  // the points left: big, in the accent colour while there are any
  UiRect pb = ui_rect(in.x, y, in.w, ui_s(c, 64));
  uid_rrect(d, pb, ui_s(c, t->radius_card), left_shown > 0 ? uid_alpha(t->accent_soft, 0.9f) : uid_alpha(t->surface_inset, 0.55f));
  char big[32], small[96];
  snprintf(big, sizeof big, "%d", left_shown);
  if (points > 0)
    snprintf(small, sizeof small, left_shown == 1 ? "point left to spend (of %d)" : "points left to spend (of %d)", points);
  else
    snprintf(small, sizeof small, "No points to spend now. Each accomplishment gives 3 more.");
  UiFont df = ui_font(c, UI_TYPE_DISPLAY);
  float bw0 = points > 0 ? ui_text_width(c, UI_TYPE_DISPLAY, big) + ui_s(c, 16) : 0;
  if (points > 0)
    ui_text_in(c, ui_rect(pb.x + ui_s(c, 18), pb.y + (pb.h - df.line) / 2, bw0, df.line), UI_TYPE_DISPLAY,
               left_shown > 0 ? t->accent_soft_text : t->text_secondary, big, 0);
  ui_text_fit(c, ui_rect(pb.x + ui_s(c, 18) + bw0, pb.y, pb.w - bw0 - ui_s(c, 36), pb.h), UI_TYPE_HEADING,
              left_shown > 0 ? t->accent_soft_text : t->text_secondary, small, 0);
  y += pb.h + ui_s(c, 12);

  // ten rows: name, a meter, the percent golf shows, then - and + (big targets)
  float rh = ui_s(c, SK_ROW), bsz = ui_s(c, 40);
  for (int i = 0; i < n; i++) {
    UiRect rr = ui_rect(in.x, y + i * rh, in.w, rh - ui_s(c, 6));
    uid_rrect(d, rr, ui_s(c, t->radius_tile), uid_alpha(t->surface_inset, i % 2 ? 0.35f : 0.55f));
    char nm[RP_TEXT];
    fix_text(nm, sizeof nm, s->it[i].s);
    float nx = rr.x + ui_s(c, 16);
    float bx_plus = rr.x + rr.w - ui_s(c, 6) - bsz, bx_minus = bx_plus - ui_s(c, 8) - bsz;
    float pct_w = ui_s(c, 64), mw = ui_s(c, 180);
    float mx = (points > 0 ? bx_minus : rr.x + rr.w) - ui_s(c, 16) - pct_w - mw;
    ui_text_fit(c, ui_rect(nx, rr.y, mx - nx - ui_s(c, 12), rr.h), UI_TYPE_HEADING, t->text_primary, nm, 0);
    ui_meter(c, ui_rect(mx, rr.y + (rr.h - ui_s(c, 10)) / 2, mw, ui_s(c, 10)), lv[i] / 10.0f);
    char pct[16];
    snprintf(pct, sizeof pct, "%d%%", lv[i] * 10);
    ui_text_in(c, ui_rect(mx + mw, rr.y, pct_w, rr.h), UI_TYPE_HEADING, lv[i] ? t->text_primary : t->text_tertiary, pct, 1);
    if (points <= 0) continue;
    int y_add = 0x5a + 0x18 * i;   // the row's arrows in golf's screen: top half adds, bottom half takes away
    UiRect mr = ui_rect(bx_minus, rr.y + (rr.h - bsz) / 2, bsz, bsz), pr2 = ui_rect(bx_plus, rr.y + (rr.h - bsz) / 2, bsz, bsz);
    char key[16];
    snprintf(key, sizeof key, "sk%d-", i);
    pf_ctl(key, mr);
    int can_minus = lv[i] > s->it[i].w, can_plus = left_shown > 0 && lv[i] < 10;
    if (ui_icon_button(c, ui_idi(ui_id("sk.minus"), i), mr, UI_ICON_MINUS, can_minus ? 0 : UI_DISABLED, "One level less") && can_minus && na < max) {
      acts[na++] = (RpAction){RP_A_CLICK, CLICK(x0 + 0x42, y_add + 0x12)};
      g_sk_target[i] = lv[i] - 1, g_sk_t = ui_now(c);
    }
    snprintf(key, sizeof key, "sk%d+", i);
    pf_ctl(key, pr2);
    if (ui_icon_button(c, ui_idi(ui_id("sk.plus"), i), pr2, UI_ICON_PLUS, can_plus ? 0 : UI_DISABLED, "One level more") && can_plus && na < max) {
      acts[na++] = (RpAction){RP_A_CLICK, CLICK(x0 + 0x42, y_add + 6)};
      g_sk_target[i] = lv[i] + 1, g_sk_t = ui_now(c);
    }
  }

  // footer
  float fy = in.y + in.h - ui_s(c, 44);
  float dw = ui_s(c, 180), bh = ui_s(c, 44);
  UiRect dr = ui_rect(in.x + in.w - dw, fy, dw, bh);
  pf_ctl("done", dr);
  if (ui_button(c, ui_id("sk.done"), dr, "Done", UI_ICON_NONE, UI_BTN_PRIMARY, 0) && na < max) acts[na++] = (RpAction){RP_A_CLOSE, 0};
  ui_text_fit(c, ui_rect(in.x, fy, in.w - dw - ui_s(c, 16), bh), UI_TYPE_CAPTION, t->text_tertiary,
              points > 0 && left_shown > 0 ? "Click + to raise a skill (up to 100%). - gives a point back." : "Done keeps these skills.", 0);
  if (k < 1.0f) ui_pop_scale(c);
  return na;
}

// ------------------------------------------------------------------ Top Ten Designers (FUN_00473470, issue-13)

#define TD_W 1180.0f
#define TD_H 800.0f

static const RpItem *td_field(const RpSnap *s, int rank, int field) {
  for (int i = 0; i < s->n; i++)
    if (s->it[i].type == RP_IT_TEXT && s->it[i].y == rank && s->it[i].x == field) return &s->it[i];
  return NULL;
}

static void td_read(const RpSnap *s, int rank, int field, char *dst, int cap) {
  const RpItem *it = td_field(s, rank, field);
  fix_text(dst, cap, it ? it->s : "");
}

static int draw_designers(UiCtx *c, const RpSnap *s, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  int na = 0;
  UiRect scr = ui_screen(c);
  float margin = ui_s(c, t->edge_margin);
  float k = fminf(1.0f, fminf((scr.w - 2 * margin) / ui_s(c, TD_W), (scr.h - 2 * margin) / ui_s(c, TD_H)));
  if (k < 1.0f) ui_push_scale(c, k);
  UiRect pr = ui_rect_round(ui_center(ui_screen(c), ui_s(c, TD_W), ui_s(c, TD_H)));
  scrim_and_panel(c, pr);
  UiRect in = ui_inset_all(pr, ui_s(c, 28));
  memset(&g_pf_layout, 0, sizeof g_pf_layout);
  g_pf_layout.panel = pr;

  UiFont tf = ui_font(c, UI_TYPE_TITLE), lf = ui_font(c, UI_TYPE_LABEL);
  float cb = ui_s(c, 40);
  UiRect close_r = ui_rect(in.x + in.w - cb, in.y, cb, cb);
  if (ui_icon_button(c, ui_id("td.close"), close_r, UI_ICON_X, 0, "Close (Esc)") && na < max)
    acts[na++] = (RpAction){RP_A_CLOSE, 0};
  g_pf_layout.close_x = close_r.x + close_r.w / 2, g_pf_layout.close_y = close_r.y + close_r.h / 2;
  char title[64];
  fix_text(title, sizeof title, s->title[0] ? s->title : "Top Ten Designers");
  ui_text_fit(c, ui_rect(in.x, in.y + (cb - tf.line) / 2, in.w - cb - ui_s(c, 12), tf.line), UI_TYPE_TITLE, t->text_primary, title, 0);
  ui_text_in(c, ui_rect(in.x, in.y + cb + ui_s(c, 8), in.w - cb, lf.line), UI_TYPE_LABEL, t->accent_soft_text,
             "COURSE DESIGN HALL OF FAME", 0);

  const float gap = 12.0f;
  const float widths[6] = {54.0f, 460.0f, 110.0f, 140.0f, 130.0f, 150.0f};
  float x[6];
  x[0] = in.x;
  for (int i = 1; i < 6; i++) x[i] = x[i - 1] + ui_s(c, widths[i - 1] + gap);
  float list_y = in.y + ui_s(c, 100);
  float table_h = in.h - ui_s(c, 178);
  float head_h = ui_s(c, 26);
  float row_h = (table_h - head_h) / 10.0f;
  const char *const heads[6] = {"", "DESIGNER / COURSE", "FUN", "SKILL", "CASH", "TOTAL SCORE"};
  for (int i = 1; i < 6; i++)
    ui_text_fit(c, ui_rect(x[i], list_y, ui_s(c, widths[i]), head_h), UI_TYPE_LABEL, t->text_tertiary, heads[i], i == 5 ? 1.0f : 0.0f);

  int rows = s->ext[1] < 0 ? 0 : s->ext[1] > 10 ? 10 : s->ext[1];
  for (int rank = 0; rank < rows; rank++) {
    char name[RP_TEXT], course[RP_TEXT], fun[RP_TEXT], skill[RP_TEXT], cash[RP_TEXT], score[RP_TEXT], mult[RP_TEXT];
    td_read(s, rank, 0, name, sizeof name);
    td_read(s, rank, 1, course, sizeof course);
    td_read(s, rank, 2, fun, sizeof fun);
    td_read(s, rank, 3, skill, sizeof skill);
    td_read(s, rank, 4, cash, sizeof cash);
    td_read(s, rank, 5, score, sizeof score);
    td_read(s, rank, 6, mult, sizeof mult);
    float y = list_y + head_h + rank * row_h;
    UiRect rr = ui_rect(in.x, y, in.w, row_h - ui_s(c, 3));
    int current = rank == s->ext[0];
    uid_rrect(d, rr, ui_s(c, t->radius_control), current ? uid_alpha(t->accent_soft, 0.72f) : uid_alpha(t->surface_inset, rank & 1 ? 0.22f : 0.42f));
    uid_rrect_stroke(d, rr, ui_s(c, t->radius_control), ui_s(c, 1), current ? uid_alpha(t->accent, 0.8f) : uid_alpha(t->divider, 0.34f));

    char number[8];
    snprintf(number, sizeof number, "%d", rank + 1);
    ui_text_fit(c, ui_rect(x[0], rr.y, ui_s(c, widths[0]), rr.h), UI_TYPE_HEADING, current ? t->accent_soft_text : t->text_secondary, number, 0.5f);
    float left_pad = ui_s(c, 12);
    UiRect details = ui_rect(x[1] + left_pad, rr.y + ui_s(c, 3), ui_s(c, widths[1]) - 2 * left_pad, rr.h - ui_s(c, 6));
    UiFont hf = ui_font(c, UI_TYPE_HEADING), cf = ui_font(c, UI_TYPE_CAPTION);
    ui_text_fit(c, ui_rect(details.x, details.y, details.w, hf.line), UI_TYPE_HEADING, current ? t->accent_soft_text : t->text_primary, name, 0);
    ui_text_fit(c, ui_rect(details.x, details.y + hf.line, details.w, cf.line), UI_TYPE_CAPTION, t->text_secondary, course, 0);
    ui_text_fit(c, ui_rect(x[2], rr.y, ui_s(c, widths[2]), rr.h), UI_TYPE_BODY, t->text_secondary, fun, 0.5f);
    ui_text_fit(c, ui_rect(x[3], rr.y, ui_s(c, widths[3]), rr.h), UI_TYPE_BODY, t->text_secondary, skill, 0.5f);
    ui_text_fit(c, ui_rect(x[4], rr.y, ui_s(c, widths[4]), rr.h), UI_TYPE_BODY, t->text_secondary, cash, 0.5f);
    UiFont mf = ui_font(c, UI_TYPE_CAPTION);
    ui_text_fit(c, ui_rect(x[5], rr.y + ui_s(c, 2), ui_s(c, widths[5]), hf.line), UI_TYPE_HEADING, current ? t->accent_soft_text : t->text_primary, score, 1.0f);
    ui_text_fit(c, ui_rect(x[5], rr.y + hf.line, ui_s(c, widths[5]), mf.line), UI_TYPE_CAPTION, t->text_tertiary, mult, 1.0f);
  }
  if (!rows)
    ui_text_in(c, ui_rect(in.x, list_y + head_h + ui_s(c, 28), in.w, ui_font(c, UI_TYPE_BODY).line), UI_TYPE_BODY, t->text_secondary,
               "No course scores have been recorded yet.", 0);

  float footer_y = in.y + in.h - ui_s(c, 34);
  ui_text_fit(c, ui_rect(in.x, footer_y, in.w - cb, ui_s(c, 24)), UI_TYPE_CAPTION, t->text_tertiary,
              "Final score combines Fun, Skill Rating, and Cash, weighted by course difficulty.", 0);
  if (k < 1.0f) ui_pop_scale(c);
  return na;
}

// ------------------------------------------------------------------ accomplishment board (FUN_0046e810, task-105)

// The snapshot (reports_game.c rp_photo): one RP_IT_RECT per photo golf pinned up (x, y its place in golf's board, art the
// accomplishment, s its name), golf's caption under each photo at (x, y + 0xa6) (course and date), each photo as
// RP_IT_PIC. ext[1] the accomplishment just won (99: review), ext[2] skill points next, ext[5] this board's opening id.
#define PH_W 820.0f

static int draw_photo(UiCtx *c, const RpSnap *s, SDL_Texture *const *art, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  int na = 0, won = s->ext[1], points_next = s->ext[2] != 0;
  const RpItem *ph[24];
  int n = 0;
  for (int i = 0; i < s->n; i++) {
    if (s->it[i].type == RP_IT_RECT && n < 24) ph[n++] = &s->it[i];
  }
  const RpItem *top = NULL;   // the accomplishment just won, else the newest photo
  for (int i = 0; i < n; i++)
    if (won != 99 && ph[i]->art == won) top = ph[i];
  if (!top && n) top = ph[n - 1];
  uint32_t history_id = ui_id("ph.history");
  float *opened = ui_state(c, ui_id("ph.opened")), *selected = ui_state(c, ui_id("ph.selected"));
  if (*opened != s->ext[5]) {
    *opened = (float)s->ext[5];
    *selected = top ? top->art : -1;
    *ui_state(c, history_id) = 0;
  }
  for (int i = 0; i < n; i++)
    if (ph[i]->art == (int)*selected) top = ph[i];
  const RpItem *pic = NULL;
  for (int i = 0; top && i < s->n; i++)
    if (s->it[i].type == RP_IT_PIC && s->it[i].x == top->x && s->it[i].y == top->y) pic = &s->it[i];
  float history_h = n > 1 ? 192.0f : 0;

  UiRect scr = ui_screen(c);
  float margin = ui_s(c, t->edge_margin);
  float photo_w = 480, photo_h = 384;
  float design_h = 28 + 22 + 44 + 16 + photo_h + 12 + 30 + (history_h ? 16 + 38 + history_h : 0) + 20 + 48 + 28;
  float k = fminf(1.0f, fminf((scr.w - 2 * margin) / ui_s(c, PH_W), (scr.h - 2 * margin) / ui_s(c, design_h)));
  if (k < 1.0f) ui_push_scale(c, k);
  UiRect pr = ui_rect_round(ui_center(ui_screen(c), ui_s(c, PH_W), ui_s(c, design_h)));
  scrim_and_panel(c, pr);
  UiRect in = ui_inset_all(pr, ui_s(c, 28));
  g_pf_layout.panel = pr;
  g_pf_layout.nctl = 0;
  g_pf_layout.close_x = g_pf_layout.close_y = 0;

  // header: what this is, then the accomplishment's name
  float y = in.y;
  UiFont lf = ui_font(c, UI_TYPE_LABEL), tf = ui_font(c, UI_TYPE_TITLE);
  ui_text_in(c, ui_rect(in.x, y, in.w, lf.line), UI_TYPE_LABEL, t->accent_soft_text,
             won == 99 ? "Professional accomplishments" : "New professional accomplishment", 0);
  y += ui_s(c, 22);
  char name[RP_TEXT];
  fix_text(name, sizeof name, top && top->s[0] ? top->s : "Your pro's accomplishments");
  ui_text_fit(c, ui_rect(in.x, y, in.w, tf.line), UI_TYPE_TITLE, t->text_primary, name, 0);
  y += ui_s(c, 44 + 16);

  // the photo golf took of the moment, with golf's caption (the course and the date)
  UiRect fr = ui_rect_round(ui_rect(in.x + (in.w - ui_s(c, photo_w)) / 2, y, ui_s(c, photo_w), ui_s(c, photo_h)));
  uid_rrect(d, ui_inset_all(fr, -ui_s(c, 8)), ui_s(c, t->radius_card), uid_alpha(t->surface_inset, 0.6f));
  SDL_Texture *tex = art && pic && pic->art > 0 ? art[pic->art - 1] : NULL;
  if (tex) {
    uid_image(d, tex, NULL, fr, 0, (UiColor){255, 255, 255, 255});
    uid_rrect_stroke(d, fr, 0, ui_s(c, 1), uid_alpha(t->divider, 0.8f));
  } else {
    ui_icon(c, UI_ICON_TROPHY, fr, ui_s(c, 96), uid_alpha(t->text_tertiary, 0.6f));
  }
  y += fr.h + ui_s(c, 12);
  char cap[RP_TEXT];
  const RpItem *ci = top ? txt(s, top->x, top->y + 0xa6) : NULL;
  fix_text(cap, sizeof cap, ci ? ci->s : "");
  ui_text_fit(c, ui_rect(in.x, y, in.w, ui_s(c, 30)), UI_TYPE_HEADING, t->text_secondary, cap, 0.5f);
  y += ui_s(c, 30);

  // Every earned award remains available, newest first. Selection changes only this native view, not golf's state.
  if (history_h) {
    y += ui_s(c, 16);
    char heading[64];
    snprintf(heading, sizeof heading, "Your board: %d accomplishments", n);
    section_label(c, in.x, y, in.w - ui_s(c, 84), heading);
    float row_h = ui_s(c, 64);
    for (int down = 0; down < 2; down++) {
      UiRect ar = ui_rect(in.x + in.w - ui_s(c, down ? 34 : 74), y, ui_s(c, 34), ui_s(c, 32));
      pf_ctl(down ? "historydown" : "historyup", ar);
      if (ui_icon_button(c, ui_idi(ui_id("ph.scroll"), down), ar, down ? UI_ICON_DOWN : UI_ICON_UP, UI_COMPACT,
                         down ? "Older accomplishments" : "Newer accomplishments"))
        *ui_state(c, history_id) += down ? row_h * 3 : -row_h * 3;
    }
    y += ui_s(c, 38);
    UiRect view = ui_rect(in.x, y, in.w, ui_s(c, history_h));
    float off = ui_vscroll_begin(c, history_id, view, n * row_h, row_h);
    for (int i = n - 1; i >= 0; i--) {
      UiRect rr = ui_rect(in.x + ui_s(c, 3), y + off + (n - 1 - i) * row_h + ui_s(c, 3), in.w - ui_s(c, 6), row_h - ui_s(c, 6));
      if (rr.y + rr.h <= view.y || rr.y >= view.y + view.h) continue;
      char nm[RP_TEXT], cp[RP_TEXT];
      fix_text(nm, sizeof nm, ph[i]->s[0] ? ph[i]->s : "Accomplishment");
      const RpItem *c2 = txt(s, ph[i]->x, ph[i]->y + 0xa6);
      fix_text(cp, sizeof cp, c2 ? c2->s : "");
      const RpItem *photo = NULL;
      for (int j = 0; j < s->n; j++)
        if (s->it[j].type == RP_IT_PIC && s->it[j].x == ph[i]->x && s->it[j].y == ph[i]->y) photo = &s->it[j];
      SDL_Texture *thumb = art && photo && photo->art > 0 ? art[photo->art - 1] : NULL;
      if (rr.y >= view.y && rr.y + rr.h <= view.y + view.h) {
        char control[20];
        snprintf(control, sizeof control, "award%d", ph[i]->art);
        pf_ctl(control, rr);
      }
      if (ui_list_row(c, ui_idi(ui_id("ph.award"), ph[i]->art), rr, thumb, nm, cp, NULL, ph[i] == top ? UI_SELECTED : 0))
        *selected = ph[i]->art;
    }
    ui_vscroll_end(c);
  }
  if (getenv("SIMGOLF_REPORTS_LOG")) {
    static int last_id = -1, last_open = -1;
    if (last_id != (top ? top->art : -1) || last_open != s->ext[5]) {
      last_id = top ? top->art : -1, last_open = s->ext[5];
      fprintf(stderr, "reports: board selected %d of %d photo %d '%s' caption '%s'\n", last_id, n, pic != NULL, name, cap);
    }
  }

  // footer: Continue (golf waits for a click), on to the skill points when they come next
  float bw = ui_s(c, points_next ? 280 : 180), bh = ui_s(c, 48);
  UiRect br = ui_rect(in.x + in.w - bw, in.y + in.h - bh, bw, bh);
  pf_ctl("continue", br);
  if (ui_button(c, ui_id("ph.continue"), br, points_next ? "Continue: 3 skill points" : "Continue", points_next ? UI_ICON_CHEV : UI_ICON_NONE,
                UI_BTN_PRIMARY, 0) && na < max)
    acts[na++] = (RpAction){RP_A_CLOSE, 0};
  if (k < 1.0f) ui_pop_scale(c);
  return na;
}

// ------------------------------------------------------------------ year summary board (FUN_0044cff0)

#define YR_W 940.0f

static int draw_year(UiCtx *c, const RpSnap *s, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  int na = 0;
  UiRect scr = ui_screen(c);
  float margin = ui_s(c, t->edge_margin);
  float design_h = 740.0f;
  float k = fminf(1.0f, fminf((scr.w - 2 * margin) / ui_s(c, YR_W), (scr.h - 2 * margin) / ui_s(c, design_h)));
  if (k < 1.0f) ui_push_scale(c, k);
  UiRect pr = ui_rect_round(ui_center(ui_screen(c), ui_s(c, YR_W), ui_s(c, design_h)));
  scrim_and_panel(c, pr);
  UiRect in = ui_inset_all(pr, ui_s(c, 28));
  memset(&g_pf_layout, 0, sizeof g_pf_layout);
  g_pf_layout.panel = pr;

  UiFont lf = ui_font(c, UI_TYPE_LABEL), tf = ui_font(c, UI_TYPE_TITLE);
  ui_text_in(c, ui_rect(in.x, in.y, in.w, lf.line), UI_TYPE_LABEL, t->accent_soft_text, "YEAR IN REVIEW", 0);
  char title[64];
  fix_text(title, sizeof title, s->title[0] ? s->title : "End of year");
  ui_text_fit(c, ui_rect(in.x, in.y + ui_s(c, 22), in.w, tf.line), UI_TYPE_TITLE, t->text_primary, title, 0);

  UiRect content = ui_rect(in.x, in.y + ui_s(c, 78), in.w, in.h - ui_s(c, 142));
  uid_rrect(d, content, ui_s(c, t->radius_card), uid_alpha(t->surface_inset, 0.28f));
  const float x0 = 0x12d, xspan = 0x12c;
  // Original highlight rows are only 15 pixels apart. Size their spacing from
  // the current fonts rather than compressing the entire 600-pixel screen.
  UiFont body = ui_font(c, UI_TYPE_BODY), heading = ui_font(c, UI_TYPE_HEADING);
  float yscale = fmaxf((body.line + ui_s(c, 4)) / 15.0f,
                      (heading.line + ui_s(c, 4)) / 18.0f);
  int ymin = 0x5d, ymax = ymin;
  for (int i = 0; i < s->n; i++) {
    const RpItem *it = &s->it[i];
    if (it->type == RP_IT_TEXT && it->s[0] && !it->big) {
      if (it->y < ymin) ymin = it->y;
      if (it->y > ymax) ymax = it->y;
    }
  }
  UiRect view = ui_inset_all(content, ui_s(c, 16));
  float content_h = (ymax - ymin) * yscale + fmaxf(body.line, heading.line);
  uint32_t scroll_id = ui_id("yr.scroll");
  float *opened = ui_state(c, ui_id("yr.opened"));
  if (*opened != (float)s->ext[5]) {
    *opened = (float)s->ext[5];
    *ui_state(c, scroll_id) = 0;
  }
  float off = ui_vscroll_begin(c, scroll_id, view, content_h, body.line * 3);
  for (int i = 0; i < s->n; i++) {
    const RpItem *it = &s->it[i];
    if (it->type != RP_IT_TEXT || !it->s[0] || it->big) continue;   // s->title already carries golf's title-font text
    char text[RP_TEXT];
    fix_text(text, sizeof text, it->s);
    float nx = ((float)it->x - x0) / xspan;
    if (nx < 0.0f) nx = 0.0f;
    if (nx > 1.0f) nx = 1.0f;
    float x = view.x + nx * view.w;
    float y = view.y + (it->y - ymin) * yscale + off;
    float w;
    float align;
    if (it->align == RP_AL_RIGHT) {
      w = ui_s(c, 220);
      x -= w;
      align = 1.0f;
    } else if (it->align == RP_AL_CENTRE) {
      w = ui_s(c, 360);
      x -= w * 0.5f;
      align = 0.5f;
    } else {
      w = view.x + view.w - x;
      align = 0.0f;
    }
    int header = it->y == 0x5d || it->y == 200;
    int style = header ? UI_TYPE_HEADING : UI_TYPE_BODY;
    UiFont f = ui_font(c, style);
    UiColor color = text_colour(t, it->col);
    if (!header && color.r == t->text_primary.r && color.g == t->text_primary.g && color.b == t->text_primary.b)
      color = t->text_secondary;
    ui_text_fit(c, ui_rect(x, y, w, f.line), style, color, text, align);
  }

  ui_vscroll_end(c);
  if (content_h > view.h)
    ui_text_in(c, ui_rect(in.x, in.y + in.h - ui_s(c, 48), in.w - ui_s(c, 200), ui_s(c, 48)),
               UI_TYPE_LABEL, t->text_secondary, "Scroll for more highlights", 0);

  float bh = ui_s(c, 48), bw = ui_s(c, 180);
  UiRect br = ui_rect(in.x + in.w - bw, in.y + in.h - bh, bw, bh);
  pf_ctl("continue", br);
  if (ui_button(c, ui_id("yr.continue"), br, "Continue", UI_ICON_CHEV, UI_BTN_PRIMARY, 0) && na < max)
    acts[na++] = (RpAction){RP_A_CLOSE, 0};
  if (k < 1.0f) ui_pop_scale(c);
  return na;
}

// ------------------------------------------------------------------ golfer pairing selector (FUN_00459850)

#define PAIR_W 1000.0f

static void pair_text(const RpSnap *s, int index, char *name, int name_cap, char *details, int details_cap) {
  name[0] = 0;
  details[0] = 0;
  for (int i = 0; i < s->n; i++) {
    const RpItem *it = &s->it[i];
    if (it->type != RP_IT_TEXT || it->group != index || !it->s[0]) continue;
    char text[RP_TEXT];
    fix_text(text, sizeof text, it->s);
    if (!name[0]) {
      snprintf(name, (size_t)name_cap, "%s", text);
    } else if (strlen(details) < (size_t)details_cap - 1) {
      size_t used = strlen(details);
      snprintf(details + used, (size_t)details_cap - used, "%s%s", used ? " | " : "", text);
    }
  }
  if (!name[0]) snprintf(name, (size_t)name_cap, "Golfer %d", index + 1);
}

static int draw_pairing(UiCtx *c, const RpSnap *s, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  int na = 0;
  int count = s->ext[2] < 0 ? 0 : s->ext[2] > 16 ? 16 : s->ext[2];
  int cursor = s->ext[1], picked = s->ext[3];
  unsigned selected = (unsigned)s->ext[0] & 0xffffu;
  char names[16][RP_TEXT], details[16][RP_TEXT * 6];
  for (int i = 0; i < count; i++) pair_text(s, i, names[i], sizeof names[i], details[i], sizeof details[i]);

  UiRect scr = ui_screen(c);
  float margin = ui_s(c, t->edge_margin);
  float design_h = 840.0f;
  float k = fminf(1.0f, fminf((scr.w - 2 * margin) / ui_s(c, PAIR_W), (scr.h - 2 * margin) / ui_s(c, design_h)));
  if (k < 1.0f) ui_push_scale(c, k);
  UiRect pr = ui_rect_round(ui_center(ui_screen(c), ui_s(c, PAIR_W), ui_s(c, design_h)));
  scrim_and_panel(c, pr);
  UiRect in = ui_inset_all(pr, ui_s(c, 28));
  memset(&g_pf_layout, 0, sizeof g_pf_layout);
  g_pf_layout.panel = pr;

  UiFont tf = ui_font(c, UI_TYPE_TITLE), hf = ui_font(c, UI_TYPE_HEADING), cf = ui_font(c, UI_TYPE_CAPTION);
  const RpItem *head = NULL;
  for (int i = 0; i < s->n; i++)
    if (s->it[i].type == RP_IT_TEXT && s->it[i].group == 0xff && s->it[i].s[0]) { head = &s->it[i]; break; }
  char title[RP_TEXT];
  fix_text(title, sizeof title, head ? head->s : "Choose two golfers");
  ui_text_fit(c, ui_rect(in.x, in.y + ui_s(c, 4), in.w - ui_s(c, 56), tf.line), UI_TYPE_TITLE, t->text_primary, title, 0);
  UiRect close_r = ui_rect(in.x + in.w - ui_s(c, 40), in.y, ui_s(c, 40), ui_s(c, 40));
  pf_ctl("close", close_r);
  g_pf_layout.close_x = close_r.x + close_r.w / 2, g_pf_layout.close_y = close_r.y + close_r.h / 2;
  int cancelling = s->ext[6] != 0;
  if (ui_icon_button(c, ui_id("pair.close"), close_r, UI_ICON_X, cancelling ? UI_DISABLED : 0, "Cancel pairing") && !cancelling && na < max)
    acts[na++] = (RpAction){RP_A_CLOSE, 0};

  char status[96];
  if (cancelling) snprintf(status, sizeof status, "Clearing selections and returning to the course.");
  else if (picked >= 2) snprintf(status, sizeof status, "Two golfers selected. Start the round when ready.");
  else snprintf(status, sizeof status, "Select %s more golfer%s to start the round.", picked == 1 ? "one" : "two", picked == 1 ? "" : "s");
  float header_y = in.y + ui_s(c, 48);
  ui_text_fit(c, ui_rect(in.x, header_y, in.w, hf.line), UI_TYPE_HEADING, t->text_secondary, status, 0);

  float footer_y = in.y + in.h - ui_s(c, 48);
  float view_y = header_y + hf.line + ui_s(c, 14);
  UiRect view = ui_rect(in.x, view_y, in.w, footer_y - view_y - ui_s(c, 14));
  float gap = ui_s(c, 14), card_w = (view.w - gap) / 2;
  int max_lines = 1;
  char lines[WRAP_LINES][WRAP_LEN];
  for (int i = 0; i < count; i++) {
    int nl = wrap(c, UI_TYPE_CAPTION, details[i], card_w - ui_s(c, 28), lines);
    if (nl > max_lines) max_lines = nl;
  }
  float row_h = ui_s(c, 12 + 24 + 6) + max_lines * cf.line + ui_s(c, 12);
  uint32_t scroll_id = ui_id("pair.scroll");
  float *opened = ui_state(c, ui_id("pair.opened"));
  if (*opened != s->ext[5]) {
    *opened = (float)s->ext[5];
    *ui_state(c, scroll_id) = 0;
  }
  float off = ui_vscroll_begin(c, scroll_id, view, ((count + 1) / 2) * row_h, row_h);
  for (int i = 0; i < count; i++) {
    int row = i / 2, col = i & 1;
    UiRect card = ui_rect(in.x + col * (card_w + gap), view.y + off + row * row_h + ui_s(c, 3), card_w, row_h - ui_s(c, 6));
    if (card.y + card.h <= view.y || card.y >= view.y + view.h) continue;
    int is_selected = (selected & (1u << i)) != 0;
    int disabled = cancelling || (picked >= 2 && !is_selected);
    uint32_t id = ui_idi(ui_id("pair.golfer"), i);
    ui_hit(c, card, ui_s(c, t->radius_tile), id, disabled ? UI_HIT_BLOCK : UI_HIT_BLOCK | UI_HIT_INTERACT);
    float hov = ui_anim(c, id, !disabled && ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
    uid_rrect(d, card, ui_s(c, t->radius_tile), uid_alpha(is_selected ? t->accent_soft : t->surface_inset, is_selected ? 0.92f : 0.5f + 0.35f * hov));
    uid_rrect_stroke(d, card, ui_s(c, t->radius_tile), ui_s(c, cursor == i ? 2 : 1),
                     uid_alpha(cursor == i ? t->accent : is_selected ? t->accent_soft_text : t->divider, cursor == i ? 0.9f : 0.65f));
    float pad = ui_s(c, 12);
    UiRect name_r = ui_rect(card.x + pad, card.y + ui_s(c, 5), card.w - 2 * pad, hf.line);
    ui_text_fit(c, name_r, UI_TYPE_HEADING, disabled ? t->text_tertiary : t->text_primary, names[i], 0);
    int nl = wrap(c, UI_TYPE_CAPTION, details[i], card.w - 2 * pad, lines);
    float dy = name_r.y + name_r.h + ui_s(c, 5);
    for (int j = 0; j < nl; j++, dy += cf.line)
      ui_text_in(c, ui_rect(card.x + pad, dy, card.w - 2 * pad, cf.line), UI_TYPE_CAPTION, disabled ? t->text_tertiary : t->text_secondary, lines[j], 0);
    char ctl[20];
    snprintf(ctl, sizeof ctl, "golfer%d", i);
    pf_ctl(ctl, card);
    if (!disabled && ui_clicked(c, id) && na < max) {
      int gx = 6 + (i & 1) * 0x14e + 0x70;
      int gy = 0x32 + (i / 2) * 0x88 + 0x44;
      acts[na++] = (RpAction){RP_A_CLICK, CLICK(gx, gy)};
    }
  }
  ui_vscroll_end(c);

  float bw = ui_s(c, 210), bh = ui_s(c, 48);
  UiRect br = ui_rect(in.x + in.w - bw, footer_y, bw, bh);
  pf_ctl("start", br);
  int can_start = picked == 2;
  if (ui_button(c, ui_id("pair.start"), br, "Start Round", UI_ICON_CHEV, UI_BTN_PRIMARY, can_start && !cancelling ? 0 : UI_DISABLED) && can_start && !cancelling && na < max)
    acts[na++] = (RpAction){RP_A_KEY, SDLK_RETURN};
  if (k < 1.0f) ui_pop_scale(c);
  return na;
}

// ------------------------------------------------------------------ dispatch

#include "ui_tracts.inc"
#include "ui_routing.inc"

static int draw_credits(UiCtx *c, const RpSnap *s, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  // Credits can start after the original title renderer has gone stale. Give
  // this page its own opaque backdrop instead of exposing that saved bitmap.
  UiColor top = t->world_bg_top, mid = t->world_bg_mid, bottom = t->world_bg_bottom;
  top.a = mid.a = bottom.a = 255;
  uid_rect_vgrad3(ui_drawer(c), scr, top, mid, bottom);
  float margin = ui_s(c, t->edge_margin), pad = ui_s(c, 28);
  UiRect panel = ui_rect_round(ui_center(scr, fminf(ui_s(c, 980), scr.w - 2 * margin), scr.h - 2 * margin));
  uid_rrect(ui_drawer(c), scr, 0, t->scrim);
  ui_hit(c, scr, 0, ui_id("credits.scrim"), UI_HIT_BLOCK | UI_HIT_MODAL);
  // The shared blur cache was captured before this page's native backdrop.
  // A solid mint panel prevents that cached original title from returning.
  ui_panel(c, panel, ui_s(c, t->radius_panel), UI_PANEL_STRONG | UI_PANEL_SOLID);
  memset(&g_pf_layout, 0, sizeof g_pf_layout);
  g_pf_layout.panel = panel;
  UiRect in = ui_inset_all(panel, pad);
  float cb = ui_s(c, 40);
  UiRect close = ui_rect(in.x + in.w - cb, in.y, cb, cb);
  g_pf_layout.close_x = close.x + cb / 2; g_pf_layout.close_y = close.y + cb / 2;
  int na = 0;
  if (ui_icon_button(c, ui_id("credits.close"), close, UI_ICON_X, 0, "Close (Esc)") && na < max)
    acts[na++] = (RpAction){RP_A_CLOSE, 0};
  ui_text_in(c, ui_rect(in.x, in.y, in.w - cb, cb), UI_TYPE_TITLE, t->text_primary, "Credits", 0);
  ui_text_in(c, ui_rect(in.x, in.y + ui_s(c, 46), in.w, ui_s(c, 28)), UI_TYPE_BODY, t->text_secondary, "Sid Meier's SimGolf", 0);
  float rh = ui_s(c, 30);
  UiRect view = ui_rect(in.x, in.y + ui_s(c, 94), in.w, in.h - ui_s(c, 132));
  float off = ui_vscroll_begin(c, ui_id("credits.scroll"), view, s->n * rh, rh * 5);
  for (int i = 0; i < s->n; i++) {
    UiRect row = ui_rect(view.x, view.y + off + i * rh, view.w - ui_s(c, 14), rh);
    if (row.y + row.h <= view.y || row.y >= view.y + view.h) continue;
    char text[RP_TEXT]; fix_text(text, sizeof text, s->it[i].s);
    int heading = text[0] != 0;
    for (const char *p = text; *p; p++) if (*p >= 'a' && *p <= 'z') heading = 0;
    ui_text_fit(c, row, heading ? UI_TYPE_HEADING : UI_TYPE_BODY, heading ? t->accent_soft_text : t->text_primary, text, 0.5f);
  }
  ui_vscroll_end(c);
  ui_text_in(c, ui_rect(in.x, in.y + in.h - ui_s(c, 28), in.w, ui_s(c, 28)), UI_TYPE_CAPTION, t->text_tertiary,
             "Scroll to read the full credits. Close or press Esc to continue.", 0.5f);
  return na;
}

static int draw_snapshot(UiCtx *c, const RpSnap *s, SDL_Texture *const *art, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  float pad = ui_s(c, 24), head = ui_s(c, 54), foot = ui_s(c, 78);
  const RpItem *photo = NULL;
  int captions = 0;
  for (int i = 0; i < s->n; i++) {
    if (s->it[i].type == RP_IT_PIC) photo = &s->it[i];
    else if (s->it[i].type == RP_IT_TEXT && strcmp(s->it[i].s, "SimFoto")) captions++;
  }
  UiRect panel = ui_center(scr, fminf(ui_s(c, 960), scr.w - 2 * pad), fminf(ui_s(c, 780), scr.h - 2 * pad));
  memset(&g_pf_layout, 0, sizeof g_pf_layout);
  g_pf_layout.panel = panel;
  ui_panel(c, panel, ui_s(c, 26), 0);
  UiRect in = ui_inset_all(panel, pad), close = ui_rect(in.x + in.w - head, in.y, head, head);
  g_pf_layout.close_x = close.x + close.w / 2; g_pf_layout.close_y = close.y + close.h / 2;
  int na = 0;
  if (ui_icon_button(c, ui_id("snapshot.close"), close, UI_ICON_X, 0, "Close (Esc)") && na < max)
    acts[na++] = (RpAction){RP_A_CLOSE, 0};
  ui_text_in(c, ui_rect(in.x, in.y, in.w - head, head), UI_TYPE_TITLE, t->text_primary, "SimFoto", 0);
  UiRect view = ui_rect(in.x, in.y + head + ui_s(c, 8), in.w, in.h - head - foot);
  uid_rrect(ui_drawer(c), view, ui_s(c, 12), t->surface_inset);
  SDL_Texture *tex = art && photo && photo->art > 0 ? art[photo->art - 1] : NULL;
  if (tex && photo->w > 0 && photo->h > 0) {
    float k = fminf(view.w / photo->w, view.h / photo->h);
    uid_image(ui_drawer(c), tex, NULL, ui_center(view, photo->w * k, photo->h * k), 0, (UiColor){255,255,255,255});
  }
  float y = view.y + view.h + ui_s(c, 12), row = ui_font(c, UI_TYPE_BODY).line;
  for (int i = 0; i < s->n; i++) if (s->it[i].type == RP_IT_TEXT && strcmp(s->it[i].s, "SimFoto")) {
    ui_text_fit(c, ui_rect(in.x, y, in.w, row), UI_TYPE_BODY, t->text_primary, s->it[i].s, 0);
    y += row;
  }
  if (!captions) y += row;
  ui_text_fit(c, ui_rect(in.x, y, in.w, row), UI_TYPE_CAPTION, t->text_secondary, "Close or press any key to continue.", 0);
  return na;
}

static int draw_analysis(UiCtx *c, const RpSnap *s, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  float pad = ui_s(c, 22), row = fmaxf(ui_s(c, 42), ui_font(c, UI_TYPE_BODY).line + ui_s(c, 12));
  float head = ui_s(c, 48), sub = ui_font(c, UI_TYPE_BODY).line + ui_s(c, 10);
  float width = fminf(ui_s(c, 620), scr.w - 2 * ui_s(c, 20));
  UiRect panel = ui_rect(scr.x + ui_s(c, 20), scr.y + ui_s(c, 80), width, 2 * pad + head + sub + 4 * row + ui_s(c, 36));
  memset(&g_pf_layout, 0, sizeof g_pf_layout);
  g_pf_layout.panel = panel;
  ui_panel(c, panel, ui_s(c, 22), 0);
  UiRect in = ui_inset_all(panel, pad);
  float close_w = ui_s(c, 36);
  UiRect close = ui_rect(in.x + in.w - close_w, in.y, close_w, close_w);
  g_pf_layout.close_x = close.x + close.w / 2; g_pf_layout.close_y = close.y + close.h / 2;
  int na = 0;
  if (ui_icon_button(c, ui_id("analysis.close"), close, UI_ICON_X, 0, "Close (Esc)") && na < max)
    acts[na++] = (RpAction){RP_A_CLOSE, 0};
  ui_text_in(c, ui_rect(in.x, in.y, in.w - close_w, head), UI_TYPE_TITLE, t->text_primary, "Shot analysis", 0);
  const RpItem *subtitle = NULL;
  for (int i = 0; i < s->n; i++) if (s->it[i].type == RP_IT_TEXT && s->it[i].y == 0x32) subtitle = &s->it[i];
  ui_text_fit(c, ui_rect(in.x, in.y + head, in.w, sub), UI_TYPE_BODY, t->text_secondary,
              subtitle ? subtitle->s : "Comparing golf skills...", 0);
  float y = in.y + head + sub, value_w = ui_s(c, 100);
  for (int r = 0; r < 4; r++) {
    int source_y = 0x54 + r * 0x14;
    const RpItem *label = NULL, *value = NULL;
    for (int i = 0; i < s->n; i++) {
      const RpItem *it = &s->it[i];
      if (it->type != RP_IT_TEXT) continue;
      if (it->x == 0x128 && it->y == source_y) label = it;
      if (it->x == 0x1ea && it->y == source_y + 3) value = it;
    }
    UiRect rr = ui_rect(in.x, y + r * row, in.w, row - ui_s(c, 4));
    uid_rrect(ui_drawer(c), rr, ui_s(c, 10), uid_alpha(t->surface_inset, 0.45f));
    UiColor tone = label ? text_colour(t, label->col) : t->text_secondary;
    if (label && label->col == 0x421f) tone = (UiColor){87,65,142,255};
    if (label && label->col == 0x7ff0) tone = (UiColor){123,84,0,255};
    ui_text_fit(c, ui_rect(rr.x + ui_s(c, 10), rr.y, rr.w - value_w - ui_s(c, 18), rr.h), UI_TYPE_BODY,
                tone, label ? label->s : "Calculating...", 0);
    ui_text_fit(c, ui_rect(rr.x + rr.w - value_w - ui_s(c, 10), rr.y, value_w, rr.h), UI_TYPE_NUMBER,
                tone, value ? value->s : "", 1);
  }
  ui_text_fit(c, ui_rect(in.x, y + 4 * row + ui_s(c, 6), in.w, ui_s(c, 30)), UI_TYPE_CAPTION,
              t->text_secondary, "Close or press any key to return to the course.", 0);
  return na;
}

int ui_pages_draw(UiCtx *c, const RpSnap *s, SDL_Texture *const *art, RpAction *acts, int max) {
  switch (s->kind) {
    case RP_PROFILE: return draw_profile(c, s, art, acts, max);
    case RP_TRACTS: return draw_tracts(c, s, art, acts, max);
    case RP_SKILLS: return draw_skills(c, s, acts, max);
    case RP_PHOTO: return draw_photo(c, s, art, acts, max);
    case RP_END_YEAR: return draw_year(c, s, acts, max);
    case RP_TOP_DESIGNERS: return draw_designers(c, s, acts, max);
    case RP_PAIRING: return draw_pairing(c, s, acts, max);
    case RP_ROUTING: return draw_routing(c, s, art, acts, max);
    case RP_CREDITS: return draw_credits(c, s, acts, max);
    case RP_SHOT_ANALYSIS: return draw_analysis(c, s, acts, max);
    case RP_SNAPSHOT: return draw_snapshot(c, s, art, acts, max);
    default: return 0;
  }
}

void ui_pages_layout(RpLayout *out) { *out = g_pf_layout; }
