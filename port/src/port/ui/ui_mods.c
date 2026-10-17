// Frosted Mint "Mods" screen, draw side (simgolf mod, card task-22), see ui_mods.h.

#include "port/ui/ui_mods.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define S(v) ui_s(c, (v))
#define MAXVIS 256

// ------------------------------------------------------------------ state and keys

void ui_mods_init(ModsUi *s, ModMgr *m) {
  UiRect z = {0};
  memset(s, 0, sizeof *s);
  s->m = m;
  s->selected = -1;
  s->focus = 1;
  s->drag = s->drag_target = -1;
  s->view.sort = MM_SORT_LOAD;
  s->log_list = s->log_detail = s->log_apply = s->log_close = s->log_profile = z;
  if (modmgr_count(m) > 0) {   // start on the first loaded mod, else the first in the list
    ModView v = s->view;
    int ids[MAXVIS];
    int n = modmgr_view(m, &v, ids, MAXVIS);
    if (n > 0) s->selected = ids[0];
  }
}

void ui_mods_say(ModsUi *s, double now, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s->msg, sizeof s->msg, fmt, ap);
  va_end(ap);
  s->msg_until = now + 6000;
}

static void log_act(const char *fmt, ...) {
  char b[300];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  fprintf(stderr, "modsui: %s\n", b);
  fflush(stderr);   // the log is buffered: a quiet game would show this late
}

static const char *sel_id(const ModsUi *s) { return s->selected >= 0 ? modmgr_manifest(s->m, s->selected)->id : ""; }

static void do_apply(ModsUi *s) {
  char err[200] = "";
  if (modmgr_apply(s->m, err, sizeof err)) {
    log_act("applied profile %s, restart %s", modmgr_profile(s->m), modmgr_restart_needed(s->m) ? "needed" : "not needed");
    if (modmgr_restart_needed(s->m)) ui_mods_say(s, s->clock, "Saved. The change takes effect after you restart the game.");
    else ui_mods_say(s, s->clock, "Saved.");
  } else {
    log_act("apply failed: %s", err);
    ui_mods_say(s, s->clock, "%s", err);
  }
}

static void do_revert(ModsUi *s) {
  modmgr_reload(s->m);
  if (s->selected >= modmgr_count(s->m)) s->selected = -1;
  log_act("reverted to load_order.toml");
  ui_mods_say(s, s->clock, "Changes undone.");
}

void ui_mods_request_close(ModsUi *s) {
  if (modmgr_dirty(s->m)) {
    s->dialog = MUI_DLG_CLOSE;
    s->focus = 0;
  } else s->req_close = 1;
}

static void open_dialog(ModsUi *s, int d) {
  s->dialog = d;
  s->dlg_err[0] = 0;
  s->focus = 2;
  s->dd_open = 0;
  if (d == MUI_DLG_NEW) s->field[0] = 0, s->dlg_copy = 0;
  if (d == MUI_DLG_RENAME) snprintf(s->field, sizeof s->field, "%s", modmgr_profile(s->m));
  if (d == MUI_DLG_DELETE || d == MUI_DLG_CLOSE) s->focus = 0;
}

static void dialog_confirm(ModsUi *s) {
  char err[160] = "";
  switch (s->dialog) {
    case MUI_DLG_NEW:
      if (modmgr_profile_create(s->m, s->field, s->dlg_copy, err, sizeof err)) {
        log_act("profile %s created%s", modmgr_profile(s->m), s->dlg_copy ? " from the current mods" : "");
        s->dialog = MUI_DLG_NONE, s->focus = 1;
      } else snprintf(s->dlg_err, sizeof s->dlg_err, "%s", err);
      break;
    case MUI_DLG_RENAME:
      if (modmgr_profile_rename(s->m, s->field, err, sizeof err)) {
        log_act("profile renamed to %s", modmgr_profile(s->m));
        s->dialog = MUI_DLG_NONE, s->focus = 1;
      } else snprintf(s->dlg_err, sizeof s->dlg_err, "%s", err);
      break;
    case MUI_DLG_DELETE: {
      char name[48];
      snprintf(name, sizeof name, "%s", modmgr_profile(s->m));
      if (modmgr_profile_delete(s->m, name, err, sizeof err)) log_act("profile %s deleted", name);
      s->dialog = MUI_DLG_NONE, s->focus = 1;
      break;
    }
    case MUI_DLG_CLOSE:   // the primary button: apply, then leave
      do_apply(s);
      s->dialog = MUI_DLG_NONE;
      s->req_close = 1;
      break;
  }
}

static int printable(SDL_Keycode k, int mods) {
  int shift = (mods & KMOD_SHIFT) != 0;
  if (k >= 'a' && k <= 'z') return shift ? toupper((int)k) : (int)k;
  if (k >= '0' && k <= '9' && !shift) return (int)k;
  if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return '1' + (int)(k - SDLK_KP_1);
  if (k == SDLK_KP_0) return '0';
  if (k == SDLK_SPACE) return ' ';
  if (k == SDLK_MINUS) return shift ? '_' : '-';
  if (k == SDLK_PERIOD && !shift) return '.';
  return 0;
}

static int step_selection(ModsUi *s, int dir) {
  int ids[MAXVIS];
  int n = modmgr_view(s->m, &s->view, ids, MAXVIS);
  if (!n) return 0;
  int at = -1;
  for (int i = 0; i < n; i++)
    if (ids[i] == s->selected) at = i;
  at = at < 0 ? 0 : at + dir;
  if (at < 0) at = 0;
  if (at >= n) at = n - 1;
  s->selected = ids[at];
  return 1;
}

int ui_mods_key(ModsUi *s, SDL_Keycode k, int mods) {
  if (s->dialog) {
    if (k == SDLK_ESCAPE) {
      s->dialog = MUI_DLG_NONE, s->focus = 1;
      return 1;
    }
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
      dialog_confirm(s);
      return 1;
    }
    if (s->dialog == MUI_DLG_NEW || s->dialog == MUI_DLG_RENAME) {
      size_t n = strlen(s->field);
      int ch = printable(k, mods);
      if (k == SDLK_BACKSPACE && n) s->field[n - 1] = 0, s->dlg_err[0] = 0;
      else if (ch && ch != '.' && n + 1 < sizeof s->field && n < 32) s->field[n] = (char)ch, s->field[n + 1] = 0, s->dlg_err[0] = 0;
    }
    return 1;
  }
  if (s->dd_open && k == SDLK_ESCAPE) {
    s->dd_open = 0;
    return 1;
  }
  if (k == SDLK_ESCAPE) {
    if (s->view.search[0]) s->view.search[0] = 0;
    else ui_mods_request_close(s);
    return 1;
  }
  if (k == SDLK_DOWN) return step_selection(s, 1) || 1;
  if (k == SDLK_UP) return step_selection(s, -1) || 1;
  if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
    if (s->selected >= 0) {
      const ModInfo *f = modmgr_info(s->m, s->selected);
      if (modmgr_set_enabled(s->m, s->selected, !f->on)) log_act("%s %s", f->on ? "enabled" : "disabled", sel_id(s));
      else ui_mods_say(s, s->clock, "Vanilla is the original game, so it has no mods. Pick or create a profile first.");
    }
    return 1;
  }
  size_t n = strlen(s->view.search);
  int ch = printable(k, mods);
  if (k == SDLK_BACKSPACE) {
    if (n) s->view.search[n - 1] = 0;
    return 1;
  }
  if (ch && n + 1 < sizeof s->view.search) {
    s->view.search[n] = (char)ch, s->view.search[n + 1] = 0;
    return 1;
  }
  return 1;   // the page takes every key while it is open
}

// ------------------------------------------------------------------ drawing helpers

typedef struct { UiColor bg, fg; } BadgeCol;

static UiColor warn_col(const UiTokens *t) { return t->warn_grad.from; }

static float badge_w(UiCtx *c, const char *txt) { return ui_text_width(c, UI_TYPE_MICRO, txt) + S(18); }

// A small pill; returns its width plus the gap.
static float badge(UiCtx *c, float x, float y, const char *txt, UiColor bg, UiColor fg) {
  float w = badge_w(c, txt), h = S(20);
  UiRect r = ui_rect(x, y, w, h);
  uid_rrect(ui_drawer(c), r, h / 2, bg);
  ui_text_in(c, r, UI_TYPE_MICRO, fg, txt, 0.5f);
  return w + S(6);
}

typedef struct { const char *txt; UiColor bg, fg; } Bdg;

static int badges_for(const UiTokens *t, const ModInfo *f, Bdg *b) {
  int n = 0;
  UiColor amber = warn_col(t);
  UiColor amber_bg = uid_alpha(amber, 0.22f);
  UiColor danger_bg = uid_alpha(t->danger, 0.16f);
  if (f->broken) b[n++] = (Bdg){"Broken mod.toml", danger_bg, t->danger};
  if (f->missing_dep) b[n++] = (Bdg){"Missing dependency", danger_bg, t->danger};
  if (f->conflict) b[n++] = (Bdg){"Conflict", amber_bg, uid_mix(amber, t->text_primary, 0.45f)};
  if (f->incompatible) b[n++] = (Bdg){"Incompatible", danger_bg, t->danger};
  if (f->needs_restart) b[n++] = (Bdg){"Needs restart", t->accent_soft, t->accent_soft_text};
  if (f->changes_saves) b[n++] = (Bdg){"Changes saves", uid_alpha(t->text_secondary, 0.16f), t->text_secondary};
  if (f->auto_enabled && !f->on) b[n++] = (Bdg){"Needed by another mod", uid_alpha(t->text_secondary, 0.12f), t->text_secondary};
  return n;
}

// The mod's picture: its icon, or a soft disc with the first letter.
static void mod_icon(UiCtx *c, ModsUi *s, int mi, UiRect r, float radius) {
  const UiTokens *t = ui_tokens(c);
  SDL_Texture *tex = s->icon_fn ? s->icon_fn(s->icon_user, mi) : NULL;
  if (tex) {
    uid_image(ui_drawer(c), tex, NULL, r, radius, (UiColor){255, 255, 255, 255});
    return;
  }
  const ModManifest *mf = modmgr_manifest(s->m, mi);
  uid_rrect(ui_drawer(c), r, radius, mf->builtin ? t->accent_soft : t->surface_inset);
  char l[2] = {mf->name && *mf->name ? (char)toupper((unsigned char)mf->name[0]) : '?', 0};
  ui_text_in(c, r, UI_TYPE_HEADING, mf->builtin ? t->accent_soft_text : t->text_secondary, l, 0.5f);
}

// Greedy word wrap into at most max lines of width w; returns the line count. Lines live in buf.
static int wrap(UiCtx *c, int style, const char *text, float w, char (*lines)[200], int max) {
  int n = 0;
  const char *p = text;
  while (*p && n < max) {
    while (*p == ' ') p++;
    if (!*p) break;
    char cur[200] = "";
    int clen = 0;
    while (*p) {
      const char *e = p;
      while (*e && *e != ' ') e++;
      char word[120];
      int wl = (int)(e - p) < 119 ? (int)(e - p) : 119;
      memcpy(word, p, (size_t)wl);
      word[wl] = 0;
      char test[330];
      snprintf(test, sizeof test, "%s%s%s", cur, clen ? " " : "", word);
      if (clen && ui_text_width(c, style, test) > w) break;
      snprintf(cur, sizeof cur, "%s", test);
      clen = (int)strlen(cur);
      p = e;
      while (*p == ' ') p++;
    }
    snprintf(lines[n++], 200, "%s", cur);
  }
  if (*p && n == max) {   // cut: end the last line with an ellipsis
    size_t l = strlen(lines[n - 1]);
    if (l > 190) l = (size_t)ui_utf8_cut(lines[n - 1], 190);
    snprintf(lines[n - 1] + l, 10, "...");
  }
  return n;
}

static float para(UiCtx *c, float x, float y, float w, int style, UiColor col, const char *text, int maxlines) {
  char lines[10][200];
  if (maxlines > 10) maxlines = 10;
  int n = wrap(c, style, text, w, lines, maxlines);
  UiFont f = ui_font(c, style);
  for (int i = 0; i < n; i++) ui_text_in(c, ui_rect(x, y + i * f.line, w, f.line), style, col, lines[i], 0);
  return n * f.line;
}

static int chip(UiCtx *c, uint32_t id, float *x, float y, const char *label, int on) {
  float w = ui_text_width(c, UI_TYPE_LABEL, label) + S(34), h = S(34);
  UiRect r = ui_rect(*x, y, w, h);
  *x += w + S(8);
  const UiTokens *t = ui_tokens(c);
  ui_hit(c, r, h / 2, id, UI_HIT_BLOCK | UI_HIT_INTERACT);
  float hov = ui_anim(c, id, ui_is_hot(c, id) ? 1.0f : 0.0f, t->motion_hover);
  if (on) {
    uid_rrect(ui_drawer(c), r, h / 2, t->accent);
  } else {
    uid_rrect(ui_drawer(c), r, h / 2, uid_mix(t->surface_inset, t->glass_fill_strong, hov));
    uid_rrect_stroke(ui_drawer(c), r, h / 2, fmaxf(1.0f, S(1)), t->surface_inset_border);
  }
  ui_text_in(c, r, UI_TYPE_LABEL, on ? t->text_on_accent : t->text_primary, label, 0.5f);
  return ui_clicked(c, id);
}

static void field_box(UiCtx *c, UiRect r, const char *text, const char *placeholder, int focus, double now) {
  const UiTokens *t = ui_tokens(c);
  uid_rrect(ui_drawer(c), r, S(t->radius_control), t->surface_inset);
  uid_rrect_stroke(ui_drawer(c), r, S(t->radius_control), focus ? fmaxf(2, S(2)) : fmaxf(1, S(1)),
                   focus ? t->accent : t->surface_inset_border);
  UiRect in = ui_inset(r, S(16), 0, S(40), 0);
  if (text[0]) {
    ui_text_fit(c, in, UI_TYPE_BODY, t->text_primary, text, 0);
  } else {
    ui_text_in(c, in, UI_TYPE_BODY, t->text_tertiary, placeholder, 0);
  }
  if (focus && fmodf((float)(now / 530.0), 2.0f) < 1.0f) {   // the caret
    float tw = text[0] ? fminf(ui_text_width(c, UI_TYPE_BODY, text), in.w) : 0;
    uid_rrect(ui_drawer(c), ui_rect(in.x + tw + S(2), r.y + S(11), fmaxf(1, S(2)), r.h - S(22)), 1, t->accent);
  }
}

static void section(UiCtx *c, float x, float *y, float w, const char *title) {
  const UiTokens *t = ui_tokens(c);
  *y += S(10);
  ui_text_in(c, ui_rect(x, *y, w, S(20)), UI_TYPE_LABEL, t->text_primary, title, 0);
  *y += S(26);
}

// ------------------------------------------------------------------ the list

static void draw_row(UiCtx *c, ModsUi *s, int mi, UiRect rr, int row_slot, int *vis, int nvis, int vi, double now) {
  (void)vis, (void)nvis, (void)vi;
  const UiTokens *t = ui_tokens(c);
  ModMgr *m = s->m;
  const ModManifest *mf = modmgr_manifest(m, mi);
  const ModInfo *f = modmgr_info(m, mi);
  uint32_t rid = ui_idi(ui_id("mods.row"), mi);
  float rad = S(t->radius_tile);
  ui_card(c, rr, s->selected == mi);
  ui_hit(c, rr, rad, rid, UI_HIT_BLOCK | UI_HIT_INTERACT);
  if (ui_clicked(c, rid)) s->selected = mi;
  if (s->drag == mi) uid_rrect(ui_drawer(c), rr, rad, uid_alpha(t->scrim, 0.35f));

  float pad = S(14);
  // the grip (drag handle) and the load position, only for mods in the list
  UiRect grip = ui_rect(rr.x + S(6), rr.y, S(34), rr.h);
  if (f->on) {
    uint32_t gid = ui_idi(ui_id("mods.grip"), mi);
    ui_hit(c, grip, S(8), gid, UI_HIT_BLOCK | UI_HIT_INTERACT);
    if (ui_is_active(c, gid) && s->drag < 0) s->drag = mi;
    for (int k = 0; k < 3; k++)
      uid_rrect(ui_drawer(c), ui_rect(grip.x + S(9), grip.y + grip.h / 2 - S(10) + k * S(8), S(16), S(3)), S(1.5f),
                uid_alpha(t->icon_muted, ui_is_hot(c, gid) ? 1.0f : 0.7f));
    if (f->load_pos >= 0) {
      char num[16];
      snprintf(num, sizeof num, "%d", f->load_pos + 1);
      ui_text_in(c, ui_rect(grip.x, grip.y + grip.h / 2 + S(14), grip.w, S(16)), UI_TYPE_MICRO, t->text_tertiary, num, 0.5f);
    }
  }
  UiRect ic = ui_rect(rr.x + S(46), rr.y + (rr.h - S(60)) / 2, S(60), S(60));
  mod_icon(c, s, mi, ic, S(14));

  // right side: up, down, toggle
  float tw = S(t->toggle_w), bs = S(36);
  UiRect tg = ui_rect(rr.x + rr.w - pad - tw, rr.y + (rr.h - S(t->toggle_h)) / 2, tw, S(t->toggle_h));
  UiRect up = ui_rect(tg.x - S(12) - bs * 2 - S(4), rr.y + (rr.h - bs) / 2, bs, bs);
  UiRect dn = ui_rect(up.x + bs + S(4), up.y, bs, bs);
  float tx = ic.x + ic.w + S(16), tr = up.x - S(14);
  UiRect hit_tg = ui_rect(tg.x - S(8), rr.y + (rr.h - S(40)) / 2, tg.w + S(16), S(40));
  int on = f->on;
  if (ui_toggle(c, ui_idi(ui_id("mods.tg"), mi), hit_tg, &on)) {
    if (f->broken && on) {
      ui_mods_say(s, now, "%s has a broken mod.toml and cannot be turned on.", mf->name);
    } else if (!strcmp(modmgr_profile(m), "vanilla")) {
      ui_mods_say(s, now, "Vanilla is the original game, so it has no mods. Pick or create a profile first.");
    } else if (modmgr_set_enabled(m, mi, on)) {
      s->selected = mi;
      log_act("%s %s", on ? "enabled" : "disabled", mf->id);
    }
  }
  if (f->on) {
    int can_up = f->pos > 0;
    if (ui_icon_button(c, ui_idi(ui_id("mods.up"), mi), up, UI_ICON_UP, UI_COMPACT | (can_up ? 0 : UI_DISABLED), "Load earlier") && can_up) {
      modmgr_move(m, mi, -1);
      log_act("moved %s earlier", mf->id);
    }
    const char *ids[MAXVIS];
    int can_dn = f->pos < modmgr_list(m, ids, MAXVIS) - 1;
    if (ui_icon_button(c, ui_idi(ui_id("mods.dn"), mi), dn, UI_ICON_DOWN, UI_COMPACT | (can_dn ? 0 : UI_DISABLED), "Load later") && can_dn) {
      modmgr_move(m, mi, 1);
      log_act("moved %s later", mf->id);
    }
  }

  // text: name, version and author, then the description, then badges
  UiFont nf = ui_font(c, UI_TYPE_BODY), cf = ui_font(c, UI_TYPE_CAPTION);
  Bdg bd[8];
  int nb = badges_for(t, f, bd);
  float th = nf.line + cf.line + (nb ? S(26) : 0);
  float ty = rr.y + (rr.h - th) / 2;
  char ver[48], auth[120];
  snprintf(ver, sizeof ver, "v%s%s", mf->version ? mf->version : "?", mf->builtin ? "  built-in" : "");
  modmgr_author(m, mi, auth, sizeof auth);
  float nw = ui_text_width(c, UI_TYPE_BODY, mf->name ? mf->name : mf->id);
  float maxn = tr - tx;
  ui_text_fit(c, ui_rect(tx, ty, fminf(nw + 2, maxn), nf.line), UI_TYPE_BODY, f->broken ? t->danger : t->text_primary,
              mf->name ? mf->name : mf->id, 0);
  float vx = tx + fminf(nw + 2, maxn) + S(10);
  if (vx < tr - S(40)) {
    char meta[200];
    snprintf(meta, sizeof meta, "%s%s%s", ver, auth[0] ? "  by " : "", auth);
    ui_text_fit(c, ui_rect(vx, ty, tr - vx, nf.line), UI_TYPE_CAPTION, t->text_tertiary, meta, 0);
  }
  ui_text_fit(c, ui_rect(tx, ty + nf.line, tr - tx, cf.line), UI_TYPE_CAPTION, t->text_secondary,
              mf->description && *mf->description ? mf->description : "No description.", 0);
  float bx = tx;
  for (int i = 0; i < nb; i++) {
    if (bx + badge_w(c, bd[i].txt) > tr) break;
    bx += badge(c, bx, ty + nf.line + cf.line + S(6), bd[i].txt, bd[i].bg, bd[i].fg);
  }

  if (row_slot < 24) {
    s->log_row[row_slot] = rr;
    s->log_toggle[row_slot] = tg;
    s->log_up[row_slot] = up;
    s->log_down[row_slot] = dn;
    s->log_row_mod[row_slot] = mi;
  }
}

static void draw_list(UiCtx *c, ModsUi *s, UiRect lr, double now) {
  const UiTokens *t = ui_tokens(c);
  ModMgr *m = s->m;
  int vis[MAXVIS];
  int n = modmgr_view(m, &s->view, vis, MAXVIS);
  float rowh = S(100), gap = S(10);
  s->log_list = lr;
  s->log_nrows = 0;
  // finish a drag that was released
  if (s->drag >= 0 && !ui_is_active(c, ui_idi(ui_id("mods.grip"), s->drag))) {
    if (s->drag_target >= 0 && s->drag_target != s->drag) {
      const ModInfo *tf = modmgr_info(m, s->drag_target);
      if (tf->on && modmgr_move_to(m, s->drag, tf->pos)) log_act("dragged %s to place %d", modmgr_manifest(m, s->drag)->id, tf->pos + 1);
    }
    s->drag = s->drag_target = -1;
  }
  if (!n) {
    const char *l1 = modmgr_count(m) ? "No mods match" : "No mods installed";
    const char *l2 = modmgr_count(m) ? "Change the search or the filters." : "Put mod folders in the mods folder, next to the game.";
    ui_text_in(c, ui_rect(lr.x, lr.y + S(60), lr.w, S(30)), UI_TYPE_HEADING, t->text_secondary, l1, 0.5f);
    ui_text_in(c, ui_rect(lr.x, lr.y + S(94), lr.w, S(24)), UI_TYPE_CAPTION, t->text_tertiary, l2, 0.5f);
    return;
  }
  uint32_t sid = ui_id("mods.list.scroll");
  float content = n * (rowh + gap);
  float off = ui_vscroll_begin(c, sid, lr, content, rowh + gap);
  float pw = lr.w - S(8);
  float px, pyy;
  int have_ptr = ui_pointer(c, &px, &pyy);
  int slot = 0;
  s->drag_target = -1;
  for (int i = 0; i < n; i++) {
    UiRect rr = ui_rect(lr.x, lr.y + off + i * (rowh + gap), pw, rowh);
    if (rr.y + rr.h < lr.y - S(2) || rr.y > lr.y + lr.h + S(2)) continue;
    if (s->drag >= 0 && have_ptr && vis[i] != s->drag && pyy >= rr.y && pyy < rr.y + rr.h + gap && modmgr_info(m, vis[i])->on)
      s->drag_target = vis[i];
    draw_row(c, s, vis[i], rr, slot++, vis, n, i, now);
    if (s->drag >= 0 && s->drag_target == vis[i]) {   // where it will land
      const ModInfo *df = modmgr_info(m, s->drag), *tf = modmgr_info(m, vis[i]);
      float ly = df->pos > tf->pos ? rr.y - gap / 2 : rr.y + rr.h + gap / 2;
      uid_rrect(ui_drawer(c), ui_rect(rr.x + S(8), ly - S(2), rr.w - S(16), S(4)), S(2), t->accent);
    }
  }
  s->log_nrows = slot < 24 ? slot : 24;
  ui_vscroll_end(c);
  if (s->drag >= 0 && have_ptr) {   // a ghost of the mod under the pointer
    const ModManifest *mf = modmgr_manifest(m, s->drag);
    float gw = ui_text_width(c, UI_TYPE_LABEL, mf->name) + S(40);
    UiRect g = ui_rect(px + S(14), pyy - S(18), gw, S(36));
    ui_panel(c, g, S(18), UI_PANEL_NOBLOCK);
    ui_text_in(c, g, UI_TYPE_LABEL, t->text_primary, mf->name, 0.5f);
  }
}

// ------------------------------------------------------------------ the detail panel

static const char *dep_state(const ModMgr *m, const char *id, int *bad) {
  int i = modmgr_find(m, id);
  *bad = 0;
  if (i < 0) return *bad = 1, "not installed";
  const ModInfo *f = modmgr_info(m, i);
  if (f->load_pos >= 0) return "ready";
  return *bad = 1, f->on ? "cannot load" : "turned off";
}

static void draw_configuration(UiCtx *c, ModsUi *s, const ModManifest *mf, const ModInfo *f,
                               float x, float *y, float w, double now) {
  const UiTokens *t = ui_tokens(c);
  const JsonVal *a = json_get(s->config, "settings");
  int count = 0;
  for (int i = 0; a && i < a->len; i++)
    count += !strcmp(modconfig_text(a->items[i], "mod"), mf->id);
  section(c, x, y, w, "Configure this mod");
  if (!count) {
    const char *msg = !s->config ? "Settings are unavailable. The settings catalog could not be loaded." :
      !strcmp(mf->id, "cut_content") ? "This mod has no extra settings. Turn it on to add the restored landmarks and bench to the build menu." :
      !strcmp(mf->id, "bridge") ? "This mod has no player settings. The local tool connection is controlled when the game starts." :
      "This mod does not offer in-game configuration. Its on/off switch still controls whether it loads.";
    *y += para(c, x, *y, w, UI_TYPE_CAPTION, t->text_secondary, msg, 6) + S(12);
    return;
  }
  int enabled = f->on && f->running && f->load_pos >= 0 && s->setting_save && strcmp(modmgr_profile(s->m), "vanilla");
  *y += para(c, x, *y, w, UI_TYPE_CAPTION, t->text_secondary,
    enabled ? "Choices save automatically for all profiles. Scroll here for more settings. Apply changes and Undo changes below only affect the mod list and profiles." :
    "To change settings, enable this mod, apply the mod list and restart the game. Vanilla keeps the original game settings.", 5) + S(16);
  for (int i = 0; a && i < a->len; i++) {
    const JsonVal *row = a->items[i];
    if (strcmp(modconfig_text(row, "mod"), mf->id)) continue;
    const char *key = modconfig_text(row, "key");
    const JsonVal *choices = json_get(row, "choices");
    const char *value = s->setting_get ? s->setting_get(key) : NULL;
    if (!value || !*value) value = modconfig_text(row, "default");
    int selected = modconfig_choice(row, value);
    ui_text_fit(c, ui_rect(x, *y, w, S(26)), UI_TYPE_LABEL, t->text_primary, modconfig_text(row, "label"), 0);
    *y += S(30);
    *y += para(c, x, *y, w, UI_TYPE_CAPTION, t->text_secondary, modconfig_text(row, "help"), 6) + S(8);
    UiRect prev = ui_rect(x, *y, S(40), S(40));
    UiRect next = ui_rect(x + w - S(40), *y, S(40), S(40));
    char custom[180];
    snprintf(custom, sizeof custom, "Custom: %s", value);
    const char *name = selected < 0 ? custom : modconfig_text(choices->items[selected], "label");
    ui_text_fit(c, ui_rect(x + S(48), *y, w - S(96), S(40)), UI_TYPE_LABEL, t->accent_soft_text, name, 0.5f);
    uint32_t id = ui_id(key);
    int direction = 0;
    if (ui_icon_button(c, ui_idi(id, 0), prev, UI_ICON_BACK, enabled ? 0 : UI_DISABLED, "Previous choice")) direction = -1;
    if (ui_icon_button(c, ui_idi(id, 1), next, UI_ICON_CHEV, enabled ? 0 : UI_DISABLED, "Next choice")) direction = 1;
    if (s->log_nsettings < 24) {
      int k = s->log_nsettings++;
      s->log_setting[k] = next;
      s->log_setting_key[k] = key;
    }
    if (direction && enabled) {
      int index = selected < 0 ? (direction > 0 ? 0 : choices->len - 1) : (selected + direction + choices->len) % choices->len;
      const char *v = modconfig_text(choices->items[index], "value");
      int ok = s->setting_save(key, v);
      const char *when = modconfig_text(row, "timing");
      log_act("setting %s = %s %s", key, v, ok ? "saved" : "save_failed");
      ui_mods_say(s, now, "%s", !ok ? "Could not save. The choice may apply for this session; try again before closing." :
        !strcmp(when, "restart") ? "Saved. Restart the game to use this choice." :
        !strcmp(when, "new_course") ? "Saved. This choice applies when you create a new course." : "Saved. This choice takes effect now.");
    }
    *y += S(46);
    const char *when = modconfig_text(row, "timing");
    *y += para(c, x, *y, w, UI_TYPE_MICRO, t->text_tertiary,
      !strcmp(when, "restart") ? "Takes effect after restarting the game." :
      !strcmp(when, "new_course") ? "New courses only. Existing saves keep their size." : "Takes effect immediately.", 3) + S(20);
  }
}

static void draw_detail(UiCtx *c, ModsUi *s, UiRect dr, double now) {
  (void)now;
  const UiTokens *t = ui_tokens(c);
  ModMgr *m = s->m;
  s->log_detail = dr;
  ui_card(c, dr, 0);
  UiRect in = ui_inset_all(dr, S(18));
  if (s->selected < 0 || s->selected >= modmgr_count(m)) {
    ui_text_in(c, ui_rect(in.x, in.y + S(50), in.w, S(28)), UI_TYPE_HEADING, t->text_secondary, "Pick a mod", 0.5f);
    ui_text_in(c, ui_rect(in.x, in.y + S(84), in.w, S(22)), UI_TYPE_CAPTION, t->text_tertiary, "Its details show here.", 0.5f);
    return;
  }
  int mi = s->selected;
  if (s->config_selected != mi) {
    s->config_selected = mi;
    s->configure = 0;
    *ui_state(c, ui_id("mods.detail.scroll")) = 0;
  }
  const ModManifest *mf = modmgr_manifest(m, mi);
  const ModInfo *f = modmgr_info(m, mi);
  uint32_t sid = ui_id("mods.detail.scroll");
  float *hstate = ui_state(c, ui_id("mods.detail.h"));
  float off = ui_vscroll_begin(c, sid, in, *hstate, S(60));
  float x = in.x, w = in.w - S(10), y = in.y + off;
  float y0 = y;

  if (s->configure) {
    UiRect back = ui_rect(x, y, w, S(40));
    s->log_configure = back;
    if (ui_button(c, ui_id("mods.configure"), back, "Back to mod details", UI_ICON_BACK, UI_BTN_PILL, 0)) {
      s->configure = 0;
      *ui_state(c, sid) = 0;
      log_act("configure %s closed", mf->id);
    }
    y += S(52);
    ui_text_fit(c, ui_rect(x, y, w, S(30)), UI_TYPE_HEADING, t->text_primary, mf->name, 0);
    y += S(34);
    draw_configuration(c, s, mf, f, x, &y, w, now);
    *hstate = y - y0 + S(10);
    ui_vscroll_end(c);
    return;
  }

  // header
  UiRect ic = ui_rect(x, y, S(84), S(84));
  mod_icon(c, s, mi, ic, S(18));
  float hx = x + S(84) + S(16), hw = w - S(100);
  ui_text_fit(c, ui_rect(hx, y + S(2), hw, S(30)), UI_TYPE_HEADING, t->text_primary, mf->name, 0);
  char meta[200], auth[120];
  modmgr_author(m, mi, auth, sizeof auth);
  snprintf(meta, sizeof meta, "Version %s%s%s", mf->version ? mf->version : "?", auth[0] ? "  by " : "", auth);
  ui_text_fit(c, ui_rect(hx, y + S(34), hw, S(22)), UI_TYPE_CAPTION, t->text_secondary, meta, 0);
  {
    char idl[120];
    snprintf(idl, sizeof idl, "%s%s", mf->id, mf->builtin ? "  (built-in)" : "");
    ui_text_fit(c, ui_rect(hx, y + S(56), hw, S(20)), UI_TYPE_MICRO, t->text_tertiary, idl, 0);
  }
  y += S(98);
  // state line
  {
    const char *st = f->broken ? "Cannot load: broken mod.toml" : f->load_pos >= 0 ? "Loads" : f->on ? "Turned on, but cannot load" : "Turned off";
    float bx = x;
    UiColor ok = f->load_pos >= 0 ? t->accent_soft : uid_alpha(t->text_secondary, 0.14f);
    bx += badge(c, bx, y, st, f->on && f->load_pos < 0 ? uid_alpha(t->danger, 0.16f) : ok,
                f->on && f->load_pos < 0 ? t->danger : f->load_pos >= 0 ? t->accent_soft_text : t->text_secondary);
    Bdg bd[8];
    int nb = badges_for(t, f, bd);
    for (int i = 0; i < nb; i++) {
      if (bx + badge_w(c, bd[i].txt) > x + w) {
        bx = x;
        y += S(26);
      }
      bx += badge(c, bx, y, bd[i].txt, bd[i].bg, bd[i].fg);
    }
    y += S(30);
  }
  if (mf->ntags) {
    float bx = x;
    for (int i = 0; i < mf->ntags; i++) {
      if (bx + badge_w(c, mf->tags[i]) > x + w) break;
      bx += badge(c, bx, y, mf->tags[i], uid_alpha(t->text_secondary, 0.10f), t->text_tertiary);
    }
    y += S(28);
  }
  y += S(6);
  y += para(c, x, y, w, UI_TYPE_LABEL, t->text_primary, mf->description && *mf->description ? mf->description : "No description.", 9) + S(6);

  // The main action belongs with the mod's identity and explanation, before
  // technical details. Use the solid green primary style so it stands out.
  UiRect configure = ui_rect(x, y + S(6), w, S(48));
  s->log_configure = configure;
  if (ui_button(c, ui_id("mods.configure"), configure, "Configure mod", UI_ICON_GEAR, UI_BTN_PRIMARY, 0)) {
    s->configure = 1;
    *ui_state(c, sid) = 0;
    log_act("configure %s opened", mf->id);
  }
  y += S(68);

  // dependencies
  if (mf->ndeps || mf->nopt) {
    section(c, x, &y, w, "Needs");
    for (int i = 0; i < mf->ndeps; i++) {
      int bad;
      const char *st = dep_state(m, mf->deps[i].id, &bad);
      int di = modmgr_find(m, mf->deps[i].id);
      char l[200];
      snprintf(l, sizeof l, "%s %s", di >= 0 ? modmgr_manifest(m, di)->name : mf->deps[i].id, mf->deps[i].range);
      ui_text_fit(c, ui_rect(x, y, w - S(120), S(22)), UI_TYPE_CAPTION, t->text_secondary, l, 0);
      ui_text_in(c, ui_rect(x, y, w, S(22)), UI_TYPE_CAPTION, bad ? t->danger : t->accent_soft_text, st, 1);
      y += S(24);
    }
    for (int i = 0; i < mf->nopt; i++) {
      int bad;
      const char *st = dep_state(m, mf->opt[i].id, &bad);
      char l[200];
      snprintf(l, sizeof l, "%s %s (optional)", mf->opt[i].id, mf->opt[i].range);
      ui_text_fit(c, ui_rect(x, y, w - S(120), S(22)), UI_TYPE_CAPTION, t->text_secondary, l, 0);
      ui_text_in(c, ui_rect(x, y, w, S(22)), UI_TYPE_CAPTION, t->text_tertiary, bad ? "not used" : st, 1);
      y += S(24);
    }
  }
  if (mf->nincompat) {
    section(c, x, &y, w, "Does not work with");
    for (int i = 0; i < mf->nincompat; i++) {
      int di = modmgr_find(m, mf->incompat[i].id);
      char l[200];
      snprintf(l, sizeof l, "%s %s", di >= 0 ? modmgr_manifest(m, di)->name : mf->incompat[i].id, mf->incompat[i].range);
      int both = di >= 0 && modmgr_info(m, di)->load_pos >= 0 && f->load_pos >= 0;
      ui_text_fit(c, ui_rect(x, y, w - S(100), S(22)), UI_TYPE_CAPTION, t->text_secondary, l, 0);
      ui_text_in(c, ui_rect(x, y, w, S(22)), UI_TYPE_CAPTION, both ? t->danger : t->text_tertiary, both ? "both on" : di >= 0 ? "installed" : "not installed", 1);
      y += S(24);
    }
  }
  // conflicts, from the resolver
  {
    char cl[6][320];
    int nc = modmgr_conflict_lines(m, mi, cl, 6);
    if (nc) {
      section(c, x, &y, w, "Conflicts");
      for (int i = 0; i < nc; i++) y += para(c, x, y, w, UI_TYPE_CAPTION, uid_mix(warn_col(t), t->text_primary, 0.5f), cl[i], 5) + S(6);
    }
  }
  // what it changes
  if (mf->ntouches) {
    section(c, x, &y, w, "What it changes");
    int shown = 0;
    for (int i = 0; i < mf->ntouches && shown < 8; i++, shown++) {
      char l[240];
      snprintf(l, sizeof l, "%s", mf->touches[i].key);
      ui_text_fit(c, ui_rect(x, y, w - S(150), S(22)), UI_TYPE_CAPTION, t->text_secondary, l, 0);
      char r[64];
      snprintf(r, sizeof r, "%s  %s", mods_kind_name(mf->touches[i].kind), mods_op_name(mf->touches[i].op));
      ui_text_in(c, ui_rect(x, y, w, S(22)), UI_TYPE_CAPTION, t->text_tertiary, r, 1);
      y += S(24);
    }
    if (mf->ntouches > shown) {
      char l[60];
      snprintf(l, sizeof l, "and %d more", mf->ntouches - shown);
      ui_text_in(c, ui_rect(x, y, w, S(22)), UI_TYPE_CAPTION, t->text_tertiary, l, 0);
      y += S(24);
    }
  } else if (!mf->error) {
    section(c, x, &y, w, "What it changes");
    y += para(c, x, y, w, UI_TYPE_CAPTION, t->text_tertiary, "It does not list what it touches, so conflicts can only show once the game runs.", 3) + S(4);
  }
  // notes from the resolver
  {
    ModNote nt[8];
    int nn = modmgr_notes(m, mi, nt, 8);
    if (nn) {
      section(c, x, &y, w, "Notes");
      for (int i = 0; i < nn; i++) {
        UiColor col = nt[i].level == MD_ERROR ? t->danger : nt[i].level == MD_WARN ? uid_mix(warn_col(t), t->text_primary, 0.5f) : t->text_secondary;
        y += para(c, x, y, w, UI_TYPE_CAPTION, col, nt[i].msg, 4) + S(6);
      }
    }
  }
  // folder and links
  section(c, x, &y, w, "Folder and links");
  float bx = x;
  {
    UiRect b = ui_rect(bx, y, S(170), S(40));
    if (ui_button(c, ui_id("mods.folder"), b, "Open folder", UI_ICON_FOLDER, UI_BTN_PILL, UI_COMPACT) && mf->dir) {
      snprintf(s->open_path, sizeof s->open_path, "%s", mf->dir);
      log_act("open folder %s", mf->dir);
    }
    bx += b.w + S(10);
  }
  const char *lname[5], *lurl[5];
  int nl = 0;
  if (mf->homepage) lname[nl] = "Homepage", lurl[nl++] = mf->homepage;
  for (int i = 0; i < mf->nlinks && nl < 5; i++) lname[nl] = mf->link_names[i], lurl[nl++] = mf->link_urls[i];
  for (int i = 0; i < nl; i++) {
    float bw = ui_text_width(c, UI_TYPE_LABEL, lname[i]) + S(60);
    if (bx + bw > x + w) bx = x, y += S(48);
    UiRect b = ui_rect(bx, y, bw, S(40));
    if (ui_button(c, ui_idi(ui_id("mods.link"), i), b, lname[i], UI_ICON_INFO, UI_BTN_PILL, UI_COMPACT)) {
      snprintf(s->open_url, sizeof s->open_url, "%s", lurl[i]);
      log_act("open link %s", lurl[i]);
    }
    bx += bw + S(10);
  }
  y += S(52);
  *hstate = (y - y0) + S(10);
  ui_vscroll_end(c);
}

// ------------------------------------------------------------------ the profile menu and dialogs

static void draw_profile_menu(UiCtx *c, ModsUi *s, UiRect anchor) {
  const UiTokens *t = ui_tokens(c);
  ModMgr *m = s->m;
  UiRect scr = ui_screen(c);
  uint32_t sc = ui_id("mods.dd.scrim");
  ui_hit(c, scr, 0, sc, UI_HIT_BLOCK | UI_HIT_INTERACT);
  if (ui_clicked(c, sc)) {
    s->dd_open = 0;
    log_act("profile menu closed by a click outside");
    return;
  }
  int n = modmgr_profile_count(m);
  float rh = S(56), pad = S(8);
  UiRect pr = ui_rect(anchor.x, anchor.y + anchor.h + S(8), S(420), n * rh + 2 * pad);
  ui_panel(c, pr, S(t->radius_card), UI_PANEL_STRONG);
  for (int k = 0; k < n; k++) {
    const char *name = modmgr_profile_name(m, k);
    UiRect rr = ui_rect(pr.x + pad, pr.y + pad + k * rh, pr.w - 2 * pad, rh);
    int cur = !strcmp(name, modmgr_profile(m));
    char right[40] = "";
    char line[80];
    if (k == 0) snprintf(line, sizeof line, "Original game, read only");
    else {
      const char *saved = modmgr_profile(m);
      (void)saved;
      snprintf(line, sizeof line, "Your own set of mods");
    }
    uint32_t id = ui_idi(ui_id("mods.dd.row"), k);
    ui_hit(c, rr, S(t->radius_tile), id, UI_HIT_BLOCK | UI_HIT_INTERACT);
    if (cur) uid_rrect(ui_drawer(c), rr, S(t->radius_tile), t->accent_soft);
    else if (ui_is_hot(c, id)) uid_rrect(ui_drawer(c), rr, S(t->radius_tile), uid_alpha(t->surface_inset, 1));
    ui_text_fit(c, ui_rect(rr.x + S(16), rr.y + S(8), rr.w - S(70), S(22)), UI_TYPE_BODY, t->text_primary, name, 0);
    ui_text_fit(c, ui_rect(rr.x + S(16), rr.y + S(30), rr.w - S(70), S(18)), UI_TYPE_CAPTION, t->text_secondary, line, 0);
    if (k == 0) ui_icon(c, UI_ICON_LOCK, ui_rect(rr.x + rr.w - S(44), rr.y, S(32), rr.h), S(18), t->icon_muted);
    else if (cur) ui_icon(c, UI_ICON_CHECK, ui_rect(rr.x + rr.w - S(44), rr.y, S(32), rr.h), S(18), t->accent_soft_text);
    (void)right;
    if (ui_clicked(c, id)) {
      modmgr_profile_use(m, name);
      log_act("profile %s selected", name);
      s->dd_open = 0;
      s->selected = s->selected >= modmgr_count(m) ? -1 : s->selected;
    }
  }
}

static void draw_dialog(UiCtx *c, ModsUi *s, double now) {
  const UiTokens *t = ui_tokens(c);
  const char *title = s->dialog == MUI_DLG_NEW ? "New profile" : s->dialog == MUI_DLG_RENAME ? "Rename profile" :
                      s->dialog == MUI_DLG_DELETE ? "Delete profile" : "Apply your changes?";
  float dh = s->dialog == MUI_DLG_NEW ? 360 : s->dialog == MUI_DLG_RENAME ? 290 : 250;
  UiRect r = ui_dialog_begin(c, ui_id("mods.dialog"), S(540), S(dh), title);
  UiRect bt = ui_rect(r.x, r.y + r.h - S(46), r.w, S(46));
  const char *ok_label = s->dialog == MUI_DLG_NEW ? "Create" : s->dialog == MUI_DLG_RENAME ? "Rename" :
                         s->dialog == MUI_DLG_DELETE ? "Delete" : "Apply and close";
  float y = r.y;
  if (s->dialog == MUI_DLG_NEW || s->dialog == MUI_DLG_RENAME) {
    ui_text_in(c, ui_rect(r.x, y, r.w, S(22)), UI_TYPE_CAPTION, t->text_secondary, "Name (letters, digits, - and _)", 0);
    y += S(28);
    UiRect fb = ui_rect(r.x, y, r.w, S(48));
    field_box(c, fb, s->field, "my_setup", 1, now);
    y += S(60);
    if (s->dialog == MUI_DLG_NEW) {
      UiRect row = ui_rect(r.x, y, r.w, S(44));
      ui_text_in(c, ui_rect(r.x, y, r.w - S(80), S(44)), UI_TYPE_BODY, t->text_primary, "Start with the mods that are on now", 0);
      ui_toggle(c, ui_id("mods.dlg.copy"), row, &s->dlg_copy);
      y += S(52);
    }
    if (s->dlg_err[0]) para(c, r.x, y, r.w, UI_TYPE_CAPTION, t->danger, s->dlg_err, 2);
  } else if (s->dialog == MUI_DLG_DELETE) {
    char l[200];
    snprintf(l, sizeof l, "Delete the profile \"%s\"? The mods themselves stay installed.", modmgr_profile(s->m));
    para(c, r.x, y, r.w, UI_TYPE_BODY, t->text_primary, l, 3);
  } else {
    para(c, r.x, y, r.w, UI_TYPE_BODY, t->text_primary,
         "You changed which mods load. Apply them to keep the changes, or discard them. Applied changes take effect after you restart the game.", 4);
  }
  float bw = s->dialog == MUI_DLG_CLOSE ? S(190) : S(150);
  UiRect okb = ui_rect(bt.x + bt.w - bw, bt.y, bw, bt.h);
  UiRect cancel = ui_rect(okb.x - S(12) - S(150), bt.y, S(150), bt.h);
  if (s->dialog == MUI_DLG_CLOSE) {
    UiRect disc = ui_rect(cancel.x - S(12) - S(130), bt.y, S(130), bt.h);
    if (ui_button(c, ui_id("mods.dlg.discard"), disc, "Discard", UI_ICON_TRASH, UI_BTN_PILL, 0)) {
      do_revert(s);
      s->dialog = MUI_DLG_NONE;
      s->req_close = 1;
    }
  }
  if (ui_button(c, ui_id("mods.dlg.cancel"), cancel, s->dialog == MUI_DLG_CLOSE ? "Keep editing" : "Cancel", UI_ICON_NONE, UI_BTN_PILL, 0)) {
    s->dialog = MUI_DLG_NONE;
    s->focus = 1;
  }
  if (ui_button(c, ui_id("mods.dlg.ok"), okb, ok_label, s->dialog == MUI_DLG_DELETE ? UI_ICON_TRASH : UI_ICON_CHECK, UI_BTN_PRIMARY, 0))
    dialog_confirm(s);
}

// ------------------------------------------------------------------ the page

void ui_mods_draw(UiCtx *c, ModsUi *s, double now) {
  s->log_nsettings = 0;
  s->log_configure = (UiRect){0};
  const UiTokens *t = ui_tokens(c);
  ModMgr *m = s->m;
  s->clock = now;
  UiRect scr = ui_screen(c);
  uid_rrect(ui_drawer(c), scr, 0, t->scrim);
  ui_hit(c, scr, 0, 0, UI_HIT_BLOCK | UI_HIT_MODAL);
  float pw = fminf(S(1400), scr.w - 2 * S(20)), ph = fminf(S(900), scr.h - 2 * S(20));
  UiRect p = ui_rect_round(ui_center(scr, pw, ph));
  ui_panel(c, p, S(t->radius_panel), UI_PANEL_STRONG);
  UiRect in = ui_inset_all(p, S(28));
  int in_dialog = s->dialog != MUI_DLG_NONE;

  // header
  int total = modmgr_count(m), loaded = 0;
  for (int i = 0; i < total; i++) loaded += modmgr_info(m, i)->load_pos >= 0;
  float hh = S(46);
  ui_text_in(c, ui_rect(in.x, in.y, S(300), hh), UI_TYPE_TITLE, t->text_primary, "Mods", 0);
  {
    char sub[160];
    snprintf(sub, sizeof sub, "%d of %d installed mods load with profile \"%s\"", loaded, total, modmgr_profile(m));
    float tw = ui_text_width(c, UI_TYPE_TITLE, "Mods");
    ui_text_fit(c, ui_rect(in.x + tw + S(18), in.y + S(4), in.w - tw - S(100), hh), UI_TYPE_CAPTION, t->text_secondary, sub, 0);
  }
  UiRect closer = ui_rect(in.x + in.w - hh, in.y, hh, hh);
  s->log_close = closer;
  if (ui_icon_button(c, ui_id("mods.close"), closer, UI_ICON_X, UI_ROUND, "Close (Esc)")) ui_mods_request_close(s);

  // profile bar
  float y = in.y + hh + S(14);
  float bh = S(44);
  ui_text_in(c, ui_rect(in.x, y, S(70), bh), UI_TYPE_CAPTION, t->text_secondary, "Profile", 0);
  UiRect pill = ui_rect(in.x + S(72), y, S(250), bh);
  s->log_profile = pill;
  if (ui_button(c, ui_id("mods.profile"), pill, modmgr_profile(m), UI_ICON_DOWN, UI_BTN_PILL, s->dd_open ? UI_SELECTED : 0)) {
    s->dd_open = !s->dd_open;
    log_act("profile menu %s", s->dd_open ? "opened" : "closed");
  }
  int ro = modmgr_profile_readonly(m, modmgr_profile(m));
  float x = pill.x + pill.w + S(12);
  UiRect rn = ui_rect(x, y, S(120), bh), rd = ui_rect(x + S(130), y, S(150), bh);
  s->log_new = rn;
  s->log_dup = rd;
  if (ui_button(c, ui_id("mods.new"), rn, "New", UI_ICON_PLUS, UI_BTN_PILL, UI_COMPACT)) open_dialog(s, MUI_DLG_NEW);
  if (ui_button(c, ui_id("mods.dup"), rd, "Duplicate", UI_ICON_REDO, UI_BTN_PILL, UI_COMPACT)) {
    char err[160];
    if (modmgr_profile_duplicate(m, err, sizeof err)) log_act("profile %s created (duplicate)", modmgr_profile(m));
  }
  UiRect rr = ui_rect(rd.x + rd.w + S(10), y, S(130), bh), rx = ui_rect(rr.x + rr.w + S(10), y, S(120), bh);
  s->log_ren = rr;
  s->log_del = rx;
  if (ui_button(c, ui_id("mods.ren"), rr, "Rename", UI_ICON_GEAR, UI_BTN_PILL, UI_COMPACT | (ro ? UI_DISABLED : 0))) open_dialog(s, MUI_DLG_RENAME);
  if (ui_button(c, ui_id("mods.del"), rx, "Delete", UI_ICON_TRASH, UI_BTN_PILL, UI_COMPACT | (ro ? UI_DISABLED : 0))) open_dialog(s, MUI_DLG_DELETE);
  if (ro) {
    float lx = rx.x + rx.w + S(16);
    ui_icon(c, UI_ICON_LOCK, ui_rect(lx, y, S(24), bh), S(16), t->icon_muted);
    ui_text_fit(c, ui_rect(lx + S(28), y, in.x + in.w - lx - S(28), bh), UI_TYPE_CAPTION, t->text_secondary, "The original game. Read only.", 0);
  }
  y += bh + S(12);

  // search and sort
  UiRect sb = ui_rect(in.x, y, S(340), bh);
  uint32_t sid = ui_id("mods.search");
  ui_hit(c, sb, S(t->radius_control), sid, UI_HIT_BLOCK | UI_HIT_INTERACT);
  if (ui_clicked(c, sid)) s->focus = 1;
  field_box(c, sb, s->view.search, "Search name, author or tag", s->focus == 1 && !in_dialog, now);
  if (s->view.search[0] && ui_icon_button(c, ui_id("mods.search.clear"), ui_rect(sb.x + sb.w - S(40), sb.y + S(4), S(36), S(36)), UI_ICON_X, UI_ROUND | UI_COMPACT, "Clear"))
    s->view.search[0] = 0;
  {
    static const char *const sort_labels[MM_SORT__COUNT] = {"Name", "Author", "Newest", "Load order", "Enabled first"};
    float lw = S(46);
    ui_text_in(c, ui_rect(sb.x + sb.w + S(24), y, lw, bh), UI_TYPE_CAPTION, t->text_secondary, "Sort", 0);
    float sw = ui_segmented_width(c, sort_labels, NULL, MM_SORT__COUNT);
    ui_segmented(c, ui_id("mods.sort"), ui_rect(sb.x + sb.w + S(24) + lw, y, sw, bh), sort_labels, NULL, MM_SORT__COUNT, &s->view.sort);
  }
  y += bh + S(10);

  // filter chips
  {
    float cx = in.x;
    ui_text_in(c, ui_rect(cx, y, S(60), S(34)), UI_TYPE_CAPTION, t->text_secondary, "Show", 0);
    cx += S(62);
    ModView *v = &s->view;
    int all = !v->only_enabled && !v->only_disabled && !v->only_conflicts;
    if (chip(c, ui_id("mods.chip.all"), &cx, y, "All", all)) v->only_enabled = v->only_disabled = v->only_conflicts = 0;
    if (chip(c, ui_id("mods.chip.on"), &cx, y, "Enabled", v->only_enabled)) {
      int was = v->only_enabled;
      v->only_enabled = !was, v->only_disabled = v->only_conflicts = 0;
    }
    if (chip(c, ui_id("mods.chip.off"), &cx, y, "Disabled", v->only_disabled)) {
      int was = v->only_disabled;
      v->only_disabled = !was, v->only_enabled = v->only_conflicts = 0;
    }
    if (chip(c, ui_id("mods.chip.cf"), &cx, y, "Conflicts", v->only_conflicts)) {
      int was = v->only_conflicts;
      v->only_conflicts = !was, v->only_enabled = v->only_disabled = 0;
    }
    char tags[12][24];
    int nt = modmgr_tags(m, tags, 12);
    if (nt) {
      cx += S(14);
      ui_divider_v(c, cx - S(8), y + S(4), y + S(30));
      ui_text_in(c, ui_rect(cx, y, S(60), S(34)), UI_TYPE_CAPTION, t->text_secondary, "Tags", 0);
      cx += S(52);
    }
    for (int i = 0; i < nt; i++) {
      int on = !strcmp(v->tag, tags[i]);
      if (cx + ui_text_width(c, UI_TYPE_LABEL, tags[i]) + S(34) > in.x + in.w) break;
      if (chip(c, ui_idi(ui_id("mods.chip.tag"), i), &cx, y, tags[i], on)) snprintf(v->tag, sizeof v->tag, "%s", on ? "" : tags[i]);
    }
  }
  y += S(34) + S(14);

  // footer
  float fh = S(48);
  UiRect foot = ui_rect(in.x, in.y + in.h - fh, in.w, fh);
  int dirty = modmgr_dirty(m);
  {
    float bwid = S(190);
    UiRect ap = ui_rect(foot.x + foot.w - bwid, foot.y, bwid, fh);
    UiRect rv = ui_rect(ap.x - S(12) - S(150), foot.y, S(150), fh);
    s->log_apply = ap;
    s->log_revert = rv;
    if (ui_button(c, ui_id("mods.revert"), rv, "Undo changes", UI_ICON_UNDO, UI_BTN_PILL, dirty ? 0 : UI_DISABLED)) do_revert(s);
    if (ui_button(c, ui_id("mods.apply"), ap, "Apply changes", UI_ICON_CHECK, UI_BTN_PRIMARY, dirty ? 0 : UI_DISABLED)) do_apply(s);
    // the line at the left: a message, the restart notice, unsaved changes, or how this works
    float nw = rv.x - foot.x - S(16);
    const char *line;
    UiColor col = t->text_secondary;
    int restart = modmgr_restart_needed(m);
    char buf[200];
    if (now < s->msg_until && s->msg[0]) line = s->msg;
    else if (restart && dirty) line = "Press Apply, then restart the game: these changes take effect after a restart.";
    else if (restart) line = "Restart the game for the saved changes to take effect.";
    else if (dirty) line = "You have changes that are not applied yet.";
    else if (s->configure) line = "Settings save automatically. Apply changes only saves the mod list and profiles.";
    else {
      snprintf(buf, sizeof buf, "%s", s->in_game ? "A game is running. Mod changes take effect after you restart the game." : "Changes are saved with Apply. Nothing changes until then.");
      line = buf;
    }
    if (restart) {
      UiRect pr = ui_rect(foot.x, foot.y + S(4), nw, fh - S(8));
      uid_rrect(ui_drawer(c), pr, pr.h / 2, t->accent_soft);
      ui_icon(c, UI_ICON_INFO, ui_rect(pr.x + S(8), pr.y, pr.h, pr.h), S(18), t->accent_soft_text);
      ui_text_fit(c, ui_rect(pr.x + pr.h + S(8), pr.y, pr.w - pr.h - S(20), pr.h), UI_TYPE_LABEL, t->accent_soft_text, line, 0);
    } else {
      if (dirty) col = t->text_primary;
      ui_text_fit(c, ui_rect(foot.x + S(4), foot.y, nw, fh), UI_TYPE_CAPTION, col, line, 0);
    }
  }

  // body: the list and the detail panel
  float body_y = y;
  UiRect body = ui_rect(in.x, body_y, in.w, foot.y - S(14) - body_y);
  float lw = floorf(body.w * 0.60f) - S(8);
  UiRect lr = ui_rect(body.x, body.y, lw, body.h);
  UiRect dr = ui_rect(body.x + lw + S(16), body.y, body.w - lw - S(16), body.h);
  draw_list(c, s, lr, now);
  draw_detail(c, s, dr, now);

  if (s->dd_open && !in_dialog) draw_profile_menu(c, s, pill);
  if (s->dialog) draw_dialog(c, s, now);
}
