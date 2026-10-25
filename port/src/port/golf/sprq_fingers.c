#include "port/golf/sprq_fingers.h"

enum { FINGER_COUNT = 64 };
static int g_fingers[FINGER_COUNT];
static int g_finger_count, g_finger_next, g_fingers_enabled = 1;

void sprq_fingers_begin(int enabled) {
  g_sprq_hint = -1;
  g_finger_count = 0;
  g_finger_next = 0;
  g_fingers_enabled = enabled != 0;
}

int sprq_pick_hint(int key, int count) {
  if (count <= 0 || count > SPRQ_N) return -1;
  int best = -1, best_key = 0;
  if (g_sprq_hint >= 0 && g_sprq_hint < count) {
    int candidate = (int32_t)g_sprq[0][g_sprq_hint];
    if (candidate < key) {
      best = g_sprq_hint;
      best_key = candidate;
      if (!g_fingers_enabled) return best;
    }
  }
  if (!g_fingers_enabled) return -1;
  for (int i = 0; i < g_finger_count; ++i) {
    int index = g_fingers[i];
    if (index < 0 || index >= count) continue;
    int candidate = (int32_t)g_sprq[0][index];
    if (candidate < key && (best < 0 || candidate > best_key)) {
      best = index;
      best_key = candidate;
    }
  }
  return best;
}

void sprq_note_insert(int index) {
  g_sprq_hint = index;
  if (!g_fingers_enabled || index < 0 || index >= SPRQ_N) return;
  g_fingers[g_finger_next] = index;
  g_finger_next = (g_finger_next + 1) % FINGER_COUNT;
  if (g_finger_count < FINGER_COUNT) ++g_finger_count;
}
