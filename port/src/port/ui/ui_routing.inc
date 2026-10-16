// Course overview: native structure over the original report's live content.
// Original text anchors come from showRoutingMapScreen (0x00456be0).
static void route_text(UiCtx *c, const RpSnap *s, int x, int y, UiRect r, int style, float align) {
  const RpItem *it = txt(s, x, y);
  if (!it) return;
  char text[RP_TEXT]; fix_text(text, sizeof text, it->s);
  ui_text_fit(c, r, style, ui_tokens(c)->text_primary, text, align);
}

static float route_paragraph(UiCtx *c, UiRect r, const char *text, int style, UiColor color) {
  char lines[WRAP_LINES][WRAP_LEN];
  int n = wrap(c, style, text, r.w, lines);
  float line = ui_font(c, style).line;
  for (int i = 0; i < n; i++)
    ui_text_fit(c, ui_rect(r.x, r.y + i * line, r.w, line), style, color, lines[i], 0);
  return n * line;
}

static int route_staff(UiCtx *c, const RpSnap *s, UiRect detail, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  int na = 0, count = s->ext[2], offset = s->ext[3];
  ui_text_in(c, ui_rect(detail.x, detail.y, detail.w, ui_s(c, 32)), UI_TYPE_HEADING, t->text_primary, "Staff activity", 0);
  char range[80];
  int end = offset + 8 < count ? offset + 8 : count;
  if (count) snprintf(range, sizeof range, "Staff %d-%d of %d", offset + 1, end, count);
  else snprintf(range, sizeof range, "No staff hired");
  ui_text_in(c, ui_rect(detail.x, detail.y + ui_s(c, 32), detail.w, ui_s(c, 24)), UI_TYPE_CAPTION, t->text_secondary, range, 0);
  float top = detail.y + ui_s(c, 64), bottom = detail.y + detail.h - ui_s(c, 52);
  float rh = (bottom - top) / 8;
  if (!count) route_paragraph(c, ui_rect(detail.x, top + ui_s(c, 16), detail.w, ui_s(c, 80)),
      "Hired staff and their recorded work will appear here.", UI_TYPE_BODY, t->text_secondary);
  for (int i = 0; i < 8; i++) {
    int x = i < 4 ? 41 : 415, y = 71 + (i % 4) * 45;
    const RpItem *name = txt(s, x, y), *hired = txt(s, x + 15, y + 15), *work = txt(s, x + 15, y + 30);
    if (!name) continue;
    UiRect row = ui_rect(detail.x, top + i * rh, detail.w, rh);
    if (!(i % 2)) uid_rrect(d, row, ui_s(c, 8), uid_alpha(t->surface_inset, .55f));
    float px = row.x + ui_s(c, 12), width = row.w - ui_s(c, 24);
    char text[RP_TEXT]; fix_text(text, sizeof text, name->s);
    ui_text_fit(c, ui_rect(px, row.y + ui_s(c, 4), width, ui_s(c, 26)), UI_TYPE_LABEL, t->text_primary, text, 0);
    if (hired) {
      fix_text(text, sizeof text, hired->s);
      ui_text_fit(c, ui_rect(px, row.y + ui_s(c, 29), width, ui_s(c, 20)), UI_TYPE_CAPTION, t->text_secondary, text, 0);
    }
    if (work) {
      fix_text(text, sizeof text, work->s);
      ui_text_fit(c, ui_rect(px, row.y + ui_s(c, 49), width, ui_s(c, 21)), UI_TYPE_CAPTION, t->accent_soft_text, text, 0);
    }
  }
  float gap = ui_s(c, 12), bw = (detail.w - gap) / 2;
  for (int i = 0; i < 2; i++) {
    UiRect r = ui_rect(detail.x + i * (bw + gap), detail.y + detail.h - ui_s(c, 40), bw, ui_s(c, 40));
    pf_ctl(i ? "next" : "previous", r);
    int disabled = i ? offset + 8 >= count : offset <= 0;
    if (ui_button(c, ui_id(i ? "routing.next" : "routing.previous"), r, i ? "Scroll down" : "Scroll up",
                  i ? UI_ICON_DOWN : UI_ICON_UP, UI_BTN_PILL, disabled ? UI_DISABLED : 0) && na < max)
      acts[na++] = (RpAction){RP_A_CLICK, CLICK(780, i ? 220 : 80)};
  }
  return na;
}

static int route_holes(UiCtx *c, const RpSnap *s, UiRect detail, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  int na = 0;
  ui_text_in(c, ui_rect(detail.x, detail.y, detail.w, ui_s(c, 32)), UI_TYPE_HEADING, t->text_primary, "Hole routing", 0);
  float head = detail.y + ui_s(c, 44), rh = ui_s(c, 29);
  static const char *labels[4] = {"Hole", "Par", "Yards", "Time"};
  static const float start[4] = {0, .26f, .48f, .76f}, width[4] = {.23f, .19f, .25f, .24f};
  for (int j = 0; j < 4; j++)
    ui_text_in(c, ui_rect(detail.x + start[j] * detail.w, head, width[j] * detail.w, ui_s(c, 28)), UI_TYPE_CAPTION, t->text_secondary, labels[j], j ? 1 : 0);
  head += ui_s(c, 32);
  for (int i = 0; i < 18; i++) {
    UiRect row = ui_rect(detail.x, head + i * rh, detail.w, rh);
    int selected = s->ext[1] == i + 1;
    if (selected || !(i % 2)) uid_rrect(d, row, ui_s(c, 6), selected ? t->accent_soft : uid_alpha(t->surface_inset, .45f));
    char key[32]; snprintf(key, sizeof key, "routing.hole%d", i + 1);
    uint32_t id = ui_id(key);
    ui_hit(c, row, ui_s(c, 6), id, UI_HIT_BLOCK | UI_HIT_INTERACT);
    snprintf(key, sizeof key, "hole%d", i + 1); pf_ctl(key, row);
    const int sx[4] = {71, 132, 182, 239};
    int y = 104 + (i % 9) * 17, dx = i < 9 ? 0 : 498;
    for (int j = 0; j < 4; j++)
      route_text(c, s, sx[j] + dx, y, ui_rect(row.x + start[j] * row.w + ui_s(c, j ? 0 : 10), row.y,
                 width[j] * row.w - ui_s(c, 10), row.h), UI_TYPE_LABEL, j ? 1 : 0);
    if (ui_clicked(c, id) && na < max) acts[na++] = (RpAction){RP_A_CLICK, CLICK(i < 9 ? 71 : 569, y)};
    else if (ui_right_clicked(c, id) && na < max) acts[na++] = (RpAction){RP_A_RCLICK, CLICK(i < 9 ? 71 : 569, y)};
  }
  float y = head + 18 * rh + ui_s(c, 16);
  route_paragraph(c, ui_rect(detail.x, y, detail.w, ui_s(c, 64)),
      "Left click to select a hole. Right click another hole to swap their order.", UI_TYPE_CAPTION, t->text_secondary);
  return na;
}

static void route_heat(UiCtx *c, const RpSnap *s, UiRect detail) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  int home = s->ext[0] == 2;
  ui_text_in(c, ui_rect(detail.x, detail.y, detail.w, ui_s(c, 32)), UI_TYPE_HEADING, t->text_primary, home ? "Home values" : "Course aura", 0);
  float y = detail.y + ui_s(c, 52);
  if (!home) {
    // Join original wrapped lines before wrapping to the native column width.
    char text[RP_TEXT * 4] = "";
    for (int i = 0; i < s->n; i++) {
      const RpItem *it = &s->it[i];
      if (it->type != RP_IT_TEXT || it->x != 250 || it->y < 110 || it->y > 170) continue;
      char line[RP_TEXT]; fix_text(line, sizeof line, it->s);
      size_t n = strlen(text);
      snprintf(text + n, sizeof text - n, "%s%s", n ? " " : "", line);
    }
    y += route_paragraph(c, ui_rect(detail.x, y, detail.w, ui_s(c, 160)), text, UI_TYPE_BODY, t->text_secondary);
  } else {
    for (int group = 0; group < 2; group++) {
      int x = group ? 494 : 51;
      route_text(c, s, x, 86, ui_rect(detail.x, y, detail.w, ui_s(c, 28)), UI_TYPE_LABEL, 0);
      y += ui_s(c, 36);
      const int sy[4] = {104,121,138,156};
      for (int i = 0; i < 4; i++) {
        const RpItem *it = txt(s, x, sy[i]);
        if (!it) continue;
        char text[RP_TEXT]; fix_text(text, sizeof text, it->s);
        uid_rrect(d, ui_rect(detail.x, y + ui_s(c, 8), ui_s(c, 4), ui_s(c, 4)), ui_s(c, 2), group ? t->danger : t->accent);
        y += route_paragraph(c, ui_rect(detail.x + ui_s(c, 16), y, detail.w - ui_s(c, 16), ui_s(c, 64)), text, UI_TYPE_CAPTION, t->text_secondary) + ui_s(c, 8);
      }
      y += ui_s(c, 20);
    }
  }
  y += ui_s(c, 28);
  ui_text_in(c, ui_rect(detail.x, y, detail.w, ui_s(c, 28)), UI_TYPE_LABEL, t->text_primary, "Map key", 0);
  y += ui_s(c, 36);
  UiColor swatch[2];
  if (home) { swatch[0] = (UiColor){222,225,195,255}; swatch[1] = (UiColor){58,151,108,255}; }
  else {
    swatch[0] = uid_mix((UiColor){169,195,165,255}, (UiColor){255,0,123,255}, .55f);
    swatch[1] = uid_mix((UiColor){169,195,165,255}, (UiColor){0,255,123,255}, .55f);
  }
  for (int i = 0; i < 2; i++) {
    uid_rrect(d, ui_rect(detail.x, y, ui_s(c, 24), ui_s(c, 24)), ui_s(c, 6), swatch[i]);
    route_text(c, s, home ? (i ? 631 : 142) : (i ? 624 : 136), 206,
        ui_rect(detail.x + ui_s(c, 36), y, detail.w - ui_s(c, 36), ui_s(c, 24)), UI_TYPE_BODY, 0);
    y += ui_s(c, 36);
  }
}

static int draw_routing(UiCtx *c, const RpSnap *s, SDL_Texture *const *art, RpAction *acts, int max) {
  const UiTokens *t = ui_tokens(c);
  UiDraw *d = ui_drawer(c);
  UiRect scr = ui_screen(c);
  float margin = ui_s(c, t->edge_margin);
  float fit = fminf(1, fminf((scr.w - 2 * margin) / ui_s(c, 1440), (scr.h - 2 * margin) / ui_s(c, 920)));
  if (fit < 1) ui_push_scale(c, fit);
  float pad = ui_s(c, 28), gap = ui_s(c, 24);
  UiRect pr = ui_rect_round(ui_center(scr, ui_s(c, 1440), ui_s(c, 920))), in = ui_inset_all(pr, pad);
  scrim_and_panel(c, pr);
  memset(&g_pf_layout, 0, sizeof g_pf_layout); g_pf_layout.panel = pr;
  int na = 0;
  float cb = ui_s(c, 40);
  UiRect close = ui_rect(in.x + in.w - cb, in.y, cb, cb);
  g_pf_layout.close_x = close.x + cb / 2; g_pf_layout.close_y = close.y + cb / 2;
  if (ui_icon_button(c, ui_id("routing.close"), close, UI_ICON_X, 0, "Close (Esc)") && na < max) acts[na++] = (RpAction){RP_A_CLOSE, 0};
  ui_text_in(c, ui_rect(in.x, in.y, in.w - cb - gap, cb), UI_TYPE_TITLE, t->text_primary, "Course overview", 0);
  static const char *labels[4] = {"Routing", "Staff", "Aura", "Home values"};
  static const int xy[4][2] = {{220,290},{100,350},{575,290},{700,350}};
  float bw = (in.w - 3 * ui_s(c, 8)) / 4;
  for (int i = 0; i < 4; i++) {
    UiRect r = ui_rect(in.x + i * (bw + ui_s(c, 8)), in.y + ui_s(c, 52), bw, ui_s(c, 40));
    char key[32], ctl[14]; snprintf(key, sizeof key, "routing.mode%d", i); snprintf(ctl, sizeof ctl, "mode%d", i); pf_ctl(ctl, r);
    if (chip(c, ui_id(key), r, labels[i], s->ext[0] == i - 1, 0, UI_TYPE_LABEL) && na < max)
      acts[na++] = (RpAction){RP_A_CLICK, CLICK(xy[i][0], xy[i][1])};
  }
  UiRect body = ui_rect(in.x, in.y + ui_s(c, 116), in.w, in.h - ui_s(c, 152));
  float dw = ui_s(c, 512);
  UiRect map = ui_rect(body.x, body.y, body.w - dw - gap, body.h);
  UiRect detail = ui_rect(map.x + map.w + gap, body.y, dw, body.h);
  uid_rrect(d, map, ui_s(c, 16), uid_alpha(t->surface_inset, .40f));
  ui_text_in(c, ui_rect(map.x + ui_s(c, 20), map.y + ui_s(c, 16), map.w - ui_s(c, 40), ui_s(c, 28)), UI_TYPE_LABEL, t->text_secondary,
      s->ext[0] == 1 ? "Course aura" : s->ext[0] == 2 ? "Home site values" : "Course map", 0);
  ui_course_map_draw(c, &s->map, ui_rect(map.x, map.y + ui_s(c, 48), map.w, map.h - ui_s(c, 96)));
  ui_text_in(c, ui_rect(map.x + ui_s(c, 20), map.y + map.h - ui_s(c, 42), map.w - ui_s(c, 40), ui_s(c, 24)),
      UI_TYPE_CAPTION, t->text_secondary, "Numbered markers show the golf holes.", 0);
  (void)art;
  if (s->ext[0] == -1) na += route_holes(c, s, detail, acts + na, max - na);
  else if (s->ext[0] == 0) na += route_staff(c, s, detail, acts + na, max - na);
  else route_heat(c, s, detail);
  ui_text_in(c, ui_rect(in.x, in.y + in.h - ui_s(c, 24), in.w, ui_s(c, 24)), UI_TYPE_CAPTION, t->text_secondary,
      "Live course overview. Esc closes this screen.", 0);
  if (fit < 1) ui_pop_scale(c);
  return na;
}
