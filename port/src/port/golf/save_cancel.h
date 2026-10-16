#ifndef SIMGOLF_SAVE_CANCEL_H
#define SIMGOLF_SAVE_CANCEL_H
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif
// textInputBox FUN_0045b2c0 returns the virtual key that ended editing.
// Cancel is not an invalid submitted name; an empty accepted name still is.
static inline int save_cancel_quiet(int key) {
#ifdef __EMSCRIPTEN__
  return key == 0x1b;
#else
  return key == 0x1b && mods_get_int("fixes.save_cancel", 1);
#endif
}
#endif
