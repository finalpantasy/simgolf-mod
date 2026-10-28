#ifndef SIMGOLF_PATH_QUEUE_H
#define SIMGOLF_PATH_QUEUE_H
#include <stdint.h>

/* One game-thread search at a time. Preserve FIFO relaxation order. */
void path_queue_begin(int16_t x, int16_t y);
int path_queue_push(int16_t x, int16_t y);
int path_queue_pop(int16_t *x, int16_t *y);
int path_queue_pending(void);
int path_queue_failed(void);
#endif
