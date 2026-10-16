// Frosted Mint golfers in the world (simgolf mod, task-31, group G5): name
// tags under the golfers, thought bubbles above them and the golfer card as a
// popover beside the selected golfer. A pure function of one GwNote, the
// snapshot the game thread takes of a course frame (golfers_game.c), drawn with
// the kit at the screen's resolution. docs/ui/KIT.md.
//
// Positions in a GwNote are canvas pixels of the frame as the game drew it
// (golf's worldToScreen, which already holds the sub-tile camera's offset);
// the course layer the golfers are in may still be sliding (smooth scrolling),
// so the caller passes that slide too and everything here follows the golfers
// exactly. Plain data and the kit: no game code.
#pragma once

#include <SDL.h>
#include <stdint.h>

#include "port/ui/ui_kit.h"

#define GW_NOTE_MAGIC 0x31575047u   // "GPW1"
#define GW_TAGS 160
#define GW_BUBBLES 48
#define GW_ART_MAX 640
// art keys (game sprites read back with alpha, published by the game thread)
#define GW_ART_FACE 0         // 0..255: conversation face sprites (index in golf's face table)
#define GW_ART_MOOD 256       // 256 + mood (1..10): the mood smileys of the golfer strip
#define GW_ART_PORTRAIT 512   // the selected golfer's head

typedef struct GwTag {
  int16_t slot, selected;
  int32_t x, y;        // feet, canvas px
  int16_t mood;        // golf's mood value (a short, about -2..12); the people tab shows it as a bar
  int16_t pad;
  char name[40];
} GwTag;

typedef struct GwBubble {
  int16_t slot, tone;  // tone: -1 bad, 0 neutral, 1 good (golf's own colour of the line)
  int32_t x, y;        // where golf drew the line (centred on x), canvas px: just above the head
  int16_t kind;        // 0: a thought (icon is the golfer's mood), 1: speech with the speaker's face
  int16_t art;         // GW_ART_* key of the icon
  char text[120];
} GwBubble;

enum { GW_METERS = 5, GW_HOLES = 18 };
typedef struct GwCard {
  int32_t on, slot;
  int32_t layout;      // 0 the card, 1 the story page (a partner)
  int32_t ax, ay;      // the golfer's feet, canvas px (-1: not on screen)
  char name[48];       // the card's own title
  char line1[96];      // status and age
  char line2[200];     // traits
  char skills[160];    // what the golfer wants more of
  char quote[220];     // the golfer's line
  char label[GW_METERS][20];
  int16_t meter[GW_METERS];   // 0..1000, full = well
  int16_t warn[GW_METERS];    // golf's own red (low need)
  int16_t score[GW_HOLES];    // strokes, 0 = not played
  int8_t tone[GW_HOLES];      // -2 eagle or better, -1 under par, 0 par, 1 over, 2 double bogey or worse
  int16_t cur_hole;           // the hole in progress (1-based) or 0
  int16_t total;
  int16_t has_scores;
  int16_t has_story;          // golf offers the story page / comments
  char story[5][100];         // golf's comment lines under the card (and the story page): what the golfer said, newest first
  int8_t story_tone[5];       // -1 bad (golf's red), 0, 1 good (green)
  int32_t n_story;
  char btn[5][24];            // golf's own words for the buttons, by hit id
  int32_t portrait_serial;    // bumps when the head was re-read
} GwCard;

#define GW_POPOVERS 48
typedef struct GwPopover {
  int32_t x, y, fn;
  uint32_t colour;
  char text[512];
} GwPopover;

typedef struct GwNote {
  uint32_t magic;
  uint32_t serial;
  int32_t n_tags, n_bubbles;
  int32_t dock_mode;   // golf's dock mode (2: the people tab, tags show a mood bar)
  int32_t paused;
  int32_t zoom;        // golf's zoom (4 is the closest); a golfer is about 10 canvas px tall per step
  GwCard card;
  GwTag tags[GW_TAGS];
  GwBubble bubbles[GW_BUBBLES];
  int32_t n_popovers;
  GwPopover popovers[GW_POPOVERS];
} GwNote;

// How the names and thoughts are shown (task-60). Set by the render side from the
// mods store (ui.tags, ui.bubbles, ui.tag_style, ui.tag_scale, ui.tag_opacity).
enum { GW_TAGS_HOVER, GW_TAGS_ALWAYS, GW_TAGS_OFF };
enum { GW_BUB_BRIEF, GW_BUB_FULL, GW_BUB_ICONS, GW_BUB_OFF };
enum { GW_STYLE_PLAIN, GW_STYLE_PILL };
typedef struct GwOpts {
  int tags;            // hover: the hovered and selected golfer's name; always: every name; off: none
  int bubbles;         // brief: big thoughts as a bubble for a few seconds, minor ones as a face; full; icons; off
  int tag_style;       // names as plain outlined text (default) or as tight glass pills
  float tag_scale;     // size of names and bubbles against the kit's design (default 0.7)
  float tag_opacity;   // glass opacity against the design (default 0.85); the text stays solid
  int brief_ms;        // how long a brief bubble stays
  int show_all;        // the show-all-names key was pressed
  int have_mouse;
  float mx, my;        // pointer, target px
  int faces_original;  // the game's own mood faces in bubbles (ui.faces=original) instead of the Frosted Mint ones
  int still;           // no fades or timers: every item at its final state (gallery, tests)
  double now_ms;
} GwOpts;
void ui_golfers_opts_default(GwOpts *o);
void ui_golfers_set_opts(const GwOpts *o);
void ui_golfers_reset(void);   // forget remembered fades and bubble timers

enum { GW_ACT_BUTTON = 1, GW_ACT_CLOSE = 2 };
typedef struct GwAction {
  int kind;
  int id;       // golf's card hit id (0 profile, 1 move / eject, 2 snapshot, 3 story, 5 next chapter)
} GwAction;

// Draws the note. view_x/y and px map canvas pixels to the target
// (target = view + (canvas + slide) * px). art[key] are textures or NULL.
// The world part (tags, bubbles) goes under the HUD, the card over it, so they
// are two calls with their own kit frames; ui_golfers_draw does both in one.
// The card returns the number of actions queued (the clicks on its buttons).
void ui_golfers_draw_world(UiCtx *c, const GwNote *n, SDL_Texture *const *art, float view_x, float view_y, float px,
                           float slide_x, float slide_y);
// task-56: while a zoom glide places the course picture, the pinned points of the
// note (tags, bubbles, the card's anchor) are at s * p + (x, y) canvas px. (1, 0, 0) at rest.
void ui_golfers_set_zoom(float s, float x, float y);
int ui_golfers_draw_card(UiCtx *c, const GwNote *n, SDL_Texture *const *art, float view_x, float view_y, float px,
                         float slide_x, float slide_y, GwAction *acts, int max_acts);
int ui_golfers_draw(UiCtx *c, const GwNote *n, SDL_Texture *const *art, float view_x, float view_y, float px,
                    float slide_x, float slide_y, GwAction *acts, int max_acts);

// Where the last drawn card put its pieces, in target pixels (tests, the
// click-through check in the gallery, golf.log with SIMGOLF_GOLFERS_LOG).
typedef struct GwLayout {
  UiRect card, close;
  UiRect bubble_text;  // the last big bubble's text area (contrast measurements)
  UiRect btn[6];       // by golf's hit id; w == 0: not offered
} GwLayout;
const GwLayout *ui_golfers_layout(void);

// The words of a score tone, for tests and the card's legend.
const char *ui_golfers_tone_name(int tone);
