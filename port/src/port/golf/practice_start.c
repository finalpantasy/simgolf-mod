// startPlayerProPair (0040f190) creates ordinary visitor records before
// converting one to the player's pro. createGolfer (00421bc0) can fail when
// every eligible member identity is already playing. Never index slot -1.
#include "port/native.h"
#include "port/runtime.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

unsigned FUN_0046d6e0(int x, unsigned y, unsigned checks, int menu, unsigned disabled);

#ifndef __EMSCRIPTEN__
int mods_get_int(const char *key, int fallback);
#endif

static struct {
  int armed, cursor, last;
  unsigned char golfers[152 * 256];
  unsigned char tee_queue[0x320];
  unsigned char class_counts[128];
} g_practice_start;

static unsigned practice_hash(const void *bytes, size_t n) {
  const unsigned char *p = bytes;
  unsigned h = 2166136261u;
  while (n--) h = (h ^ *p++) * 16777619u;
  return h;
}

static void practice_trace(const char *stage, const void *golfers, const void *queue, const void *classes) {
  if (getenv("SIMGOLF_PRACTICE_LOG"))
    fprintf(stderr, "practice: %s golfers %08x queue %08x classes %08x\n", stage,
            practice_hash(golfers, sizeof g_practice_start.golfers),
            practice_hash(queue, sizeof g_practice_start.tee_queue),
            practice_hash(classes, sizeof g_practice_start.class_counts));
}

static int practice_start_on(void) {
#ifdef __EMSCRIPTEN__
  return 1;
#else
  return mods_get_int("fixes.practice_start", 1) != 0;
#endif
}

void practice_start_begin(void) {
  g_practice_start.armed = practice_start_on();
  if (!g_practice_start.armed) return;
  // These are exactly the record, queue and balance tables changed by
  // createGolfer and releaseGolfer (00426670), including startup cleanup.
  memcpy(g_practice_start.golfers, (const void *)0x5794b8, sizeof g_practice_start.golfers);
  memcpy(g_practice_start.tee_queue, (const void *)0x5689e8, sizeof g_practice_start.tee_queue);
  memcpy(g_practice_start.class_counts, (const void *)0x59dea0, sizeof g_practice_start.class_counts);
  g_practice_start.cursor = *(int *)0x59ae7c;
  g_practice_start.last = *(int *)0x5a5a24;
  practice_trace("snapshot", g_practice_start.golfers, g_practice_start.tee_queue, g_practice_start.class_counts);
}

int practice_start_failed(unsigned slot) {
  if (!g_practice_start.armed || slot < 152) return 0;
  g_practice_start.armed = 0;
  memcpy((void *)0x5794b8, g_practice_start.golfers, sizeof g_practice_start.golfers);
  memcpy((void *)0x5689e8, g_practice_start.tee_queue, sizeof g_practice_start.tee_queue);
  memcpy((void *)0x59dea0, g_practice_start.class_counts, sizeof g_practice_start.class_counts);
  *(int *)0x59ae7c = g_practice_start.cursor;
  *(int *)0x5a5a24 = g_practice_start.last;
  practice_trace("restored", (const void *)0x5794b8, (const void *)0x5689e8, (const void *)0x59dea0);
  // Consume this request once; preserve a pending opponent challenge.
  *(unsigned *)0x59e7b8 &= ~0x4000u;
  snprintf((char *)0x51a068, 1024,
           "A playing partner is not available right now.\n"
           "Try again after a group finishes its round.\n OK\n");
  fprintf(stderr, "practice: no available pair; golfer records and queues restored\n");
  FUN_0046d6e0(400, 100, 0, 1, 0);
  return 1;
}

void practice_start_commit(void) { g_practice_start.armed = 0; }
