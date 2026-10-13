#ifndef SIMGOLF_STAFF_H
#define SIMGOLF_STAFF_H
#include <stdint.h>
#define STAFF_CAPACITY 128
#define STAFF_STRIDE 76
#define STAFF_BYTES (STAFF_CAPACITY * STAFF_STRIDE)
#define STAFF_DEFAULT_MAX 20
#define STAFF_HIRE_MAX 100
uintptr_t staff_base(void);
uintptr_t staff_translate(uintptr_t addr);
int staff_max(void);
int staff_paid_count(void);
int staff_free_slot(int job);
int staff_can_hire(void);
int staff_preflight(int fd, int course, int offset);
void staff_save_tag(int fd, int reading);
#endif
