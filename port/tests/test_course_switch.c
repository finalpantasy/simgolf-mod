// Isolated Windows test of the production transition against a mocked game
// arena. Build: gcc -std=c11 -I port/tests -I port/src
//   port/tests/test_course_switch.c port/src/port/golf/course_switch.c -o test_course_switch.exe
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "port/roster.h"

enum { BASE = 0x400000, BYTES = 0xc00000, STRIDE = 0x2e };
#define I32(a) (*(int32_t *)(uintptr_t)(a))
#define U8(a) (*(uint8_t *)(uintptr_t)(a))
static uint8_t *saves[16];
static char save_names[16][64];

static int site_of(const char *name) {
  const char *p = strrchr(name, '_');
  return p ? atoi(p + 1) : -1;
}

unsigned roster_address(unsigned address) {
  if (address == 0x4d6088) return 0x900000;
  if (address == 0x5849e0) return 0xa00000;
  if (address == 0x543d10) return 0xb00000;
  if (address == 0x58dd50) return 0xd00000;
  abort();
}

int fs_resolve(const char *path, char *out, int cap, int writing) {
  int site = site_of(path);
  const char *base = strrchr(path, '\\');
  base = base ? base + 1 : path;
  if (site < 0 || site >= 16 || !saves[site] || strncmp(base, save_names[site], strlen(save_names[site]))) return 0;
  snprintf(out, cap, "%s", path);
  (void)writing;
  return 1;
}

int FUN_0040b4a0(char *name) {
  int site = site_of(name);
  assert(site >= 0 && site < 16);
  if (!saves[site]) saves[site] = malloc(BYTES);
  assert(saves[site]);
  snprintf(save_names[site], sizeof save_names[site], "%s", name);
  memcpy(saves[site], (void *)(uintptr_t)BASE, BYTES);
  return 1;
}

int FUN_0040b9b0(char *name, int apply_theme, int header_only, int championship) {
  int site = site_of(name);
  assert(site >= 0 && site < 16 && saves[site]);
  assert(apply_theme == 1 && !header_only && !championship);
  memcpy((void *)(uintptr_t)BASE, saves[site], BYTES);
  return 1;
}

void course_switch_before_purchase(int site);
void course_switch_refresh(void);
int course_switch_available(int tag);
int course_switch_request(int tag);
void course_switch_apply(void);

int main(void) {
  assert(VirtualAlloc((void *)(uintptr_t)BASE, BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  I32(0x822c78) = 12345; // one career
  I32(0x834170) = 100;
  I32(0x59bf90) = 0;
  U8(0x571ff4) = 0; // map tag 0 -> site 0
  U8(0x571ff4 + STRIDE) = 1; // tag 1 -> site 1
  I32(0x571fd4) = 400; // site A, after paying for B
  I32(0x5a6364) = 13; // A's course progress
  U8(0x5722e8) = 11; // A's terrain
  U8(0xa00000) = 3; // golfer membership stat
  course_switch_refresh();
  assert(!course_switch_available(1) && !course_switch_request(1));
  course_switch_before_purchase(1);
  assert(saves[0]);

  I32(0x59bf90) = 1;
  I32(0x571fd4) = 900;
  I32(0x5a6364) = 22;
  U8(0x5722e8) = 22; // B's terrain
  U8(0xa00000) = 7; // golfer progressed on B
  U8(0x571ffb + STRIDE) = 8; // B's shared ownership record
  course_switch_refresh();
  assert(course_switch_available(0));
  assert(course_switch_request(0));
  course_switch_apply();
  assert(I32(0x59bf90) == 0 && I32(0x571fd4) == 400 && U8(0x5722e8) == 11);
  assert(I32(0x5a6364) == 13);
  assert(U8(0xa00000) == 7 && U8(0x571ffb + STRIDE) == 8);

  I32(0x571fd4) = 300;
  U8(0xa00000) = 9;
  course_switch_refresh();
  assert(course_switch_request(1));
  course_switch_apply();
  assert(I32(0x59bf90) == 1 && I32(0x571fd4) == 900 && U8(0x5722e8) == 22);
  assert(I32(0x5a6364) == 22);
  assert(U8(0xa00000) == 9);
  I32(0x822c78) = 99999; // a different career cannot open these snapshots
  course_switch_refresh();
  assert(!course_switch_available(0) && !course_switch_request(0));
  puts("course switch: A/B layouts, cash, golfers and ownership preserved");
  return 0;
}
