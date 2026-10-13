// Frosted Mint course HUD screen (task-30), built from the kit. Draws the top
// stats capsule, news toast, camera capsule, bottom dock and the open tray
// from a HudSnap, and returns what the player asked for as HudActions (which
// golf/hud_hooks.c turns into the original controls' clicks). No game code
// here: the gallery draws it from a made-up snapshot.
#pragma once

#include <SDL.h>

#include "port/hud_frosted.h"
#include "port/ui/ui_kit.h"

typedef struct {
  int course_tab;     // 0 Holes, 1 Fairway, 2 Hazards, 3 Trees (4 Extras follows amenities_on)
  int density;        // ui.hud_density: 0 regular, 1 compact (one bottom strip), 2 minimal,
                      // 3 compact_column (the strip's dock, the tray as a column on the right edge)
  int tray_hidden;    // compact and minimal: the tray tucked away after a click on the course or Esc
  int last_terrain;   // the course tab follows a tool picked by hotkey
  int chip_open;      // minimal: the stats chip is showing all stats
  int radial_open;    // collapsed corner: the categories are fanned out
  int bar_open;       // jwe (task-57): the build bar is out; 0 = collapsed to one small button (the default)
  int hidden;         // task-57: the whole HUD is hidden (the hide key), only a small "show" hint is drawn
  char hide_key[12];  // the hide key's name for that hint ("U"); empty: no key named
  int msg_open;       // task-62 (jwe): seq of the message shown in full (clicked), 0 = the strip's card as it comes
  int hist_open;      // the bell's message history is out
  int seen_seq;       // newest message the history has shown (the bell's dot)
  int last_news;      // news_seq of the last frame (a new message replaces the expanded one)
} UiHud;
#define UI_HUD_INIT {0, 0, 0, -1, 0, 0}
// density values past the three named ones: the hub variants the maintainer compares
// (ui.hud_density = wheel | fan | wheel_stats)
enum { HUD_REGULAR, HUD_COMPACT, HUD_MINIMAL, HUD_COMPACT_COLUMN, HUD_WHEEL, HUD_FAN, HUD_WHEEL_STATS,
       // round two, corner looks (ui.hud_density = card | quarter | lshape | collapsed | stock | card_right)
       HUD_CARD, HUD_QUARTER, HUD_LSHAPE, HUD_COLLAPSED, HUD_STOCK, HUD_CARD_RIGHT,
       // round three, full-width bars (ui.hud_density = bars | topbar | ribbon | bottombar | jwe)
       HUD_BARS, HUD_TOPBAR, HUD_RIBBON, HUD_BOTTOMBAR, HUD_JWE };

// Esc on the course (task-112): changes h and writes the actions it asks for; *what is the step taken.
int ui_hud_escape(UiHud *h, const HudSnap *s, HudAction *out, int max_out, const char **what);

// art: HUD_ART_MAX textures (NULL where none). Returns the number of actions.
int ui_hud_draw(UiCtx *c, UiHud *h, const HudSnap *s, SDL_Texture *const *art, HudAction *out, int max_out);

// Formats dollars as "$12,345" ("-$500" when negative).
void ui_hud_money(char *buf, int cap, int dollars);
