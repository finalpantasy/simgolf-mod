// Whether a game-world mouse-down should leave the Frosted Mint construction
// tray open. Input runs on the game thread, so this is evaluated from the last
// published HUD snapshot (under hud_frosted.c's snapshot mutex).
//
// The game's own placement rules are not changed. Only the renderer-side
// automatic tuck is suppressed while a tool is armed. This also covers the
// click that completes a green or places its flag, before the game has applied
// the new state. Explicit Escape and close actions remain unaffected.
#pragma once
#include "port/hud_frosted.h"

static inline int hudf_keep_tray_on_world_click(const HudSnap *s) {
  if (!s || !s->valid || !s->visible) return 0;
  if (s->dock_mode == 0) {
    // Course and Extras are different tabs of the same original game dock.
    return s->amenities_on ? s->build_tool >= 0 : s->terrain_tool >= 0;
  }
  if (s->dock_mode == 1) {
    // Buildings and Landscape likewise share a dock.
    return s->elevation_on ? (s->elev_edit || s->elevation_tool >= 0) : s->build_tool >= 0;
  }
  // Do not interfere with People, reports, normal course interactions or
  // the shot selector. Those require independent dismissal-state analysis.
  return 0;
}
