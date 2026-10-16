// Frosted Mint dialogs (simgolf mod, task-27): popup menus, check lists, text
// entry and the other small modal screens of the original.
//
// Two halves that meet here (same idea as the front end, frontend_frosted.h):
//   dialogs_game.c      game thread: hooks the translator patches call
//                       (port/tools/translate/golf.dialogs.json). Reads what the
//                       original dialog shows into a DlgSnap and turns what the
//                       player chose into a click or key for the ORIGINAL
//                       dialog, so its handlers run.
//   dialogs_frosted.c   render thread: a native UI layer (hd_native.h) that
//                       draws the dialog (ui/ui_dialogs.c) over the picture and
//                       takes the pointer.
// Switch: ui.dialogs = frosted_mint | original (mods store; the built-in mod
// ui_dialogs sets frosted_mint). docs/ui/KIT.md.
#pragma once

#include <stdint.h>

#include "port/ui/ui_dialogs.h"

#ifdef __cplusplus
extern "C" {
#endif

// game thread
int dlg_game_active(void);               // original or native modal owns input until return
int dlg_entry_nav(int vk, int shift);    // game-thread input while text entry owns keyboard
int dlgf_enabled(void);                  // ui.dialogs=frosted_mint and the native layer is up
void dlgf_publish(const DlgSnap *s);     // the dialog the original is showing now
// Atomically publish a face snapshot and its matching page/preview images.
void dlgf_publish_faces(const DlgSnap *s,const DlgPixels *tiles,const DlgPixels *preview);
void dlgf_end(void);                     // the original dialog has returned
int dlgf_take_action(DlgAction *a);      // next choice on the native dialog, 1 if any
void dlgf_publish_art(int key, const uint32_t *argb, int w, int h);   // DLG_ART_* key; NULL clears
// any thread: a native dialog is up (the front end keeps its screen under it)
int dlgf_gap(void);      // test aid: a dialog is up in the game but the layer drew none this frame
void dlgf_early(void);   // task-106: register the layer when the presenter starts
int dlgf_active(void);

#ifdef __cplusplus
}
#endif
