// Frosted Mint reports (simgolf mod, task-28, UI-8): golf's report screens (course report, player
// comments, histograph, SGA evaluation, financial report, membership roster, keyboard shortcuts, best
// scores) drawn as one tabbed glass panel at the screen's resolution.
//
// Each report is still the original's own modal loop (draw once, then wait for a click or a key). With
// ui.reports=frosted_mint and the presenter up, translator patches (port/tools/translate/golf.reports.json)
// route the report's text calls through rp_t* (reports_game.c), which record every string with its place
// and colour and then draw it as before, and call rp_frame once per loop iteration: the game thread
// publishes an RpSnap, puts back the picture from before the report (golf's own board never shows) and
// turns native actions into what the original takes (a key closes it; a tab sets golf's screen state or
// calls the report's routine). The render thread (reports_frosted.c) draws ui_reports_draw over the
// frosted course and takes every real pointer event while a report is up.
#ifndef PORT_REPORTS_FROSTED_H
#define PORT_REPORTS_FROSTED_H

#include <stdint.h>
#include "port/reports_kinds.h"

int rp_game_active(void);

#include "port/ui/ui_kit.h"
#include "port/ui/ui_course_map.h"

enum { RP_AL_LEFT = 0, RP_AL_CENTRE = 1, RP_AL_RIGHT = 2 };

#define RP_MAGIC 0x52505331u
#define RP_ITEMS 520
#define RP_TEXT 96
#define RP_ART 32   // distinct golf sprites a report shows as art

enum { RP_IT_TEXT = 0, RP_IT_RECT, RP_IT_LINE, RP_IT_CHIP, RP_IT_PIC, RP_IT_GOAL };   // GOAL: next unearned accomplishment, in original order

typedef struct {
  int16_t x, y;     // golf's 800x600 page coordinates of the anchor (a line's first end)
  int16_t w, h;     // RP_IT_RECT / RP_IT_CHIP: size; RP_IT_LINE: the second end
  uint8_t type;     // RP_IT_*
  uint8_t align;    // RP_AL_*
  uint8_t big;      // text: drawn in golf's title font; RP_IT_CHIP: a flat highlight box of colour col
  uint8_t art;      // RP_IT_CHIP: 1 + the art slot (rpf_publish_art)
  uint8_t group;    // RP_PAIRING text: the golfer card index, 0xff for the screen heading
  uint16_t col;     // golf's 565 colour (text, rect, line; a chip's colour read back from the page)
  char s[RP_TEXT];
} RpItem;

typedef struct {
  uint32_t magic, serial;
  int kind;              // RP_*
  char title[64];        // the report's own title (first title-font text)
  int32_t ext[8];        // task-73 pages: kind specific numbers (golf's hover, a nested editor is open, ...)
  int32_t ranking_counts[10][5]; // RP_MONTH: counts of the original five pictograms in displayed row order
  char bio[512];         // RP_PROFILE: the golfer's bio text as it is in golf's buffer (live while it is edited)
  int n;
  RpItem it[RP_ITEMS];
  UiCourseMap map;
} RpSnap;

enum { RP_A_NONE, RP_A_CLOSE, RP_A_TAB, RP_A_CLICK, RP_A_RCLICK, RP_A_KEY, RP_A_PAIR_STORY };
typedef struct {
  int kind;   // RP_A_*
  int arg;    // RP_A_TAB: the report; RP_A_CLICK / RP_A_RCLICK: x | y << 16 in golf's page; RP_A_KEY: an SDL key code
} RpAction;

// ui/ui_reports.c: the panel as a pure function of a snapshot. Returns the number of actions written.
int ui_reports_draw(UiCtx *c, const RpSnap *s, SDL_Texture *const *art, RpAction *acts, int max);
// Where the last ui_reports_draw put its controls (target pixels; for the log and the tests).
typedef struct {
  UiRect panel;
  float close_x, close_y;
  int ntabs;
  float tab_x[8], tab_y;
  int nctl;   // the profile page's controls (chips, buttons): centre in target pixels, for the log and the tests
  char ctl_name[48][14];
  float ctl_x[48], ctl_y[48];
} RpLayout;
void ui_reports_layout(RpLayout *out);
void ui_pages_layout(RpLayout *out);   // the task-73 pages' controls (ui_pages.c)
// The original page rectangle the panel maps (for tests and the gallery).
void ui_reports_bounds(const RpSnap *s, float *x0, float *y0, float *x1, float *y1);

// reports_frosted.c (render thread side, thread safe)
int rpf_gap(void);                            // test aid: the last layer draw expected a report but drew none
int rpf_visible(void);                        // render-thread state: the report panel drew in this frame
void rpf_early(void);                         // task-106: register the layer when the presenter starts
int rpf_enabled(void);                        // ui.reports=frosted_mint, the layer is up and drawing
int rpf_enabled_kind(int kind);               // rpf_enabled, and for the task-73 pages (kind >= RP_PROFILE) ui.dialogs too
void rpf_publish(const RpSnap *s);            // game thread, once per frame of the report
void rpf_end(void);                           // the report closed
int rpf_take_action(RpAction *a);             // game thread: next native action, 1 if any
int rpf_publish_art(int slot, const uint32_t *argb, int w, int h);   // 1 committed; (NULL,0,0) clears; 0 preserves prior art
void rp_route_selected(int hole, int staff_count, int staff_offset); // original overview state before presenting

#endif
