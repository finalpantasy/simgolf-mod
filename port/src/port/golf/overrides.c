// Hand-written replacements for golf.exe functions, marked with
// `// @override <name>` so tools/translate skips the generated version.
// Signatures must match build/gen/golf/golf_protos.h.

#include "golf_protos.h"
#include "port/runtime.h"
#include "re_bounds.h"

// task-40: golf pushes water elevations into Terrain only for tiles near the camera.
// FUN_00449540 (patch in tools/translate/golf.json) asks here whether to push
// every water tile at load; mods.ini [fixes] water_push=0 gives the original.
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
// task-114: saves written while FUN_0045de80 stamped hole 1 starts with one byte of the clock carry a hole 1
// time total that is far too big (the course report's Time to Play). Hole h keeps its rounds at
// 0x575ad0 + h * 0x208 and its time total at 0x575c9c + h * 0x208 (minutes = total / rounds / 40). When
// hole 1 averages over 4 times the slowest other hole (or over 60 minutes with no other hole to compare), its
// total is set to rounds * the median of the other holes (20 minutes with none). fixes.hole_time=0 skips it.
int fix_hole_time(void) {
  if (!mods_get_int("fixes.hole_time", 1)) return 0;
  // PR #3: loaded participants can still carry the old signed-byte start.
  // Repair them even when the accumulated hole total needs no repair.
  int changed = 0;
  int clock = *(int *)(uintptr_t)0x834170;
  if (clock > 255) {
    for (int i = 0; i < 0x98; i++) {   // the original 152 saved participant records
      unsigned char *golfer = (unsigned char *)(uintptr_t)(0x5794b8 + i * 0x100);
      int *start = (int *)(golfer + 0xc0);
      if (golfer[0x21] == 1 && *start >= -128 && *start <= 127) {
        *start = clock;
        changed = 1;
      }
    }
  }
  enum { REC = 0x208, ROUNDS = 0x575ad0, TIME = 0x575c9c, PER_MIN = 40, MINUTE_FLOOR = 20 };
  int r1 = *(int *)(ROUNDS + REC), t1 = *(int *)(TIME + REC);
  if (r1 <= 0 || t1 <= 0) return changed;
  int avg[19], n = 0, worst = 0;
  for (int h = 2; h <= 18; h++) {
    int r = *(int *)(ROUNDS + h * REC), t = *(int *)(TIME + h * REC);
    if (r <= 0 || t <= 0) continue;
    avg[n] = t / r;
    if (avg[n] > worst) worst = avg[n];
    n++;
  }
  int a1 = t1 / r1;
  int limit = n ? worst * 4 : 60 * PER_MIN;
  if (limit < 60 * PER_MIN) limit = 60 * PER_MIN;
  if (a1 <= limit) return changed;
  for (int i = 1; i < n; i++)   // insertion sort for the median
    for (int j = i; j > 0 && avg[j - 1] > avg[j]; j--) { int t = avg[j]; avg[j] = avg[j - 1]; avg[j - 1] = t; }
  int typical = n ? avg[n / 2] : MINUTE_FLOOR * PER_MIN;
  *(int *)(TIME + REC) = typical * r1;
  return 1;
}
int fix_water_push(void) { return mods_get_int("fixes.water_push", 1); }
// task-114t: the tile (x or y) picked for a new weed -> the world coordinate the landmark check should use
int fix_weed_guard(int tile, int original) { return mods_get_int("fixes.no_dandelion", 1) ? tile * 0x400 + 0x200 : original; }
void fix_newcareer_landmarks(void) {
  if (mods_get_int("fixes.newcareer_landmarks", 1)) *(volatile uint32_t *)(uintptr_t)0x543cfc = 0;
}
#else
int fix_water_push(void) { return 1; }
int fix_hole_time(void) { return 0; }
int fix_weed_guard(int tile, int original) { return tile * 0x400 + 0x200; }
void fix_newcareer_landmarks(void) { *(volatile uint32_t *)(uintptr_t)0x543cfc = 0; }
#endif

// task-44: Shift+T ("trees off/on") flips bit 0x400 of the game flags at
// 0x59e7b8, which only the terrain trees test. FUN_0040f5c0's decoration
// switch (patches in tools/translate/golf.json) sets g_decor_tree_pass while
// it draws a decoration tree (scenic trees, the cactus group, the scenic-tree
// cell decoration, dead swamp trees) and clears it after the switch; the
// sprite queue functions FUN_004628d0 and FUN_00462a30 drop the sprite then
// when hide_decor_trees() is 1. Only the picture changes: the game's code and
// state (growth frames, cell flags, sounds) run exactly as before, so golfers,
// shots and saves are the same. mods.ini [view] hide_decor_trees=0 gives the
// original Shift+T (terrain trees only). The flag is tested first, so the
// setting is only looked up while trees are hidden.
#define HIDE_TREES_FLAG 0x400u
int g_decor_tree_pass;
#ifndef __EMSCRIPTEN__
int hide_decor_trees(void) {
  if ((*(volatile uint32_t *)(uintptr_t)0x59e7b8 & HIDE_TREES_FLAG) == 0) return 0;
  return mods_get_int("view.hide_decor_trees", 1) != 0;
}
#else
int hide_decor_trees(void) { return (*(volatile uint32_t *)(uintptr_t)0x59e7b8 & HIDE_TREES_FLAG) != 0; }
#endif

// task-90: FUN_0040f5c0 draws the terrain trees (cell classes with decoration
// type 13: the tree tiles, palms, pines) only when the zoom 0x4c2844 is above 1,
// so the whole-course view showed bare tree areas and beaches (a 2002 saving).
// A patch (tools/translate/golf.trees.json) adds far_trees() to that test: at
// zoom 1 it says draw them anyway. mods.ini [ui] far_trees=0 gives the original.
//
// The draw queue held 1024 sprites; a big course (map scale 10) at zoom 1 with
// its trees needs many thousands, and golf's sorted insert walks the list from
// the start for every sprite. The queue arrays now live in g_sprq (SPRQ_N each,
// see ghidra_prelude.h), and the insert starts from the last inserted sprite
// when the new one sorts after it (the list comes out the same, in far fewer
// steps). With far_trees=0 the cap is the original 1024.
// The setting is read once per frame, at the queue reset (sprq_begin).
uint32_t g_sprq[10][SPRQ_N];
int g_sprq_cap = 0x400, g_sprq_hint = -1;
static int g_far_trees = 1;
void sprq_begin(void) {
#ifndef __EMSCRIPTEN__
  g_far_trees = mods_get_int("ui.far_trees", 1) != 0;
#endif
  g_sprq_cap = g_far_trees ? SPRQ_N : 0x400;
  g_sprq_hint = -1;
}
int far_trees(void) { return g_far_trees && *(volatile int *)(uintptr_t)0x4c2844 == 1; }

// task-48: the original lets a lot be placed on top of a building of its own kind (the
// validity check FUN_0040db90 accepts a tile occupied by the same kind, which the
// upgrade relies on: placing a lot whose level is above 0 first removes the old ones).
// At level 0 nothing is removed, so a click on a built snack bar (or any lot already
// standing) put a second, overlapping one on top and took its price. fix_place_check
// is what the main loop calls for the player's placement click and the hover ghost
// (translator patch in FUN_0040f5c0); the AI's own placements still call FUN_0040db90.
// mods.ini [fixes] building_overlap=0 gives the original (the browser build: always 1).
static int g_overlap_guard;
#ifndef __EMSCRIPTEN__
static int overlap_fix_on(void) { return mods_get_int("fixes.building_overlap", 1); }
#else
static int overlap_fix_on(void) { return 1; }
#endif
int fix_overlap_blocks(int tool) { return g_overlap_guard && tool >= 6 && tool <= 14; }
int fix_place_check(int x, int y, int size, int tool) {
  int level = tool >= 6 && tool <= 14 ? *(volatile int32_t *)(uintptr_t)(0x5a8c38 + tool * 4) : 1;
  g_overlap_guard = level == 0 && overlap_fix_on();
  int r = FUN_0040db90(x, y, size, tool);
  g_overlap_guard = 0;
  return r;
}

// task-6: the official v1.03 fix ("a crash when selecting female golfer heads", theme packs
// and imported heads). FUN_00462020 takes a golfer's default skin from a per head table
// (stride 0x44: 19 male entries at 0x4d55e8, the female ones 20 entries on at 0x4d5b38) and
// writes it into the golfer record. v1.02 never checked the head, so a custom head (index 19
// and up, heads\F*.pcx) read past the table and the golfer got a junk skin, later used as a
// palette object index. v1.03 checks the head (signed byte, < 0x13) and takes record % 4
// otherwise (translator patch golf.femaleheads.json). mods.ini [fixes] female_heads=0 gives
// the v1.02 lookup (the browser build: always the fix).
#ifndef __EMSCRIPTEN__
static int female_heads_fix_on(void) { return mods_get_int("fixes.female_heads", 1); }
#else
static int female_heads_fix_on(void) { return 1; }
#endif
int head_default_skin(int male, int rec) {
  int bounds = fix_re_bounds_on();
  if (bounds && !roster_profile_valid(rec)) return 0;
  int head = *(volatile int8_t *)(uintptr_t)(roster_address(0x4d60aa) + rec * 0x230);
  if (bounds && head < 0) return (int)((unsigned)rec & 3u);
  if ((head >= 0x13 || head < 0) && female_heads_fix_on()) return rec % 4;   // task-118: a negative head too
  return *(volatile int8_t *)(uintptr_t)((male ? 0x4d55e8u : 0x4d5b38u) + head * 0x44);
}

// @override FUN_004ae020
// Writes a JPEG snapshot with the statically linked IJG libjpeg 6b
// (jpeg_CreateCompress etc. at 0x4ae020-0x4b6400). Not ported yet: report
// failure, which the snapshot code handles.
undefined4 FUN_004ae020(int param_1, int param_2, int param_3, code *param_4) {
  (void)param_1; (void)param_2; (void)param_3; (void)param_4;
  return 1;
}

// 0x004321a0 copies a string into the global at 0x587da0 (strcpy inlined as
// repne scasb / rep movsd). Ghidra merged it into FUN_00432170 because of
// junk bytes before it, so callers see an undefined func_0x004321a0; the
// return type is int because those call sites have no prototype.
int func_0x004321a0(char *s) {
  char *d = (char *)0x587da0;
  while ((*d++ = *s++) != 0) {}
  return 0;
}

// 0x00474a80 is a C++ static initializer (in the CRT's list at 0x4c1000)
// that Ghidra never made a function: it calls 0x474a90 and tail-jumps to
// 0x474aa0. Registered by golf_register_overrides().
static uint32_t init_00474a80(ICALL_PARAMS) {
  FUN_00474a90();
  FUN_00474aa0();
  return 0;
}

void golf_register_overrides(void) {
  icall_register(0x00474a80, init_00474a80);
}
