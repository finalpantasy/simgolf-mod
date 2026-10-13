#pragma clang diagnostic error "-Wimplicit-function-declaration"
// The white route of a hole (tester report F26, card task-119). Golf draws, for each of a hole's tees, the planned
// shots from the tee through the landing spots to the cup (FUN_0040f5c0, two FUN_004493d0 segments per shot, a
// black one row down and a white one). Each tee's whole route is moved sideways by -3, 0 or +3 canvas pixels so
// the routes of the back, middle and front tee do not lie on top of each other; that moves both ends off the tee
// marker's middle and off the cup at the foot of the flag (and at the far zoom a tee is only a few pixels wide).
// With mods.ini [fixes] hole_line_centre=1 (default) every route starts at its tee's middle and ends on the cup;
// 0 gives golf's offsets. Translator patch: tools/translate/golf.holeline.json.
#include "golf_protos.h"
#include "port/runtime.h"

#ifdef __EMSCRIPTEN__
int hole_line_shift(int tee) { return tee * 3 - 3; }
#else
int mods_get_int(const char *key, int def);
int hole_line_shift(int tee) { return mods_get_int("fixes.hole_line_centre", 1) ? 0 : tee * 3 - 3; }
#endif
