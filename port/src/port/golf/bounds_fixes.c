// Bounds on three of golf's unchecked table writes and reads (simgolf mod, card task-118; static findings in
// docs/re/bug-notes.md, "Crash-prone reads and writes"). Each keeps the original's result for every input the
// original handled and only refuses what would have run past its table:
//   FUN_00402970  uses the relocated staff allocator with a fixed physical bound. Paid hiring
//                 follows gameplay.staff_max; an exhausted pool always returns -1.
//   FUN_0043d2a0  the file list of the load and head pickers: 100 names of 100 bytes at 0x80b130 and every name
//                 appended to the 4096-byte text buffer at 0x51a068, with no count limit and an unbounded copy of
//                 the file name. Now at most 100 names; names longer than 99 bytes are refused, never aliased.
//   FUN_0043d6f0  a sprite cell from (id, frame, view): only "no such sprite" and "frame past its count" were
//                 checked. A negative frame or view, an id outside the table or a cell past the 0xdd40 the loader
//                 fills now give the empty cell (0x53ba48), as an unknown sprite already did.
// mods.ini [fixes] bounds=0 restores the old file-list comparison. Relocated staff storage
// remains bounded; sprite safety is enabled if either bounds switch is on.
#include "golf_protos.h"
#include "port/runtime.h"
#include "port/staff.h"

#define G8(a) (*(volatile int8_t *)(uintptr_t)(a))
#define G32(a) (*(volatile int32_t *)(uintptr_t)(a))

#ifndef __EMSCRIPTEN__
int mods_get_int(const char *key, int def);
unsigned mods_serial(void);
#endif

static int bounds_on(void) {
#ifdef __EMSCRIPTEN__
  return 1;
#else
  static unsigned seen = ~0u;
  static int v = 1;
  unsigned s = mods_serial();
  if (s != seen) {
    seen = s;
    v = mods_get_int("fixes.bounds", 1) != 0;
  }
  return v;
#endif
}

int fix_table_bounds_on(void) { return bounds_on(); }

// ------------------------------------------------------------------ staff and walker records

#define STAFF staff_base()
#define STAFF_SIZE STAFF_STRIDE

// @override FUN_00402970
int FUN_00402970(undefined1 param_1) {
  int i = staff_free_slot((signed char)param_1);
  if (i < 0) return -1;
  volatile int32_t *rec = (volatile int32_t *)(uintptr_t)(STAFF + i * STAFF_SIZE);
  for (int k = 0; k < 0x13; k++) rec[k] = 0;
  uint32_t r = FUN_0045c1e0((void *)(uintptr_t)0x822d9c, 5);
  int col = *(volatile int16_t *)(uintptr_t)0x58bcbc;
  rec[0] = *(volatile int16_t *)(uintptr_t)0x58bcba * 0x400 + 0x600;
  rec[1] = col * 0x400 + 0x600;
  uint8_t *b = (uint8_t *)(uintptr_t)(STAFF + i * STAFF_SIZE);
  b[0x12] = 1;
  b[0x16] = (uint8_t)((G8(0x575cb9) + (char)r - 2) & 7);
  b[0x13] = (uint8_t)param_1;
  *(int16_t *)(b + 0x1e) = 0xb;
  r = FUN_0045c1e0((void *)(uintptr_t)0x822d9c, 0x20);
  *(int16_t *)(b + 0x1a) = (int16_t)r;
  b[0x10] = 0xff;
  return i;
}

// ------------------------------------------------------------------ file list

#define LIST 0x80b130u        // 100 names of 100 bytes
#define LIST_N 100
#define TEXT 0x51a068u        // the shared text buffer, 4096 bytes
#define TEXT_SIZE 4096

static int g_file_list_omitted;
int file_list_omitted(void) { return g_file_list_omitted; }
void file_list_draw_notice(void) {
  if (g_file_list_omitted)
    FUN_00404b70("Shorten long filenames to show missing files.", 500, 88, 0x80000000);
}

static int has(const char *s, const char *sub) {
  if (!*sub) return 1;
  for (; *s; s++) {
    const char *a = s, *b = sub;
    while (*a && *b && *a == *b) a++, b++;
    if (!*b) return 1;
  }
  return 0;
}
static int len_of(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
// appends src to the text buffer; bounded: never past its end
static void text_cat(const char *src, int bounded) {
  char *t = (char *)(uintptr_t)TEXT;
  int n = len_of(t);
  for (int i = 0; src[i]; i++) {
    if (bounded && n >= TEXT_SIZE - 1) break;
    t[n++] = src[i];
  }
  t[n] = 0;
}

// @override FUN_0043d2a0
int FUN_0043d2a0(LPCSTR param_1, char *param_2, char *param_3) {
  int on = bounds_on(), count = 0;
  g_file_list_omitted = 0;
  char *list = (char *)(uintptr_t)LIST;
  for (int i = 0; i < LIST_N * 100; i++) list[i] = 0;
  _WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(param_1, &fd);
  if (h == (HANDLE)0xffffffff) return 0;
  const char *skip1 = (const char *)(uintptr_t)0x4c8a34, *skip2 = (const char *)(uintptr_t)0x4c8a2c;
  const char *sep = (const char *)(uintptr_t)0x4c8a28;
  char name[100];
  do {
    const char *src = (const char *)fd.cFileName;
    int n = 0;
    if (on) {
      while (n < 99 && src[n]) name[n] = src[n], n++;
      // The same slot is used for preview, load and delete. A shortened name
      // can identify another file, so never admit a partial filename.
      if (src[n]) { g_file_list_omitted++; continue; }
      name[n] = 0;
    } else {   // the original: the whole name into the 100-byte local
      char *dst = name;
      while ((*dst++ = *src++) != 0) {}
    }
    int ok = 1;
    if (*param_2 && !has(name, param_2)) ok = 0;
    if (*param_3 && has(name, param_3)) ok = 0;
    if (has(name, skip1)) ok = 0;
    if (ok && !has(name, skip2)) {
      if (on && count >= LIST_N) continue;   // FindNextFileA below still runs: the do-while's condition
      text_cat(name, on);
      text_cat(sep, on);
      char *slot = list + count * 100;
      for (n = 0; name[n] && (!on || n < 99); n++) slot[n] = name[n];
      slot[n] = 0;
      count++;
    }
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  return count;
}

// Sprite lookup shares the stronger RE override in re_bounds.c.
