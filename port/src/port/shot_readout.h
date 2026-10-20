#ifndef SIMGOLF_SHOT_READOUT_H
#define SIMGOLF_SHOT_READOUT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { SHOT_READOUT_LINES = 4, SHOT_READOUT_TEXT = 96 };
enum { SHOT_SKILL_COUNT = 10 };
typedef struct { char text[SHOT_SKILL_COUNT][SHOT_READOUT_TEXT]; unsigned present, active; } ShotSkills;
void shot_skills_reset(void);
int shot_skill_text(int index, int active, char *text, int x, int y, uint32_t flags);
void shot_skills_copy(ShotSkills *out);
int shot_readout_text(int line, char *text, int x, int y, uint32_t flags);
int shot_readout_copy(char out[SHOT_READOUT_LINES][SHOT_READOUT_TEXT]);
void shot_readout_reset(void);
void pro_pairing_note(int open);
#ifdef __cplusplus
}
#endif
#endif
