// Frosted Mint golfers in the world (simgolf mod, task-31), the two halves.
//
//   golfers_game.c      game thread: the hooks the translator patches call
//                       (golf/golfers_hooks.c has the overview). Records the
//                       name tags, thought bubbles and golfer card the original
//                       was about to draw into a GwNote (ui/ui_golfers.h), hands
//                       it to the presenter with the frame it belongs to
//                       (hd_ui_note_set), reads the game's art, and turns clicks
//                       on the card's buttons into clicks on the ORIGINAL
//                       card's buttons.
//   golfers_frosted.c   render thread: a native UI layer (hd_native.h) that draws
//                       the note with the kit, pinned to the golfers (the course
//                       layer's smooth-scroll slide included), and routes the
//                       pointer over the card.
// Switch: ui.golfers = frosted_mint | original (mods store; the built-in mod
// ui_golfers sets frosted_mint). docs/ui/KIT.md.
#pragma once

#include <stdint.h>

#include "port/ui/ui_golfers.h"

#ifdef __cplusplus
extern "C" {
#endif

// game thread
int gwf_enabled(void);                                  // ui.golfers=frosted_mint and the world layer (tags, bubbles) is up
int gwf_card_enabled(void);                             // the same for the card layer, which goes in after the HUD's
void gwf_publish_art(int key, const uint32_t *argb, int w, int h);   // GW_ART_* key; NULL clears
int gwf_escape(int repeat);                            // visible card owns Escape; queue its original Close action
int gwf_take_action(GwAction *a);                       // next click on the card, 1 if any
void gwf_set_injecting(int on);                         // the game thread is feeding the original card a click

#ifdef __cplusplus
}
#endif
