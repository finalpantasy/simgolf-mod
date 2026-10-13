// The pro golfer's skills past 100 percent and the bad-shot skill loss switch (card task-74).
// Skill bytes 0..10 show as value * 10 percent. golf.exe grows them in FUN_00407e00 (soft cap by
// course size, then a hard gate of 10); this file gives the numbers those gates read.
//   [skills] cap=15        the highest level a skill can grow to (10 = the original 100 percent)
//   [skills] any_course=0  0: past 10 only once the course has 18 holes (the original's own intent,
//                          smaller courses keep their 6 / 8 / 10 caps); 1: on every course
//   [skills] loss=1        1: a ball resting on a hazard can cost a skill point (original); 0: never
// cap=10 gives the original game exactly (translator patches in tools/translate/golf.skills.json).
#pragma clang diagnostic error "-Wimplicit-function-declaration"

#include "golf_protos.h"
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif

static int skill_setting(const char *key, int fallback) {
#ifndef __EMSCRIPTEN__
  return mods_get_int(key, fallback);
#else
  // The browser target has no native mod store. Keep these settings' defaults.
  (void)key;
  return fallback;
#endif
}

static int skills_cap(void) {
  int c = skill_setting("skills.cap", 15);
  return c < 10 ? 10 : c > 30 ? 30 : c;
}

// The grow limit for course tier t (FUN_0044faf0: 0 to 3). The original: t * 2 + 6, and 99 for tier 3
// (the hard gate of 10 then ends it).
int skill_soft_cap(int t) {
  int c = skills_cap();
  if (c > 10 && (t >= 3 || skill_setting("skills.any_course", 0) != 0)) return c;
  return t == 3 ? 99 : t * 2 + 6;
}

// The hard gate of the grow branch (the original: below 10).
int skill_hard_cap(void) { return skills_cap(); }

int skill_loss_on(void) { return skill_setting("skills.loss", 1) != 0; }

// @override FUN_0044faf0
// Course size tier from the hole count: 0 (under 6), 1 (6 to 9), 2 (10 to 17), 3 (18 or more). The
// original returns -1 above 18 holes, which capped skills at 40 percent on a 36 hole course.
int FUN_0044faf0(int param_1) {
  PORT_TRACE(0x0044faf0);
  if (param_1 < 6) return 0;
  if (param_1 < 10) return 1;
  if (param_1 < 0x12) return 2;
  return 3;
}
