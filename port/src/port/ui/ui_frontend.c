// Frosted Mint front end (task-32): see ui_frontend.h.
#include "port/ui/ui_frontend.h"
#include "port/ui/ui_text.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static FeLayout g_layout;
const FeLayout *ui_frontend_layout(void) { return &g_layout; }

void fe_money(char *out, int n, int dollars) {
  if (dollars < 0) {
    snprintf(out, (size_t)n, "Unlimited");
    return;
  }
  char digits[24];
  snprintf(digits, sizeof digits, "%d", dollars);
  int len = (int)strlen(digits), o = 0;
  if (n < 2) return;
  out[o++] = '$';
  for (int i = 0; i < len && o < n - 1; i++) {
    out[o++] = digits[i];
    int left = len - 1 - i;
    if (left > 0 && left % 3 == 0 && o < n - 1) out[o++] = ',';
  }
  out[o] = 0;
}

// The picture the original screen drew is covered by world.bg and two soft
// mint glows; the panels then frost this.
void ui_frontend_background(SDL_Renderer *r, const UiTokens *t, int w, int h) {
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_Color top = {t->world_bg_top.r, t->world_bg_top.g, t->world_bg_top.b, 255};
  SDL_Color mid = {t->world_bg_mid.r, t->world_bg_mid.g, t->world_bg_mid.b, 255};
  SDL_Color bot = {t->world_bg_bottom.r, t->world_bg_bottom.g, t->world_bg_bottom.b, 255};
  float ym = h * 0.55f;
  SDL_Vertex v[6] = {{{0, 0}, top, {0, 0}},     {{(float)w, 0}, top, {0, 0}},     {{0, ym}, mid, {0, 0}},
                     {{(float)w, ym}, mid, {0, 0}}, {{0, (float)h}, bot, {0, 0}}, {{(float)w, (float)h}, bot, {0, 0}}};
  int idx[12] = {0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4};
  SDL_RenderGeometry(r, NULL, v, 6, idx, 12);
  // soft glows: radial fans, opaque-ish centre fading to nothing
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  struct { float x, y, rad; SDL_Color c; } glows[3] = {
      {0.18f, 0.85f, 0.55f, {120, 230, 180, 120}}, {0.85f, 0.15f, 0.5f, {255, 255, 255, 140}}, {0.55f, 0.5f, 0.6f, {255, 255, 255, 60}}};
  for (int g = 0; g < 3; g++) {
    float cx = glows[g].x * w, cy = glows[g].y * h, rad = glows[g].rad * (float)h;
    enum { N = 48 };
    SDL_Vertex fan[N + 2];
    int fi[N * 3];
    fan[0] = (SDL_Vertex){{cx, cy}, glows[g].c, {0, 0}};
    SDL_Color edge = glows[g].c;
    edge.a = 0;
    for (int i = 0; i <= N; i++) {
      float a = 6.2831853f * i / N;
      fan[1 + i] = (SDL_Vertex){{cx + rad * cosf(a), cy + rad * sinf(a)}, edge, {0, 0}};
    }
    for (int i = 0; i < N; i++) {
      fi[i * 3] = 0;
      fi[i * 3 + 1] = 1 + i;
      fi[i * 3 + 2] = 2 + i;
    }
    SDL_RenderGeometry(r, NULL, fan, N + 2, fi, N * 3);
  }
}


// ------------------------------------------------------------------ shared pieces

typedef struct {
  UiCtx *c;
  const UiTokens *t;
  UiRect S;        // the 1920 x 1080 design, centred in the target
  float u;         // target px per design px
} Scene;

static float U(const Scene *s, float v) { return floorf(v * s->u + 0.5f); }
static UiRect R(const Scene *s, float x, float y, float w, float h) {
  return ui_rect_round(ui_rect(s->S.x + x * s->u, s->S.y + y * s->u, w * s->u, h * s->u));
}

static void scene_init(Scene *s, UiCtx *c) {
  s->c = c;
  s->t = ui_tokens(c);
  UiRect screen = ui_screen(c);
  float k = ui_scale(c);   // the caller already fitted the scale to the screen
  s->u = k;
  s->S = ui_rect_round(ui_center(screen, 1920 * k, 1080 * k));
}

// A round back button and a title capsule (the head of every screen after the start menu).
static int head_capsule(Scene *s, const char *context, const char *title, UiRect *out_back) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  float h = 68, pad = 10;
  float tw = ui_text_width(c, UI_TYPE_TITLE, title) / s->u;
  float cw = pad + 48 + 14 + (tw > 150 ? tw : 150) + 28;
  UiRect r = R(s, 40, 36, cw, h);
  ui_panel(c, r, r.h / 2, UI_PANEL_READABLE);
  UiRect b = R(s, 40 + pad, 36 + (h - 48) / 2, 48, 48);
  uid_rrect(ui_drawer(c), b, b.w / 2, uid_alpha(t->surface_inset, 0.9f));
  int clicked = ui_icon_button(c, ui_id("fe.back"), b, UI_ICON_BACK, UI_ROUND, "Back");
  if (out_back) *out_back = b;
  float tx = b.x + b.w + U(s, 14);
  UiFont cf = ui_font(c, UI_TYPE_CAPTION), tf = ui_font(c, UI_TYPE_TITLE);
  float th = (context && *context ? cf.line : 0) + tf.line;
  float y = r.y + (r.h - th) / 2;
  if (context && *context) {
    ui_text_in(c, ui_rect(tx, y, r.w, cf.line), UI_TYPE_CAPTION, t->text_secondary, context, 0);
    y += cf.line;
  }
  ui_text_in(c, ui_rect(tx, y, r.w, tf.line), UI_TYPE_TITLE, t->text_primary, title, 0);
  return clicked;
}

// A start menu tile: icon, name, line. primary: the green gradient tile.
static int menu_tile(Scene *s, uint32_t id, UiRect r, UiIcon icon, const char *name, const char *line, int primary) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  float rad = U(s, t->radius_menu_tile);
  ui_hit(c, r, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
  float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
  float press = ui_is_active(c, id) ? 0.97f : 1.0f;
  UiRect b = r;
  if (press < 1) {
    b.x += r.w * (1 - press) / 2;
    b.y += r.h * (1 - press) / 2;
    b.w *= press;
    b.h *= press;
  }
  UiColor ink = primary ? t->text_on_accent : t->text_primary;
  UiColor sub = primary ? (UiColor){255, 255, 255, 220} : t->text_secondary;
  if (primary) {
    uid_rrect_shadow(ui_drawer(c), b, rad, 0, U(s, 12), U(s, 30), (UiColor){14, 143, 100, (uint8_t)(100 + 40 * hov)});
    uid_rrect_grad(ui_drawer(c), b, rad, t->accent_grad);
    uid_rrect_stroke(ui_drawer(c), b, rad, U(s, 1), (UiColor){255, 255, 255, 100});
  } else {
    uid_rrect(ui_drawer(c), b, rad, uid_mix(t->surface_inset, t->glass_fill_strong, hov));
    uid_rrect_stroke(ui_drawer(c), b, rad, U(s, 1), t->surface_inset_border);
    if (hov > 0.01f) uid_rrect_stroke(ui_drawer(c), b, rad, U(s, 2), uid_alpha(t->accent, hov * 0.6f));
  }
  float pad = U(s, 18);
  ui_icon(c, icon, ui_rect(b.x + pad, b.y + pad, U(s, 26), U(s, 26)), U(s, 26), primary ? t->text_on_accent : t->accent);
  UiFont nf = ui_font(c, UI_TYPE_HEADING), lf = ui_font(c, UI_TYPE_CAPTION);
  float ty = b.y + b.h - pad - lf.line - nf.line - U(s, 2);
  ui_text_fit(c, ui_rect(b.x + pad, ty, b.w - 2 * pad, nf.line), UI_TYPE_HEADING, ink, name, 0);
  if (line && *line) ui_text_fit(c, ui_rect(b.x + pad, ty + nf.line + U(s, 2), b.w - 2 * pad, lf.line), UI_TYPE_CAPTION, sub, line, 0);
  return ui_clicked(c, id);
}

static void push_act(FeAction *acts, int max, int *n, int kind, int arg) {
  if (*n < max) {
    acts[*n].kind = kind;
    acts[*n].arg = arg;
    (*n)++;
  }
}

#ifndef SIMGOLF_VERSION
#define SIMGOLF_VERSION "dev"
#endif

// ------------------------------------------------------------------ the start menu

static void draw_updates_ui(Scene *s, UpdState *u, const UiRect *panel, FeAction *acts, int max, int *n);

static int draw_title(Scene *s, const FeSnap *snap, SDL_Texture *const *art, FeAction *acts, int max) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  int n = 0;
  const float PW = 820, PAD = 44;
  const int full = snap->logo_full;   // task-80: the HD logo art already carries "Remastered"
  float logo_w = full ? 600 : 420, logo_h = 157;
  SDL_Texture *logo = art[FE_ART_LOGO];
  if (logo) {
    int tw = 1, th = 1;
    SDL_QueryTexture(logo, NULL, NULL, &tw, &th);
    logo_h = logo_w * th / (float)tw;
  }
  float tile_h = 126, gap = 12;
  const float rem_h = full ? 0 : 34;   // task-80: room for "Remastered", which overlaps the logo's bottom edge
  float ph = 40 + logo_h + rem_h + 20 + tile_h * 2 + gap + 22 + 40 + 30;
  UiRect p = R(s, (1920 - PW) / 2, (1080 - ph) / 2, PW, ph);
  ui_panel(c, p, U(s, t->radius_panel), UI_PANEL_READABLE);
  if (logo) {
    UiRect lr = R(s, (1920 - logo_w) / 2, (1080 - ph) / 2 + 40, logo_w, logo_h);
    uid_image(ui_drawer(c), logo, NULL, lr, 0, (UiColor){255, 255, 255, 255});
  } else {
    UiRect lr = R(s, (1920 - logo_w) / 2, (1080 - ph) / 2 + 40, logo_w, logo_h);
    ui_text_in(c, lr, UI_TYPE_DISPLAY, t->text_primary, "SimGolf", 0.5f);
  }
  // task-80: "Remastered" styled like the logo's lettering (heavy green letters, white rim, dark green
  // edge and drop shadow), tucked up into the bottom right of the logo so it reads as part of it.
  if (!full) {
    UiText *tx = ui_texter(c);
    const char *word = "Remastered";
    float px = 50 * s->u, rim = 3.2f * s->u, edge = 5.4f * s->u;
    float w = uit_measure(tx, 800, px, word, -0.01f);
    float x = s->S.x + ((1920 + logo_w) / 2 - 18) * s->u - w;
    float base = s->S.y + ((1080 - ph) / 2 + 40 + logo_h + 22) * s->u;
    const UiColor shadow = {6, 48, 18, 110}, dark = {12, 82, 30, 255}, white = {255, 255, 255, 255}, green = {38, 150, 58, 255};
    uit_draw(tx, x + 2 * s->u, base + 4 * s->u, 800, px, word, -0.01f, shadow);
    for (int i = 0; i < 16; i++) {   // dark outer edge, then the white rim, sampled around a circle
      float a = i * 0.3927f;
      uit_draw(tx, x + cosf(a) * edge, base + sinf(a) * edge, 800, px, word, -0.01f, dark);
    }
    for (int i = 0; i < 16; i++) {
      float a = i * 0.3927f;
      uit_draw(tx, x + cosf(a) * rim, base + sinf(a) * rim, 800, px, word, -0.01f, white);
    }
    uit_draw(tx, x, base, 800, px, word, -0.01f, green);
    uit_draw(tx, x, base - 1.2f * s->u, 800, px, word, -0.01f, (UiColor){70, 182, 84, 120});   // light top edge, like the logo bevel
  }
  float gx = (1920 - PW) / 2 + PAD, gy = (1080 - ph) / 2 + 40 + logo_h + rem_h + 20;
  float gw = PW - 2 * PAD, tw3 = (gw - 2 * gap) / 3;
  char sub[64];
  snprintf(sub, sizeof sub, "Theme: %s", snap->theme_name[0] ? snap->theme_name : "Standard");
  static const struct { int act; UiIcon icon; const char *name, *line; } T[5] = {
      {1, UI_ICON_FLAG, "New game", NULL},
      {0, UI_ICON_FOLDER, "Continue", "Load a saved game"},
      {4, UI_ICON_SPARKLE, "Sandbox", "Unlimited money"},
      {3, UI_ICON_CUP, "Championship", "Play the pros"},
      {2, UI_ICON_PALETTE, "Themes", "Pick a content pack"}};
  UiRect pos[5] = {R(s, gx, gy, tw3 * 2 + gap, tile_h), R(s, gx + 2 * (tw3 + gap), gy, tw3, tile_h),
                   R(s, gx, gy + tile_h + gap, tw3, tile_h), R(s, gx + tw3 + gap, gy + tile_h + gap, tw3, tile_h),
                   R(s, gx + 2 * (tw3 + gap), gy + tile_h + gap, tw3, tile_h)};
  for (int i = 0; i < 5; i++) {
    g_layout.btn[T[i].act] = pos[i];
    if (menu_tile(s, ui_idi(ui_id("fe.title"), i), pos[i], T[i].icon, T[i].name, i == 0 ? sub : T[i].line, i == 0))
      push_act(acts, max, &n, FE_A_TITLE, T[i].act);
  }
  float fy = gy + tile_h * 2 + gap + 22;
  ui_text_in(c, R(s, gx, fy, 400, 40), UI_TYPE_CAPTION, t->text_secondary, "SimGolf Remastered  v" SIMGOLF_VERSION, 0);
  UiRect q = R(s, gx + gw - 118, fy, 118, 40);
  g_layout.btn[5] = q;
  if (ui_button(c, ui_id("fe.quit"), q, "Quit", UI_ICON_DOOR, UI_BTN_PILL, 0)) push_act(acts, max, &n, FE_A_TITLE, 5);
  UiRect ub = R(s, gx + gw - 118 - 12 - 150 - 12 - 110 - 12 - 140, fy, 140, 40);   // task-80: Updates (a dot after an update until it is opened)
  g_layout.upd_btn = ub;
  if (snap->upd) {
    if (ui_button(c, ui_id("fe.updates"), ub, "Updates", UI_ICON_BELL, UI_BTN_PILL, 0)) {
      snap->upd->open = 1;
      snap->upd->expanded = -1;
      push_act(acts, max, &n, FE_A_UPDATES, 0);
    }
    if (snap->upd->show_dot) {
      float d = U(s, 12);
      UiRect dr = ui_rect(ub.x + ub.w - d - U(s, 6), ub.y - d / 2 + U(s, 4), d, d);
      uid_rrect(ui_drawer(c), ui_inset_all(dr, -U(s, 2)), d, (UiColor){255, 255, 255, 255});
      uid_rrect(ui_drawer(c), dr, d / 2, (UiColor){232, 87, 62, 255});
    }
  }
  UiRect o = R(s, gx + gw - 118 - 12 - 150, fy, 150, 40);   // task-24: the native Options page (handled by the presenter, not the original)
  g_layout.btn[6] = o;
  if (ui_button(c, ui_id("fe.options"), o, "Options", UI_ICON_GEAR, UI_BTN_PILL, 0)) push_act(acts, max, &n, FE_A_OPTIONS, 0);
  UiRect md = R(s, gx + gw - 118 - 12 - 150 - 12 - 110, fy, 110, 40);   // task-22: the native Mods screen
  g_layout.btn[7] = md;
  if (ui_button(c, ui_id("fe.mods"), md, "Mods", UI_ICON_SPARKLE, UI_BTN_PILL, 0)) push_act(acts, max, &n, FE_A_MODS, 0);
  if (snap->upd) draw_updates_ui(s, snap->upd, &p, acts, max, &n);
  return n;
}

// ------------------------------------------------------------------ difficulty

static int draw_difficulty(Scene *s, const FeSnap *snap, SDL_Texture *const *art, FeAction *acts, int max) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  int n = 0;
  UiRect back;
  if (head_capsule(s, snap->championship ? "Championship" : "New game", "How hard should it be?", &back))
    push_act(acts, max, &n, FE_A_BACK, 0);
  g_layout.back = back;
  static const char *const names[4] = {"Easy", "Moderate", "Difficult", "Impossible"};
  static const char *const notes[4] = {"Relaxed", "Balanced", "Demanding", "Brutal"};
  const float CW = 250, CH = 380, G = 20;
  float total = 4 * CW + 3 * G;
  float x0 = (1920 - total) / 2, y0 = (1080 - CH) / 2 + 20;
  for (int i = 0; i < 4; i++) {
    UiRect r = R(s, x0 + i * (CW + G), y0, CW, CH);
    uint32_t id = ui_idi(ui_id("fe.diff"), i);
    float rad = U(s, t->radius_card);
    ui_hit(c, r, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
    float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
    UiRect b = r;
    b.y -= U(s, 6) * hov;
    uid_rrect_shadow(ui_drawer(c), b, rad, 0, U(s, 14), U(s, 36), (UiColor){0, 60, 30, (uint8_t)(50 + 40 * hov)});
    ui_panel(c, b, rad, UI_PANEL_NOSHADOW | UI_PANEL_NOBLOCK | UI_PANEL_READABLE);
    if (hov > 0.01f) uid_rrect_stroke(ui_drawer(c), b, rad, U(s, 2), uid_alpha(t->accent, hov));
    g_layout.btn[i] = r;
    float fs = 150;
    UiRect fr = R(s, x0 + i * (CW + G) + (CW - fs) / 2, y0 + 34 - 6 * hov, fs, fs);
    SDL_Texture *face = art[FE_ART_FACE + i];
    if (face) uid_image(ui_drawer(c), face, NULL, fr, 0, (UiColor){255, 255, 255, 255});
    else {
      uid_rrect(ui_drawer(c), fr, fr.w / 2, t->accent_soft);
      ui_icon(c, UI_ICON_SMILE, fr, U(s, 80), t->accent);
    }
    UiFont nf = ui_font(c, UI_TYPE_TITLE), bf = ui_font(c, UI_TYPE_BODY), cf = ui_font(c, UI_TYPE_CAPTION);
    float ty = fr.y + fr.h + U(s, 26);
    ui_text_in(c, ui_rect(r.x, ty, r.w, nf.line), UI_TYPE_TITLE, t->text_primary, names[i], 0.5f);
    ui_text_in(c, ui_rect(r.x, ty + nf.line + U(s, 4), r.w, bf.line), UI_TYPE_BODY, t->text_secondary, notes[i], 0.5f);
    {
      // The difficulty table changes payroll frequency and fatigue per hole.
      static const char *const tire[5] = {"-", "Low", "Medium", "High", "Very high"};
      char w[32];
      snprintf(w, sizeof w, "%d times/month", snap->wages[i] > 0 ? snap->wages[i] : 2);
      int fa = snap->fatigue[i] < 1 ? 1 : snap->fatigue[i] > 4 ? 4 : snap->fatigue[i];
      float ry = r.y + r.h - U(s, 28) - U(s, 76);
      const char *lab[2] = {"Payroll", "Golfer fatigue"};
      const char *val[2] = {w, tire[fa]};
      for (int k = 0; k < 2; k++) {
        UiRect row = ui_rect(r.x + U(s, 24), ry + k * U(s, 40), r.w - U(s, 48), U(s, 36));
        uid_rrect(ui_drawer(c), row, row.h / 2, t->accent_soft);
        ui_text_in(c, ui_rect(row.x + U(s, 14), row.y, row.w, row.h), UI_TYPE_CAPTION, t->text_secondary, lab[k], 0);
        ui_text_in(c, ui_rect(row.x, row.y, row.w - U(s, 14), row.h), UI_TYPE_LABEL, t->text_primary, val[k], 1);
      }
      (void)cf;
    }
    if (ui_clicked(c, id)) push_act(acts, max, &n, FE_A_DIFFICULTY, i);
  }
  return n;
}

// ------------------------------------------------------------------ the property (world) screen
#include "ui_map_medallions.inc"

static int cmp_tags(const FeTag *a, const FeTag *b) {
  // affordable first (cheapest first), then too dear (cheapest first), owned last
  int ra = a->state == 1 ? 0 : a->state == 0 ? 1 : 2, rb = b->state == 1 ? 0 : b->state == 0 ? 1 : 2;
  if (ra != rb) return ra - rb;
  if (a->price != b->price) return a->price < b->price ? -1 : 1;
  return strcmp(a->name, b->name);
}

static void wrap_text(UiCtx *c, int style, const char *s, float w, char (*out)[160], int max, int *n) {
  *n = 0;
  char line[160] = "";
  const char *p = s;
  while (*p && *n < max) {
    const char *e = p;
    while (*e && *e != ' ') e++;
    char word[100];
    int wl = (int)(e - p);
    if (wl > 99) wl = 99;
    memcpy(word, p, (size_t)wl);
    word[wl] = 0;
    char trial[260];
    snprintf(trial, sizeof trial, "%s%s%s", line, line[0] ? " " : "", word);
    if (line[0] && ui_text_width(c, style, trial) > w) {
      snprintf(out[(*n)++], 160, "%s", line);
      snprintf(line, sizeof line, "%s", word);
    } else {
      snprintf(line, sizeof line, "%s", trial);
    }
    p = *e ? e + 1 : e;
  }
  if (*n < max && line[0]) snprintf(out[(*n)++], 160, "%s", line);
}

// ------------------------------------------------------------------ updates (task-80)

// "SimGolf Remastered x.y.z is out" with Update now and Later, over the start menu panel p.
static void draw_update_notice(Scene *s, UpdState *u, const UiRect *p, FeAction *acts, int max, int *n) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  char msg[160];
  if (u->busy == 1 && !strncmp(u->status_text,"Downloading",11)) {
    if(u->download_per_mille>=0)snprintf(msg,sizeof msg,"Downloading update %d%% (%d / %d MB)",u->download_per_mille/10,u->download_kb/1000,u->download_total_kb/1000);
    else snprintf(msg,sizeof msg,"Downloading update (%d MB received)",u->download_kb/1000);
  } else if (u->busy == 1) snprintf(msg, sizeof msg, "%s", u->status_text[0] ? u->status_text : "Updating");
  else if (u->busy == 2) snprintf(msg, sizeof msg, "%s", u->status_text[0] ? u->status_text : "The update did not work");
  else snprintf(msg, sizeof msg, "SimGolf Remastered %s is out", u->latest);
  float tw = ui_text_width(c, UI_TYPE_HEADING, msg) / s->u;
  int buttons = u->busy == 0 || u->busy == 2;
  const float bw = 150, lw = 100;
  const float h = u->busy==1 ? 90 : 64;
  float w = 58 + tw + (buttons ? 24 + bw + 10 + lw : 0) + 18;
  if (w > 1500) w = 1500;
  UiRect r = R(s, (1920 - w) / 2, (p->y - s->S.y) / s->u - h - 22, w, h);
  if (r.y < s->S.y + U(s, 16)) r.y = s->S.y + U(s, 16);
  ui_panel(c, r, r.h / 2, UI_PANEL_READABLE);
  uid_rrect(ui_drawer(c), ui_rect(r.x + U(s, 20), r.y + (r.h - U(s, 12)) / 2, U(s, 12), U(s, 12)), U(s, 6), u->busy == 2 ? t->danger : t->accent);
  ui_text_fit(c, ui_rect(r.x + U(s, 44), r.y, U(s, tw + 8), r.h), UI_TYPE_HEADING, t->text_primary, msg, 0);
  if (u->busy==1) {
    UiRect track=ui_rect(r.x+U(s,44),r.y+r.h-U(s,20),r.w-U(s,64),U(s,6));
    uid_rrect(ui_drawer(c),track,U(s,3),t->meter_track);
    int amount=u->download_per_mille;
    if(amount>=0){UiRect fill=track;fill.w*=fminf(1000,fmaxf(0,(float)amount))/1000;uid_rrect(ui_drawer(c),fill,U(s,3),t->accent);}
  }
  if (!buttons) return;
  UiRect now = ui_rect(r.x + r.w - U(s, 18 + lw + 10 + bw), r.y + U(s, 10), U(s, bw), r.h - U(s, 20));
  UiRect later = ui_rect(r.x + r.w - U(s, 18 + lw), r.y + U(s, 10), U(s, lw), r.h - U(s, 20));
  g_layout.upd_now = now;
  g_layout.upd_later = later;
  if (ui_button(c, ui_id("fe.upd.now"), now, u->busy == 2 ? "Try again" : "Update now", UI_ICON_NONE, UI_BTN_PRIMARY, 0))
    push_act(acts, max, n, FE_A_UPDATES, 2);
  if (ui_button(c, ui_id("fe.upd.later"), later, "Later", UI_ICON_NONE, UI_BTN_PILL, 0)) push_act(acts, max, n, FE_A_UPDATES, 1);
}

// The panel: every release newest first; a row opens to its bullet points.
static void draw_update_panel(Scene *s, UpdState *u) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  UiRect scr = ui_screen(c);
  uid_rrect(ui_drawer(c), scr, 0, t->scrim);
  ui_hit(c, scr, 0, 0, UI_HIT_BLOCK | UI_HIT_MODAL);
  UiRect p = R(s, (1920 - 940) / 2, 70, 940, 940);
  uid_rrect(ui_drawer(c), p, U(s, t->radius_panel), (UiColor){236, 247, 241, 255});   // opaque: the menu must not show through the list
  ui_panel(c, p, U(s, t->radius_panel), UI_PANEL_READABLE | UI_PANEL_SOLID);
  UiRect in = ui_inset_all(p, U(s, 32));
  float hh = U(s, 44);
  ui_text_in(c, ui_rect(in.x, in.y, in.w - hh - U(s, 12), hh), UI_TYPE_TITLE, t->text_primary, "Updates", 0);
  UiRect closer = ui_rect(in.x + in.w - hh, in.y, hh, hh);
  g_layout.upd_close = closer;
  if (ui_icon_button(c, ui_id("fe.upd.close"), closer, UI_ICON_X, UI_ROUND, "Close (Esc)") || u->close_req) {
    u->open = 0;
    u->close_req = 0;
    return;
  }
  UiFont cf = ui_font(c, UI_TYPE_CAPTION), bf = ui_font(c, UI_TYPE_BODY), hf = ui_font(c, UI_TYPE_HEADING);
  ui_text_in(c, ui_rect(in.x, in.y + hh, in.w, cf.line + U(s, 4)), UI_TYPE_CAPTION, t->text_secondary,
             "You have SimGolf Remastered v" SIMGOLF_VERSION ". Click a release to see what is new.", 0);
  UiRect view = ui_rect(in.x, in.y + hh + cf.line + U(s, 22), in.w, in.h - hh - cf.line - U(s, 22));
  g_layout.upd_view = view;
  const float ROW = 74, GAP = 8;
  float line_h = bf.line;
  static char wrapped[40][8][160];
  static int wn[40];
  int open = u->expanded;
  float body_h = 0;
  if (open >= 0 && open < u->n_rel) {
    const UpdRelease *R_ = &u->rel[open];
    for (int k = 0; k < R_->n && k < 40; k++) {
      wrap_text(c, UI_TYPE_BODY, R_->note[k], view.w - U(s, 6 + 32 + 40), wrapped[k], 8, &wn[k]);
      body_h += wn[k] * line_h + U(s, 12);
    }
    body_h += U(s, 22);
  }
  float content = u->n_rel * U(s, ROW + GAP) + body_h;
  float off = ui_vscroll_begin(c, ui_id("fe.upd.scroll"), view, content, U(s, 70));
  float y = view.y + off;
  for (int i = 0; i < u->n_rel; i++) {
    const UpdRelease *R_ = &u->rel[i];
    UiRect rr = ui_rect(view.x, y, view.w - U(s, 6), U(s, ROW));
    uint32_t id = ui_idi(ui_id("fe.upd.row"), i);
    float rad = U(s, 18);
    ui_hit(c, rr, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
    float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
    uid_rrect(ui_drawer(c), rr, rad, uid_mix(t->surface_inset, t->glass_fill_strong, hov));
    uid_rrect_stroke(ui_drawer(c), rr, rad, U(s, i == open ? 2 : 1), i == open ? t->accent : t->surface_inset_border);
    ui_text_in(c, ui_rect(rr.x + U(s, 24), rr.y, U(s, 110), rr.h), UI_TYPE_HEADING, t->text_primary, R_->version, 0);
    float tagx = rr.x + U(s, 24 + 110);
    if (!upd_version_cmp(R_->version, SIMGOLF_VERSION)) {   // the build the player runs
      float tgw = ui_text_width(c, UI_TYPE_MICRO, "Installed") + U(s, 20);
      UiRect tg = ui_rect(tagx, rr.y + (rr.h - U(s, 22)) / 2, tgw, U(s, 22));
      uid_rrect(ui_drawer(c), tg, tg.h / 2, t->accent_soft);
      ui_text_in(c, tg, UI_TYPE_MICRO, t->accent_soft_text, "Installed", 0.5f);
      tagx += tgw + U(s, 14);
    }
    float dw = ui_text_width(c, UI_TYPE_CAPTION, R_->date) + U(s, 4);
    ui_icon(c, i == open ? UI_ICON_UP : UI_ICON_DOWN, ui_rect(rr.x + rr.w - U(s, 24 + 22), rr.y + (rr.h - U(s, 22)) / 2, U(s, 22), U(s, 22)), U(s, 22), t->text_secondary);
    float dx = rr.x + rr.w - U(s, 24 + 22 + 16) - dw;
    ui_text_in(c, ui_rect(dx, rr.y, dw, rr.h), UI_TYPE_CAPTION, t->text_secondary, R_->date, 0);
    ui_text_fit(c, ui_rect(tagx, rr.y, dx - tagx - U(s, 14), rr.h), UI_TYPE_BODY, t->text_primary, R_->name, 0);
    if (i < 8) g_layout.upd_row[i] = rr;
    if (ui_clicked(c, id)) u->expanded = i == open ? -1 : i;
    y += rr.h + U(s, GAP);
    if (i == open) {
      float yy = y + U(s, 4);
      for (int k = 0; k < R_->n && k < 40; k++) {
        uid_rrect(ui_drawer(c), ui_rect(rr.x + U(s, 30), yy + (line_h - U(s, 8)) / 2 + U(s, 2), U(s, 8), U(s, 8)), U(s, 4), t->accent);
        for (int li = 0; li < wn[k]; li++)
          ui_text_in(c, ui_rect(rr.x + U(s, 50), yy + li * line_h, rr.w, line_h), UI_TYPE_BODY, t->text_secondary, wrapped[k][li], 0);
        yy += wn[k] * line_h + U(s, 12);
      }
      y += body_h;
    }
  }
  ui_vscroll_end(c);
}

static void draw_updates_ui(Scene *s, UpdState *u, const UiRect *panel, FeAction *acts, int max, int *n) {
  if (u->avail || u->busy) draw_update_notice(s, u, panel, acts, max, n);
  if (u->open) draw_update_panel(s, u);   // last: its scrim blocks the menu under it
}

static void pin_dot(Scene *s, float cx, float cy, UiColor col, int selected, int hot) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  float r = U(s, selected ? 11 : hot ? 9 : 7.5f);
  if (selected) uid_rrect(ui_drawer(c), ui_rect(cx - r - U(s, 7), cy - r - U(s, 7), 2 * (r + U(s, 7)), 2 * (r + U(s, 7))), r + U(s, 7), (UiColor){35, 162, 107, 64});
  uid_rrect_shadow(ui_drawer(c), ui_rect(cx - r, cy - r, 2 * r, 2 * r), r, 0, U(s, 2), U(s, 6), (UiColor){0, 0, 0, 90});
  uid_rrect(ui_drawer(c), ui_rect(cx - r, cy - r, 2 * r, 2 * r), r, (UiColor){255, 255, 255, 255});
  float ri = r - U(s, 2);
  uid_rrect(ui_drawer(c), ui_rect(cx - ri, cy - ri, 2 * ri, 2 * ri), ri, col);
  (void)t;
}

static int draw_world(Scene *s, const FeSnap *snap, SDL_Texture *const *art, FeAction *acts, int max) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  const FeWorld *W = &snap->world;
  int n = 0;
  static const char *const diffs[4] = {"Easy", "Moderate", "Difficult", "Impossible"};
  char ctx[64];
  if (snap->in_game) snprintf(ctx, sizeof ctx, "Menu");
  else if (W->sandbox) snprintf(ctx, sizeof ctx, "Sandbox");
  else if (W->difficulty >= 0) snprintf(ctx, sizeof ctx, "New game  \xC2\xB7  %s", diffs[W->difficulty]);
  else snprintf(ctx, sizeof ctx, "New game");
  UiRect back;
  if (head_capsule(s, ctx, snap->in_game ? "World map" : W->heading[0] ? W->heading : "Where will you build your golf course?", &back))
    push_act(acts, max, &n, FE_A_BACK, 0);
  g_layout.back = back;

  // order of the list
  int order[FE_TAGS], cnt = 0;
  for (int i = 0; i < W->n; i++)
    if (W->tag[i].name[0]) order[cnt++] = i;
  for (int a = 1; a < cnt; a++)
    for (int b = a; b > 0 && cmp_tags(&W->tag[order[b]], &W->tag[order[b - 1]]) < 0; b--) {
      int tmp = order[b];
      order[b] = order[b - 1];
      order[b - 1] = tmp;
    }
  float *selp = ui_state(c, ui_id("fe.world.sel"));
  int sel = (int)(*selp + 0.5f) - 1;
  if (sel >= W->n || (sel >= 0 && !W->tag[sel].name[0])) sel = -1;

  // the globe and its pins
  const float GD = 820, GCX = 640, GCY = 610;
  float k = GD / (2.0f * W->gr);
  SDL_Texture *globe = art[FE_ART_GLOBE];
  UiRect gr_ = R(s, GCX - GD / 2, GCY - GD / 2, GD, GD);
  if (globe) {
    int tw = 1, th = 1;
    SDL_QueryTexture(globe, NULL, NULL, &tw, &th);
    float ds = ((tw > 400 ? 424 : tw) * k);   // the HD globe covers golf's 392 px circle plus 16 px each side
    UiRect gi = R(s, GCX - ds / 2, GCY - ds / 2, ds, ds);
    uid_rrect_shadow(ui_drawer(c), gr_, gr_.w / 2, 0, U(s, 18), U(s, 50), (UiColor){0, 60, 30, 70});
    uid_image(ui_drawer(c), globe, NULL, gi, 0, (UiColor){255, 255, 255, 255});
  } else {
    uid_rrect(ui_drawer(c), gr_, gr_.w / 2, t->accent_soft);
  }
  int pin_hit = -1;
  for (int i = 0; i < W->n; i++) {
    const FeTag *T = &W->tag[i];
    if (!T->name[0]) continue;
    float px = GCX + (T->px - W->gcx) * k, py = GCY + (T->py - W->gcy) * k;
    UiRect hr = R(s, px - 14, py - 14, 28, 28);
    uint32_t pid = ui_idi(ui_id("fe.world.pin"), i);
    ui_hit(c, hr, hr.w / 2, pid, UI_HIT_BLOCK | UI_HIT_INTERACT);
    if (ui_clicked(c, pid)) pin_hit = i;
  }
  for (int pass = 0; pass < 2; pass++)   // the selected pin on top
    for (int i = 0; i < W->n; i++) {
      const FeTag *T = &W->tag[i];
      if (!T->name[0] || (pass == 1) != (i == sel)) continue;
      float px = GCX + (T->px - W->gcx) * k, py = GCY + (T->py - W->gcy) * k;
      UiColor col = T->state == 1 ? (UiColor){242, 181, 58, 255} : T->state == 2 ? t->danger : (UiColor){154, 165, 159, 255};
      if (i == sel) col = (UiColor){35, 162, 107, 255};
      pin_dot(s, floorf(s->S.x + px * s->u + 0.5f), floorf(s->S.y + py * s->u + 0.5f), col, i == sel,
              ui_is_hot(c, ui_idi(ui_id("fe.world.pin"), i)));
    }

  // the legend
  {
    static const struct { const char *label; UiColor col; } L[3] = {{"Available", {242, 181, 58, 255}}, {"Can't afford", {154, 165, 159, 255}}, {"Purchased", {217, 88, 59, 255}}};
    float x = 50;
    for (int i = 0; i < 3; i++) {
      pin_dot(s, s->S.x + U(s, x + 8), s->S.y + U(s, 1038), L[i].col, 0, 0);
      float tw = ui_text_width(c, UI_TYPE_CAPTION, L[i].label);
      UiFont cf = ui_font(c, UI_TYPE_CAPTION);
      ui_text_in(c, ui_rect(s->S.x + U(s, x + 24), s->S.y + U(s, 1038) - cf.line / 2, tw + 2, cf.line), UI_TYPE_CAPTION, t->text_secondary, L[i].label, 0);
      x += 24 + tw / s->u + 26;
    }
  }

  // the list
  UiRect panel = R(s, 1240, 40, 640, 1000);
  ui_panel(c, panel, U(s, t->radius_panel), UI_PANEL_READABLE);
  float pad = U(s, 18);
  UiRect in = ui_inset_all(panel, pad);
  UiRect head = ui_rect(in.x, in.y, in.w, U(s, 52));
  uid_rrect(ui_drawer(c), head, U(s, 18), uid_alpha(t->surface_inset, 0.9f));
  ui_icon(c, UI_ICON_PIN, ui_rect(head.x + U(s, 14), head.y, U(s, 20), head.h), U(s, 20), t->accent);
  char cntline[40];
  snprintf(cntline, sizeof cntline, "%d properties", cnt);
  ui_text_in(c, ui_rect(head.x + U(s, 42), head.y, head.w, head.h), UI_TYPE_LABEL, t->text_secondary, cntline, 0);
  float right = head.x + head.w - U(s, 14);
  if (W->budget[0]) {
    float bw = ui_text_width(c, UI_TYPE_NUMBER, W->budget);
    right -= bw;
    ui_text_in(c, ui_rect(right, head.y, bw + 1, head.h), UI_TYPE_NUMBER, t->text_primary, W->budget, 0);
    right -= U(s, 26);
    ui_icon(c, UI_ICON_COINS, ui_rect(right, head.y, U(s, 18), head.h), U(s, 18), t->money);
    right -= U(s, 10);
  }
  if (W->can_refresh) {
    UiRect rb = ui_rect(right - U(s, 40), head.y + U(s, 6), U(s, 40), U(s, 40));
    if (ui_icon_button(c, ui_id("fe.world.refresh"), rb, UI_ICON_ROTR, UI_ROUND, "Look at different properties")) push_act(acts, max, &n, FE_A_REFRESH, 0);
  }
  // All 16 compact rows fit above the fixed detail/purchase footer at both 1080p and 4K.
  float gap = U(s, 3), rowh = U(s, 43), footer_h = U(s, 150);
  UiRect footer = ui_rect(in.x, in.y + in.h - footer_h, in.w, footer_h);
  UiRect view = ui_rect(in.x, head.y + head.h + U(s, 10), in.w, footer.y - U(s, 14) - (head.y + head.h + U(s, 10)));
  float y = view.y;
  int clicked_row = -1;
  for (int oi = 0; oi < cnt; oi++) {
    int i = order[oi];
    const FeTag *T = &W->tag[i];
    UiRect rr = ui_rect(view.x, y, view.w, rowh);
    uint32_t id = ui_idi(ui_id("fe.world.row"), i);
    ui_hit(c, rr, U(s, 9), id, UI_HIT_BLOCK | UI_HIT_INTERACT);
    int hot = ui_is_hot(c, id);
    if (i == sel || hot) uid_rrect(ui_drawer(c), rr, U(s, 9), uid_alpha(t->surface_inset, i == sel ? 0.95f : 0.65f));
    if (i == sel) uid_rrect_stroke(ui_drawer(c), rr, U(s, 9), U(s, 1), t->accent);
    // golf's own emblem for the property (the maintainer: the drawn medallions were no fun; [ui] map_icons=medallions brings them back)
    SDL_Texture *av = T->art >= 0 ? art[FE_ART_EMBLEM + T->art] : NULL;
    // the painted badge (task-109), greyed when you cannot buy it as golf's emblems are; [ui] map_badges=original
    SDL_Texture *bv = W->badges && !W->medallions && T->badge >= 0 ? art[(T->state == 1 ? FE_ART_BADGE : FE_ART_BADGE_DIM) + T->badge] : NULL;
    if (bv) {
      float ah = U(s, 40);
      uid_image(ui_drawer(c), bv, NULL, ui_rect(rr.x + U(s, 4), rr.y + (rr.h - ah) / 2, ah, ah), 0, (UiColor){255, 255, 255, 255});
    } else if (av && !W->medallions) {
      int ew = 1, eh = 1;
      SDL_QueryTexture(av, NULL, NULL, &ew, &eh);
      SDL_Rect src = {0, 0, ew < eh ? ew : eh, eh};   // the round badge is on the left, a sliver of golf's card follows it
      float ah = U(s, 40), aw = ah * src.w / (float)eh;
      UiRect ar = ui_rect(rr.x + U(s, 4) + (U(s, 40) - aw) / 2, rr.y + (rr.h - ah) / 2, aw, ah);
      uid_image(ui_drawer(c), av, &src, ar, 0, (UiColor){255, 255, 255, (uint8_t)(T->state == 1 ? 255 : 200)});
    } else {
      map_medallion(c, ui_rect(rr.x + U(s, 5), rr.y + U(s, 4), U(s, 35), U(s, 35)), i, T->state != 1);
    }
    char line[100], price[32] = "";
    if (T->state == 2) snprintf(line, sizeof line, "%s  |  Purchased", T->feature);
    else if (T->acres) snprintf(line, sizeof line, "%s  |  %d acres", T->feature, T->acres);
    else snprintf(line, sizeof line, "%s", T->feature);
    if (!W->sandbox && T->price > 0) fe_money(price, sizeof price, T->price);
    float tx = rr.x + U(s, 50), pw = price[0] ? ui_text_width(c, UI_TYPE_LABEL, price) + U(s, 16) : 0;
    float tw = rr.x + rr.w - U(s, 8) - tx - pw;
    ui_text_fit(c, ui_rect(tx, rr.y + U(s, 1), tw, U(s, 22)), UI_TYPE_LABEL, t->text_primary, T->name, 0);
    ui_text_fit(c, ui_rect(tx, rr.y + U(s, 22), tw, U(s, 19)), UI_TYPE_CAPTION, t->text_secondary, line, 0);
    if (price[0]) ui_text_fit(c, ui_rect(rr.x + rr.w - pw - U(s, 8), rr.y, pw, rr.h), UI_TYPE_LABEL,
                            T->state == 0 ? t->text_tertiary : t->text_primary, price, 1);
    // Unaffordable or purchased properties are still inspectable; only their purchase button is disabled.
    if (ui_clicked(c, id)) clicked_row = i;
    g_layout.row[oi] = rr;
    g_layout.row_tag[oi] = i;
    y += rowh + gap;
  }
  uid_line(ui_drawer(c), footer.x, footer.y, footer.x + footer.w, footer.y, U(s, 1), t->divider);
  if (sel >= 0) {
    const FeTag *T = &W->tag[sel];
    g_layout.world_selected = sel;
    g_layout.world_state = T->state;
    char text[256] = "";
    for (int li = 0; li < T->n_detail; li++) {
      const char *part = T->detail[li];
      while (*part == ' ') part++;
      strncat(text, part, sizeof text - strlen(text) - 1);
      strncat(text, " ", sizeof text - strlen(text) - 1);
    }
    if (!text[0]) snprintf(text, sizeof text, "%s. %s", T->feature, T->info[0] ? T->info : "Loading property details...");
    ui_text_fit(c, ui_rect(footer.x, footer.y + U(s, 8), footer.w, U(s, 24)), UI_TYPE_HEADING, t->text_primary, T->name, 0);
    char wrapped[12][160]; int nl = 0;
    wrap_text(c, UI_TYPE_CAPTION, text, footer.w - U(s, 4), wrapped, 12, &nl);
    UiFont df = ui_font(c, UI_TYPE_CAPTION);
    UiRect detail = ui_rect(footer.x, footer.y + U(s, 34), footer.w, U(s, 58));
    float offset = ui_vscroll_begin(c, ui_id("fe.world.details"), detail, nl * df.line, df.line);
    for (int li = 0; li < nl; li++) ui_text_in(c, ui_rect(detail.x, detail.y + offset + li * df.line, detail.w, df.line), UI_TYPE_CAPTION, t->text_secondary, wrapped[li], 0);
    ui_vscroll_end(c);
    UiRect bb = ui_rect(footer.x + footer.w - U(s, 220), footer.y + footer.h - U(s, 46), U(s, 220), U(s, 44));
    const char *label = T->state == 1 ? "Build here" : T->state == 2 ? "Already purchased" : "Not enough money";
    if (ui_button(c, ui_id("fe.world.build"), bb, label, T->state == 1 ? UI_ICON_CHEV : UI_ICON_NONE, UI_BTN_PRIMARY, T->state == 1 ? 0 : UI_DISABLED))
      push_act(acts, max, &n, FE_A_TAG, sel);
    g_layout.ok = bb;
  } else {
    ui_text_fit(c, ui_rect(footer.x, footer.y + U(s, 25), footer.w, U(s, 32)), UI_TYPE_BODY, t->text_secondary, "Select a property to inspect its details and price.", 0);
  }
  if (pin_hit >= 0) clicked_row = pin_hit;
  float *lastp = ui_state(c, ui_id("fe.world.sent"));   // when the pointer was last sent to the selected tag (ms)
  double now = ui_now(c);
  if (clicked_row >= 0 && clicked_row != sel) {
    *selp = (float)(clicked_row + 1);
    sel = clicked_row;
    *lastp = (float)fmod(now, 1e6);
    push_act(acts, max, &n, FE_A_HOVER_TAG, clicked_row);
  }
  // the original shows the description of the tag its pointer is on: keep that on the selected tag
  if (sel >= 0 && W->hover != sel && clicked_row < 0) {
    double since = fmod(now, 1e6) - *lastp;
    if (since < 0) since += 1e6;
    if (since > 500) {
      *lastp = (float)fmod(now, 1e6);
      push_act(acts, max, &n, FE_A_HOVER_TAG, sel);
    }
  }
  return n;
}

// ------------------------------------------------------------------ load game and championship course lists

// The list panel shared by the load game, championship and pro screens: rows in
// view (golf shows 16 at a time and scrolls 4 rows a click on its arrows).
static void draw_list_panel(Scene *s, const FeList *Lst, const char *what, const char *empty_text, FeAction *acts, int max, int *n) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  UiRect p = R(s, 40, 130, 1000, 910);
  ui_panel(c, p, U(s, t->radius_panel), UI_PANEL_READABLE);
  float pad = U(s, 18);
  UiRect in = ui_inset_all(p, pad);
  UiRect head = ui_rect(in.x, in.y, in.w, U(s, 52));
  uid_rrect(ui_drawer(c), head, U(s, 18), uid_alpha(t->surface_inset, 0.9f));
  char cnt[64];
  if (Lst->total == 1 && !strcmp(what, "championship courses")) what = "championship course";
  else if (Lst->total == 1 && !strcmp(what, "saved games")) what = "saved game";
  else if (Lst->total == 1 && !strcmp(what, "pro golfers")) what = "pro golfer";
  snprintf(cnt, sizeof cnt, "%d %s", Lst->total, what);
  ui_text_in(c, ui_rect(head.x + U(s, 18), head.y, head.w, head.h), UI_TYPE_LABEL, t->text_secondary, cnt, 0);
  if (Lst->total > FE_ROWS) {
    char rng[64];
    int a = Lst->scroll + 1, b = Lst->scroll + Lst->n_rows;
    snprintf(rng, sizeof rng, "%d to %d", a, b);
    float rw = ui_text_width(c, UI_TYPE_LABEL, rng);
    ui_text_in(c, ui_rect(head.x + head.w - U(s, 18) - rw - U(s, 92), head.y, rw + 1, head.h), UI_TYPE_LABEL, t->text_primary, rng, 0);
    UiRect up = ui_rect(head.x + head.w - U(s, 88), head.y + U(s, 6), U(s, 40), U(s, 40));
    UiRect dn = ui_rect(head.x + head.w - U(s, 44), head.y + U(s, 6), U(s, 40), U(s, 40));
    g_layout.btn[6] = up;
    g_layout.btn[7] = dn;
    if (ui_icon_button(c, ui_id("fe.list.up"), up, UI_ICON_BACK, UI_ROUND, "Earlier")) push_act(acts, max, n, FE_A_SCROLL, -1);
    if (ui_icon_button(c, ui_id("fe.list.down"), dn, UI_ICON_CHEV, UI_ROUND, "Later")) push_act(acts, max, n, FE_A_SCROLL, 1);
  }
  float rowh = U(s, 48), gap = U(s, 4);
  UiRect view = ui_rect(in.x, head.y + head.h + U(s, 10), in.w, in.y + in.h - (head.y + head.h + U(s, 10)));
  if (Lst->omitted) {
    ui_text_fit(c, ui_rect(view.x + U(s, 12), view.y, view.w - U(s, 24), U(s, 26)),
                UI_TYPE_CAPTION, t->text_primary, "Shorten long filenames to show missing files.", 0);
    view.y += U(s, 28); view.h -= U(s, 28);
    rowh = U(s, 45); gap = U(s, 3);
    if (!Lst->n_rows) empty_text = "These filenames are too long. Shorten them in their folder, then reopen this list.";
  }
  uint32_t wid = ui_id("fe.list.wheel");
  ui_hit(c, view, 0, wid, UI_HIT_BLOCK | UI_HIT_WHEEL);
  float wheel = ui_wheel(c, wid);
  if (wheel != 0) push_act(acts, max, n, FE_A_SCROLL, wheel > 0 ? -1 : 1);
  if (Lst->n_rows == 0) {
    UiFont bf = ui_font(c, UI_TYPE_BODY);
    char wl[4][160];
    int nl = 0;
    wrap_text(c, UI_TYPE_BODY, empty_text, view.w - U(s, 80), wl, 4, &nl);
    float y = view.y + view.h / 2 - nl * bf.line / 2;
    for (int i = 0; i < nl; i++) {
      ui_text_in(c, ui_rect(view.x, y, view.w, bf.line), UI_TYPE_BODY, t->text_secondary, wl[i], 0.5f);
      y += bf.line;
    }
    return;
  }
  for (int i = 0; i < Lst->n_rows; i++) {
    UiRect rr = ui_rect(view.x, view.y + i * (rowh + gap), view.w, rowh);
    if (!Lst->row[i][0]) continue;
    uint32_t id = ui_idi(ui_id("fe.list.row"), i);
    float rad = U(s, 14);
    ui_hit(c, rr, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
    float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
    int sel = i == Lst->sel;
    if (sel) {
      uid_rrect(ui_drawer(c), ui_inset_all(rr, -U(s, 2)), rad + U(s, 2), t->accent);
      uid_rrect(ui_drawer(c), rr, rad, t->glass_fill_strong);
    } else if (hov > 0.01f) {
      uid_rrect(ui_drawer(c), rr, rad, uid_alpha(t->surface_inset, hov));
    }
    ui_text_fit(c, ui_rect(rr.x + U(s, 18), rr.y, rr.w - U(s, 36), rr.h), UI_TYPE_BODY, t->text_primary, Lst->row[i], 0);
    g_layout.row[i] = rr;
    if (ui_clicked(c, id)) push_act(acts, max, n, FE_A_ROW, i);
  }
}

static void stat_chip(Scene *s, UiRect r, const char *value, const char *label) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  uid_rrect(ui_drawer(c), r, U(s, 18), uid_alpha(t->surface_inset, 0.9f));
  uid_rrect_stroke(ui_drawer(c), r, U(s, 18), U(s, 1), t->surface_inset_border);
  UiFont nf = ui_font(c, UI_TYPE_HEADING), cf = ui_font(c, UI_TYPE_CAPTION);
  float y = r.y + (r.h - nf.line - cf.line) / 2;
  ui_text_fit(c, ui_rect(r.x + U(s, 10), y, r.w - U(s, 20), nf.line), UI_TYPE_HEADING, t->text_primary, value, 0.5f);
  ui_text_in(c, ui_rect(r.x, y + nf.line, r.w, cf.line), UI_TYPE_CAPTION, t->text_secondary, label, 0.5f);
}

static int draw_load(Scene *s, const FeSnap *snap, SDL_Texture *const *art, FeAction *acts, int max) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  const FeLoad *L = &snap->load;
  int n = 0;
  UiRect back;
  if (head_capsule(s, L->championship ? "Championship" : snap->in_game ? "Menu" : "Continue", L->list.title[0] ? L->list.title : "Load a saved game", &back))
    push_act(acts, max, &n, FE_A_BACK, 0);
  g_layout.back = back;
  draw_list_panel(s, &L->list, L->championship ? "championship courses" : "saved games",
                  L->championship ? "No championship courses yet. In a running game, open the menu and choose Save course for championship."
                                  : "No saved games yet. Start a new game and save it from the menu.",
                  acts, max, &n);
  // the details of the highlighted save
  UiRect p = R(s, 1060, 130, 820, 910);
  ui_panel(c, p, U(s, t->radius_panel), UI_PANEL_READABLE);
  float pad = U(s, 28);
  UiRect in = ui_inset_all(p, pad);
  if (!L->has_info) {
    UiFont bf = ui_font(c, UI_TYPE_BODY);
    ui_text_in(c, ui_rect(in.x, in.y + in.h / 2 - bf.line / 2, in.w, bf.line), UI_TYPE_BODY, t->text_secondary,
               L->championship ? "Pick a course to see its holes." : "Pick a saved game to see its holes.", 0.5f);
  } else {
    SDL_Texture *emb = art[FE_ART_COURSE];
    float es = U(s, 112);
    UiRect er = ui_rect(in.x, in.y, es, es);
    uid_rrect(ui_drawer(c), er, es / 2, t->accent_soft);
    if (emb) {
      int tw = 1, th = 1;
      SDL_QueryTexture(emb, NULL, NULL, &tw, &th);
      float k = (tw > th ? es / tw : es / th) * 0.9f;
      UiRect ir = ui_rect(er.x + (es - tw * k) / 2, er.y + (es - th * k) / 2, tw * k, th * k);
      uid_image(ui_drawer(c), emb, NULL, ir, 0, (UiColor){255, 255, 255, 255});
    }
    float tx = er.x + es + U(s, 22);
    UiFont hf = ui_font(c, UI_TYPE_TITLE), cf = ui_font(c, UI_TYPE_CAPTION), bf = ui_font(c, UI_TYPE_BODY);
    float th_ = hf.line + bf.line * 2;
    float ty = er.y + (es - th_) / 2;
    ui_text_fit(c, ui_rect(tx, ty, in.x + in.w - tx, hf.line), UI_TYPE_TITLE, t->text_primary, L->line1, 0);
    ui_text_fit(c, ui_rect(tx, ty + hf.line, in.x + in.w - tx, bf.line), UI_TYPE_BODY, t->text_secondary, L->line2, 0);
    ui_text_fit(c, ui_rect(tx, ty + hf.line + bf.line, in.x + in.w - tx, bf.line), UI_TYPE_BODY, t->text_secondary, L->line3, 0);
    (void)cf;
    // stats
    float cy = er.y + es + U(s, 22);
    float cw = (in.w - 3 * U(s, 12)) / 4, chh = U(s, 76);
    char cash[40];
    snprintf(cash, sizeof cash, "$%s", L->cash);
    const char *vals[4] = {L->holes, L->par, L->yards, cash};
    const char *labs[4] = {"Holes", "Par", "Yards", "Cash"};
    for (int i = 0; i < 4; i++) stat_chip(s, ui_rect(in.x + i * (cw + U(s, 12)), cy, cw, chh), vals[i], labs[i]);
    // ratings
    float ry = cy + chh + U(s, 14);
    float rw = (in.w - U(s, 12)) / 2, rh = U(s, 46);
    for (int i = 0; i < 4; i++) {
      UiRect rr = ui_rect(in.x + (i % 2) * (rw + U(s, 12)), ry + (i / 2) * (rh + U(s, 8)), rw, rh);
      uid_rrect(ui_drawer(c), rr, rr.h / 2, t->accent_soft);
      ui_text_in(c, ui_rect(rr.x + U(s, 18), rr.y, rr.w, rr.h), UI_TYPE_LABEL, t->text_secondary, L->stat_label[i], 0);
      ui_text_in(c, ui_rect(rr.x, rr.y, rr.w - U(s, 18), rr.h), UI_TYPE_NUMBER, t->text_primary, L->stat_value[i], 1);
    }
    // the holes, two columns
    float hy = ry + 2 * (rh + U(s, 8)) + U(s, 10);
    float colw = (in.w - U(s, 24)) / 2, lh = U(s, 25);
    for (int col = 0; col < 2; col++) {
      float x = in.x + col * (colw + U(s, 24));
      UiRect hh = ui_rect(x, hy, colw, lh);
      uid_rrect(ui_drawer(c), hh, hh.h / 2, uid_alpha(t->surface_inset, 0.9f));
      ui_text_in(c, ui_rect(x + U(s, 14), hy, colw, lh), UI_TYPE_MICRO, t->text_secondary, "HOLE", 0);
      ui_text_in(c, ui_rect(x + colw * 0.40f, hy, colw, lh), UI_TYPE_MICRO, t->text_secondary, "PAR", 0);
      ui_text_in(c, ui_rect(x, hy, colw - U(s, 14), lh), UI_TYPE_MICRO, t->text_secondary, "YARDS", 1);
      for (int k = 0; k < 9; k++) {
        int idx = col * 9 + k;
        if (idx >= L->n_holes) break;
        float y = hy + lh + U(s, 4) + k * lh;
        ui_text_in(c, ui_rect(x + U(s, 14), y, colw, lh), UI_TYPE_CAPTION, t->text_primary, L->hole_no[idx], 0);
        ui_text_in(c, ui_rect(x + colw * 0.40f, y, colw, lh), UI_TYPE_CAPTION, t->text_primary, L->hole_par[idx], 0);
        ui_text_in(c, ui_rect(x, y, colw - U(s, 14), lh), UI_TYPE_CAPTION, t->text_primary, L->hole_yards[idx], 1);
        if (k < 8) uid_rrect(ui_drawer(c), ui_rect(x + U(s, 10), y + lh - U(s, 0.5f), colw - U(s, 20), U(s, 1)), 0, t->divider);
      }
    }
  }
  // the buttons
  float bh = U(s, 48);
  UiRect ok = ui_rect(in.x + in.w - U(s, 210), p.y + p.h - U(s, 28) - bh, U(s, 210), bh);
  UiRect del = ui_rect(ok.x - U(s, 14) - U(s, 150), ok.y, U(s, 150), bh);
  g_layout.ok = ok;
  g_layout.del = del;
  if (ui_button(c, ui_id("fe.list.ok"), ok, L->championship ? "Choose course" : "Load game", UI_ICON_CHEV, UI_BTN_PRIMARY, L->has_info ? 0 : UI_DISABLED))
    push_act(acts, max, &n, FE_A_OK, 0);
  if (!L->championship &&
      ui_button(c, ui_id("fe.list.del"), del, "Delete", UI_ICON_TRASH, UI_BTN_PILL, L->has_info ? 0 : UI_DISABLED))
    push_act(acts, max, &n, FE_A_DELETE, 0);
  return n;
}

// ------------------------------------------------------------------ the pro pick

static int draw_pro(Scene *s, const FeSnap *snap, SDL_Texture *const *art, FeAction *acts, int max) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  const FePro *P = &snap->pro;
  int n = 0;
  UiRect back;
  if (head_capsule(s, "Championship", P->list.title[0] ? P->list.title : "Pick a pro", &back)) push_act(acts, max, &n, FE_A_BACK, 0);
  g_layout.back = back;
  draw_list_panel(s, &P->list, "pro golfers", "No pro golfers found in this theme.", acts, max, &n);
  UiRect p = R(s, 1060, 130, 820, 910);
  ui_panel(c, p, U(s, t->radius_panel), UI_PANEL_READABLE);
  UiRect in = ui_inset_all(p, U(s, 28));
  if (!P->has_info) {
    UiFont bf = ui_font(c, UI_TYPE_BODY);
    ui_text_in(c, ui_rect(in.x, in.y + in.h / 2 - bf.line / 2, in.w, bf.line), UI_TYPE_BODY, t->text_secondary, "Pick a pro golfer to see their skills.", 0.5f);
  } else {
    float es = U(s, 170);
    UiRect er = ui_rect(in.x, in.y, es, es);
    uid_rrect(ui_drawer(c), er, es / 2, t->accent_soft);
    SDL_Texture *pt = art[FE_ART_PORTRAIT];
    if (pt) {
      int tw = 1, th = 1;
      SDL_QueryTexture(pt, NULL, NULL, &tw, &th);
      float k = (tw > th ? es / tw : es / th) * 0.82f;
      uid_image(ui_drawer(c), pt, NULL, ui_rect(er.x + (es - tw * k) / 2, er.y + (es - th * k) / 2, tw * k, th * k), 0, (UiColor){255, 255, 255, 255});
    }
    float tx = er.x + es + U(s, 26);
    UiFont hf = ui_font(c, UI_TYPE_TITLE), cf = ui_font(c, UI_TYPE_CAPTION), qf = ui_font(c, UI_TYPE_QUOTE);
    ui_text_in(c, ui_rect(tx, er.y + U(s, 14), in.x + in.w - tx, hf.line), UI_TYPE_TITLE, t->text_primary, P->name[0] ? P->name : "Pro golfer", 0);
    ui_text_in(c, ui_rect(tx, er.y + U(s, 14) + hf.line, in.x + in.w - tx, cf.line), UI_TYPE_CAPTION, t->text_secondary, "Skills that carry over to the championship", 0);
    // the signature saying
    UiRect qb = ui_rect(tx, er.y + U(s, 14) + hf.line + cf.line + U(s, 16), in.x + in.w - tx, es - (hf.line + cf.line + U(s, 30)));
    uid_rrect(ui_drawer(c), qb, U(s, 18), uid_alpha(t->surface_inset, 0.9f));
    char wl[4][160];
    int nl = 0;
    wrap_text(c, UI_TYPE_QUOTE, P->saying, qb.w - U(s, 36), wl, 3, &nl);
    float qy = qb.y + (qb.h - nl * qf.line) / 2;
    for (int i = 0; i < nl; i++) {
      ui_text_in(c, ui_rect(qb.x + U(s, 18), qy, qb.w, qf.line), UI_TYPE_QUOTE, t->text_primary, wl[i], 0);
      qy += qf.line;
    }
    // the skills, two columns
    float sy = er.y + es + U(s, 28);
    ui_text_in(c, ui_rect(in.x, sy, in.w, cf.line), UI_TYPE_LABEL, t->text_secondary, "SKILLS", 0);
    sy += U(s, 30);
    float cw = (in.w - U(s, 16)) / 2, rh = U(s, 54);
    for (int i = 0; i < P->n_skills; i++) {
      UiRect rr = ui_rect(in.x + (i % 2) * (cw + U(s, 16)), sy + (i / 2) * (rh + U(s, 10)), cw, rh);
      uid_rrect(ui_drawer(c), rr, rr.h / 2, uid_alpha(t->surface_inset, 0.9f));
      UiRect vp = ui_rect(rr.x + U(s, 8), rr.y + U(s, 8), U(s, 78), rr.h - U(s, 16));
      uid_rrect(ui_drawer(c), vp, vp.h / 2, t->accent_soft);
      ui_text_in(c, vp, UI_TYPE_NUMBER, t->accent_soft_text, P->skill_value[i], 0.5f);
      ui_text_fit(c, ui_rect(vp.x + vp.w + U(s, 16), rr.y, rr.w - vp.w - U(s, 40), rr.h), UI_TYPE_LABEL, t->text_primary, P->skill[i], 0);
    }
  }
  float bh = U(s, 48);
  UiRect ok = ui_rect(in.x + in.w - U(s, 210), p.y + p.h - U(s, 28) - bh, U(s, 210), bh);
  g_layout.ok = ok;
  if (ui_button(c, ui_id("fe.list.ok"), ok, "Pick this pro", UI_ICON_CHEV, UI_BTN_PRIMARY, P->has_info ? 0 : UI_DISABLED))
    push_act(acts, max, &n, FE_A_OK, 0);
  UiRect del = ui_rect(ok.x - U(s, 14) - U(s, 150), ok.y, U(s, 150), bh);
  g_layout.del = del;
  if (ui_button(c, ui_id("fe.list.del"), del, "Delete", UI_ICON_TRASH, UI_BTN_PILL, P->has_info ? 0 : UI_DISABLED))
    push_act(acts, max, &n, FE_A_DELETE, 0);
  return n;
}

// ------------------------------------------------------------------ theme packs

static int draw_theme(Scene *s, const FeSnap *snap, FeAction *acts, int max) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  const FeTheme *T = &snap->theme;
  int n = 0;
  UiRect back;
  if (head_capsule(s, "Themes", "Select a theme pack", &back)) push_act(acts, max, &n, FE_A_BACK, 0);
  g_layout.back = back;
  const float PW = 1240, NAMEW = 340;
  float rows = T->n > 0 ? (float)T->n : 1;
  float rowh = 76, head_h = 64, gap = 6;
  float ph = 36 + head_h + rows * (rowh + gap) + 36 + 48 + 26;
  if (ph < 420) ph = 420;
  if (ph > 820) ph = 820;
  UiRect p = R(s, (1920 - PW) / 2, (1080 - ph) / 2 + 20, PW, ph);
  ui_panel(c, p, U(s, t->radius_panel), UI_PANEL_READABLE);
  float x0 = p.x + U(s, 36), w = p.w - U(s, 72);
  float colw = (w - U(s, NAMEW)) / 5;
  float y = p.y + U(s, 36);
  UiFont cf = ui_font(c, UI_TYPE_LABEL);
  for (int k = 0; k < 5; k++)
    ui_text_in(c, ui_rect(x0 + U(s, NAMEW) + k * colw, y, colw, U(s, head_h)), UI_TYPE_LABEL, t->text_secondary, T->column[k], 0.5f);
  (void)cf;
  ui_text_in(c, ui_rect(x0 + U(s, 18), y, U(s, NAMEW), U(s, head_h)), UI_TYPE_LABEL, t->text_secondary, "Theme pack", 0);
  y += U(s, head_h);
  UiRect view = ui_rect(x0, y, w, U(s, ph - 36 - head_h - 36 - 48 - 26));
  g_layout.theme_view = view;
  y += ui_vscroll_begin(c, ui_id("fe.theme.scroll"), view, U(s, rows * (rowh + gap)), U(s, rowh + gap));
  for (int i = 0; i < T->n; i++) {
    UiRect rr = ui_rect(x0, y, w, U(s, rowh));
    uint32_t id = ui_idi(ui_id("fe.theme.row"), i);
    float rad = U(s, 18);
    ui_hit(c, rr, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
    float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
    int sel = i == T->sel;
    if (sel) {
      uid_rrect(ui_drawer(c), ui_inset_all(rr, -U(s, 2)), rad + U(s, 2), t->accent);
      uid_rrect(ui_drawer(c), rr, rad, t->glass_fill_strong);
    } else if (hov > 0.01f) {
      uid_rrect(ui_drawer(c), rr, rad, uid_alpha(t->surface_inset, hov));
    }
    ui_text_fit(c, ui_rect(rr.x + U(s, 18), rr.y, U(s, NAMEW) - U(s, 30), rr.h), UI_TYPE_HEADING, t->text_primary, T->name[i], 0);
    if (sel) {
      UiRect tag = ui_rect(rr.x + U(s, 18), rr.y + rr.h - U(s, 22), U(s, 84), U(s, 0));
      (void)tag;
    }
    for (int k = 0; k < 5; k++) {
      if (!(T->mask[i] & (1u << k))) continue;
      float d = U(s, 34);
      UiRect br = ui_rect(rr.x + U(s, NAMEW) + k * colw + (colw - d) / 2, rr.y + (rr.h - d) / 2, d, d);
      uid_rrect(ui_drawer(c), br, d / 2, t->accent);
      ui_icon(c, UI_ICON_CHECK, br, U(s, 20), t->text_on_accent);
    }
    g_layout.row[i] = rr;
    if (ui_clicked(c, id)) push_act(acts, max, &n, FE_A_ROW, i);
    y += U(s, rowh + gap);
  }
  ui_vscroll_end(c);
  UiRect ok = ui_rect(p.x + p.w - U(s, 36) - U(s, 230), p.y + p.h - U(s, 26) - U(s, 48), U(s, 230), U(s, 48));
  g_layout.ok = ok;
  if (ui_button(c, ui_id("fe.theme.ok"), ok, "Use this theme", UI_ICON_CHEV, UI_BTN_PRIMARY, T->sel >= 0 ? 0 : UI_DISABLED))
    push_act(acts, max, &n, FE_A_OK, 0);
  ui_text_in(c, ui_rect(x0, ok.y, w - U(s, 250), ok.h), UI_TYPE_CAPTION, t->text_secondary,
             "A theme pack adds stories, characters, celebrities, pro golfers or courses to a new game.", 0);
  return n;
}

// ------------------------------------------------------------------ loading

// The original left its screen for a while (a course is being built or loaded): the screen stays,
// dimmed and blocked, with this pill on top instead of the original's own loading picture.
void ui_frontend_loading(UiCtx *c, const char *text) {
  Scene s;
  scene_init(&s, c);
  const UiTokens *t = s.t;
  UiRect screen = ui_screen(c);
  uid_rrect(ui_drawer(c), screen, 0, uid_alpha(t->scrim, 0.5f));
  ui_hit(c, screen, 0, 0, UI_HIT_BLOCK | UI_HIT_MODAL);
  float tw = ui_text_width(c, UI_TYPE_TITLE, text);
  float dots_w = U(&s, 3 * 8 + 2 * 8);
  float w = (tw + U(&s, 10) + dots_w) / s.u + 80;
  UiRect r = R(&s, (1920 - w) / 2, 480, w, 84);
  ui_panel(c, r, r.h / 2, UI_PANEL_READABLE | UI_PANEL_NOBLOCK);
  float x = r.x + U(&s, 40);
  ui_text_in(c, ui_rect(x, r.y, tw + 2, r.h), UI_TYPE_TITLE, t->text_primary, text, 0);
  x += tw + U(&s, 10);
  int on = (int)(ui_now(c) / 300.0) % 4;   // the dots fill one after another
  for (int i = 0; i < 3; i++) {
    float d = U(&s, 8);
    UiColor col = t->accent;
    col.a = (uint8_t)(i < on ? 255 : 70);
    uid_rrect(ui_drawer(c), ui_rect(x + i * U(&s, 16), r.y + (r.h - d) / 2 + U(&s, 6), d, d), d / 2, col);
  }
}

// ------------------------------------------------------------------ loading (task-69)

static int draw_loading(Scene *s, const FeSnap *snap, SDL_Texture *const *art) {
  UiCtx *c = s->c;
  const UiTokens *t = s->t;
  const FeLoading *L = &snap->loading;
  SDL_Texture *pic = art[FE_ART_LOADING];
  const float PW = 900, PAD = 44, pic_w = 620;
  float pic_h = 0;
  if (pic) {
    int tw = 1, th = 1;
    SDL_QueryTexture(pic, NULL, NULL, &tw, &th);
    pic_h = pic_w * th / (float)tw;
  }
  char lines[4][160];
  int nl = 0;
  UiFont qf = ui_font(c, UI_TYPE_BODY);
  if (L->quote[0]) wrap_text(c, UI_TYPE_BODY, L->quote, U(s, PW - 2 * PAD), lines, 4, &nl);
  float qh = nl ? nl * qf.line / s->u + 20 : 0;
  float gap = pic ? 24 : 0;
  float ph = 40 + 40 + 20 + pic_h + gap + qh + 54 + 40;
  float py = (1080 - ph) / 2;
  UiRect p = R(s, (1920 - PW) / 2, py, PW, ph);
  ui_panel(c, p, U(s, t->radius_panel), UI_PANEL_READABLE);
  float y = py + 36;
  ui_text_in(c, R(s, (1920 - PW) / 2, y, PW, 40), UI_TYPE_TITLE, t->text_primary, L->title[0] ? L->title : "Getting the course ready", 0.5f);
  y += 40 + 20;
  if (pic) uid_image(ui_drawer(c), pic, NULL, R(s, (1920 - pic_w) / 2, y, pic_w, pic_h), 0, (UiColor){255, 255, 255, 255});
  y += pic_h + gap;
  for (int i = 0; i < nl; i++, y += qf.line / s->u)
    ui_text_in(c, R(s, (1920 - PW) / 2 + PAD, y, PW - 2 * PAD, qf.line / s->u), UI_TYPE_BODY, t->text_secondary, lines[i], 0.5f);
  if (nl) y += 20;
  UiRect bar = R(s, (1920 - PW) / 2 + PAD + 60, y + 10, PW - 2 * PAD - 120, 14);
  if (L->pct >= 0) {
    uid_rrect(ui_drawer(c), bar, bar.h / 2, t->meter_track);   // always the green fill: a load never "runs low"
    UiRect f = bar;
    f.w = fmaxf(bar.h, bar.w * (L->pct > 100 ? 1.0f : L->pct / 100.0f));
    uid_rrect_grad(ui_drawer(c), f, bar.h / 2, t->meter_grad);
  } else {   // no bar from golf: the dots of the Loading pill
    uid_rrect(ui_drawer(c), bar, bar.h / 2, t->meter_track);
    int on = (int)(ui_now(c) / 300.0) % 4;
    for (int i = 0; i < 3; i++) {
      UiColor col = t->accent;
      col.a = (uint8_t)(i < on ? 255 : 70);
      float d = bar.h;
      uid_rrect(ui_drawer(c), ui_rect(bar.x + bar.w / 2 + (i - 1) * 2 * d, bar.y, d, d), d / 2, col);
    }
  }
  char pct[32];
  if (L->pct >= 0) snprintf(pct, sizeof pct, "Loading %d%%", (int)L->pct);
  else snprintf(pct, sizeof pct, L->title[0] ? "One moment please" : "Loading");
  ui_text_in(c, R(s, (1920 - PW) / 2, y + 34, PW, 24), UI_TYPE_CAPTION, t->text_secondary, pct, 0.5f);
  return 0;
}

// ------------------------------------------------------------------ entry

int ui_frontend_draw(UiCtx *c, const FeSnap *snap, SDL_Texture *const *art, FeAction *acts, int max) {
  Scene s;
  scene_init(&s, c);
  memset(&g_layout, 0, sizeof g_layout);
  g_layout.world_selected = -1;
  switch (snap->screen) {
    case FE_TITLE: return draw_title(&s, snap, art, acts, max);
    case FE_DIFFICULTY: return draw_difficulty(&s, snap, art, acts, max);
    case FE_WORLD: return draw_world(&s, snap, art, acts, max);
    case FE_LOAD: return draw_load(&s, snap, art, acts, max);
    case FE_THEME: return draw_theme(&s, snap, acts, max);
    case FE_PRO: return draw_pro(&s, snap, art, acts, max);
    case FE_LOADING: return draw_loading(&s, snap, art);
    default: return 0;
  }
}
