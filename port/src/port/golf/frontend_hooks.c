// Frosted Mint front end, game-thread hooks (simgolf mod, task-32).
//
// The start menu, the difficulty pick, the property screen, the load game and
// championship lists, the pro pick and the theme packs are modal loops of the
// original (FUN_0040f5c0's title menu, FUN_0043a400, FUN_0046f550,
// FUN_0043b610, FUN_0043a8c0, FUN_004725b0). The native versions are drawn by
// the presenter's UI layer (frontend_frosted.c, ui/ui_frontend.c) over the
// original picture; the original loops keep running, so every click on a native
// control becomes a click on the original control and the original handlers
// run. Translator patches (tools/translate/golf.frontend.json) call fe_frame
// once per frame of each loop, fe_base after its base picture is drawn and
// fe_context where a screen needs to know its caller.
//
// The native versions of the hooks are in port/frontend_game.c; the browser
// build gets the no-op versions below, which keep the original behaviour.
// With ui.frontend=original they are no-ops on native too.

#include "golf_protos.h"
#include "port/runtime.h"

#ifdef __EMSCRIPTEN__

void fe_frame(int screen) { (void)screen; }
void fe_base(int screen) { (void)screen; }
void fe_context(int n) { (void)n; }
int fe_text(int fn, int tag, const char *text, int x, int y, unsigned colour) {
  (void)tag;
  switch (fn) {
    case 0: return FUN_00404b70((char *)text, x, y, colour);
    case 1: return FUN_00404bc0((char *)text, x, y, colour);
    case 2: return FUN_004049d0((char *)text, x, y, colour);
    default: return FUN_00404ad0((char *)text, x, y, colour);
  }
}
void fe_world_loop(int on) { (void)on; }
void fe_world_tag(int tag, int state, int theme, int owned, int hover, int sprite) {
  (void)tag, (void)state, (void)theme, (void)owned, (void)hover, (void)sprite;
}
void fe_world_pin(int tag, int x, int y, int state) { (void)tag, (void)x, (void)y, (void)state; }
void fe_world_hover(int tag) { (void)tag; }
void fe_rect(int x, int y, int w, int h, unsigned colour) { FUN_00480b00((void *)0x519a60, x, y, w, h, colour); }
int fe_sprite3(void *self, int p1, int target, unsigned x, unsigned y, int p5) { return (int)FUN_00473f60(self, p1, target, x, y, p5); }
int fe_sprite_b(void *self, int target, int x, int y, int a, int b, int c, int d) { return FUN_00473cb0(self, target, x, y, a, b, c, d); }
void fe_list_vars(int count, int *scroll, int sel, int p1, int p2) { (void)count, (void)scroll, (void)sel, (void)p1, (void)p2; }
void fe_theme_vars(int n, const unsigned char *mask, int sel) { (void)n, (void)mask, (void)sel; }
void fe_loading_pic(void *sprite) { (void)sprite; }
void fe_loading(int pct, const char *quote) { (void)pct, (void)quote; }
void fe_loading_progress(int assets) { (void)assets; }
void fe_suspend(void) {}
void fe_world_end(void) {}
int fe_theme_key_ready(void) { return FUN_0045ae70(); }

#endif
