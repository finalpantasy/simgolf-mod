// Native multi-course snapshots.  The original save contains one live course;
// keep one ordinary .sve per site and use the game's own save/load routines.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/fs.h"
#include "port/roster.h"

// The generated Ghidra header redefines CRT types, so this game-side helper
// declares just the two original functions it calls.
void FUN_0040b4a0(char *name);
void FUN_0040b9b0(char *name, int apply_theme, int header_only, int championship);

#ifndef __EMSCRIPTEN__
enum { SITE_COUNT = 16, SITE_SIZE = 0x2e, SITE_BYTES = 0x30e };
#define VALUE(a) (*(volatile int32_t *)(uintptr_t)(a))
static int pending_site = -1;
static uint8_t cached_snapshot[SITE_COUNT];
static int cache_valid;
static int32_t cache_career;

static int site_for_tag(int tag) {
  if (tag < 0 || tag >= SITE_COUNT) return -1;
  for (int site = 0; site < SITE_COUNT; site++)
    if (*(const uint8_t *)(uintptr_t)(0x571ff4 + site * SITE_SIZE) == tag) return site;
  return -1;
}

static void snapshot_name(int site, char *name, size_t cap, int extension) {
  snprintf(name, cap, "&Course_%08x_%02d%s", (unsigned)VALUE(0x822c78), site,
           extension ? ".sve" : "");
}

static int snapshot_exists(int site) {
  char name[64], path[96], resolved[768];
  snapshot_name(site, name, sizeof name, 1);
  snprintf(path, sizeof path, "Saved Games\\%s", name);
  return fs_resolve(path, resolved, sizeof resolved, 0);
}

int course_switch_available(int tag) {
  int site = site_for_tag(tag);
  return site >= 0 && site != VALUE(0x59bf90) && cached_snapshot[site];
}

void course_switch_refresh(void) {
  if (VALUE(0x834170) == 0) { memset(cached_snapshot, 0, sizeof cached_snapshot); cache_valid = 0; return; }
  if (cache_valid && cache_career == VALUE(0x822c78)) return;
  for (int site = 0; site < SITE_COUNT; site++) cached_snapshot[site] = (uint8_t)snapshot_exists(site);
  cache_career = VALUE(0x822c78);
  cache_valid = 1;
}

void course_switch_invalidate(void) { cache_valid = 0; }

int course_switch_current(int tag) {
  int site = site_for_tag(tag);
  return site >= 0 && site == VALUE(0x59bf90);
}

int course_switch_request(int tag) {
  int site = site_for_tag(tag);
  if (site < 0 || site == VALUE(0x59bf90) || !cached_snapshot[site] || !snapshot_exists(site)) return 0;
  pending_site = site;
  return 1;
}

static int save_current(void) {
  int site = VALUE(0x59bf90);
  if (site < 0 || site >= SITE_COUNT || VALUE(0x834170) == 0) return 0;
  char name[64];
  snapshot_name(site, name, sizeof name, 0);
  crt_reset_last_save_write();
  FUN_0040b4a0(name);
  if (crt_last_save_write() != 1 || !snapshot_exists(site)) {
    fprintf(stderr, "course-switch: could not save site %d\n", site);
    return 0;
  }
  cached_snapshot[site] = 1;
  return 1;
}

// Save a recovery copy before the original map begins an in-game purchase.
// The post-purchase save below updates it with the actual deducted balance.
int course_switch_prepare_purchase(void) {
  return VALUE(0x834170) == 0 || save_current();
}

// Called at the end of the map, after the purchase has deducted its price but
// before FUN_00407d30 clears the old course.  A new career has no old course.
void course_switch_before_purchase(int new_site) {
  if (new_site >= 0 && new_site < SITE_COUNT && VALUE(0x834170) != 0 &&
      new_site != VALUE(0x59bf90)) save_current();
}

// Copies only golfer identity, membership statistics and their quote/history
// table.  Course golfers, cash, buildings, clock and local progress come from
// the target save.  roster_address accounts for the port's expanded roster.
typedef struct Traveler {
  uint8_t people[ROSTER_CAPACITY * 560];
  uint8_t membership[ROSTER_CAPACITY * 44];
  uint8_t quotes[ROSTER_CAPACITY * 1250];
  uint8_t pros[120 * 56];
  uint8_t sites[SITE_BYTES];
} Traveler;

void course_switch_apply(void) {
  int target = pending_site;
  pending_site = -1;
  if (target < 0 || target >= SITE_COUNT || !snapshot_exists(target)) return;
  if (!save_current()) return;
  Traveler *carry = malloc(sizeof *carry);
  if (!carry) return;
  memcpy(carry->people, (void *)(uintptr_t)roster_address(0x4d6088), sizeof carry->people);
  memcpy(carry->membership, (void *)(uintptr_t)roster_address(0x5849e0), sizeof carry->membership);
  memcpy(carry->quotes, (void *)(uintptr_t)roster_address(0x543d10), sizeof carry->quotes);
  memcpy(carry->pros, (void *)(uintptr_t)roster_address(0x58dd50), sizeof carry->pros);
  memcpy(carry->sites, (void *)(uintptr_t)0x571fd8, sizeof carry->sites);
  char name[64];
  snapshot_name(target, name, sizeof name, 1);
  FUN_0040b9b0(name, 1, 0, 0);
  memcpy((void *)(uintptr_t)roster_address(0x4d6088), carry->people, sizeof carry->people);
  memcpy((void *)(uintptr_t)roster_address(0x5849e0), carry->membership, sizeof carry->membership);
  memcpy((void *)(uintptr_t)roster_address(0x543d10), carry->quotes, sizeof carry->quotes);
  memcpy((void *)(uintptr_t)roster_address(0x58dd50), carry->pros, sizeof carry->pros);
  memcpy((void *)(uintptr_t)0x571fd8, carry->sites, sizeof carry->sites);
  free(carry);
  fprintf(stderr, "course-switch: opened site %d\n", target);
}
#else
int course_switch_available(int tag) { (void)tag; return 0; }
int course_switch_current(int tag) { (void)tag; return 0; }
int course_switch_request(int tag) { (void)tag; return 0; }
void course_switch_refresh(void) {}
void course_switch_invalidate(void) {}
int course_switch_prepare_purchase(void) { return 1; }
void course_switch_before_purchase(int site) { (void)site; }
void course_switch_apply(void) {}
#endif
