// Frosted Mint dialogs, game-thread hooks (simgolf mod, task-27).
//
// The popup menu (FUN_0046d6e0: options, information, confirmations, the
// preferences check list) and the text entry (FUN_0045b2c0: save name, rename)
// are modal loops of the original. The native versions are drawn by the
// presenter's UI layer (dialogs_frosted.c, ui/ui_dialogs.c) over the picture;
// the original loops keep running, so every native choice becomes a click or key
// for the original dialog and its handlers run. Translator patches
// (port/tools/translate/golf.dialogs.json) call dlg_begin once at the start of
// a dialog, dlg_menu / dlg_entry once per frame and dlg_done on every return.
//
// The native versions are in port/dialogs_game.c; the browser build gets the
// no-op versions below, which keep the original behaviour. With
// ui.dialogs=original they are no-ops on native too.

#include "golf_protos.h"
#include "port/runtime.h"

#ifdef __EMSCRIPTEN__

void dlg_pre(void) {}
void dlg_begin(int kind) { (void)kind; }
int dlg_menu(int px, int py, int sel, int checks, unsigned mask, unsigned disabled, int left, int first_y, int n_items, int width, int height) {
  (void)px, (void)py, (void)sel, (void)checks, (void)mask, (void)disabled, (void)left, (void)first_y, (void)n_items, (void)width, (void)height;
  return 0;
}
int dlg_entry(const char *prompt, const char *text, int caret, int max_chars) {
  (void)prompt, (void)text, (void)caret, (void)max_chars;
  return 0;
}
void dlg_hint_pre(void) {}
void dlg_wait_begin(void) {}
int dlg_wait(void) { return 0; }
void dlg_hint_end(void) {}
void dlg_profile_tick(void) {}
int dlg_hire(int hover) {
  (void)hover;
  return 0;
}
unsigned dlg_done(unsigned v) { return v; }

#endif
