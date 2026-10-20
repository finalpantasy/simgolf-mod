#pragma once

#include <stddef.h>
#include <stdio.h>

// Skill ratings are signed hundredths. Widen before negating so INT_MIN is safe.
static inline void ui_hud_skill_format(char *buf, size_t cap, int skill_x100) {
  unsigned magnitude = skill_x100 < 0 ? (unsigned)(-(long long)skill_x100) : (unsigned)skill_x100;
  snprintf(buf, cap, "%s%u.%02u", skill_x100 < 0 ? "-" : "", magnitude / 100, magnitude % 100);
}
