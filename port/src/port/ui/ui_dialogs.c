// Frosted Mint dialogs (simgolf mod, task-27), see ui_dialogs.h.
#include "port/ui/ui_dialogs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WRAP_MAX = 14 };

// golf's text uses 0xA7 for the dollar sign and pads the choices with blanks
static void clean(char *dst, int cap, const char *src) {
  while (*src == ' ') src++;
  int n = 0;
  for (; *src && n < cap - 1; src++) dst[n++] = (unsigned char)*src == 0xA7 ? '$' : *src;
  while (n > 0 && dst[n - 1] == ' ') n--;
  dst[n] = 0;
}

// Greedy word wrap of s to width w; returns the number of lines (at most WRAP_MAX).
static int wrap(UiCtx *c, int style, const char *s, float w, char out[WRAP_MAX][DLG_TEXT]) {
  int n = 0;
  char cur[DLG_TEXT] = "";
  const char *p = s;
  while (*p && n < WRAP_MAX) {
    char word[DLG_TEXT];
    int k = 0;
    while (*p == ' ') p++;
    while (*p && *p != ' ' && k < DLG_TEXT - 1) word[k++] = *p++;
    word[k] = 0;
    if (!k) break;
    char test[DLG_TEXT * 2];
    snprintf(test, sizeof test, "%s%s%s", cur, cur[0] ? " " : "", word);
    if (cur[0] && ui_text_width(c, style, test) > w) {
      snprintf(out[n++], DLG_TEXT, "%s", cur);
      snprintf(cur, sizeof cur, "%s", word);
    } else {
      snprintf(cur, sizeof cur, "%.*s", DLG_TEXT - 1, test);
    }
  }
  if (cur[0] && n < WRAP_MAX) snprintf(out[n++], DLG_TEXT, "%s", cur);
  return n;
}

// The heading when the original's text has none: what the list is for.
static const char *fallback_title(const DlgSnap *s) {
  for (int i = 0; i < s->n_lines; i++) {
    if (s->line[i].item < 0) continue;
    char t[DLG_TEXT];
    clean(t, sizeof t, s->line[i].text);
    if (strstr(t, "Save the Current Game")) return "Options";
    if (strstr(t, "Repeat Last Message")) return "Information";
    break;
  }
  return s->kind == DLG_CHECKS ? "Preferences" : "Choose";
}

static float row_h(UiCtx *c, float avail, int n, float want) {
  float gap = ui_s(c, 6);
  float h = (avail - gap * (n - 1)) / (n > 0 ? n : 1);
  if (h > want) h = want;
  float lo = ui_s(c, 34);
  return h < lo ? lo : h;
}

static int draw_list(UiCtx *c, const DlgSnap *s, DlgAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  int n_acts = 0;
  UiRect scr = ui_screen(c);
  static unsigned log_frames;
  int log_layout = getenv("SIMGOLF_DIALOGS_LOG") && (++log_frames % 60) == 0;
  float u = ui_s(c, 1);
  // texts above the list, the choices
  char texts[DLG_LINES][DLG_TEXT];
  int n_text = 0, n_item = 0;
  char items[DLG_LINES][DLG_TEXT];
  int item_no[DLG_LINES];
  for (int i = 0; i < s->n_lines; i++) {
    char tmp[DLG_TEXT];
    clean(tmp, sizeof tmp, s->line[i].text);
    if (s->line[i].item < 0) {
      if (tmp[0] && n_text < DLG_LINES) snprintf(texts[n_text++], DLG_TEXT, "%s", tmp);
    } else if (n_item < DLG_LINES && tmp[0]) {   // golf's preferences list ends with a blank choice
      snprintf(items[n_item], DLG_TEXT, "%s", tmp);
      item_no[n_item++] = s->line[i].item;
    }
  }
  // The pro export's overwrite prompt names an internal folder. Show the
  // complete filename, keeping the game's original path and choices intact.
  if (n_text && !strncmp(texts[0], "Themes\\Championship\\", sizeof("Themes\\Championship\\") - 1)) {
    const char *name = strrchr(texts[0], '\\');
    if (name && name[1]) memmove(texts[0], name + 1, strlen(name + 1) + 1);
  }
  const char *title = NULL;
  int first_body = 0;
  if (n_text && ui_text_width(c, UI_TYPE_TITLE, texts[0]) < ui_s(c, 560)) {
    size_t L = strlen(texts[0]);   // "Preferences..." -> "Preferences"
    while (L > 0 && (texts[0][L - 1] == '.' || texts[0][L - 1] == ':' || texts[0][L - 1] == ' ')) texts[0][--L] = 0;
    title = texts[0];
    first_body = 1;
  } else {
    title = fallback_title(s);
  }
  // width: the widest choice or the title, within the dialog limits
  float wide = ui_text_width(c, UI_TYPE_TITLE, title);
  for (int i = 0; i < n_item; i++) {
    float w = ui_text_width(c, UI_TYPE_HEADING, items[i]) + ui_s(c, s->kind == DLG_CHECKS ? 120 : 64);
    if (w > wide) wide = w;
  }
  float pad = ui_s(c, t->pad_dialog);
  float w = wide + 2 * pad;
  if (w < ui_s(c, 440)) w = ui_s(c, 440);
  if (w > ui_s(c, 760)) w = ui_s(c, 760);
  if (w > scr.w - ui_s(c, 40)) w = scr.w - ui_s(c, 40);
  float inner = w - 2 * pad;
  // body paragraphs, wrapped
  static char body[WRAP_MAX * 4][DLG_TEXT];
  int n_body = 0;
  for (int i = first_body; i < n_text && n_body < WRAP_MAX * 4 - WRAP_MAX; i++) {
    char wr[WRAP_MAX][DLG_TEXT];
    int k = wrap(c, UI_TYPE_BODY, texts[i], inner, wr);
    for (int j = 0; j < k; j++) snprintf(body[n_body++], DLG_TEXT, "%s", wr[j]);
  }
  UiFont tf = ui_font(c, UI_TYPE_TITLE), bf = ui_font(c, UI_TYPE_BODY);
  float gap = ui_s(c, 6);
  float foot_h = ui_s(c, 44) + ui_s(c, 16);
  float head_h = tf.line + ui_s(c, 12) + n_body * bf.line + (n_body ? ui_s(c, 12) : 0);
  float avail = scr.h - ui_s(c, 80) - 2 * pad - head_h - foot_h;
  float rh = row_h(c, avail, n_item, ui_s(c, 50));
  float list_h = n_item * rh + (n_item > 1 ? (n_item - 1) * gap : 0);
  float h = 2 * pad + head_h + list_h + foot_h;
  UiRect in = ui_dialog_begin(c, ui_id("dlg"), w, h, title);
  float y = in.y;
  for (int i = 0; i < n_body; i++, y += bf.line)
    ui_text_in(c, ui_rect(in.x, y, in.w, bf.line), UI_TYPE_BODY, t->text_secondary, body[i], 0);
  if (n_body) y += ui_s(c, 12);
  int checks = s->kind == DLG_CHECKS;
  for (int i = 0; i < n_item; i++) {
    UiRect r = ui_rect(in.x, y, in.w, rh);
    uint32_t id = ui_idi(ui_id("dlg.item"), i);
    int no = item_no[i];
    int dis = (s->disabled >> no) & 1;
    if (log_layout)
      fprintf(stderr, "dialogs: item %d at %.0f,%.0f size %.0fx%.0f target %.0fx%.0f [%s]\n",
              no, r.x, r.y, r.w, r.h, scr.w, scr.h, items[i]);
    ui_hit(c, r, ui_s(c, t->radius_tile), id, dis ? UI_HIT_BLOCK : UI_HIT_BLOCK | UI_HIT_INTERACT);
    UiDraw *d = ui_drawer(c);
    float rad = ui_s(c, t->radius_tile);
    float hov = ui_anim(c, id, !dis && ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
    int keysel = !checks && s->sel == no;
    if (keysel) {
      uid_rrect(d, ui_inset_all(r, -ui_s(c, 2)), rad + ui_s(c, 2), t->accent);
      uid_rrect(d, r, rad, t->glass_fill_strong);
    } else if (hov > 0) {
      uid_rrect(d, r, rad, uid_alpha(t->surface_inset, hov));
    } else {
      uid_rrect(d, r, rad, uid_alpha(t->surface_inset, 0.55f));
    }
    UiColor tc = dis ? t->text_tertiary : t->text_primary;
    ui_text_fit(c, ui_rect(r.x + ui_s(c, 18), r.y, r.w - ui_s(c, checks ? 92 : 36), r.h), UI_TYPE_HEADING, tc, items[i], 0);
    if (checks) {
      int on = (s->mask >> no) & 1;
      float sw = ui_s(c, t->toggle_w), sh = ui_s(c, t->toggle_h);
      UiRect sr = ui_rect(r.x + r.w - sw - ui_s(c, 16), r.y + (r.h - sh) / 2, sw, sh);
      float k = ui_anim(c, id ^ 0x70661eu, on ? 1.0f : 0.0f, t->motion_hover);
      uid_rrect(d, sr, sh / 2, uid_mix(t->meter_track, t->accent, k));
      if (k < 0.5f) uid_rrect_stroke(d, sr, sh / 2, ui_s(c, 1), t->divider);
      float kd = sh - ui_s(c, 6);
      uid_rrect(d, ui_rect(sr.x + ui_s(c, 3) + (sw - kd - ui_s(c, 6)) * k, sr.y + ui_s(c, 3), kd, kd), kd / 2,
                (UiColor){255, 255, 255, 255});
    } else {
      ui_icon(c, UI_ICON_CHEV, ui_rect(r.x + r.w - ui_s(c, 36), r.y, ui_s(c, 24), r.h), ui_s(c, 16), uid_alpha(tc, 0.6f));
    }
    if (!dis && ui_clicked(c, id) && n_acts < max) acts[n_acts++] = (DlgAction){DLG_A_ITEM, no};
    y += rh + gap;
  }
  // footer: Done for the check list, Close for a menu
  float by = in.y + in.h - ui_s(c, 44);
  const char *lab = checks ? "Done" : "Close";
  float bw = ui_text_width(c, UI_TYPE_LABEL, lab) + ui_s(c, 64);
  UiRect br = ui_rect(in.x + in.w - bw, by, bw, ui_s(c, 44));
  if (ui_button(c, ui_id("dlg.close"), br, lab, UI_ICON_NONE, checks ? UI_BTN_PRIMARY : UI_BTN_PILL, 0) && n_acts < max)
    acts[n_acts++] = (DlgAction){checks ? DLG_A_OK : DLG_A_CANCEL, 0};
  ui_text_in(c, ui_rect(in.x, by, in.w - bw - ui_s(c, 12), ui_s(c, 44)), UI_TYPE_CAPTION, t->text_tertiary, "Esc closes", 1);
  (void)u;
  return n_acts;
}

static int draw_entry(UiCtx *c, const DlgSnap *s, DlgAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  int n_acts = 0;
  float pad = ui_s(c, t->pad_dialog);
  float w = ui_s(c, 560);
  UiFont tf = ui_font(c, UI_TYPE_TITLE);
  float fh = ui_s(c, 48), bh = ui_s(c, 44);
  float h = 2 * pad + tf.line + ui_s(c, 12) + fh + ui_s(c, 20) + bh;
  char title[DLG_TEXT];
  clean(title, sizeof title, s->prompt);
  size_t L = strlen(title);
  while (L && (title[L - 1] == ':' || title[L - 1] == '.' || title[L - 1] == ' ')) title[--L] = 0;
  UiRect in = ui_dialog_begin(c, ui_id("dlg.entry"), w, h, title[0] ? title : "Name");
  UiDraw *d = ui_drawer(c);
  UiRect f = ui_rect(in.x, in.y, in.w, fh);
  float rad = ui_s(c, t->radius_control);
  uid_rrect(d, f, rad, t->surface_inset);
  uid_rrect_stroke(d, f, rad, ui_s(c, 2), t->accent);
  char text[160];
  snprintf(text, sizeof text, "%s", s->field);
  for (char *p = text; *p; p++)
    if ((unsigned char)*p == 0xA7) *p = '$';
  float tx = f.x + ui_s(c, 16);
  UiFont bf = ui_font(c, UI_TYPE_BODY);
  ui_text_in(c, ui_rect(tx, f.y, f.w - ui_s(c, 32), f.h), UI_TYPE_BODY, t->text_primary, text, 0);
  int caret = s->caret < 0 ? 0 : s->caret;
  if (caret > (int)strlen(text)) caret = (int)strlen(text);
  char pre[160];
  snprintf(pre, sizeof pre, "%.*s", caret, text);
  float cx = tx + ui_text_width(c, UI_TYPE_BODY, pre);
  if (((int)(ui_now(c) / 500.0)) % 2 == 0) uid_rrect(d, ui_rect(cx, f.y + (f.h - bf.line) / 2, ui_s(c, 2), bf.line), 1, t->accent);
  float by = in.y + in.h - bh;
  float bw = ui_s(c, 120);
  UiRect ok = ui_rect(in.x + in.w - bw, by, bw, bh);
  UiRect cancel = ui_rect(ok.x - bw - ui_s(c, 12), by, bw, bh);
  if (ui_button(c, ui_id("dlg.ok"), ok, "OK", UI_ICON_NONE, UI_BTN_PRIMARY, 0) && n_acts < max) acts[n_acts++] = (DlgAction){DLG_A_OK, 0};
  if (ui_button(c, ui_id("dlg.cancel"), cancel, "Cancel", UI_ICON_NONE, UI_BTN_PILL, 0) && n_acts < max)
    acts[n_acts++] = (DlgAction){DLG_A_CANCEL, 0};
  ui_text_in(c, ui_rect(in.x, by, cancel.x - in.x - ui_s(c, 12), bh), UI_TYPE_CAPTION, t->text_tertiary, "Enter saves, Esc cancels", 0);
  return n_acts;
}

static int draw_hire(UiCtx *c, const DlgSnap *s, DlgAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  int n_acts = 0;
  float pad = ui_s(c, t->pad_dialog);
  float w = ui_s(c, 640), rh = ui_s(c, 54), gap = ui_s(c, 6);
  UiFont tf = ui_font(c, UI_TYPE_TITLE), lf = ui_font(c, UI_TYPE_LABEL);
  float group_h = lf.line + ui_s(c, 6) + 2 * rh + gap + ui_s(c, 16);
  float foot = ui_s(c, 44) + ui_s(c, 12);
  float h = 2 * pad + tf.line + ui_s(c, 12) + 4 * group_h + foot;
  UiRect scr = ui_screen(c);
  if (h > scr.h - ui_s(c, 40)) h = scr.h - ui_s(c, 40);
  UiRect in = ui_dialog_begin(c, ui_id("dlg.hire"), w, h, "Hire an employee");
  UiDraw *d = ui_drawer(c);
  static const UiIcon icon[8] = {UI_ICON_STAR, UI_ICON_SPARKLE, UI_ICON_FLAG, UI_ICON_EYE, UI_ICON_TREE, UI_ICON_GEAR, UI_ICON_COINS, UI_ICON_SMILE};
  float y = in.y;
  float rad = ui_s(c, t->radius_tile);
  for (int g = 0; g < 4; g++) {
    char head[48];
    snprintf(head, sizeof head, "%s", s->group[g]);
    size_t L = strlen(head);
    while (L && (head[L - 1] == '.' || head[L - 1] == ' ' || head[L - 1] == ':')) head[--L] = 0;
    ui_text_in(c, ui_rect(in.x, y, in.w, lf.line), UI_TYPE_LABEL, t->text_secondary, head, 0);
    y += lf.line + ui_s(c, 6);
    for (int k = 0; k < 2; k++) {
      int i = g * 2 + k;
      UiRect r = ui_rect(in.x, y, in.w, rh);
      uint32_t id = ui_idi(ui_id("dlg.job"), i);
      ui_hit(c, r, rad, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
      float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
      uid_rrect(d, r, rad, uid_alpha(t->surface_inset, 0.55f + 0.45f * hov));
      float av = rh - ui_s(c, 14);
      UiRect ar = ui_rect(r.x + ui_s(c, 10), r.y + (r.h - av) / 2, av, av);
      uid_rrect(d, ar, av / 2, t->accent_soft);
      ui_icon(c, icon[i], ar, av * 0.5f, t->accent_soft_text);
      ui_text_fit(c, ui_rect(ar.x + av + ui_s(c, 14), r.y, r.w - av - ui_s(c, 190), r.h), UI_TYPE_HEADING, t->text_primary, s->job[i], 0);
      char wage[32];
      snprintf(wage, sizeof wage, "$%d a week", s->wage[i]);
      ui_text_in(c, ui_rect(r.x + r.w - ui_s(c, 170), r.y, ui_s(c, 154), r.h), UI_TYPE_NUMBER, t->money, wage, 1);
      if (ui_clicked(c, id) && n_acts < max) acts[n_acts++] = (DlgAction){DLG_A_ITEM, i};
      y += rh + gap;
    }
    y += ui_s(c, 16) - gap;
  }
  float by = in.y + in.h - ui_s(c, 44);
  float bw = ui_text_width(c, UI_TYPE_LABEL, "Close") + ui_s(c, 64);
  if (ui_button(c, ui_id("dlg.close"), ui_rect(in.x + in.w - bw, by, bw, ui_s(c, 44)), "Close", UI_ICON_NONE, UI_BTN_PILL, 0) && n_acts < max)
    acts[n_acts++] = (DlgAction){DLG_A_CANCEL, 0};
  ui_text_in(c, ui_rect(in.x, by, in.w - bw - ui_s(c, 12), ui_s(c, 44)), UI_TYPE_CAPTION, t->text_tertiary, "Esc closes", 1);
  return n_acts;
}

static int draw_hint(UiCtx *c, const DlgSnap *s, SDL_Texture *const *art, DlgAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  int n_acts = 0;
  float pad = ui_s(c, t->pad_dialog);
  float w = ui_s(c, 640);
  int portrait = s->has_art && art && art[DLG_ART_PORTRAIT];
  float pw = portrait ? ui_s(c, 112) : 0;
  float text_w = w - 2 * pad - (portrait ? pw + ui_s(c, 20) : 0);
  char lines[WRAP_MAX * 3][DLG_TEXT];
  int n = 0;
  char para[480];
  snprintf(para, sizeof para, "%s", s->text);
  for (char *p = para; *p; p++)
    if (*p == '^' || *p == 10) *p = ' ';
  char wr[WRAP_MAX][DLG_TEXT];
  int k = wrap(c, UI_TYPE_BODY, para, text_w, wr);
  for (int i = 0; i < k && n < WRAP_MAX * 3; i++) snprintf(lines[n++], DLG_TEXT, "%s", wr[i]);
  UiFont bf = ui_font(c, UI_TYPE_BODY);
  float body_h = n * bf.line;
  float content_h = fmaxf(body_h, portrait ? pw : 0);
  float bh = ui_s(c, 44);
  float h = 2 * pad + content_h + ui_s(c, 20) + bh;
  UiRect in = ui_dialog_begin(c, ui_id("dlg.hint"), w, h, NULL);
  UiDraw *d = ui_drawer(c);
  float tx = in.x;
  if (portrait) {
    UiRect pr = ui_rect(in.x, in.y, pw, pw);
    uid_rrect(d, pr, pw / 2, t->surface_inset);
    int tw = 1, th = 1;   // the game's picture kept to its aspect, as large as fits
    SDL_QueryTexture(art[DLG_ART_PORTRAIT], NULL, NULL, &tw, &th);
    float k = fminf((pw - ui_s(c, 8)) / tw, (pw - ui_s(c, 8)) / th);
    UiRect ir = ui_rect(pr.x + (pw - tw * k) / 2, pr.y + (pw - th * k) / 2, tw * k, th * k);
    uid_image(d, art[DLG_ART_PORTRAIT], NULL, ir, 0, (UiColor){255, 255, 255, 255});
    tx = in.x + pw + ui_s(c, 20);
  }
  float y = in.y + (content_h - body_h) / 2;
  for (int i = 0; i < n; i++, y += bf.line) ui_text_in(c, ui_rect(tx, y, text_w, bf.line), UI_TYPE_BODY, t->text_primary, lines[i], 0);
  float by = in.y + in.h - bh;
  float bw = ui_s(c, 120);
  if (ui_button(c, ui_id("dlg.ok"), ui_rect(in.x + in.w - bw, by, bw, bh), "OK", UI_ICON_NONE, UI_BTN_PRIMARY, 0) && n_acts < max)
    acts[n_acts++] = (DlgAction){DLG_A_OK, 0};
  ui_text_in(c, ui_rect(in.x, by, in.w - bw - ui_s(c, 12), bh), UI_TYPE_CAPTION, t->text_tertiary, "Any key closes", 1);
  return n_acts;
}

int ui_dialogs_draw(UiCtx *c, const DlgSnap *s, SDL_Texture *const *art, DlgAction *acts, int max) {
  switch (s->kind) {
    case DLG_MENU:
    case DLG_CHECKS: return draw_list(c, s, acts, max);
    case DLG_ENTRY: return draw_entry(c, s, acts, max);
    case DLG_HIRE: return draw_hire(c, s, acts, max);
    case DLG_HINT: return draw_hint(c, s, art, acts, max);
    default: return 0;
  }
}
