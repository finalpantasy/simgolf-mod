#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "port/golf/sprq_fingers.h"

uint32_t g_sprq[10][SPRQ_N];
int g_sprq_cap = SPRQ_N, g_sprq_hint = -1;

static int next[SPRQ_N], baseline[SPRQ_N], candidate[SPRQ_N];
static uint32_t seed = 0x91e10da5;

static int key_for(int mode, int index, int total) {
  switch (mode) {
    case 0: return index;
    case 1: return total - index;
    case 2: seed = seed * 1664525u + 1013904223u; return (int)(seed % 20000u);
    case 3: return (index % 61) * 31;
    default: return index % 7;
  }
}

static unsigned long long run(int mode, int use_fingers, int total, int *order) {
  int head = -1;
  unsigned long long walks = 0;
  sprq_fingers_begin(use_fingers);
  seed = 0x91e10da5;
  for (int i = 0; i < total; ++i) {
    int key = key_for(mode, i, total);
    g_sprq[0][i] = (uint32_t)key;
    int prev = -1, cur = head;
    int hint = sprq_pick_hint(key, i);
    if (hint >= 0 && head >= 0) {
      assert((int32_t)g_sprq[0][hint] < key);
      prev = hint;
      cur = next[hint];
    }
    while (cur >= 0) {
      ++walks;
      if ((int32_t)g_sprq[0][cur] >= key) break;
      prev = cur;
      cur = next[cur];
    }
    next[i] = cur;
    if (prev < 0) head = i;
    else next[prev] = i;
    sprq_note_insert(i);
  }
  int index = head;
  for (int i = 0; i < total; ++i) {
    assert(index >= 0 && index < total);
    order[i] = index;
    index = next[index];
  }
  assert(index == -1);
  return walks;
}

int main(void) {
  for (int mode = 0; mode < 5; ++mode) {
    int total = 8000;
    unsigned long long old_walks = run(mode, 0, total, baseline);
    unsigned long long new_walks = run(mode, 1, total, candidate);
    for (int i = 0; i < total; ++i) assert(candidate[i] == baseline[i]);
    printf("pattern %d: %llu -> %llu walks\n", mode, old_walks, new_walks);
  }
  return 0;
}
