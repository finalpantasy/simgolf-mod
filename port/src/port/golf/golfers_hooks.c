// Frosted Mint golfers in the world, game-thread hooks (simgolf mod, task-31).
//
// Name tags under the golfers, thought bubbles above them and the golfer card
// as a popover (docs/ui/KIT.md). The original draws all three into the course
// frame; the native versions are drawn by the presenter's UI layer at the
// screen's resolution (golfers_frosted.c, ui/ui_golfers.c). The game thread
// side (golfers_game.c) records what the original was about to draw: translator
// patches (tools/translate/golf.golfers.json) call the gw_* and gc_* hooks
// from golf's golfer draw (FUN_00462be0), the thought loop and the card call
// in the main loop (FUN_0040f5c0) and the card itself (FUN_0045c560). The
// original code keeps running (its logic, hit tests and button handlers are
// untouched); only the pixels it would have drawn are skipped or put back.
//
// The native versions of the hooks are in port/golfers_game.c; the browser
// build gets the no-op versions below, which keep the original behaviour.
// With ui.golfers=original they are no-ops on native too.

#include "golf_protos.h"
#include "port/runtime.h"

int gw_hide;   // 1 while the original thought bubble's draws are skipped

#ifdef __EMSCRIPTEN__

int gw_popover_active(void) { return 0; }
int gw_popover_text(int fn, const char *text, int x, int y, unsigned colour) {
  switch (fn & 7) {
    case 4:
    case 0: return FUN_00404b70((char *)text, x, y, colour);
    case 1: return FUN_00404bc0((char *)text, x, y, colour);
    case 2: return FUN_004049d0((char *)text, x, y, colour);
    default: return FUN_00404ad0((char *)text, x, y, colour);
  }
}

void gw_frame_begin(void) {}
void pv_frame_begin(void) {}   // task-53: cart path help is native only
void pv_course_done(void) {}
int pv_building_of_text(const char *text) { return (void)text, -1; }
void pv_show_building(int idx) { (void)idx; }
void gw_course_done(void) {}
void gw_seen(int slot, int x, int y) { (void)slot, (void)x, (void)y; }
int gw_name(int slot, const char *text, int x, int y, int mood) { return (void)slot, (void)text, (void)x, (void)y, (void)mood, 0; }
int gw_active(void) { return 0; }
void gw_thought_begin(int type, int state) { (void)type, (void)state; }
void gw_face(int idx) { (void)idx; }
void gw_thought_end(int slot, int x, int y, const char *text, unsigned colour) { (void)slot, (void)x, (void)y, (void)text, (void)colour; }
int gw_staff_name(int idx, const char *text, int x, int y) { return (void)idx, (void)text, (void)x, (void)y, 0; }
void gw_staff_begin(void) {}
void gw_staff_end(int idx, int x, int y, const char *text) { (void)idx, (void)x, (void)y, (void)text; }
void gc_begin(int slot) { (void)slot; }
void gc_end(int slot) { (void)slot; }
int gc_text(int fn, const char *text, int x, int y, unsigned colour) {
  switch (fn) {
    case 0: return FUN_00404b70((char *)text, x, y, colour);
    case 1: return FUN_00404bc0((char *)text, x, y, colour);
    case 2: return FUN_004049d0((char *)text, x, y, colour);
    default: return FUN_00404ad0((char *)text, x, y, colour);
  }
}
void gc_rect(int x, int y, int w, int h, unsigned colour) { FUN_00480b00((void *)0x519a60, x, y, w, h, colour); }
void gc_mouse(int *x, int *y) { (void)x, (void)y; }

#endif
