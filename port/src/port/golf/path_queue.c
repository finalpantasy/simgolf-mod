#include "port/golf/path_queue.h"
#include <stdlib.h>
#include <stdint.h>

#ifndef PATH_QUEUE_ALLOC
#define PATH_QUEUE_ALLOC malloc
#endif

typedef struct { int16_t x, y; } PathCell;
static PathCell initial[1024];
/* Reuse peak storage between searches. Capacity stays a power of two. */
static PathCell *cells = initial;
static size_t capacity = 1024, head, count;
static int failed;

int path_queue_push(int16_t x, int16_t y) {
  if (failed) return 0;
  if (count == capacity) {
    if (capacity > SIZE_MAX / 2 / sizeof *cells) {
      failed = 1;
      return 0;
    }
    size_t next_capacity = capacity * 2;
    PathCell *next = PATH_QUEUE_ALLOC(next_capacity * sizeof *next);
    if (!next) {
      failed = 1;
      return 0;
    }
    for (size_t i = 0; i < count; ++i)
      next[i] = cells[(head + i) & (capacity - 1)];
    if (cells != initial) free(cells);
    cells = next;
    capacity = next_capacity;
    head = 0;
  }
  cells[(head + count) & (capacity - 1)] = (PathCell){x, y};
  ++count;
  return 1;
}

void path_queue_begin(int16_t x, int16_t y) {
  head = count = 0;
  failed = 0;
  path_queue_push(x, y);
}

int path_queue_pop(int16_t *x, int16_t *y) {
  if (!count) return 0;
  *x = cells[head].x;
  *y = cells[head].y;
  head = (head + 1) & (capacity - 1);
  --count;
  return 1;
}

int path_queue_pending(void) { return !failed && count != 0; }
int path_queue_failed(void) { return failed; }
