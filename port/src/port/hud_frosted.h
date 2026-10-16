// Frosted Mint course HUD (simgolf mod, task-30 on the task-26 UI kit).
//
// Two halves that meet here:
//   hud_game.c         game thread: hides the old HUD (translator patches in
//                      FUN_0040f5c0; golf/hud_hooks.c has the overview, the
//                      news strip override and the browser build's no-ops),
//                      reads the game's state once per course frame into a
//                      HudSnap, snapshots the game's own tool art, and turns
//                      HUD actions into clicks on the ORIGINAL wheel and tray
//                      controls (the game's own handlers run).
//   hud_frosted.c      render thread: draws the HUD with the kit (port/ui) as a
//                      native UI layer of the presenter (hd_native.h,
//                      hd_ui_add_layer / hd_ui_add_input) and routes the pointer.
// Switch: ui.hud = frosted_mint | original (mods store; the built-in mod
// ui_hud sets frosted_mint). docs/ui/KIT.md, docs/ui/HUD_DISPATCH.md.
#pragma once

#include <stdint.h>
#include "port/shot_readout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HUD_ART_MAX 96

// Bottom of the original top-right money, fun, and skill plates in game-canvas pixels.
#define HUD_ORIGINAL_STATS_BOTTOM 142

enum {   // art slots (game sprites read back with alpha)
  HUD_ART_TERRAIN = 0,      // 16 terrain tray slots
  HUD_ART_AMENITY = 16,     // 8 amenities
  HUD_ART_BUILDING = 24,    // 9 building lots
  HUD_ART_ELEVATION = 33,   // 4 elevation tools
  HUD_ART_EMBLEM = 37,      // the course emblem
  // task-95: each amenity design as the original strip draws it (FUN_00432200), for the design chooser
  HUD_ART_BENCH = 38,       // 6 (the cut_content mod's lovers' bench is the sixth)
  HUD_ART_LANDMARK = 44,    // 16
  HUD_ART_FLOWER = 60,      // 15
  HUD_ART_TREE = 75,        // 7
  HUD_ART_BRIDGE = 82,      // 8, up to 89
};

// One game message (task-62): what the game's news strip said, kept for the HUD's history.
#define HUD_MSG_MAX 50
enum { HUD_SHOW_NONE = 0, HUD_SHOW_GOLFER = 1, HUD_SHOW_HOLE = 2, HUD_SHOW_BUILDING = 3, HUD_SHOW_STAFF = 4 };   // BUILDING (task-53): target is the building's record number
#define HUD_NEWS_TEXT 1024

typedef struct HudMsg {
  char text[HUD_NEWS_TEXT];
  char date[24];     // the game's date when it came
  double t_ms;       // host clock when it came (the HUD says "2 min ago")
  int seq;           // 1, 2, 3 ... in order of arrival
  int show;          // HUD_SHOW_*: the game can show what it is about
  int target;        // golfer slot, or hole number 1..18
} HudMsg;

#define HUD_GOLFER_MAX 152
#include "port/staff.h"
#define HUD_STAFF_MAX STAFF_CAPACITY
typedef struct HudSnap {
  int valid;
  uint32_t serial;
  int canvas_w, canvas_h;
  int visible;            // course view running, no dialog or info screen over it
  int capsule_visible;    // no golfer card / hole stats at the top
  int money;              // dollars
  int sandbox, cheat;
  int fun, skill_x100, golfers, waiting;
  char course[64], date[48];
  int dock_mode, amenities_on, elevation_on, employee_overlay;
  int terrain_tool, build_tool, elevation_tool;
  int paused, fast, zoom, rotation, theme, difficulty;
  double game_speed;      // player multiplier, shared with Options
  int speed_button_off;   // ui.speed_button=0 restores the previous transport controls
  int unlock, next_hole, building_level[9];
  int paths_available, paths_on; // cart path view control in the build toolbar
  int news_on;
  char news[HUD_NEWS_TEXT];
  uint32_t art_serial;    // bumps when hud_hooks re-read the art
  int elev_edit;          // flags & 2: an elevation tool is armed
  char terrain_name[16][24];
  int terrain_cost[16];   // dollars, -1 unknown
  char building_name[9][24];
  int building_cost[9];   // dollars for the next level, -1 unknown
  int people_mode;        // 2 golfers, 3/4 player, with employee_overlay
  int shape;              // the placement shape / rotation counter (Tab), 0x5a34f0
  int news_seq;           // task-62: seq of the message the news strip is showing now (0: none)
  int msg_n;              // history, oldest first, the last HUD_MSG_MAX messages
  HudMsg msgs[HUD_MSG_MAX];
  // task-92: the player's own pro (the original's player panel, FUN_004362f0 / FUN_00436060)
  int pro_ready;          // the pro's skills are set up (golf's FUN_0040f190 gate: skill mask or any skill)
  int pro_out;            // the pro is out on the course (0x5a59f8 != -1)
  int pro_practice;       // Practice Round can start (two holes or more, pro in, no event under way)
  int pro_match;          // a famous golfer's challenge is waiting (flags 0x2000, 0x4c2e14)
  int pro_tourney;        // the SGA offers a tournament (flags 0x400000)
  int pro_shots;          // the pro waits for a shot choice: 0 none, 1 straight only, 2 every shot
  int pro_shot;           // golf's shot choice 0x58f330: 0 straight, -1 fade, 1 draw, 3 high backspin, 4 low punch
  int pro_skill[10];      // Power Hitter .. Luck, 0..10 (or the [skills] cap)
  char pro_name[32];
  int shot_readout;
  char shot_text[SHOT_READOUT_LINES][SHOT_READOUT_TEXT];
  // task-96 / task-97: the design chooser for amenities and the clubhouse cue
  int design;             // 0x56a514: the design the armed tool will place (-1: none picked yet, landmarks)
  int design_n;           // designs the armed amenity offers (0: the tool has no choice); mod tables can widen it
  unsigned landmark_mask; // 0x543cfc: landmark designs the player has (bit per design)
  unsigned landmark_free; // 0x822c70: landmark designs that cost nothing
  char landmark_name[16][20];   // FUN_004074a0's short names, "Sundial"
  char landmark_perk[4][32];    // drawAmenitiesPanel: original benefit text, indexed by design & 3
  int clubhouse;          // placing the clubhouse (build tool 15, flag 0x800)
  int home_site_valid;    // the game's current Home Site preview is valid
  int home_site_value[4]; // dollars: lot value, site clear, site prep, profit
  // task-94: the People lists. Golfers: every golfer on the course (hole > 0), moods always, names while the
  // Golfers tab is open. Staff: every hired employee, in the order of golf's own table.
  int golfer_n;
  struct { short slot; signed char hole; signed char mood; char name[28]; } golfer[HUD_GOLFER_MAX];
  int mood_count[5];      // golfers per mood: 0 furious .. 4 delighted (golf's faces 1-2, 3-4, 5-6, 7-8, 9-10)
  int staff_n;
  int staff_limit;
  struct { short rec; signed char job; char name[28]; int wage; } staff[HUD_STAFF_MAX];   // job: 0..7 as HUD_JOB_NAMES
  int staff_sel;          // record of the selected employee, -1 none
  int staff_moving;       // record being moved (the next click on the course places them), -1 none
  int staff_fire_cost;    // dollars a Fire costs
  int weather_report, weather_kind, weather_night; // Optional current condition only.
  int weather_air_valid, weather_fahrenheit;
  double weather_c, weather_rh;
} HudSnap;

enum { HUD_ACT_CLICK = 1, HUD_ACT_KEY = 2, HUD_ACT_VKEY = 3, HUD_ACT_GOAL = 4,
       HUD_ACT_SHOW = 5,     // SHOW: x = HUD_SHOW_*, y = golfer slot or hole number (camera jump, as the game's own clicks and keys)
       HUD_ACT_PRO = 6,      // task-92: x = HUD_PRO_*
       HUD_ACT_DESIGN = 7,   // task-96: x = amenity build tool (1 bench, 2 flower, 4 landmark, 16 tree, 19 bridge), y = design
       HUD_ACT_STAFF = 8,   // task-94: x = HUD_STAFF_*, y = employee record (SELECT)
       HUD_ACT_PATH_VIEW = 9, // render-side cart path view toggle
       HUD_ACT_SPEED = 10 }; // x = player multiplier (1 through 4)
enum { HUD_STAFF_SELECT = 1, HUD_STAFF_FIRE = 2, HUD_STAFF_MOVE = 3, HUD_STAFF_RENAME = 4, HUD_STAFF_SCROLL = 5 };
enum { HUD_PRO_SKILLS = 1,   // the skill dialog: the first setup (10 points, 3 more per accomplishment) or the skills with OK
       HUD_PRO_PROFILE = 2,  // the pro's profile page (name, look, bio)
       HUD_PRO_CREATE = 3,   // profile, then the skill setup (the start of a new career)
       HUD_PRO_SCORECARD = 4 }; // the active pro's original live golfer card and round scores
enum {   // goals: reached over a few frames, one original click per frame
  HUD_GOAL_COURSE = 1, HUD_GOAL_EXTRAS, HUD_GOAL_BUILDINGS, HUD_GOAL_LANDSCAPE, HUD_GOAL_PEOPLE, HUD_GOAL_CLOSE
};
typedef struct {
  int kind;
  int x, y;      // CLICK (and GOAL's follow-up click): a point of the original 800x600 layout;
                 // GOAL only: x == -1 no follow-up, x <= -2 terrain tray slot -2 - x
  int goal;      // GOAL: HUD_GOAL_*
  int key;       // KEY: character for the 0x822b9c mailbox; VKEY: VK code for 0x822b98
} HudAction;

// render side (hud_frosted.c), called from the game thread
int hudf_history_visible(void);               // drawn history panel; does not change the HUD
int hudf_enabled(void);                       // ui.hud=frosted_mint and the native layer is up
void hudf_early(void);                        // register before the first course frame
int hudf_original_rotate_enabled(void);       // ui.hud=original and its optional Rotate pill is enabled
void hud_home_site_capture_value(int row, int dollars); // game thread: values from golf's placement calculation
void hudf_publish(const HudSnap *s);
void hudf_publish_weather(int show,int kind,int night,int air_valid,double c,double rh,int fahrenheit);
int hudf_take_action(HudAction *a);           // next queued action, 1 if any
void hudf_publish_art(int slot, const uint32_t *argb, int w, int h);   // NULL clears
void hudf_set_injecting(int on);              // the game thread is feeding it a click

// game side (hud_game.c), for the render side and tests
int hud_injecting(void);
int hud_course_transition(void);            // bounded, tagged original tray transition
void hud_course_frame_presented(void);      // a complete course frame after consumption ends the transition

#ifdef __cplusplus
}
#endif
