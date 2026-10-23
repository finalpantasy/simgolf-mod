// Frosted Mint front end (simgolf mod, task-32): the start menu, difficulty,
// property, load, championship and theme screens.
//
// Two halves that meet here:
//   frontend_game.c      game thread: the hooks the translator patches call
//                        (golf/frontend_hooks.c has the overview). Reads what
//                        the original screen shows into an FeSnap, reads the
//                        game's art, and turns what the player clicked into
//                        clicks on the ORIGINAL controls (scripted mouse input),
//                        so the original handlers, checks and messages run.
//   frontend_frosted.c   render thread: a native UI layer (hd_native.h) that
//                        covers the original screen's picture, draws the screen
//                        (ui/ui_frontend.c) and takes the pointer.
// Switch: ui.frontend = frosted_mint | original (mods store; the built-in mod
// ui_frontend sets frosted_mint). docs/ui/KIT.md.
#pragma once

#include <stdint.h>

#include "port/ui/ui_frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

// game thread
int fe_game_page_active(void);                         // original full-screen loop, independent of the UI skin
int fef_loading_enabled(void);                          // fef_enabled() and ui.loading is not original (task-69)
void fef_early(void);                                   // task-106: register the layer when the presenter starts
void fef_release_cover(void);                           // task-106: the card goes once golf has presented a few new frames (the closing credits)
int fef_enabled(void);                                  // ui.frontend=frosted_mint and the native layer is up
void fef_publish(const FeSnap *s);                      // the screen the original is showing now
void fef_loading_progress(float pct);                  // fine progress, no art work or game calls
int fef_take_action(FeAction *a);                       // next click on the native screen, 1 if any
int fef_publish_art(int key, const uint32_t *argb, int w, int h);   // 1 committed; (NULL,0,0) clears; 0 preserves prior art
void fef_set_injecting(int on);                         // the game thread is feeding the original a click
int fef_cover_screen(void);                             // test aid: the FE_* screen the layer covered the last frame with, -1 when it drew nothing
void fef_note_press(void);                              // ... and has just pressed the button (the original may leave its screen)

#ifdef __cplusplus
}
#endif
