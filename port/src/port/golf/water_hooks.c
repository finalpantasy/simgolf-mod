// Realistic water (task-78): golf's water pass looks its water sprites up
// through water_sprite (translator patch golf.water.json). While the
// presenter draws the water (display.water=hd), golf's pure water animation
// (ripples and ocean wave crests 0x233-0x235, waterfall sheets 0x237 0x238;
// spray 0x23b 0x23c stays, task-78c) comes back as no sprite (0) and the main loop's sprite
// queue calls (water_queue) skip it; FUN_0043d6f0 never returns 0 itself, so
// every other sprite is queued as before. Optional water.fish replaces only
// decorative rocks, coral, shoals and sand bars (0x22e-0x232). Bridges and
// placed objects retain their sprites, and all game state remains unchanged.
// The browser build has no water pass: golf always draws its own there.
#include "golf_protos.h"


#ifdef __EMSCRIPTEN__
int water_hide_golf_ripples(void) { return 0; }
int water_fish_enabled(void) { return 0; }
#endif

void *water_sprite(int id, int frame, int view) {
  if(water_fish_enabled() && id>=0x22e && id<=0x232)return 0;
  if (water_hide_golf_ripples() && ((id >= 0x233 && id <= 0x235) || id == 0x237 || id == 0x238))
    return 0;
  return FUN_0043d6f0(id, frame, view);
}

uint32_t water_queue(int x, int y, uint32_t depth, int w, int h, uint32_t sprite, int a, uint32_t b) {
  if (!sprite) return 0;
  return FUN_004628d0(x, y, depth, w, h, sprite, a, b);
}
