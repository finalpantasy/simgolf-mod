// Presentation only. Never wait for the bar before releasing a loaded screen.
#pragma once
#include <math.h>
#include <stdint.h>

typedef struct LoadingProgress {
  uint32_t epoch;
  double value, time;
  int active;
} LoadingProgress;

static inline float loading_progress_step(LoadingProgress *p, uint32_t epoch, float target, double now) {
  if (target < 0) {
    p->active = 0;
    return target;
  }
  // The original's 100 callback precedes the last asset and the title/course.
  // Only leaving the loading screen signals completion.
  if (target > 99) target = 99;
  if (!p->active || p->epoch != epoch) {
    p->active = 1;
    p->epoch = epoch;
    p->value = 0;
    p->time = now;
  }
  double dt = now - p->time;
  p->time = now;
  if (dt < 0) dt = 0;
  if (dt > 100) dt = 100;
  if (target > p->value) p->value += (target - p->value) * (1 - exp(-dt / 65.0));
  return (float)p->value;
}
