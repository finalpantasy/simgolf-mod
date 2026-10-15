// Preserve integer heading bits in the bridge renderer's reused float register.
#pragma clang diagnostic error "-Wimplicit-function-declaration"
#include "port/ghidra_prelude.h"
#include "port/native.h"
#ifndef __EMSCRIPTEN__
#include "port/host.h"
#endif

float bridge_connections_heading(int heading) {
#ifndef __EMSCRIPTEN__
  if (!bridge_mod_int("fixes.bridge_connections", 1)) return (float)heading;
#endif
  return I2F(heading);
}
