// Frosted Mint dialogs (simgolf mod, task-27), see ui_dialogs.h.
#include "port/ui/ui_dialogs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/ui/ui_dialog_text.h"

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
  const char *item_labels[DLG_LINES];
  int item_no[DLG_LINES];
  for (int i = 0; i < s->n_lines; i++) {
    char tmp[DLG_TEXT];
    clean(tmp, sizeof tmp, s->line[i].text);
    if (s->line[i].item < 0) {
      if (tmp[0] && n_text < DLG_LINES) snprintf(texts[n_text++], DLG_TEXT, "%s", tmp);
    } else if (n_item < DLG_LINES && tmp[0]) {   // golf's preferences list ends with a blank choice
      snprintf(items[n_item], DLG_TEXT, "%s", tmp);
      item_labels[n_item] = items[n_item];
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
    const char *override = s->kind == DLG_MENU ? ui_dialog_choice_label(item_labels, n_item, i) : NULL;
    ui_text_fit(c, ui_rect(r.x + ui_s(c, 18), r.y, r.w - ui_s(c, checks ? 92 : 36), r.h), UI_TYPE_HEADING, tc,
                override ? override : items[i], 0);
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
  int anchor = s->selection_anchor;
  if (anchor >= 0 && anchor != s->caret && anchor <= (int)strlen(text)) {
    int lo = anchor < s->caret ? anchor : s->caret;
    int hi = anchor > s->caret ? anchor : s->caret;
    if (lo < 0) lo = 0;
    if (hi > (int)strlen(text)) hi = (int)strlen(text);
    char prefix[160];
    snprintf(prefix, sizeof prefix, "%.*s", lo, text);
    float x0 = tx + ui_text_width(c, UI_TYPE_BODY, prefix);
    snprintf(prefix, sizeof prefix, "%.*s", hi, text);
    float x1 = tx + ui_text_width(c, UI_TYPE_BODY, prefix);
    uid_rrect(d, ui_rect(x0, f.y + (f.h - bf.line) / 2, x1 - x0, bf.line), 1, t->accent);
  }
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
  UiRect scr = ui_screen(c);
  float pad = ui_s(c, t->pad_dialog);
  int portrait = s->has_art && art && art[DLG_ART_PORTRAIT];
  float pw = portrait ? ui_s(c, 112) : 0;
  float portrait_gap = portrait ? ui_s(c, 20) : 0;
  char para[480], formatted[480];
  snprintf(para, sizeof para, "%s", s->text);
  if (ui_dialog_format_date_hint(para, formatted, sizeof formatted)) snprintf(para, sizeof para, "%s", formatted);
  for (char *p = para; *p; p++)
    if (*p == '^' || *p == 10) *p = ' ';
  float w = ui_dialog_hint_width(ui_text_width(c, UI_TYPE_BODY, para), pad, pw, portrait_gap,
                                 ui_s(c, 360), ui_s(c, 640), scr.w - ui_s(c, 40));
  float text_w = w - 2 * pad - pw - portrait_gap;
  char lines[WRAP_MAX * 3][DLG_TEXT];
  int n = 0;
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

// Face tiles share one catalog with original/custom heads and authored faces.
// A tile previews; only Use face commits. The surrounding profile stays intact.
static void face_image(UiCtx *c,SDL_Texture *image,UiRect box) {
  if(!image)return;
  int w=1,h=1;SDL_QueryTexture(image,NULL,NULL,&w,&h);
  if(w<1 || h<1)return;
  float scale=fminf(box.w/w,box.h/h);
  uid_image(ui_drawer(c),image,NULL,ui_rect(box.x+(box.w-w*scale)/2,box.y+(box.h-h*scale)/2,w*scale,h*scale),0,(UiColor){255,255,255,255});
}
static int draw_faces(UiCtx *c,const DlgSnap *s,SDL_Texture *const *art,DlgAction *acts,int max) {
  const UiTokens *t=ui_tokens(c);UiDraw *d=ui_drawer(c);UiRect screen=ui_screen(c);
  float unit=ui_s(c,1),pad=ui_s(c,t->pad_dialog);
  float w=fminf(ui_s(c,1120),screen.w-ui_s(c,40)),h=fminf(ui_s(c,800),screen.h-ui_s(c,40));
  UiRect panel=ui_rect(screen.x+(screen.w-w)/2,screen.y+(screen.h-h)/2,w,h);
  uid_rrect(d,screen,0,t->scrim);ui_hit(c,screen,0,0,UI_HIT_BLOCK|UI_HIT_MODAL);
  ui_panel(c,panel,ui_s(c,t->radius_panel),UI_PANEL_STRONG);
  UiRect in=ui_inset_all(panel,pad);int n=0;
  ui_text_in(c,ui_rect(in.x,in.y,in.w,ui_s(c,38)),UI_TYPE_TITLE,t->text_primary,"Choose a face",0);
  char count[80];snprintf(count,sizeof count,"%s: %d faces",s->face_male?"Men":"Women",s->face_total);
  ui_text_in(c,ui_rect(in.x,in.y+ui_s(c,42),in.w,ui_s(c,26)),UI_TYPE_BODY,t->text_secondary,count,0);
  const char *filters[]={"All faces","New faces","Original / imported"};int filter=s->face_filter;
  UiRect tabs=ui_rect(in.x+in.w-ui_s(c,490),in.y+ui_s(c,8),ui_s(c,490),ui_s(c,44));
  if(ui_segmented(c,ui_id("faces.filter"),tabs,filters,NULL,3,&filter) && n<max)acts[n++]=(DlgAction){DLG_A_FACE_FILTER,filter};
  float filter_x[3],fx=tabs.x+ui_s(c,3),extra=fmaxf(0,(tabs.w-ui_segmented_width(c,filters,NULL,3))/3);
  for(int i=0;i<3;i++){float sw=ui_segmented_width(c,&filters[i],NULL,1)-ui_s(c,6)+extra;filter_x[i]=fx+sw/2;fx+=sw+ui_s(c,2);}
  float gap=ui_s(c,12),grid_w=in.w-ui_s(c,320),tile_w=(grid_w-gap*3)/4;
  float grid_y=in.y+ui_s(c,90),tile_h=ui_s(c,174),radius=ui_s(c,t->radius_tile);
  int pages=(s->face_total+DLG_FACE_TILES-1)/DLG_FACE_TILES;if(pages<1)pages=1;
  static unsigned last_log;int log=getenv("SIMGOLF_DIALOGS_LOG") && last_log!=s->serial/30;
  if(log)last_log=s->serial/30;
  for(int i=0;i<s->face_rows && i<DLG_FACE_TILES;i++) {
    UiRect tile=ui_rect(in.x+(i%4)*(tile_w+gap),grid_y+(i/4)*(tile_h+gap),tile_w,tile_h);
    uint32_t id=ui_idi(ui_id("faces.tile"),s->face_code[i]);int chosen=s->face_code[i]==s->face_selected;
    int ready=s->face_ready[i] && art && art[DLG_ART_FACE_FIRST+i];
    ui_hit(c,tile,radius,id,UI_HIT_BLOCK|(ready?UI_HIT_INTERACT:0));
    uid_rrect(d,tile,radius,chosen?t->accent_soft:t->surface_inset);
    if(chosen)uid_rrect_stroke(d,tile,radius,ui_s(c,2),t->accent);
    else if(ui_is_hot(c,id))uid_rrect_stroke(d,tile,radius,ui_s(c,1),t->accent);
    UiRect picture=ui_rect(tile.x+ui_s(c,8),tile.y+ui_s(c,8),tile.w-ui_s(c,16),ui_s(c,128));
    if(ready)face_image(c,art[DLG_ART_FACE_FIRST+i],picture);
    else ui_text_in(c,picture,UI_TYPE_CAPTION,t->text_secondary,"Unavailable",1);
    ui_text_fit(c,ui_rect(tile.x+ui_s(c,8),tile.y+tile.h-ui_s(c,29),tile.w-ui_s(c,16),ui_s(c,23)),UI_TYPE_LABEL,t->text_primary,s->face_name[i],1);
    if(chosen)ui_icon(c,UI_ICON_CHECK,ui_rect(tile.x+tile.w-ui_s(c,25),tile.y+ui_s(c,7),ui_s(c,18),ui_s(c,18)),ui_s(c,16),t->accent);
    if(ready && ui_clicked(c,id) && n<max)acts[n++]=(DlgAction){DLG_A_ITEM,s->face_code[i]};
    if(log)fprintf(stderr,"faces: tile code %d at %.0f,%.0f ready %d target %.0fx%.0f [%s]\n",s->face_code[i],tile.x+tile.w/2,tile.y+tile.h/2,ready,screen.w,screen.h,s->face_name[i]);
  }
  float preview_x=in.x+grid_w+ui_s(c,24),preview_w=in.x+in.w-preview_x;
  UiRect picture=ui_rect(preview_x,grid_y+ui_s(c,8),preview_w,ui_s(c,340));
  int preview=s->has_art && art && art[DLG_ART_PORTRAIT];
  if(preview)face_image(c,art[DLG_ART_PORTRAIT],picture);
  else ui_text_in(c,picture,UI_TYPE_BODY,t->text_secondary,"Face unavailable",1);
  ui_text_fit(c,ui_rect(preview_x,picture.y+picture.h+ui_s(c,20),preview_w,ui_s(c,34)),UI_TYPE_HEADING,t->text_primary,s->face_preview,1);
  const char *origin=s->face_selected>=1000?"New face":s->face_selected<20?"Original face":"Imported face";
  ui_text_in(c,ui_rect(preview_x,picture.y+picture.h+ui_s(c,58),preview_w,ui_s(c,26)),UI_TYPE_BODY,t->text_secondary,origin,1);
  float by=in.y+in.h-ui_s(c,46),bh=ui_s(c,46);
  UiRect prev=ui_rect(in.x,by,ui_s(c,118),bh),next=ui_rect(in.x+ui_s(c,242),by,ui_s(c,118),bh);
  if(ui_button(c,ui_id("faces.previous"),prev,"Previous",UI_ICON_BACK,UI_BTN_PILL,s->face_page<=0?UI_DISABLED:0) && n<max)acts[n++]=(DlgAction){DLG_A_FACE_PAGE,-1};
  char page[40];snprintf(page,sizeof page,"%d / %d",s->face_page+1,pages);
  ui_text_in(c,ui_rect(in.x+ui_s(c,126),by,ui_s(c,108),bh),UI_TYPE_BODY,t->text_primary,page,1);
  if(ui_button(c,ui_id("faces.next"),next,"Next",UI_ICON_CHEV,UI_BTN_PILL,s->face_page+1>=pages?UI_DISABLED:0) && n<max)acts[n++]=(DlgAction){DLG_A_FACE_PAGE,1};
  UiRect apply=ui_rect(in.x+in.w-ui_s(c,152),by,ui_s(c,152),bh),cancel=ui_rect(apply.x-ui_s(c,128),by,ui_s(c,116),bh);
  if(ui_button(c,ui_id("faces.cancel"),cancel,"Cancel",UI_ICON_NONE,UI_BTN_PILL,0) && n<max)acts[n++]=(DlgAction){DLG_A_CANCEL,0};
  if(ui_button(c,ui_id("faces.apply"),apply,"Use face",UI_ICON_CHECK,UI_BTN_PRIMARY,preview?0:UI_DISABLED) && n<max)acts[n++]=(DlgAction){DLG_A_OK,0};
  ui_text_in(c,ui_rect(in.x,grid_y+3*(tile_h+gap)+ui_s(c,10),grid_w,ui_s(c,24)),UI_TYPE_CAPTION,t->text_secondary,"Choose a face to preview. Esc cancels.",0);
  if(log)fprintf(stderr,"faces: layout target %.0fx%.0f page %d filter %d selected %d prev %.0f,%.0f next %.0f,%.0f cancel %.0f,%.0f apply %.0f,%.0f filter0 %.0f,%.0f filter1 %.0f,%.0f filter2 %.0f,%.0f\n",screen.w,screen.h,s->face_page,s->face_filter,s->face_selected,prev.x+prev.w/2,prev.y+bh/2,next.x+next.w/2,next.y+bh/2,cancel.x+cancel.w/2,cancel.y+bh/2,apply.x+apply.w/2,apply.y+bh/2,filter_x[0],tabs.y+tabs.h/2,filter_x[1],tabs.y+tabs.h/2,filter_x[2],tabs.y+tabs.h/2);
  return n;
}

static int draw_document(UiCtx *c, const DlgSnap *s, DlgAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  UiRect scr = ui_screen(c);
  float pad = ui_s(c, 28), gap = ui_s(c, 10);
  float width = fminf(ui_s(c, 820), scr.w - ui_s(c, 48));
  float inner = width - 2 * pad, total = 0;
  UiFont font = ui_font(c, UI_TYPE_BODY);
  struct { char *text; int item, lines; float height; } rows[256];
  char document[sizeof s->document]; snprintf(document, sizeof document, "%s", s->document);
  int nr = 0, raw = 0;
  for (char *p = document; *p && nr < 256; raw++) {
    char *end = strchr(p, '\n'); if (end) *end = 0;
    int item = raw - s->document_first;
    if (s->document_first < 0 || item < 0 || item >= s->n_items || item >= 32 || (*p != ' ' && *p != '_')) item = -1;
    if (*p == '_') ++p;
    for (char *q = p; *q; q++) if (*q == '|') *q = ' ';
    char wrapped[WRAP_MAX][DLG_TEXT];
    int n = wrap(c, UI_TYPE_BODY, p, inner - ui_s(c, 36), wrapped);
    float h = fmaxf(font.line, n * font.line) + (item >= 0 ? ui_s(c, 18) : 0);
    rows[nr].text = p; rows[nr].item = item; rows[nr].lines = n; rows[nr].height = h;
    total += h + gap; nr++;
    if (!end) break;
    p = end + 1;
  }
  float header = ui_s(c, 58), footer = ui_s(c, 58);
  float height = fminf(total + header + footer + 2 * pad, scr.h - ui_s(c, 48));
  UiRect panel = ui_rect_round(ui_center(scr, width, height));
  uid_rrect(d, scr, 0, t->scrim);
  ui_hit(c, scr, 0, ui_id("document.scrim"), UI_HIT_BLOCK | UI_HIT_MODAL);
  ui_panel(c, panel, ui_s(c, t->radius_panel), UI_PANEL_STRONG);
  UiRect in = ui_inset_all(panel, pad);
  ui_text_in(c, ui_rect(in.x, in.y, in.w - ui_s(c, 44), ui_s(c, 40)), UI_TYPE_TITLE, t->text_primary, s->n_items > 0 ? "Choose" : "Notice", 0);
  UiRect view = ui_rect(in.x, in.y + header, in.w, height - 2 * pad - header - footer);
  float offset = ui_vscroll_begin(c, ui_idi(ui_id("document.scroll"), s->document_epoch), view, total, font.line * 4);
  float y = view.y + offset;
  int na = 0;
  for (int i = 0; i < nr; y += rows[i++].height + gap) {
    UiRect r = ui_rect(view.x, y, view.w - ui_s(c, 12), rows[i].height);
    if (r.y + r.h <= view.y || r.y >= view.y + view.h) continue;
    int item = rows[i].item, disabled = item >= 0 && (s->disabled & (1u << item));
    uint32_t id = ui_idi(ui_id("document.item"), item);
    if (item >= 0) {
      ui_hit(c, r, ui_s(c, t->radius_control), id, disabled ? UI_HIT_BLOCK : UI_HIT_BLOCK | UI_HIT_INTERACT);
      uid_rrect(d, r, ui_s(c, t->radius_control), uid_alpha(item == s->sel || ui_is_hot(c, id) ? t->accent_soft : t->surface_inset, .7f));
      if (!disabled && na < max) {
        if (ui_clicked(c, id)) acts[na++] = (DlgAction){DLG_A_ITEM, item};
        else if (ui_right_clicked(c, id)) acts[na++] = (DlgAction){DLG_A_ITEM, item | 0x10000};
      }
    }
    char wrapped[WRAP_MAX][DLG_TEXT];
    int n = wrap(c, UI_TYPE_BODY, rows[i].text, inner - ui_s(c, 36), wrapped);
    for (int line = 0; line < n; line++)
      ui_text_in(c, ui_rect(r.x + ui_s(c, 12), r.y + (item >= 0 ? ui_s(c, 9) : 0) + line * font.line, r.w - ui_s(c, 24), font.line),
                 UI_TYPE_BODY, disabled ? t->text_tertiary : t->text_primary, wrapped[line], 0);
    if (getenv("SIMGOLF_DIALOGS_LOG") && item >= 0) {
      static uint32_t logged_epoch; static int logged[32];
      if (logged_epoch != s->document_epoch) { memset(logged, 0, sizeof logged); logged_epoch = s->document_epoch; }
      if (!logged[item]) {
        fprintf(stderr, "dialogs: document item %d at %.0f,%.0f target %.0fx%.0f\n", item, r.x + r.w / 2, r.y + r.h / 2, scr.w, scr.h);
        logged[item] = 1;
      }
    }
  }
  ui_vscroll_end(c);
  UiRect close = ui_rect(in.x + in.w - ui_s(c, 120), panel.y + panel.h - pad - ui_s(c, 40), ui_s(c, 120), ui_s(c, 40));
  if (ui_button(c, ui_id("document.close"), close, "Close", UI_ICON_NONE, UI_BTN_PILL, 0) && na < max)
    acts[na++] = (DlgAction){DLG_A_CANCEL, 0};
  ui_text_in(c, ui_rect(in.x, close.y, in.w - close.w - gap, close.h), UI_TYPE_CAPTION, t->text_tertiary, "Esc closes", 0);
  return na;
}

int ui_dialogs_draw(UiCtx *c, const DlgSnap *s, SDL_Texture *const *art, DlgAction *acts, int max) {
  switch (s->kind) {
    case DLG_MENU:
    case DLG_CHECKS: return draw_list(c, s, acts, max);
    case DLG_ENTRY: return draw_entry(c, s, acts, max);
    case DLG_HIRE: return draw_hire(c, s, acts, max);
    case DLG_FACE: return draw_faces(c,s,art,acts,max);
    case DLG_HINT: return draw_hint(c, s, art, acts, max);
    case DLG_DOCUMENT: return draw_document(c, s, acts, max);
    default: return 0;
  }
}
