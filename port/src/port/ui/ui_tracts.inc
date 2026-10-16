// The land purchase map (FUN_004587a0, year end): included by ui_pages.c. Nine tract cards, the map picture and Cash Reserve,
// all read from what golf drew (reports_game.c recorded the tile texts with their places). A card clicks the tile's centre in
// golf's page, so golf's own hit test, its confirm question and the purchase run; "No thanks" clicks golf's check button.

typedef struct {
  int has;          // golf drew this tile
  int sold;         // "Already purchased."
  char text[RP_TEXT];
  char price[RP_TEXT];
} Tract;

// golf's tile k (0..8, numbered k + 1): column k / 3 and row k % 3 of the tile band
static const int TR_X[3] = {140, 400, 656};
static const int TR_Y[3] = {88, 156, 224};

static void tr_read(const RpSnap *s, Tract tr[9], char *cash, char *cash_label) {
  memset(tr, 0, 9 * sizeof *tr);
  for (int i = 0; i < s->n; i++) {
    const RpItem *it = &s->it[i];
    if (it->type != RP_IT_TEXT || !it->s[0] || it->y < 56 || it->y >= 262 || it->align == RP_AL_CENTRE) continue;
    int xa = it->align == RP_AL_RIGHT ? it->x - 0xa5 : it->x;   // a price ends at the tile's text column plus 0xa5
    int col = xa < 200 ? 0 : xa < 470 ? 1 : 2;
    int row = (it->y - 62) / 68;
    if (row < 0) row = 0;
    if (row > 2) row = 2;
    Tract *t = &tr[col * 3 + row];
    t->has = 1;
    if (it->align == RP_AL_RIGHT) fix_text(t->price, sizeof t->price, it->s);
    else if (!strncmp(it->s, "Already", 7)) t->sold = 1;
    else fix_text(t->text, sizeof t->text, it->s);
  }
  const RpItem *a = txt(s, 720, 264), *l = txt(s, 548, 263);
  fix_text(cash, RP_TEXT, a ? a->s : "");
  fix_text(cash_label, RP_TEXT, l ? l->s : "Cash Reserve");
}

#define TR_W 1440.0f
#define TR_H 920.0f

static int draw_tracts(UiCtx *c, const RpSnap *s, SDL_Texture *const *art, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  int na = 0;
  Tract tr[9]; char cash[RP_TEXT], cash_label[RP_TEXT];
  tr_read(s, tr, cash, cash_label);
  UiRect scr = ui_screen(c);
  float margin = ui_s(c, t->edge_margin);
  float k = fminf(1, fminf((scr.w - 2 * margin) / ui_s(c, TR_W), (scr.h - 2 * margin) / ui_s(c, TR_H)));
  if (k < 1) ui_push_scale(c, k);
  float pad = ui_s(c, 28), gap = ui_s(c, 24);
  UiRect pr = ui_rect_round(ui_center(scr, ui_s(c, TR_W), ui_s(c, TR_H))), in = ui_inset_all(pr, pad);
  scrim_and_panel(c, pr);
  memset(&g_pf_layout, 0, sizeof g_pf_layout); g_pf_layout.panel = pr;
  float cb = ui_s(c, 40);
  UiRect close = ui_rect(in.x + in.w - cb, in.y, cb, cb);
  if (ui_icon_button(c, ui_id("pages.close"), close, UI_ICON_X, 0, "Close (Esc)") && na < max) acts[na++] = (RpAction){RP_A_CLOSE, 0};
  g_pf_layout.close_x = close.x + cb / 2; g_pf_layout.close_y = close.y + cb / 2;
  ui_text_in(c, ui_rect(in.x, in.y, in.w - cb - gap, cb), UI_TYPE_TITLE, t->text_primary, "Tracts for sale", 0);
  // Cash and purchase choices share one column, away from the map.
  float dw = ui_s(c, 512), mw = in.w - dw - gap, top = in.y + ui_s(c, 64);
  UiRect map = ui_rect(in.x, top, mw, in.h - ui_s(c, 124));
  UiRect list = ui_rect(map.x + mw + gap, top, dw, map.h);
  uid_rrect(d, map, ui_s(c, 16), uid_alpha(t->surface_inset, .40f));
  ui_text_in(c, ui_rect(map.x + ui_s(c, 20), map.y + ui_s(c, 16), map.w - ui_s(c, 40), ui_s(c, 28)), UI_TYPE_LABEL, t->text_secondary, "Course map", 0);
  (void)art;
  ui_course_map_draw(c, &s->map, ui_rect(map.x, map.y + ui_s(c, 48), map.w, map.h - ui_s(c, 96)));
  ui_text_in(c, ui_rect(map.x + ui_s(c, 20), map.y + map.h - ui_s(c, 42), map.w - ui_s(c, 40), ui_s(c, 24)), UI_TYPE_CAPTION,
      t->text_secondary, "Numbered markers show the golf holes.", 0);
  ui_text_in(c, ui_rect(list.x, list.y, list.w * .48f, ui_s(c, 32)), UI_TYPE_LABEL, t->text_secondary, cash_label, 0);
  ui_text_fit(c, ui_rect(list.x + list.w * .48f, list.y, list.w * .52f, ui_s(c, 32)), UI_TYPE_HEADING, t->text_primary, cash, 1);
  float row_top = list.y + ui_s(c, 48), rh = (list.h - ui_s(c, 48)) / 9;
  for (int n = 0; n < 9; n++) {
    UiRect r = ui_rect(list.x, row_top + n * rh, list.w, rh);
    const Tract *tt = &tr[n];
    char key[24]; snprintf(key, sizeof key, "tr.tract%d", n);
    uint32_t id = ui_id(key);
    int can = tt->has && !tt->sold && tt->text[0];
    ui_hit(c, r, ui_s(c, 8), id, can ? UI_HIT_BLOCK | UI_HIT_INTERACT : UI_HIT_BLOCK);
    pf_ctl(key, r);
    float hov = ui_anim(c, id, can && ui_is_hot(c, id) ? 1 : 0, t->motion_hover);
    if (!(n % 2) || hov > 0) uid_rrect(d, r, ui_s(c, 8), uid_alpha(hov > 0 ? t->accent_soft : t->surface_inset, .55f + .35f * hov));
    if (can && ui_is_hot(c, id)) uid_rrect_stroke(d, r, ui_s(c, 8), ui_s(c, 1), t->accent);
    char num[12]; snprintf(num, sizeof num, "%d", n + 1);
    ui_text_in(c, ui_rect(r.x + ui_s(c, 4), r.y, ui_s(c, 32), r.h), UI_TYPE_LABEL, can ? t->accent_soft_text : t->text_secondary, num, .5f);
    float x = r.x + ui_s(c, 48), width = r.w - ui_s(c, 60);
    if (tt->sold || !tt->has) {
      ui_text_in(c, ui_rect(x, r.y, width, r.h), UI_TYPE_BODY, t->text_secondary, tt->sold ? "Already purchased" : "Unavailable", 0);
    } else {
      // Number is already in the first column. Preserve the original factual description.
      const char *description = strchr(tt->text, ',');
      description = description ? description + 1 : tt->text;
      while (*description == ' ') description++;
      float ty = r.y + ui_s(c, 6);
      char price[RP_TEXT]; snprintf(price, sizeof price, "%s", tt->price);
      const char *amount = !strncmp(price, "Price: ", 7) ? price + 7 : price;
      ui_text_fit(c, ui_rect(x, ty, width, ui_s(c, 22)), UI_TYPE_LABEL, t->accent_soft_text, amount, 0);
      char lines[WRAP_LINES][WRAP_LEN];
      int nl = wrap(c, UI_TYPE_CAPTION, description, width, lines);
      float line = ui_font(c, UI_TYPE_CAPTION).line;
      for (int j = 0; j < nl && j < 2; j++)
        ui_text_fit(c, ui_rect(x, ty + ui_s(c, 24) + j * line, width, line), UI_TYPE_CAPTION, t->text_secondary, lines[j], 0);
      ui_tooltip(c, id, r, tt->text);
    }
    if (can && ui_clicked(c, id) && na < max) acts[na++] = (RpAction){RP_A_CLICK, CLICK(TR_X[n / 3], TR_Y[n % 3])};
  }
  float fy = in.y + in.h - ui_s(c, 44);
  UiRect no = ui_rect(in.x + in.w - ui_s(c, 180), fy, ui_s(c, 180), ui_s(c, 44));
  pf_ctl("decline", no);
  if (ui_button(c, ui_id("tr.no"), no, "No thanks", UI_ICON_NONE, UI_BTN_PRIMARY, 0) && na < max)
    acts[na++] = (RpAction){RP_A_CLICK, CLICK(688, 560)};
  ui_text_in(c, ui_rect(in.x, fy, in.w - no.w - gap, no.h), UI_TYPE_CAPTION, t->text_secondary,
      "Choose a tract from the list. Esc leaves without buying.", 0);
  if (k < 1) ui_pop_scale(c);
  return na;
}
