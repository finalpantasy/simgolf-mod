// Frosted Mint cart path help (simgolf mod, task-53), the two halves.
//
//   paths_game.c      game thread: once per course frame reads golf's own connectivity (the flood fill
//                     FUN_0042f340 marks every path and building tile the clubhouse reaches with 0x40 in
//                     the tile flags, and every building record that is connected with 0x40 at +7), turns
//                     it into a PvNote (ui/ui_paths.h) and hands it to the presenter on the second note
//                     channel (hd_ui_note2_set). It only reads: the rule itself is the original's.
//   paths_frosted.c   render thread: two native UI layers (hd_native.h): the world layer under the HUD (the
//                     path view's tint, the "No path" markers) and the top layer over it (the
//                     legend, the hover card), plus the key that toggles the view.
// Switch: ui.paths = frosted_mint | original (mods store; the built-in mod ui_paths sets frosted_mint).
// docs/ui/KIT.md.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int pvf_view_available(void); // configured path overlay is available to the HUD

// game thread
int pvf_enabled(void);        // ui.paths=frosted_mint and the world layer is up (registers it on first call)
int pvf_view_user(void);      // the player switched the path view on (button or key)
void pvf_set_view(int on);    // the same from the game side (the notification's "Show the building")

#ifdef __cplusplus
}
#endif
