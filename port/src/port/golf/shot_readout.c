// Capture the game's own four formatted values before its scratch buffer is
// reused. The original calculations and shot controls still run unchanged.
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "port/shot_readout.h"
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif
extern int FUN_00404b70(char *, int, int, uint32_t);
extern int hud_on(void);
static char text[SHOT_READOUT_LINES][SHOT_READOUT_TEXT];
static unsigned valid;
static ShotSkills skills;
extern int gw_popover_text(int, char *, int, int, uint32_t);
static int enabled(void) {
#ifdef __EMSCRIPTEN__
  return 0;
#else
  const char *v = mods_get("ui.shot_readout");
  return !v || strcmp(v, "original") != 0;
#endif
}
void shot_skills_reset(void) { memset(&skills, 0, sizeof skills); }
void shot_readout_reset(void) { valid = 0; shot_skills_reset(); }
int shot_skill_text(int index, int active, char *s, int x, int y, uint32_t flags) {
  if (index >= 0 && index < SHOT_SKILL_COUNT && s) {
    snprintf(skills.text[index], sizeof skills.text[index], "%s", s);
    skills.present |= 1u << index;
    if (active) skills.active |= 1u << index;
    else skills.active &= ~(1u << index);
  }
  if (hud_on() && enabled()) return 0;
  return gw_popover_text(2, s, x, y, flags);
}
void shot_skills_copy(ShotSkills *out) {
  if (enabled()) *out = skills;
  else memset(out, 0, sizeof *out);
}
int shot_readout_text(int line, char *s, int x, int y, uint32_t flags) {
  if (line >= 0 && line < SHOT_READOUT_LINES && s) {
    int n = 0;
    while (n < SHOT_READOUT_TEXT - 1 && s[n]) { text[line][n] = s[n]; n++; }
    text[line][n] = 0;
    valid |= 1u << line;
  }
  if (hud_on() && enabled()) return 0;
  return FUN_00404b70(s, x, y, flags);
}
int shot_readout_copy(char out[SHOT_READOUT_LINES][SHOT_READOUT_TEXT]) {
  if (!enabled() || valid != (1u << SHOT_READOUT_LINES) - 1) return 0;
  memcpy(out, text, sizeof text);
  return 1;
}
void pro_pairing_note(int open) {
  if (getenv("SIMGOLF_PRO_SCREENS_LOG"))
    fprintf(stderr, "pro-screens: pairing %s\n", open ? "open" : "closed");
}
