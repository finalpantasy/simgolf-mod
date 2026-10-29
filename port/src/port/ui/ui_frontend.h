// Frosted Mint front end (simgolf mod, task-32, group G1): the start menu, the
// difficulty pick, the property (world) screen, the load game and championship
// lists, the pro pick and the theme packs. Pure functions of one FeSnap, the
// snapshot the game thread takes of whichever original screen is running
// (frontend_game.c), drawn with the kit at the screen's resolution. docs/ui/KIT.md.
//
// The screens only show what the original screen draws and ask for what the
// player clicked (FeAction): the game thread turns each action into a click on
// the ORIGINAL control, so the original handlers, checks and messages run.
// Plain data and the kit: no game code.
#pragma once

#include <SDL.h>
#include <stdint.h>

#include "port/ui/ui_kit.h"
#include "port/ui/ui_updates.h"

#define FE_MAGIC 0x31454653u   // "SFE1"

enum { FE_NONE, FE_TITLE, FE_DIFFICULTY, FE_WORLD, FE_LOAD, FE_PRO, FE_THEME, FE_LOADING };

// art keys (game pictures read back with alpha, published by the game thread)
enum {
  FE_ART_LOGO,          // the logo oval of the title
  FE_ART_GLOBE,         // the golf-ball globe of the world screen (round, alpha)
  FE_ART_FACE,          // 4 difficulty faces (FE_ART_FACE + 0..3)
  FE_ART_EMBLEM = 6,    // 16 property emblems: golf's sprite index (theme * 4 + state)
  FE_ART_COURSE = 22,   // the emblem of the highlighted save
  FE_ART_PORTRAIT,      // the highlighted pro
  FE_ART_LOADING,       // the loading picture (task-69), an oval with a soft edge
  FE_ART_BADGE = 32,    // 16 painted property badges (task-109), FeTag.badge order
  FE_ART_BADGE_DIM = 48,// the same 16 greyed, for rows you cannot buy
  FE_ART_MAX = 64
};

// ------------------------------------------------------------------ the screens' data

enum { FE_TAGS = 16 };
typedef struct FeTag {
  int16_t ex, ey;        // emblem top-left in the original's 800x600 (hover and click target = centre)
  int16_t px, py;        // the pin on the globe, original 800x600 (top-left of golf's pin sprite)
  int8_t state;          // 0 too dear, 1 available, 2 already purchased
  int8_t theme;          // 0 parkland, 1 desert, 2 tropical, 3 links
  int8_t pin;            // golf's pin sprite variant (4 available, 5 too dear, 6/7 purchased, near the edge)
  int8_t art;            // FE_ART_EMBLEM slot or -1
  int8_t badge;          // painted badge 0..15 (San Diego .. Northeast) by property name, -1 unknown
  int32_t price;        // dollars, 0 unknown
  int32_t acres;
  char name[32];
  char feature[40];
  char info[48];         // golf's "N acres: $price" line
  char detail[3][72];    // golf's description, shown for the highlighted tag only
  int32_t n_detail;
} FeTag;

typedef struct FeWorld {
  int32_t n;
  FeTag tag[FE_TAGS];
  int32_t hover;         // the tag the original has highlighted (the pointer is parked on it), -1 none
  int32_t sandbox;       // unlimited money
  int32_t medallions;    // [ui] map_icons=medallions: drawn medallions instead of golf's emblems
  int32_t badges;        // painted property badges (default); [ui] map_badges=original keeps golf's emblems
  int32_t difficulty;    // 0..3, -1 unknown
  int32_t can_refresh;   // golf's refresh disc is on this screen
  char budget[32];
  char heading[64];      // golf's "Where will you build your golf course?"
  // the globe in the original's 800x600: centre and radius
  int16_t gcx, gcy, gr;
} FeWorld;

enum { FE_ROWS = 16, FE_HOLES = 18 };
typedef struct FeList {
  char title[64];        // golf's title ("Load Previous Game", "Select Championship Course", "Pick A Pro")
  int32_t n_rows;        // rows in view (up to 16)
  char row[FE_ROWS][112];
  int32_t sel;           // row in view, -1 none
  int32_t scroll, total; // first row in view, rows in all
  int32_t more;          // "(more...)" is drawn
  int32_t omitted;       // filenames too long for the original authoritative slots
  int32_t save_sort,sort_mode;
  char detail[FE_ROWS][48];
} FeList;

typedef struct FeLoad {
  FeList list;
  int32_t championship;  // 1: the list of championship courses
  int32_t has_info;      // the highlighted save's details are drawn
  char line1[96], line2[96], line3[96];   // course (theme), designer, record
  char holes[16], par[16], yards[16], cash[32];
  char stat_label[4][24], stat_value[4][16];
  int32_t n_holes;
  char hole_no[FE_HOLES][8], hole_par[FE_HOLES][8], hole_yards[FE_HOLES][12];
} FeLoad;

typedef struct FePro {
  FeList list;
  int32_t has_info;
  char name[64];         // the pro's own words under the portrait
  char saying[160];
  char saying_label[48];
  int32_t n_skills;
  char skill[12][40];    // golf's rows (trait or skill), name then value
  char skill_value[12][24];
} FePro;

enum { FE_THEMES = 16, FE_BEADS = 6 };
typedef struct FeTheme {
  int32_t n;
  char heading[64];
  char column[5][24];
  char name[FE_THEMES][40];
  uint8_t mask[FE_THEMES];   // bit k: the pack has content for bead k
  int32_t sel;               // golf's highlighted row, -1 none
} FeTheme;

// task-69: the loading screen (golf's FUN_00406250: picture, a designer's quote, a progress bar)
typedef struct FeLoading {
  float pct;            // asset progress, -1 golf shows no bar
  uint32_t epoch;        // a new load resets presentation interpolation
  char title[64];        // the property screen's "We're off to X!" line (new game), empty on a load
  char quote[256];       // golf's quote line, empty when it shows none
} FeLoading;

typedef struct FeSnap {
  uint32_t magic;
  uint32_t serial;
  int32_t screen;        // FE_*
  int32_t art_serial;
  int32_t championship;  // difficulty: the championship flow (no starting cash)
  int32_t in_game;       // the screen opened from a running course (Load game, the world map: task-27)
  char theme_name[40];   // title: the theme a new game uses
  int32_t wages[4];      // difficulty: how many times a month wages are paid (0 unknown)
  int32_t fatigue[4];    // difficulty: the mood a hole costs a golfer (1 to 4, 0 unknown)
  FeWorld world;
  FeLoad load;
  FePro pro;
  FeTheme theme;
  FeLoading loading;
  int32_t logo_full;     // title: the logo art already says "Remastered" (data/ui/logo_remastered.png, task-80)
  UpdState *upd;         // title: the Updates panel and the "is out" notice (task-80); NULL draws neither. Owned by the presenter.
} FeSnap;

// ------------------------------------------------------------------ what the player asked for

enum {
  FE_A_NONE,
  FE_A_TITLE,       // arg 0 continue, 1 new game, 2 theme, 3 championship, 4 sandbox, 5 exit
  FE_A_DIFFICULTY,  // arg 0..3
  FE_A_BACK,
  FE_A_TAG,         // world: build on tag arg (a click on it)
  FE_A_HOVER_TAG,   // world: highlight tag arg (the pointer moves onto it)
  FE_A_REFRESH,     // world: golf's refresh disc
  FE_A_ROW,         // list and theme screens: click row arg
  FE_A_OK,
  FE_A_DELETE,
  FE_A_SCROLL,      // list: arg rows down (negative up)
  FE_A_SAVE_SORT,   // saved games: 0 recent, 1 fun, 2 combined skill
  FE_A_OPTIONS,     // title: open the native Options page (presenter only, never reaches the original)
  FE_A_UPDATES,     // title: arg 0 panel opened, 1 Later, 2 Update now (presenter only, never reaches the original)
  FE_A_MODS         // title: open the native Mods screen (presenter only, task-22)
};
typedef struct FeAction {
  int kind;
  int arg;
} FeAction;

// Draws the screen s. art[key] are textures or NULL. Returns the number of
// actions queued (up to max). Safe area: a 1920x1080 design centred in the target.
int ui_frontend_draw(UiCtx *c, const FeSnap *s, SDL_Texture *const *art, FeAction *acts, int max);

// Where the pieces of the last drawn screen are, in target pixels (tests, golf.log).
typedef struct FeLayout {
  UiRect btn[8];         // title tiles (6) or the screen's main buttons
  UiRect back, ok, del;
  UiRect row[FE_ROWS + FE_TAGS];
  int row_tag[FE_TAGS], world_selected, world_state;
  UiRect upd_btn, upd_close, upd_row[8], upd_now, upd_later;   // task-80: the Updates button, panel and notice
  UiRect upd_view;       // read-only viewport geometry for input checks
  UiRect theme_view;     // the scrolling content-pack list, excluding its fixed footer
} FeLayout;
const FeLayout *ui_frontend_layout(void);

// The full-screen background of the front end (world.bg and two soft mint glows), painted before the
// kit frame so its panels frost it. Plain SDL calls on the current render target.
void ui_frontend_background(SDL_Renderer *r, const UiTokens *t, int w, int h);

// The pill shown over the last screen while the original builds or loads a course (after the draw call).
void ui_frontend_loading(UiCtx *c, const char *text);

// Starting cash line ("$100,000" or "Unlimited") for tests and the gallery.
void fe_money(char *out, int n, int dollars);
