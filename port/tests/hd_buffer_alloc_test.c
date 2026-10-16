// Native render-buffer allocation fault-injection regression.
// Run with clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined
//   -Iport/src port/tests/hd_buffer_alloc_test.c -o /tmp/hd_buffer_alloc_test
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

static int fail_at, alloc_count, live_count;
static void *test_calloc(size_t n, size_t s) {
  if (++alloc_count == fail_at) return NULL;
  void *p = calloc(n, s);
  if (p) live_count++;
  return p;
}
static void test_free(void *p) {
  if (p) live_count--;
  free(p);
}
#define HD_BUFFER_CALLOC test_calloc
#define HD_BUFFER_FREE test_free
#include "port/hd_buffer_alloc.h"

int main(void) {
  for (int fail = 0; fail <= 3; ++fail) {
    fail_at = fail;
    alloc_count = 0;
    live_count = 0;
    uint16_t *course = (void *)1, *ui = (void *)1;
    uint8_t *mask = (void *)1;
    int ok = hd_allocate_render_buffers(&course, &ui, &mask, 800u * 600u);
    assert(ok == (fail == 0));
    assert(alloc_count == 3);
    if (fail) {
      assert(!course && !ui && !mask && live_count == 0);
    } else {
      assert(course && ui && mask && live_count == 3);
      course[0] = 0xffff;
      ui[800u * 600u - 1] = 7;
      mask[100] = 2;
      hd_free_render_buffers(&course, &ui, &mask);
      assert(!course && !ui && !mask && live_count == 0);
    }
  }
  uint16_t *course;
  uint16_t *ui;
  uint8_t *mask;
  assert(!hd_allocate_render_buffers(&course, &ui, &mask, 0));
  assert(!course && !ui && !mask);
  assert(!hd_allocate_render_buffers(&course, &ui, &mask, SIZE_MAX));
  assert(!course && !ui && !mask);
  return 0;
}
