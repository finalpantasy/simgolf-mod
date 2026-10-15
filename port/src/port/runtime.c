// Runtime for translated code: loads module images at their original
// addresses and maps original code addresses to callable wrappers.

#include "port/runtime.h"
#include "port/grid.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include "port/host.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif

static unsigned g_loading_depth;
static int g_loading_fast;

void port_loading_begin(void) {
  if (g_loading_depth++ == 0) {
#ifndef __EMSCRIPTEN__
    g_loading_fast = mods_get_int("performance.fast_loading", 1) != 0;
#endif
  }
}

void port_loading_end(void) {
  if (g_loading_depth) g_loading_depth--;
}

int port_loading_fast(void) { return g_loading_depth && g_loading_fast; }

void *ExceptionList;

// Course creation only. Read the switch once at the outer generation scope.
static unsigned g_course_depth;
static int g_course_fast;
static int g_course_textures;
static unsigned g_height_depth;
static uint32_t g_height_epoch;
static uint32_t g_height_stamp[GRID_MAX_SIDE * GRID_MAX_SIDE];
static uint32_t g_height_value[GRID_MAX_SIDE * GRID_MAX_SIDE];
void port_course_begin(int creating, uint32_t fn) {
  if (g_course_depth++ == 0) {
    g_course_fast = 0;
#ifndef __EMSCRIPTEN__
    g_course_fast = creating && mods_get_int("performance.fast_course_loading", 1) != 0;
#endif
  }
  // During one heightfield rebuild, procedural height inputs do not change.
  // Discard the cache at every rebuild boundary, including nested rebuilds.
  if (fn == 0x0042f7a0 && g_course_fast) {
    if (++g_height_epoch == 0) { memset(g_height_stamp, 0, sizeof g_height_stamp); g_height_epoch = 1; }
    g_height_depth = g_course_depth;
  }
#ifndef __EMSCRIPTEN__
  if (getenv("SIMGOLF_COURSE_TRACE")) fprintf(stderr, "course-phase %.3f %08x begin %u\n", host_now_ms(), fn, g_course_depth);
#else
  (void)creating; (void)fn;
#endif
}
void port_course_end(uint32_t fn) {
  if (fn == 0x0042f7a0 && g_height_depth == g_course_depth) g_height_depth = 0;
  if (fn == 0x00470a60 && g_course_depth == 1) g_course_textures = g_course_fast;
#ifndef __EMSCRIPTEN__
  if (getenv("SIMGOLF_COURSE_TRACE")) fprintf(stderr, "course-phase %.3f %08x end %u\n", host_now_ms(), fn, g_course_depth);
#else
  (void)fn;
#endif
  if (g_course_depth) g_course_depth--;
}
int port_course_fast(void) { return g_course_depth && g_course_fast; }
int port_course_textures_fast(void) { return g_course_textures; }
void port_course_textures_done(void) { g_course_textures = 0; }
uint32_t port_course_height_cached(int x, int y, uint32_t (*height)(int, int)) {
  if (!g_height_depth || (unsigned)x >= GRID_MAX_SIDE || (unsigned)y >= GRID_MAX_SIDE) return height(x, y);
  unsigned i = (unsigned)x * GRID_MAX_SIDE + (unsigned)y;
  if (g_height_stamp[i] != g_height_epoch) {
    g_height_value[i] = height(x, y);
    g_height_stamp[i] = g_height_epoch;
  }
  return g_height_value[i];
}

uint32_t port_trace_buf[1024];
uint32_t port_trace_pos;
uint32_t port_watch_addr[3];   // ghidra_prelude.h PORT_TRACE
uint32_t port_watch_hits[3];

// ---- task-113 call tracer (see ghidra_prelude.h): selected functions count their calls and keep a log.
volatile uint32_t port_trace_any;
uint8_t port_trace_sel[0xa4000 >> 3];
PortCallLog port_calllog[PORT_CALLLOG_N];
volatile uint32_t port_calllog_pos;
static struct { uint32_t addr; uint32_t calls; } g_tracesel[PORT_TRACE_MAX];
static int g_ntracesel;

void port_trace_select(uint32_t addr, int on) {
  if (addr - 0x401000u >= 0xa4000u) return;
  int i;
  for (i = 0; i < g_ntracesel; i++)
    if (g_tracesel[i].addr == addr) break;
  if (on && i == g_ntracesel && g_ntracesel < PORT_TRACE_MAX) {
    g_tracesel[g_ntracesel].addr = addr;
    g_tracesel[g_ntracesel++].calls = 0;
  } else if (!on && i < g_ntracesel) {
    g_tracesel[i] = g_tracesel[--g_ntracesel];
  } else if (on) {
    return;
  }
  uint32_t o = addr - 0x401000u;
  if (on) port_trace_sel[o >> 3] |= (uint8_t)(1u << (addr & 7));
  else port_trace_sel[o >> 3] &= (uint8_t)~(1u << (addr & 7));
  port_trace_any = (uint32_t)g_ntracesel;
}

void port_trace_clear(void) {
  while (g_ntracesel) port_trace_select(g_tracesel[g_ntracesel - 1].addr, 0);
  port_calllog_pos = 0;
  memset(port_calllog, 0, sizeof port_calllog);
}

int port_trace_selected(int i, uint32_t *addr, uint32_t *calls) {
  if (i < 0 || i >= g_ntracesel) return 0;
  *addr = g_tracesel[i].addr;
  *calls = g_tracesel[i].calls;
  return 1;
}

uint32_t port_trace_frame;   // set by the overlay each frame

void port_trace_hit(uint32_t addr) {
  for (int i = 0; i < g_ntracesel; i++)
    if (g_tracesel[i].addr == addr) {
      g_tracesel[i].calls++;
      break;
    }
  uint32_t p = port_calllog_pos++;
  PortCallLog *e = &port_calllog[p % PORT_CALLLOG_N];
  e->addr = addr;
  e->frame = port_trace_frame;
  e->after = port_trace_buf[(port_trace_pos - 1) & 1023];   // the function entered just before: usually the caller
}

void port_debug4(const char *what, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  fprintf(stderr, "debug %s: %08x %08x %08x %08x\n", what, a, b, c, d);
}

// task-38: SIMGOLF_SEED=<n> starts golf's random numbers from n instead of
// the clock (port/src/port/golf/seed.c), so scripted runs can get the same
// new-game site every time. Unset: the clock, as the original.
uint32_t port_seed(uint32_t clock) {
  const char *s = getenv("SIMGOLF_SEED");
  if (!s || !*s) return clock;
  uint32_t v = (uint32_t)strtoul(s, NULL, 0);
  fprintf(stderr, "seed: SIMGOLF_SEED=%u (instead of the clock)\n", v);
  return v;
}

void port_ret_log(uint32_t fn, int where, uint32_t value) {
  fprintf(stderr, "return %06x @%d: %u (0x%x)\n", fn, where, value, value);
}

void port_trace_dump(int n) {
  if (n > 1024) n = 1024;
  fprintf(stderr, "last %d functions entered (oldest first):", n);
  for (int i = n; i > 0; i--) {
    if (i % 8 == 0) fprintf(stderr, "\n ");
    fprintf(stderr, " %06x", port_trace_buf[(port_trace_pos - i) & 1023]);
  }
  fprintf(stderr, "\n");
}
double g_fret;
uint32_t g_edx;

typedef struct { uint32_t addr; IcallFn fn; uint32_t flags; } Entry;

static Entry *g_table;
static unsigned g_count, g_cap;
static int g_sorted;

static int cmp_entry(const void *a, const void *b) {
  uint32_t x = ((const Entry *)a)->addr, y = ((const Entry *)b)->addr;
  return x < y ? -1 : x > y;
}

void icall_register(uint32_t addr, IcallFn fn) { icall_register_flags(addr, fn, 0); }

void icall_register_flags(uint32_t addr, IcallFn fn, uint32_t flags) {
  if (g_count == g_cap) {
    g_cap = g_cap ? g_cap * 2 : 4096;
    g_table = realloc(g_table, g_cap * sizeof(Entry));
  }
  g_table[g_count].addr = addr;
  g_table[g_count].fn = fn;
  g_table[g_count].flags = flags;
  g_count++;
  g_sorted = 0;
}

void icall_register_table(const IcallEntry *t, unsigned n) {
  for (unsigned i = 0; i < n; i++) icall_register_flags(t[i].addr, t[i].fn, t[i].flags);
}

static uint32_t g_next_fake = PORT_FAKE_CODE_BASE;

uint32_t icall_register_native(IcallFn fn) {
  uint32_t a = g_next_fake;
  g_next_fake += 16;
  icall_register(a, fn);
  return a;
}

static uint32_t unresolved(ICALL_PARAMS) {
  (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
  (void)a8; (void)a9; (void)a10; (void)a11; (void)a12; (void)a13; (void)a14; (void)a15;
  return 0;
}

static const Entry *find(uint32_t addr) {
  if (!g_sorted) {
    qsort(g_table, g_count, sizeof(Entry), cmp_entry);
    g_sorted = 1;
  }
  unsigned lo = 0, hi = g_count;
  while (lo < hi) {
    unsigned mid = (lo + hi) / 2;
    if (g_table[mid].addr < addr) lo = mid + 1;
    else hi = mid;
  }
  return lo < g_count && g_table[lo].addr == addr ? &g_table[lo] : NULL;
}

// A virtual call whose `this` Ghidra dropped: pass it only to __thiscall targets.
uint32_t icall_this(uint32_t fn, uint32_t obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                    uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9,
                    uint32_t a10, uint32_t a11, uint32_t a12, uint32_t a13, uint32_t a14) {
  const Entry *e = find(fn);
  if (!e) {
    fprintf(stderr, "icall: no function at 0x%08x (this=0x%08x)\n", fn, obj);
    port_abort("virtual call to unknown address");
  }
  if (e->flags & 1)
    return e->fn(obj, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
  return e->fn(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, 0);
}

// icall_lookup without the abort: NULL for an unknown address (the bridge's
// "call" command, task-13). flags bit 0: __thiscall.
IcallFn icall_find(uint32_t addr, uint32_t *flags) {
  const Entry *e = find(addr);
  if (flags) *flags = e ? e->flags : 0;
  return e ? e->fn : NULL;
}

// Puts fn in the place of the function at addr (every entry for it, flags
// kept) and returns the one it replaced, NULL if addr is unknown. The art
// pack (hd_art.c) wraps jgld's sprite draws this way.
IcallFn icall_replace(uint32_t addr, IcallFn fn, uint32_t *flags) {
  const Entry *e = find(addr);
  if (!e) return NULL;
  IcallFn old = e->fn;
  if (flags) *flags = e->flags;
  for (unsigned i = 0; i < g_count; i++)
    if (g_table[i].addr == addr) g_table[i].fn = fn;
  return old;
}

IcallFn icall_lookup(uint32_t addr) {
  if (!g_sorted) {
    qsort(g_table, g_count, sizeof(Entry), cmp_entry);
    g_sorted = 1;
  }
  unsigned lo = 0, hi = g_count;
  while (lo < hi) {
    unsigned mid = (lo + hi) / 2;
    if (g_table[mid].addr < addr) lo = mid + 1;
    else hi = mid;
  }
  if (lo < g_count && g_table[lo].addr == addr) return g_table[lo].fn;
  fprintf(stderr, "icall: no function at 0x%08x\n", addr);
  port_backtrace();
  port_abort("indirect call to unknown address");
  return unresolved;
}

void port_backtrace(void) {
#ifdef __EMSCRIPTEN__
  EM_ASM({ console.log(new Error().stack); });
#endif
}

double port_now_ms(void) {
#ifdef __EMSCRIPTEN__
  return emscripten_get_now();
#else
  return bridge_clock_ms();   // real time unless the bridge paused or sped it (task-13)
#endif
}

void port_halt(const char *fn, const char *why) {
  fprintf(stderr, "halt in %s: %s\n", fn, why);
  port_abort("halt");
}

void port_abort(const char *why) {
  fprintf(stderr, "port_abort: %s\n", why);
  port_trace_dump(64);
  port_backtrace();
#ifndef __EMSCRIPTEN__
  host_fatal(why);
#endif
  abort();
}

int port_load_image(const char *path, uint32_t base, uint32_t limit) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "image %s: not found\n", path);
    return 0;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n <= 0 || base + (uint32_t)n > limit) {
    fprintf(stderr, "image %s: bad size %ld\n", path, n);
    fclose(f);
    return 0;
  }
  size_t got = fread((void *)(uintptr_t)base, 1, (size_t)n, f);
  fclose(f);
  return got == (size_t)n;
}

int port_quiet;

void port_unimplemented(const char *name) {
  fprintf(stderr, "unimplemented: %s\n", name);
}
