#ifndef SIMGOLF_PLACEMENT_ANIMATION_H
#define SIMGOLF_PLACEMENT_ANIMATION_H

#include <stdint.h>
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif

// Surface_drawSprite FUN_00474440 forwards an untyped x86 argument word.
// Its backing jgld method FUN_10018920 interprets that word as a float.
// Numeric conversion makes every construction opacity below one invisible.
static inline uint32_t placement_animation_alpha(float opacity) {
#ifndef __EMSCRIPTEN__
  if (!mods_get_int("fixes.placement_animation", 1)) return (uint32_t)opacity;
#endif
  union { float f; uint32_t bits; } value = {opacity};
  return value.bits;
}

#endif
