#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static int fail_allocation;
static void *test_alloc(size_t size) { return fail_allocation ? NULL : malloc(size); }
#define PATH_QUEUE_ALLOC test_alloc
#include "../src/port/golf/path_queue.c"

static void expect_cell(int x, int y) {
  int16_t actual_x, actual_y;
  assert(path_queue_pop(&actual_x, &actual_y));
  assert(actual_x == x && actual_y == y);
}

int main(void) {
  /* A full original ring looked empty: read and write were both zero. */
  unsigned old_read = 0, old_write = 0;
  for (int i = 0; i < 1024; ++i) old_write = (old_write + 1) & 1023;
  assert(old_read == old_write);
  path_queue_begin(0, 0);
  for (int i = 1; i < 1024; ++i) assert(path_queue_push(i, -i));
  assert(path_queue_pending());

  /* Failed growth preserves pending entries and the allocation for retry. */
  fail_allocation = 1;
  assert(!path_queue_push(1024, -1024));
  assert(path_queue_failed());
  for (int i = 0; i < 1024; ++i) expect_cell(i, -i);
  int16_t x, y;
  assert(!path_queue_pop(&x, &y));
  fail_allocation = 0;
  path_queue_begin(0, 0);
  for (int i = 1; i < 1000; ++i) assert(path_queue_push(i, -i));
  for (int i = 0; i < 900; ++i) expect_cell(i, -i);
  /* Grow while wrapped; preserve every relaxation in exact FIFO order. */
  for (int i = 1000; i < 20000; ++i) assert(path_queue_push(i, -i));
  for (int i = 900; i < 20000; ++i) expect_cell(i, -i);
  assert(!path_queue_pending() && !path_queue_failed());
  path_queue_begin(159, 159);
  expect_cell(159, 159);
  assert(!path_queue_pending());
  /* Ordinary searches keep the original ring's exact order across wraps. */
  int16_t old_x[1024], old_y[1024];
  unsigned rd = 0, wr = 1, queued = 1, rng = 1234567;
  old_x[0] = old_y[0] = 0;
  path_queue_begin(0, 0);
  for (int i = 0; i < 100000; ++i) {
    rng = rng * 1664525u + 1013904223u;
    if (!queued || (queued < 512 && (rng & 0x10000))) {
      int16_t a = (int16_t)(rng & 0xffff), b = (int16_t)(rng >> 16);
      old_x[wr] = a; old_y[wr] = b; wr = (wr + 1) & 1023;
      assert(path_queue_push(a, b)); ++queued;
    } else {
      expect_cell(old_x[rd], old_y[rd]); rd = (rd + 1) & 1023; --queued;
    }
    assert(path_queue_pending() == (queued != 0));
  }
  while (queued--) { expect_cell(old_x[rd], old_y[rd]); rd = (rd + 1) & 1023; }
  puts("PASS path queue: full, wrapped growth, FIFO, allocation failure and retry");
  return 0;
}
