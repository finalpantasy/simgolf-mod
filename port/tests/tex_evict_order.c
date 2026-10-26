#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

enum { N = 1 << 16, LIVE = 900, RUNS = 100, TICK = 1000 };
typedef struct { unsigned used, bytes; int resident; } Tex;
static Tex slots[N];
static Tex *order[N];
static int old_pick[LIVE], new_pick[LIVE];

static int oldest_first(const void *a, const void *b) {
  const Tex *x = *(Tex *const *)a, *y = *(Tex *const *)b;
  if (x->used != y->used) return x->used < y->used ? -1 : 1;
  return x == y ? 0 : x < y ? -1 : 1;
}

static unsigned reset(int mixed_sizes, int protected_recent) {
  for (int i = 0; i < N; i++) slots[i] = (Tex){0};
  uint32_t seed = 0x77a4u;
  unsigned total = 0;
  for (int i = 0; i < LIVE; i++) {
    int index = (i * 71) % N;
    seed = seed * 1664525u + 1013904223u;
    slots[index].resident = 1;
    slots[index].used = protected_recent && i % 7 == 0 ? TICK - 2 : seed % 900u;
    slots[index].bytes = mixed_sizes ? 1u + (seed >> 8) % 8u : 1u;
    total += slots[index].bytes;
  }
  return total;
}

static int old_run(unsigned total, unsigned budget) {
  int p = 0, live = LIVE;
  while (total > budget && live > 0) {
    Tex *old = NULL;
    for (int i = 0; i < N; i++)
      if (slots[i].resident && (!old || slots[i].used < old->used)) old = &slots[i];
    if (!old || old->used + 2 >= TICK) break;
    old_pick[p++] = (int)(old - slots);
    total -= old->bytes;
    old->resident = 0;
    live--;
  }
  return p;
}

static int new_run(unsigned total, unsigned budget) {
  int n = 0, p = 0;
  for (int i = 0; i < N; i++)
    if (slots[i].resident && slots[i].used + 2 < TICK) order[n++] = &slots[i];
  qsort(order, (size_t)n, sizeof *order, oldest_first);
  for (int i = 0; i < n && total > budget; i++) {
    Tex *old = order[i];
    new_pick[p++] = (int)(old - slots);
    total -= old->bytes;
    old->resident = 0;
  }
  return p;
}

static void compare_case(int mixed_sizes, int protected_recent, unsigned budget) {
  unsigned total = reset(mixed_sizes, protected_recent);
  int a = old_run(total, budget);
  reset(mixed_sizes, protected_recent);
  int b = new_run(total, budget);
  assert(a == b);
  for (int i = 0; i < a; i++) assert(old_pick[i] == new_pick[i]);
  printf("mixed=%d protected=%d budget=%u victims=%d exact_order=1\n",
         mixed_sizes, protected_recent, budget, a);
}

static double ms(LARGE_INTEGER a, LARGE_INTEGER b, LARGE_INTEGER freq) {
  return (double)(b.QuadPart - a.QuadPart) * 1000.0 / freq.QuadPart;
}

int main(void) {
  compare_case(0, 0, 853);
  compare_case(1, 0, 3900);
  compare_case(1, 1, 3500);
  compare_case(1, 1, 0);
  LARGE_INTEGER a, b, c, f;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&a);
  for (int i = 0; i < RUNS; i++) old_run(reset(0, 0), 853);
  QueryPerformanceCounter(&b);
  for (int i = 0; i < RUNS; i++) new_run(reset(0, 0), 853);
  QueryPerformanceCounter(&c);
  printf("selection: old_ms=%.3f new_ms=%.3f (%d runs)\n",
         ms(a, b, f), ms(b, c, f), RUNS);
  return 0;
}
