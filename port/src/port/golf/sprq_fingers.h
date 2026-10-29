#ifndef SIMGOLF_SPRQ_FINGERS_H
#define SIMGOLF_SPRQ_FINGERS_H

#include <stdint.h>

// The original sprite queue's ten parallel arrays, expanded for far trees.
#define SPRQ_N 32768
extern uint32_t g_sprq[10][SPRQ_N];
extern int g_sprq_cap, g_sprq_hint;

// A bounded set of already inserted nodes. Every returned node has a key
// strictly below the new key, so the original forward walk and tie order stay
// unchanged. Disabling the fingers retains the existing last-node hint.
void sprq_fingers_begin(int enabled);
int sprq_pick_hint(int key, int count);
void sprq_note_insert(int index);

#endif
