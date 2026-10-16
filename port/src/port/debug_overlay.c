#include "port/roster.h"
// Debug overlay (simgolf mod, task-113). Native build only.
//
// A Frosted Mint panel over the game that shows what the game is doing, decoded with the names the RE kit
// (tools/re, docs/re/README.md) learned: live state, every golfer and staff record field by field, the tile,
// object and sprite under the cursor (which sheet and frame drew a pixel, and whether golf's old interface
// or a new layer did: the old-UI leak and dark-box hunts), a watch list by symbol name, a call tracer for
// chosen functions, and a bug report button that saves a screenshot, the state and the log in one folder.
//
// Off by default and nothing runs until it is switched on:
//   [debug] overlay=1        visible from the start (mods.ini, also live through the bridge's mods_set)
//   [debug] overlay_key=...  the toggle key, default "shift+f12" (modifiers shift, ctrl, alt; "none" = no key)
//   [debug] watch=a,b+0x4:2  symbols to watch (name, optional +offset and :size)
//   [debug] trace=fn1,fn2    functions to trace (names from the database, or 0xADDRESS)
//   [debug] report_dir=...   where bug reports go (default bugreports/ next to golf.exe)
//   [debug] report_now=1     save a bug report now (the bridge sets it; the key is cleared afterwards)
//   [debug] tab=0..6         the tab shown
//   [debug] symbols=...      the symbol list (default re/symbols.tsv next to golf.exe or the build/re one)
//
// Threads: the game thread reads golf's memory once per course frame into a snapshot (dbg_overlay_frame,
// called from golf/hd_ui.c's frame hook), the render thread draws the snapshot, the game thread also takes
// the clicks (an input handler, like the HUD) and does the bug report (it needs the screenshot call).
#include <SDL.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <windows.h>

#include "mods/mods.h"
#include "port/grid.h"
#include "port/hd_art.h"
#include "port/hd_native.h"
#include "port/host.h"
#include "port/runtime.h"
#include "port/ui/ui_kit.h"
#include "port/ui/ui_png.h"

#define G32(a) (*(volatile int32_t *)(uintptr_t)(a))
#define G16(a) (*(volatile int16_t *)(uintptr_t)(a))
#define G8(a) (*(volatile int8_t *)(uintptr_t)(a))
#define GU8(a) (*(volatile uint8_t *)(uintptr_t)(a))

#define TEXT_BUF 0x51a068
#define MOUSE_X 0x822d60
#define MOUSE_Y 0x822d64

uint32_t FUN_004676e0(uint32_t slot, int what);   // golfer name -> text buffer
uint32_t FUN_00467600(int rec);                   // employee name -> text buffer
int hd_native_shot(const char *name);             // hd_native.c: the presented picture as a BMP
extern int win_screen_w, win_screen_h;            // win32.c: golf's canvas

typedef struct { short off, size; char sign; const char *name; const char *type; const char *note; } DbgField;
#include "port/debug_layouts.inc"
#define NGF (int)(sizeof dbg_golfer_fields / sizeof dbg_golfer_fields[0])
#define NEF (int)(sizeof dbg_employee_fields / sizeof dbg_employee_fields[0])
#define NHF (int)(sizeof dbg_hole_fields / sizeof dbg_hole_fields[0])

#define NG DBG_GOLFER_COUNT
#define NS 64
#define NH 20
#define EMP_BASE DBG_EMPLOYEE_BASE
#define EMP_STRIDE DBG_EMPLOYEE_STRIDE
#define NAMELEN 28
#define MAX_WATCH 24
#define NTABS 7
#define PRO_SKILLS 0x5a5a04
#define PRO_NAME roster_address(0x4d6098)
#define PRO_SLOT 0x5a59f8
#define SKILL_NAME_PTRS 0x4c2c3c

enum { TAB_STATE, TAB_GOLFERS, TAB_STAFF, TAB_CURSOR, TAB_WATCH, TAB_TRACE, TAB_REPORT };
static const char *const k_tabs[NTABS] = {"State", "Golf", "Staff", "Cursor", "Watch", "Trace", "Report"};

// ------------------------------------------------------------------ symbols (build/re/symbols.tsv)

typedef struct { uint32_t addr; int size; char kind; char name[48]; } Sym;
static Sym *g_sym;
static int g_nsym, g_sym_tried;
static int *g_by_addr;   // indexes of g_sym sorted by address (functions and globals)
static char g_sym_path[600];

static int cmp_sym_name(const void *a, const void *b) { return strcasecmp(g_sym[*(const int *)a].name, g_sym[*(const int *)b].name); }
static int cmp_sym_addr(const void *a, const void *b) {
  const Sym *x = &g_sym[*(const int *)a], *y = &g_sym[*(const int *)b];
  return x->addr < y->addr ? -1 : x->addr > y->addr;
}
static int *g_glist, *g_flist;   // indexes sorted by name: globals, functions
static int g_nglist, g_nflist;

static int file_exists(const char *p) {
  FILE *f = fopen(p, "rb");
  if (f) fclose(f);
  return f != NULL;
}

static void load_symbols(void) {
  if (g_sym_tried) return;
  g_sym_tried = 1;
  char cand[5][600];
  int n = 0;
  const char *env = getenv("SIMGOLF_RE_SYMBOLS");
  const char *ini = mods_get("debug.symbols");
  if (env && *env) snprintf(cand[n++], 600, "%s", env);
  if (ini && *ini) snprintf(cand[n++], 600, "%s", ini);
  snprintf(cand[n++], 600, "%s/re/symbols.tsv", host_exe_dir());
  snprintf(cand[n++], 600, "%s/../re/symbols.tsv", host_exe_dir());
  snprintf(cand[n++], 600, "%s/re/symbols.tsv", host_game_dir());
  FILE *f = NULL;
  for (int i = 0; i < n && !f; i++) {
    f = fopen(cand[i], "rb");
    if (f) snprintf(g_sym_path, sizeof g_sym_path, "%s", cand[i]);
  }
  if (!f) {
    fprintf(stderr, "debug: no symbol list (run tools/re/build.sh; the overlay still works without names)\n");
    return;
  }
  char line[512];
  int cap = 0;
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#' || line[0] == '\n') continue;
    unsigned a;
    int sz;
    char kind, name[64];
    if (sscanf(line, "%x\t%c\t%d\t%63[^\t\r\n]", &a, &kind, &sz, name) != 4) continue;
    if (g_nsym == cap) g_sym = realloc(g_sym, (size_t)(cap = cap ? cap * 2 : 1024) * sizeof *g_sym);
    Sym *s = &g_sym[g_nsym++];
    s->addr = a, s->size = sz, s->kind = kind;
    snprintf(s->name, sizeof s->name, "%s", name);
  }
  fclose(f);
  g_by_addr = malloc((size_t)g_nsym * sizeof(int));
  g_glist = malloc((size_t)g_nsym * sizeof(int));
  g_flist = malloc((size_t)g_nsym * sizeof(int));
  for (int i = 0; i < g_nsym; i++) {
    g_by_addr[i] = i;
    if (g_sym[i].kind == 'g') g_glist[g_nglist++] = i;
    else g_flist[g_nflist++] = i;
  }
  qsort(g_by_addr, (size_t)g_nsym, sizeof(int), cmp_sym_addr);
  qsort(g_glist, (size_t)g_nglist, sizeof(int), cmp_sym_name);
  qsort(g_flist, (size_t)g_nflist, sizeof(int), cmp_sym_name);
  fprintf(stderr, "debug: %d symbols (%d globals, %d functions) from %s\n", g_nsym, g_nglist, g_nflist, g_sym_path);
}

static const Sym *sym_by_name(const char *name, char kind) {
  for (int i = 0; i < g_nsym; i++)
    if ((!kind || g_sym[i].kind == kind) && !strcasecmp(g_sym[i].name, name)) return &g_sym[i];
  return NULL;
}

// the symbol whose extent holds addr (functions: their size; globals: their size), by binary search
static const Sym *sym_at(uint32_t addr, char kind) {
  int lo = 0, hi = g_nsym - 1, best = -1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (g_sym[g_by_addr[mid]].addr <= addr) best = mid, lo = mid + 1;
    else hi = mid - 1;
  }
  for (int i = best; i >= 0 && i > best - 6; i--) {
    const Sym *s = &g_sym[g_by_addr[i]];
    if ((!kind || s->kind == kind) && addr < s->addr + (uint32_t)(s->size > 0 ? s->size : 1)) return s;
  }
  return NULL;
}

// ------------------------------------------------------------------ settings

static volatile int g_vis;           // the panel is up
static volatile int g_registered, g_failed;
static int g_tab;
static unsigned g_seen_serial = ~0u;
static int g_mod_shift = 1, g_mod_ctrl, g_mod_alt;
static SDL_Keycode g_key = SDLK_F12;
static int g_key_none;
static int g_swallow_up;
static int g_started;

typedef struct { char name[48]; uint32_t addr; int size; int sign; } Watch;
static Watch g_watch[MAX_WATCH];
static int g_nwatch;

static void parse_key(const char *v) {
  g_mod_shift = g_mod_ctrl = g_mod_alt = 0;
  g_key = SDLK_F12;
  g_key_none = 0;
  if (!v || !*v) {
    g_mod_shift = 1;
    return;
  }
  if (!strcasecmp(v, "none") || !strcasecmp(v, "off")) {
    g_key_none = 1;
    return;
  }
  char buf[64];
  snprintf(buf, sizeof buf, "%s", v);
  char *save = NULL;
  for (char *tok = strtok_s(buf, "+- ", &save); tok; tok = strtok_s(NULL, "+- ", &save)) {
    if (!strcasecmp(tok, "shift")) g_mod_shift = 1;
    else if (!strcasecmp(tok, "ctrl") || !strcasecmp(tok, "control")) g_mod_ctrl = 1;
    else if (!strcasecmp(tok, "alt")) g_mod_alt = 1;
    else {
      SDL_Keycode k = SDL_GetKeyFromName(tok);
      if (k != SDLK_UNKNOWN) g_key = k;
    }
  }
}

static int add_watch(const char *name, uint32_t addr, int size, int sign) {
  for (int i = 0; i < g_nwatch; i++)
    if (g_watch[i].addr == addr && g_watch[i].size == size) return 0;
  if (g_nwatch >= MAX_WATCH) return 0;
  Watch *w = &g_watch[g_nwatch++];
  snprintf(w->name, sizeof w->name, "%s", name);
  w->addr = addr, w->size = size, w->sign = sign;
  return 1;
}

static int parse_addr_text(const char *s, uint32_t *a) {
  while (*s == ' ') s++;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    char *end;
    unsigned long v = strtoul(s, &end, 16);
    if (end == s + 2) return 0;
    *a = (uint32_t)v;
    return 1;
  }
  return 0;
}

// "name", "name+0xoff", "name:2", "name+0x4:2", "0x571fd4:4"
static void watch_spec(const char *spec) {
  char buf[96];
  snprintf(buf, sizeof buf, "%s", spec);
  int size = 0;
  char *c = strrchr(buf, ':');
  if (c) {
    size = atoi(c + 1);
    *c = 0;
  }
  uint32_t off = 0;
  char *p = strchr(buf, '+');
  if (p) {
    off = (uint32_t)strtoul(p + 1, NULL, 0);
    *p = 0;
  }
  uint32_t addr;
  const Sym *s = NULL;
  if (parse_addr_text(buf, &addr)) {
    if (!size) size = 4;
  } else {
    s = sym_by_name(buf, 'g');
    if (!s) {
      fprintf(stderr, "debug: watch %s: no such symbol\n", buf);
      return;
    }
    addr = s->addr;
    if (!size) size = s->size == 1 || s->size == 2 ? s->size : 4;
  }
  char label[48];
  snprintf(label, sizeof label, off ? "%s+0x%x" : "%s", s ? s->name : buf, off);
  add_watch(label, addr + off, size > 4 ? 4 : size, size == 4 || size == 2 || size == 1);
}

static void trace_spec(const char *spec) {
  uint32_t addr;
  if (!parse_addr_text(spec, &addr)) {
    const Sym *s = sym_by_name(spec, 'f');
    if (!s) {
      fprintf(stderr, "debug: trace %s: no such function\n", spec);
      return;
    }
    addr = s->addr;
  }
  port_trace_select(addr, 1);
}

static void for_each_item(const char *list, void (*fn)(const char *)) {
  char buf[600];
  snprintf(buf, sizeof buf, "%s", list);
  char *save = NULL;
  for (char *tok = strtok_s(buf, ", ", &save); tok; tok = strtok_s(NULL, ", ", &save)) fn(tok);
}

static void report_now(void);
static void show(int on);
static void poll_settings(void) {
  unsigned ser = mods_serial();
  if (ser == g_seen_serial) return;
  g_seen_serial = ser;
  parse_key(mods_get("debug.overlay_key"));
  int want = mods_get_int("debug.overlay", 0);
  if (want != 0 && !g_started) {
    g_started = 1;
    g_vis = 1;
  } else if (want == 0 && g_started) {
    g_started = 0;
    g_vis = 0;
  }
  const char *tab = mods_get("debug.tab");
  static char last_tab[16];
  if (tab && *tab && strcmp(tab, last_tab)) {   // only when the setting itself changed: a click on a tab stays
    snprintf(last_tab, sizeof last_tab, "%s", tab);
    g_tab = atoi(tab) < 0 ? 0 : atoi(tab) >= NTABS ? NTABS - 1 : atoi(tab);
  }
  const char *w = mods_get("debug.watch");
  static char last_watch[600], last_trace[600];
  if (w && strcmp(w, last_watch)) {
    snprintf(last_watch, sizeof last_watch, "%s", w);
    load_symbols();
    for_each_item(w, watch_spec);
  }
  const char *t = mods_get("debug.trace");
  if (t && strcmp(t, last_trace)) {
    snprintf(last_trace, sizeof last_trace, "%s", t);
    load_symbols();
    for_each_item(t, trace_spec);
  }
}

// ------------------------------------------------------------------ the snapshot (game thread -> render thread)

typedef struct {
  int valid;
  double t;
  uint32_t frame;
  int tick, year, month, day;
  int money, fun, skill, fame, difficulty, holes_built, flags, theme, canvas_w, canvas_h, next_hole;
  int hover_x, hover_y, cam_x, cam_y, map_w;
  int tile_id, tile_flags, obj_idx;
  char tile_name[32];
  int16_t obj[8];
  char obj_kind[24];
  int mouse_x, mouse_y;          // golf's cursor, canvas px
  uint8_t golf[NG][256];
  char gname[NG][NAMELEN];
  uint8_t staff[NS][EMP_STRIDE];
  char sname[NS][NAMELEN];
  uint8_t holes[NH][DBG_HOLE_STRIDE];
  uint8_t pro[12];
  int pro_slot;
  char pro_name[NAMELEN], skill_name[12][20];
  int pick_rc;
  HdArtPick pick;
  int pick_x, pick_y;
  struct { char name[24]; uint32_t id; UiRect r; } layer_hit[8];
  int nlayer_hit;
  int nwatch;
  struct { char name[48]; uint32_t addr; int size, sign; uint32_t value; } watch[MAX_WATCH];
  int ntrace;
  struct { uint32_t addr, calls; double rate; } trace[PORT_TRACE_MAX];
  struct { uint32_t addr, frame, after; } log[24];
  int nlog;
  uint32_t log_total;
  const char *textures;
  int pack_on;
  double fps;
} Snap;

static SDL_mutex *g_lock;
static Snap g_snap;          // published
static Snap g_work;          // being built (game thread)
static uint32_t g_frame_no;
static int g_sel_golfer = -1, g_sel_staff = -1;
static volatile int g_report_req;
static double g_report_t;
static char g_last_report[600];
static int g_reports;
static volatile int g_panel_x0 = 1 << 20;           // panel left edge in canvas px

static void copy_text(char *dst, size_t cap, const char *src) {
  size_t i = 0;
  for (; src && src[i] && i + 1 < cap; i++) dst[i] = (src[i] >= 32 && src[i] < 127) ? src[i] : '?';
  dst[i] = 0;
}

static void read_names(Snap *s) {
  char keep[512];
  memcpy(keep, (void *)(uintptr_t)TEXT_BUF, sizeof keep);
  for (int i = 0; i < NG; i++) {
    s->gname[i][0] = 0;
    if (s->golf[i][0x21] == 0) continue;
    if (i >= 152) {   // the scratch slots (player pro, previews) have no roster name
      copy_text(s->gname[i], NAMELEN, "(scratch slot)");
      continue;
    }
    *(char *)(uintptr_t)TEXT_BUF = 0;
    FUN_004676e0((uint32_t)i, 0);
    copy_text(s->gname[i], NAMELEN, (const char *)(uintptr_t)TEXT_BUF);
  }
  for (int r = 0; r < NS; r++) {
    s->sname[r][0] = 0;
    if (s->staff[r][0x12] == 0) continue;
    if ((int8_t)s->staff[r][0x13] >= 0 || (int8_t)s->staff[r][0x13] == -6) {   // not hired staff: an ambient walker or Gary
      copy_text(s->sname[r], NAMELEN, "(walker)");
      continue;
    }
    *(char *)(uintptr_t)TEXT_BUF = 0;
    FUN_00467600(r);
    copy_text(s->sname[r], NAMELEN, (const char *)(uintptr_t)TEXT_BUF);
  }
  memcpy((void *)(uintptr_t)TEXT_BUF, keep, sizeof keep);
}

static uint32_t read_value(uint32_t addr, int size, int sign) {
  switch (size) {
    case 1: return sign ? (uint32_t)(int32_t)G8(addr) : (uint32_t)GU8(addr);
    case 2: return sign ? (uint32_t)(int32_t)G16(addr) : (uint32_t)(uint16_t)G16(addr);
    default: return (uint32_t)G32(addr);
  }
}

static int safe_addr(uint32_t a, int n) { return a >= 0x400000 && a + (uint32_t)n < 0x1100000; }

static void act(void);

void dbg_overlay_frame(void) {
  static double last_snap, last_rate_t;
  static uint32_t last_calls[PORT_TRACE_MAX];
  static double fps_t0;
  static uint32_t fps_n0;
  port_trace_frame = ++g_frame_no;
  if (!g_lock) g_lock = SDL_CreateMutex();
  act();
  if (g_started && !g_registered && !g_failed) show(1);
  if (!g_vis) return;
  double now = host_now_ms();
  if (now - last_snap < 33) return;   // 30 snapshots a second at most
  last_snap = now;
  Snap *s = &g_work;
  s->valid = 1;
  s->t = now;
  s->frame = g_frame_no;
  if (now - fps_t0 > 1000) {
    s->fps = (g_frame_no - fps_n0) * 1000.0 / (now - fps_t0);
    fps_t0 = now, fps_n0 = g_frame_no;
  } else {
    s->fps = g_snap.fps;
  }
  s->tick = G32(0x834170);
  uint32_t t = (uint32_t)s->tick;
  s->year = 2001 + (int)(t >> 13);
  s->month = (int)((t >> 10) & 7);
  s->day = (int)(((t & 1023) * 30 >> 10) + 1);
  s->money = G32(0x571fd4) * 100;
  s->fun = G32(0x59ae78), s->skill = G32(0x541cd8), s->fame = G32(0x5787cc), s->difficulty = G32(0x822c88);
  s->holes_built = G32(0x5685f0), s->flags = G32(0x59e7b8), s->theme = GU8(0x5a34e0);
  s->next_hole = G32(0x5685f0);
  s->canvas_w = win_screen_w, s->canvas_h = win_screen_h;
  s->map_w = g_map_w;
  s->mouse_x = G32(MOUSE_X), s->mouse_y = G32(MOUSE_Y);
  memcpy(s->golf, (void *)(uintptr_t)DBG_GOLFER_BASE, sizeof s->golf);
  memcpy(s->staff, (void *)(uintptr_t)EMP_BASE, sizeof s->staff);
  memcpy(s->holes, (void *)(uintptr_t)DBG_HOLE_BASE, sizeof s->holes);
  read_names(s);
  memcpy(s->pro, (void *)(uintptr_t)PRO_SKILLS, 12);
  s->pro_slot = G32(PRO_SLOT);
  copy_text(s->pro_name, NAMELEN, (const char *)(uintptr_t)PRO_NAME);
  for (int i = 0; i < 12; i++) {
    uint32_t p = (uint32_t)G32(SKILL_NAME_PTRS + 4 * i);
    s->skill_name[i][0] = 0;
    if (safe_addr(p, 20)) copy_text(s->skill_name[i], sizeof s->skill_name[i], (const char *)(uintptr_t)p);
  }
  // under the cursor
  s->hover_x = G32(0x543d08), s->hover_y = G32(0x543d0c);
  s->cam_x = G32(0x4c2ba0), s->cam_y = G32(0x4c2ba4);
  s->tile_id = s->tile_flags = -1;
  s->obj_idx = -1;
  s->tile_name[0] = s->obj_kind[0] = 0;
  if (s->hover_x >= 0 && s->hover_y >= 0 && s->hover_x < g_map_w && s->hover_y < g_map_h) {
    int i = s->hover_x * g_map_w + s->hover_y;
    s->tile_id = *(uint8_t *)(grid_translate(0x5722e8) + i);
    s->tile_flags = *(uint16_t *)(grid_translate(0x53caf0) + (uintptr_t)i * 2);
    int o = *(uint8_t *)(grid_translate(0x56988c) + i);
    s->obj_idx = o == 0xff ? -1 : o;
    if (s->tile_id >= 0 && s->tile_id < 40) copy_text(s->tile_name, sizeof s->tile_name, (const char *)(uintptr_t)(0x578350 + s->tile_id * 0x30));
    if (s->obj_idx >= 0) {
      memcpy(s->obj, (void *)(uintptr_t)(0x58bcb8 + s->obj_idx * 16), 16);
      if (s->obj[0] >= 0 && s->obj[0] < 200) copy_text(s->obj_kind, sizeof s->obj_kind, (const char *)(uintptr_t)(0x4c26b0 + s->obj[0] * 0x14));
    }
  }
  // the sprite under the pointer (the last point outside the panel; the render thread keeps it)
  s->pick_x = s->mouse_x, s->pick_y = s->mouse_y;   // golf's cursor stays where it left the game picture (the panel swallows its moves)
  s->pick_rc = hd_art_pick(s->pick_x, s->pick_y, &s->pick);
  s->textures = hd_art_textures_name();
  s->pack_on = hd_art_active();
  // watch list
  s->nwatch = g_nwatch;
  for (int i = 0; i < g_nwatch; i++) {
    snprintf(s->watch[i].name, sizeof s->watch[i].name, "%s", g_watch[i].name);
    s->watch[i].addr = g_watch[i].addr, s->watch[i].size = g_watch[i].size, s->watch[i].sign = g_watch[i].sign;
    s->watch[i].value = safe_addr(g_watch[i].addr, 4) ? read_value(g_watch[i].addr, g_watch[i].size, g_watch[i].sign) : 0;
  }
  // call tracer
  s->ntrace = 0;
  uint32_t a, calls;
  int dt_ok = now - last_rate_t > 500;
  for (int i = 0; port_trace_selected(i, &a, &calls) && i < PORT_TRACE_MAX; i++) {
    s->trace[i].addr = a, s->trace[i].calls = calls;
    if (dt_ok) s->trace[i].rate = (calls - last_calls[i]) * 1000.0 / (now - last_rate_t), last_calls[i] = calls;
    else s->trace[i].rate = g_snap.ntrace > i ? g_snap.trace[i].rate : 0;
    s->ntrace = i + 1;
  }
  if (dt_ok) last_rate_t = now;
  uint32_t pos = port_calllog_pos;
  s->log_total = pos;
  s->nlog = 0;
  for (uint32_t i = pos > 24 ? pos - 24 : 0; i < pos; i++) {
    const PortCallLog *e = &port_calllog[i % PORT_CALLLOG_N];
    s->log[s->nlog].addr = e->addr, s->log[s->nlog].frame = e->frame, s->log[s->nlog++].after = e->after;
  }
  SDL_LockMutex(g_lock);
  s->nlayer_hit = g_snap.nlayer_hit;   // filled by the render thread
  memcpy(s->layer_hit, g_snap.layer_hit, sizeof s->layer_hit);
  g_snap = *s;
  SDL_UnlockMutex(g_lock);
}

// ------------------------------------------------------------------ record decoding

static uint32_t field_raw(const uint8_t *rec, const DbgField *f, int *neg) {
  uint32_t v = 0;
  int n = f->size > 4 ? 4 : f->size;
  for (int i = 0; i < n; i++) v |= (uint32_t)rec[f->off + i] << (8 * i);
  *neg = 0;
  if (f->sign && n < 4 && (v >> (8 * n - 1)) & 1) v |= ~0u << (8 * n), *neg = 1;
  else if (f->sign && n == 4 && (v >> 31)) *neg = 1;
  return v;
}

static void field_text(const uint8_t *rec, const DbgField *f, char *out, size_t cap) {
  if (f->type[0] == '[' || f->size > 4 || !strncmp(f->type, "char[", 5)) {
    size_t n = 0;
    int cnt = f->size > 14 ? 14 : f->size;
    out[0] = 0;
    for (int i = 0; i < cnt && n + 4 < cap; i++) n += (size_t)snprintf(out + n, cap - n, "%02x ", rec[f->off + i]);
    if (f->size > cnt && n + 3 < cap) snprintf(out + n, cap - n, "..");
    return;
  }
  int neg;
  uint32_t v = field_raw(rec, f, &neg);
  if (f->sign) snprintf(out, cap, "%d (0x%x)", (int32_t)v, v & (f->size >= 4 ? 0xffffffffu : (1u << (8 * f->size)) - 1));
  else snprintf(out, cap, "%u (0x%x)", v, v);
}

// ------------------------------------------------------------------ bug report

static void json_str(FILE *f, const char *s) {
  fputc('"', f);
  for (; s && *s; s++) {
    if (*s == '"' || *s == '\\') fprintf(f, "\\%c", *s);
    else if ((unsigned char)*s < 32 || (unsigned char)*s > 126) fprintf(f, "\\u%04x", (unsigned char)*s);
    else fputc(*s, f);
  }
  fputc('"', f);
}

static void json_record(FILE *f, const uint8_t *rec, const DbgField *fl, int nf) {
  fputc('{', f);
  for (int i = 0; i < nf; i++) {
    const DbgField *d = &fl[i];
    if (i) fputc(',', f);
    json_str(f, d->name);
    fputc(':', f);
    if (d->type[0] == '[' || d->size > 4) {
      fputc('[', f);
      int cnt = d->size > 32 ? 32 : d->size;
      for (int k = 0; k < cnt; k++) fprintf(f, k ? ",%d" : "%d", rec[d->off + k]);
      fputc(']', f);
    } else {
      int neg;
      uint32_t v = field_raw(rec, d, &neg);
      if (d->sign) fprintf(f, "%d", (int32_t)v);
      else fprintf(f, "%u", v);
    }
  }
  fputc('}', f);
}

static int copy_file(const char *from, const char *to) {
  FILE *a = fopen(from, "rb");
  if (!a) return 0;
  FILE *b = fopen(to, "wb");
  if (!b) {
    fclose(a);
    return 0;
  }
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
  fclose(a);
  fclose(b);
  return 1;
}

static void bmp_to_png(const char *bmp, const char *png) {
  FILE *f = fopen(bmp, "rb");
  if (!f) return;
  uint8_t hdr[54];
  if (fread(hdr, 1, 54, f) != 54 || hdr[0] != 'B') {
    fclose(f);
    return;
  }
  int w = *(int32_t *)(hdr + 18), h = *(int32_t *)(hdr + 22), bpp = *(uint16_t *)(hdr + 28);
  int top_down = h < 0;
  if (h < 0) h = -h;
  uint32_t off = *(uint32_t *)(hdr + 10);
  if ((bpp != 24 && bpp != 32) || w <= 0 || h <= 0) {
    fclose(f);
    return;
  }
  int stride = ((w * bpp / 8) + 3) & ~3;
  uint8_t *row = malloc((size_t)stride);
  uint32_t *px = malloc((size_t)w * h * 4);
  fseek(f, (long)off, SEEK_SET);
  for (int y = 0; y < h; y++) {
    if (fread(row, 1, (size_t)stride, f) != (size_t)stride) break;
    int dy = top_down ? y : h - 1 - y;
    for (int x = 0; x < w; x++) {
      const uint8_t *p = row + x * (bpp / 8);
      px[(size_t)dy * w + x] = 0xff000000u | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
    }
  }
  fclose(f);
  ui_png_write(png, px, w, h, w);
  free(px);
  free(row);
}

static void write_state_json(const char *path, const Snap *s) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "{\n \"about\": \"SimGolf debug overlay state dump (task-113). Offsets and names come from the RE database.\",\n");
  fprintf(f, " \"date\": {\"year\": %d, \"month_index\": %d, \"day\": %d, \"tick\": %d},\n", s->year, s->month, s->day, s->tick);
  fprintf(f, " \"money\": %d, \"fun\": %d, \"skill\": %d, \"fame\": %d, \"difficulty\": %d, \"holes_built\": %d, \"game_flags\": %u, \"theme\": %d,\n",
          s->money, s->fun, s->skill, s->fame, s->difficulty, s->holes_built, (unsigned)s->flags, s->theme);
  fprintf(f, " \"canvas\": [%d, %d], \"map_w\": %d, \"frame\": %u, \"fps\": %.1f, \"textures\": ", s->canvas_w, s->canvas_h, s->map_w, s->frame, s->fps);
  json_str(f, s->textures ? s->textures : "");
  fprintf(f, ",\n \"cursor\": {\"mouse\": [%d, %d], \"hover_tile\": [%d, %d], \"camera_tile\": [%d, %d], \"tile_id\": %d, \"tile_name\": ", s->mouse_x, s->mouse_y, s->hover_x, s->hover_y, s->cam_x, s->cam_y, s->tile_id);
  json_str(f, s->tile_name);
  fprintf(f, ", \"tile_flags\": %d, \"object_index\": %d, \"object_kind\": ", s->tile_flags, s->obj_idx);
  json_str(f, s->obj_kind);
  fprintf(f, ", \"object\": [");
  for (int i = 0; i < 8; i++) fprintf(f, i ? ",%d" : "%d", s->obj[i]);
  fprintf(f, "]},\n \"sprite_under_cursor\": {\"point\": [%d, %d], \"result\": %d", s->pick_x, s->pick_y, s->pick_rc);
  if (s->pick_rc == 1) {
    fprintf(f, ", \"layer\": \"%s\", \"sheet\": ", s->pick.layer == HD_ART_TOP ? "golf UI (old interface or page)" : "course world");
    json_str(f, s->pick.file);
    fprintf(f, ", \"category\": \"%s\", \"look\": \"%s\", \"view\": %d, \"frame\": %d, \"variant\": %d, \"flip\": %d, \"box\": [%.0f, %.0f, %.0f, %.0f], \"entry\": %d, \"covering\": %d, \"shadow\": ",
            s->pick.category, s->pick.look, s->pick.view, s->pick.frame, s->pick.variant, s->pick.flip, s->pick.x, s->pick.y, s->pick.w, s->pick.h, s->pick.entry, s->pick.ncover);
    json_str(f, s->pick.shadow);
  }
  fprintf(f, ", \"native_layers_at_point\": [");
  for (int i = 0; i < s->nlayer_hit; i++) {
    if (i) fputc(',', f);
    json_str(f, s->layer_hit[i].name);
  }
  fprintf(f, "]},\n \"pro\": {\"name\": ");
  json_str(f, s->pro_name);
  fprintf(f, ", \"out_slot\": %d, \"skills\": {", s->pro_slot);
  for (int i = 0; i < 12; i++) {
    if (i) fputc(',', f);
    json_str(f, s->skill_name[i][0] ? s->skill_name[i] : "skill");
    fprintf(f, ":%d", s->pro[i]);
  }
  fprintf(f, "}},\n \"golfers\": [");
  int first = 1;
  for (int i = 0; i < NG; i++) {
    if (s->golf[i][0x21] == 0) continue;
    fprintf(f, first ? "\n  {\"slot\": %d, \"name\": " : ",\n  {\"slot\": %d, \"name\": ", i);
    first = 0;
    json_str(f, s->gname[i]);
    fprintf(f, ", \"fields\": ");
    json_record(f, s->golf[i], dbg_golfer_fields, NGF);
    fputc('}', f);
  }
  fprintf(f, "\n ],\n \"staff\": [");
  first = 1;
  for (int r = 0; r < NS; r++) {
    if (s->staff[r][0x12] == 0) continue;
    fprintf(f, first ? "\n  {\"record\": %d, \"name\": " : ",\n  {\"record\": %d, \"name\": ", r);
    first = 0;
    json_str(f, s->sname[r]);
    fprintf(f, ", \"fields\": ");
    json_record(f, s->staff[r], dbg_employee_fields, NEF);
    fputc('}', f);
  }
  fprintf(f, "\n ],\n \"holes\": [");
  first = 1;
  for (int h = 1; h < NH; h++) {
    if (s->holes[h][0] == 0) continue;
    fprintf(f, first ? "\n  {\"hole\": %d, \"fields\": " : ",\n  {\"hole\": %d, \"fields\": ", h);
    first = 0;
    json_record(f, s->holes[h], dbg_hole_fields, NHF);
    fputc('}', f);
  }
  fprintf(f, "\n ],\n \"watch\": [");
  for (int i = 0; i < s->nwatch; i++) {
    fprintf(f, i ? ",\n  {\"name\": " : "\n  {\"name\": ");
    json_str(f, s->watch[i].name);
    fprintf(f, ", \"addr\": \"0x%08x\", \"size\": %d, \"value\": %d}", s->watch[i].addr, s->watch[i].size, (int32_t)s->watch[i].value);
  }
  fprintf(f, "\n ],\n \"trace\": [");
  for (int i = 0; i < s->ntrace; i++) {
    const Sym *sy = g_nsym ? sym_at(s->trace[i].addr, 'f') : NULL;
    fprintf(f, i ? ",\n  {\"fn\": " : "\n  {\"fn\": ");
    json_str(f, sy ? sy->name : "?");
    fprintf(f, ", \"addr\": \"0x%08x\", \"calls\": %u}", s->trace[i].addr, s->trace[i].calls);
  }
  fprintf(f, "\n ]\n}\n");
  fclose(f);
}

static void report_now(void) {
  Snap *s = &g_work;
  if (!s->valid) {   // not on a course frame yet: take what the game has now
    s->valid = 1;
    memcpy(s->golf, (void *)(uintptr_t)DBG_GOLFER_BASE, sizeof s->golf);
    memcpy(s->staff, (void *)(uintptr_t)EMP_BASE, sizeof s->staff);
    memcpy(s->holes, (void *)(uintptr_t)DBG_HOLE_BASE, sizeof s->holes);
  }
  load_symbols();
  const char *base = mods_get("debug.report_dir");
  char root[600], dir[700];
  if (base && *base) snprintf(root, sizeof root, "%s", base);
  else snprintf(root, sizeof root, "%s/bugreports", host_exe_dir());
  time_t tt = time(NULL);
  struct tm tmv;
  localtime_s(&tmv, &tt);
  CreateDirectoryA(root, NULL);
  snprintf(dir, sizeof dir, "%s/report-%04d%02d%02d-%02d%02d%02d", root, tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  CreateDirectoryA(dir, NULL);
  char p[760];
  snprintf(p, sizeof p, "%s/screenshot.bmp", dir);
  int shot = hd_native_shot(p);
  char png[760];
  snprintf(png, sizeof png, "%s/screenshot.png", dir);
  if (shot == 1) {
    bmp_to_png(p, png);
    remove(p);
  }
  snprintf(p, sizeof p, "%s/state.json", dir);
  SDL_LockMutex(g_lock);
  Snap copy = g_snap;
  SDL_UnlockMutex(g_lock);
  write_state_json(p, copy.valid ? &copy : s);
  snprintf(p, sizeof p, "%s/golf.log", dir);
  int log_ok = copy_file(host_log_path(), p);
  snprintf(p, sizeof p, "%s/README.txt", dir);
  FILE *r = fopen(p, "wb");
  if (r) {
    fprintf(r, "SimGolf bug report (debug overlay, task-113)\n\nscreenshot.png  what was on screen (the overlay included)\nstate.json      game state, golfers, staff, holes, cursor, watch list, call trace\ngolf.log        the game's log\n\n"
               "Send this whole folder. Nothing in it leaves your PC until you send it.\n");
    fclose(r);
  }
  snprintf(g_last_report, sizeof g_last_report, "%s", dir);
  g_reports++;
  fprintf(stderr, "debug: bug report saved to %s (screenshot %s, log %s)\n", dir, shot == 1 ? "yes" : "no", log_ok ? "yes" : "no");
}

// runs on the game thread, every event pump and course frame
static void act(void) {
  poll_settings();
  if (mods_get_int("debug.report_now", 0)) {
    mods_set("debug.report_now", "0");
    g_report_req = 1;
  }
  if (g_report_req && host_now_ms() - g_report_t > 1500) {
    g_report_req = 0;
    g_report_t = host_now_ms();
    report_now();
  }
}

// ------------------------------------------------------------------ the panel (render thread)

static UiCtx *g_ui;
static float g_view_x, g_view_y, g_view_px = 1;
static char g_filter[2][40];
static volatile int g_focus = -1;    // which filter box takes the keys: 0 watch, 1 trace, -1 none
static int g_content_h[8];

typedef struct {
  UiCtx *c;
  const UiTokens *t;
  float x, y, w, rowh;
  float y0, y1;   // visible band
} Pane;

static int row_vis(const Pane *p) { return p->y + p->rowh >= p->y0 && p->y <= p->y1; }

static void kv(Pane *p, const char *k, const char *fmt, ...) {
  char v[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(v, sizeof v, fmt, ap);
  va_end(ap);
  if (row_vis(p)) {
    float kw = p->w * 0.38f;
    ui_text_fit(p->c, ui_rect(p->x, p->y, kw, p->rowh), UI_TYPE_CAPTION, p->t->text_secondary, k, 0);
    ui_text_fit(p->c, ui_rect(p->x + kw, p->y, p->w - kw, p->rowh), UI_TYPE_CAPTION, p->t->text_primary, v, 0);
  }
  p->y += p->rowh;
}

static void head(Pane *p, const char *s) {
  p->y += p->rowh * 0.3f;
  if (row_vis(p)) ui_text_fit(p->c, ui_rect(p->x, p->y, p->w, p->rowh), UI_TYPE_LABEL, p->t->accent, s, 0);
  p->y += p->rowh;
}

static void note(Pane *p, const char *s) {
  if (row_vis(p)) ui_text_fit(p->c, ui_rect(p->x, p->y, p->w, p->rowh), UI_TYPE_CAPTION, p->t->text_tertiary, s, 0);
  p->y += p->rowh;
}

static const char *const k_months[8] = {"March", "April", "May", "June", "July", "August", "September", "October"};

static void tab_state(Pane *p, const Snap *s) {
  head(p, "Game");
  kv(p, "date", "%s %d, %d  (tick %d)", k_months[s->month & 7], s->day, s->year, s->tick);
  kv(p, "money", "$%d", s->money);
  kv(p, "fun / skill / fame", "%d / %d / %d", s->fun, s->skill, s->fame);
  kv(p, "difficulty / theme", "%d / %d", s->difficulty, s->theme);
  kv(p, "holes built", "%d", s->holes_built > 0 ? s->holes_built - 1 : 0);
  char fl[160] = "";
  static const struct { uint32_t bit; const char *n; } fb[] = {{4, "paused"}, {0x100000, "first-tee rule"}, {0x200000, "tournament"}, {0x1000000, "sandbox"},
                                                               {0x20000000, "cheats"}, {0x40000000, "extra profiles"}};
  for (size_t i = 0; i < sizeof fb / sizeof fb[0]; i++)
    if ((uint32_t)s->flags & fb[i].bit) snprintf(fl + strlen(fl), sizeof fl - strlen(fl), "%s%s", fl[0] ? ", " : "", fb[i].n);
  kv(p, "flags", "0x%08x %s", (unsigned)s->flags, fl);
  int ng = 0, nw = 0, ns = 0;
  for (int i = 0; i < NG; i++) {
    int st = (int8_t)s->golf[i][0x21];
    if (st > 0) ng++;
    else if (st < 0) nw++;
  }
  for (int r = 0; r < NS; r++) ns += s->staff[r][0x12] != 0;
  kv(p, "golfers out / waiting", "%d / %d", ng, nw);
  kv(p, "staff records", "%d", ns);
  head(p, "Display");
  kv(p, "canvas", "%d x %d, map %d x %d", s->canvas_w, s->canvas_h, s->map_w, s->map_w);
  kv(p, "frame / rate", "%u / %.0f per s", s->frame, s->fps);
  kv(p, "textures / art pack", "%s / %s", s->textures ? s->textures : "?", s->pack_on ? "on" : "off");
  head(p, "Pro skills");
  kv(p, "pro", "%s%s", s->pro_name, s->pro_slot >= 0 ? " (out on the course)" : "");
  for (int i = 0; i < 10; i++) kv(p, s->skill_name[i][0] ? s->skill_name[i] : "skill", "%d", s->pro[i]);
  head(p, "Holes");
  for (int h = 1; h < NH; h++) {
    if (!s->holes[h][0]) continue;
    int len = *(int32_t *)(s->holes[h] + 4);
    char k[16];
    snprintf(k, sizeof k, "hole %d", h);
    kv(p, k, "par %d, length %d", s->holes[h][0], len);
  }
}

static void record_view(Pane *p, const uint8_t *rec, const DbgField *fl, int nf) {
  for (int i = 0; i < nf; i++) {
    char v[80];
    field_text(rec, &fl[i], v, sizeof v);
    char k[56];
    snprintf(k, sizeof k, "+%02x %s", fl[i].off, fl[i].name);
    kv(p, k, "%s", v);
  }
}

static float list_area(Pane *p, uint32_t id, float h, int n, float itemh, int (*draw_item)(Pane *, int, void *), void *u, int *clicked) {
  UiRect view = ui_rect(p->x - ui_s(p->c, 4), p->y, p->w + ui_s(p->c, 8), h);
  float off = ui_vscroll_begin(p->c, id, view, n * itemh, itemh * 2);
  int first = (int)((view.y - (view.y + off)) / itemh);
  if (first < 0) first = 0;
  int last = first + (int)(h / itemh) + 2;
  *clicked = -1;
  for (int i = first; i < n && i < last; i++) {
    Pane q = *p;
    q.y = view.y + off + i * itemh;
    q.rowh = itemh;
    if (draw_item(&q, i, u)) *clicked = i;
  }
  ui_vscroll_end(p->c);
  p->y += h;
  return h;
}

typedef struct { const Snap *s; int sel; } ListCtx;

static int golfer_item(Pane *q, int i, void *u) {
  ListCtx *lc = u;
  const Snap *s = lc->s;
  int slot = -1, k = 0;
  for (int g = 0; g < NG; g++)
    if (s->golf[g][0x21] != 0 && k++ == i) {
      slot = g;
      break;
    }
  if (slot < 0) return 0;
  const uint8_t *r = s->golf[slot];
  char name[64], line[96], right[48];
  snprintf(name, sizeof name, "#%d %s", slot, s->gname[slot][0] ? s->gname[slot] : "-");
  int st = (int8_t)r[0x21];
  snprintf(line, sizeof line, "%s, strokes %d, mood %d", st > 0 ? "hole" : "waiting", r[0x22], (int16_t)(r[0xa4] | r[0xa5] << 8));
  if (st > 0) snprintf(line, sizeof line, "hole %d, strokes %d, mood %d", st, r[0x22], (int16_t)(r[0xa4] | r[0xa5] << 8));
  snprintf(right, sizeof right, "tile %d,%d", *(int32_t *)r >> 10, *(int32_t *)(r + 4) >> 10);
  return ui_list_row(q->c, ui_idi(ui_id("dbg.g"), slot), ui_rect(q->x, q->y, q->w, q->rowh - 2), NULL, name, line, right, lc->sel == slot ? UI_SELECTED : 0);
}

static int golfer_slot_of_index(const Snap *s, int idx) {
  int k = 0;
  for (int g = 0; g < NG; g++)
    if (s->golf[g][0x21] != 0 && k++ == idx) return g;
  return -1;
}

static void tab_golfers(Pane *p, const Snap *s, float bottom) {
  int n = 0;
  for (int g = 0; g < NG; g++) n += s->golf[g][0x21] != 0;
  char t[64];
  snprintf(t, sizeof t, "%d golfers on the course or waiting", n);
  head(p, t);
  float itemh = ui_s(p->c, 44);
  float lh = (bottom - p->y) * 0.38f;
  ListCtx lc = {s, g_sel_golfer};
  int clicked;
  list_area(p, ui_id("dbg.glist"), lh, n, itemh, golfer_item, &lc, &clicked);
  if (clicked >= 0) g_sel_golfer = golfer_slot_of_index(s, clicked);
  if (g_sel_golfer < 0 || s->golf[g_sel_golfer][0x21] == 0) {
    int first = golfer_slot_of_index(s, 0);
    g_sel_golfer = first;
  }
  if (g_sel_golfer < 0) {
    note(p, "no golfers yet");
    return;
  }
  snprintf(t, sizeof t, "#%d %s: every field", g_sel_golfer, s->gname[g_sel_golfer]);
  head(p, t);
  UiRect view = ui_rect(p->x - ui_s(p->c, 4), p->y, p->w + ui_s(p->c, 8), bottom - p->y);
  float off = ui_vscroll_begin(p->c, ui_id("dbg.gdetail"), view, NGF * p->rowh, p->rowh * 3);
  Pane q = *p;
  q.y = view.y + off;
  q.y0 = view.y, q.y1 = view.y + view.h;
  record_view(&q, s->golf[g_sel_golfer], dbg_golfer_fields, NGF);
  ui_vscroll_end(p->c);
  p->y = bottom;
}

static int staff_item(Pane *q, int i, void *u) {
  ListCtx *lc = u;
  const Snap *s = lc->s;
  int rec = -1, k = 0;
  for (int r = 0; r < NS; r++)
    if (s->staff[r][0x12] != 0 && k++ == i) {
      rec = r;
      break;
    }
  if (rec < 0) return 0;
  const uint8_t *r = s->staff[rec];
  char name[64], line[96], right[48];
  snprintf(name, sizeof name, "#%d %s", rec, s->sname[rec][0] ? s->sname[rec] : "-");
  snprintf(line, sizeof line, "job %d, status 0x%02x, wages %d", (int8_t)r[0x13], r[0x12], *(int32_t *)(r + 0x24));
  snprintf(right, sizeof right, "tile %d,%d", r[0x10], r[0x11]);
  return ui_list_row(q->c, ui_idi(ui_id("dbg.s"), rec), ui_rect(q->x, q->y, q->w, q->rowh - 2), NULL, name, line, right, lc->sel == rec ? UI_SELECTED : 0);
}

static int staff_rec_of_index(const Snap *s, int idx) {
  int k = 0;
  for (int r = 0; r < NS; r++)
    if (s->staff[r][0x12] != 0 && k++ == idx) return r;
  return -1;
}

static void tab_staff(Pane *p, const Snap *s, float bottom) {
  int n = 0;
  for (int r = 0; r < NS; r++) n += s->staff[r][0x12] != 0;
  char t[64];
  snprintf(t, sizeof t, "%d staff and walker records", n);
  head(p, t);
  float itemh = ui_s(p->c, 44);
  ListCtx lc = {s, g_sel_staff};
  int clicked;
  list_area(p, ui_id("dbg.slist"), (bottom - p->y) * 0.38f, n, itemh, staff_item, &lc, &clicked);
  if (clicked >= 0) g_sel_staff = staff_rec_of_index(s, clicked);
  if (g_sel_staff < 0 || s->staff[g_sel_staff][0x12] == 0) g_sel_staff = staff_rec_of_index(s, 0);
  if (g_sel_staff < 0) {
    note(p, "no staff yet");
    return;
  }
  snprintf(t, sizeof t, "#%d %s: every field", g_sel_staff, s->sname[g_sel_staff]);
  head(p, t);
  UiRect view = ui_rect(p->x - ui_s(p->c, 4), p->y, p->w + ui_s(p->c, 8), bottom - p->y);
  float off = ui_vscroll_begin(p->c, ui_id("dbg.sdetail"), view, NEF * p->rowh, p->rowh * 3);
  Pane q = *p;
  q.y = view.y + off;
  q.y0 = view.y, q.y1 = view.y + view.h;
  record_view(&q, s->staff[g_sel_staff], dbg_employee_fields, NEF);
  ui_vscroll_end(p->c);
  p->y = bottom;
}

static void tab_cursor(Pane *p, const Snap *s) {
  head(p, "Tile under the cursor");
  kv(p, "golf cursor (canvas px)", "%d, %d", s->mouse_x, s->mouse_y);
  kv(p, "hover tile", "%d, %d   (camera tile %d, %d)", s->hover_x, s->hover_y, s->cam_x, s->cam_y);
  if (s->tile_id >= 0) {
    kv(p, "terrain", "%s (id %d)", s->tile_name[0] ? s->tile_name : "?", s->tile_id);
    char fl[96] = "";
    static const struct { int bit; const char *n; } tb[] = {{0x20, "path"}, {0x40, "connected"}, {0x100, "bridge"}, {0x200, "bench"}};
    for (size_t i = 0; i < sizeof tb / sizeof tb[0]; i++)
      if (s->tile_flags & tb[i].bit) snprintf(fl + strlen(fl), sizeof fl - strlen(fl), "%s%s", fl[0] ? ", " : "", tb[i].n);
    kv(p, "tile flags", "0x%04x %s", s->tile_flags, fl);
  } else {
    kv(p, "terrain", "off the map");
  }
  if (s->obj_idx >= 0) {
    kv(p, "object", "#%d %s", s->obj_idx, s->obj_kind[0] ? s->obj_kind : "?");
    kv(p, "  kind/row/col/rot", "%d / %d / %d / %d", s->obj[0], s->obj[1], s->obj[2], s->obj[3]);
    kv(p, "  level/value", "%d / %d", s->obj[4], s->obj[6]);
  } else {
    kv(p, "object", "none");
  }
  head(p, "Sprite under the cursor");
  kv(p, "point (canvas px)", "%d, %d  (last point outside this panel)", s->pick_x, s->pick_y);
  if (s->pick_rc < 0) {
    note(p, "art pack is off: no sheet or frame tags.");
    note(p, "Set Textures to HD (Options > Graphics) to identify sprites.");
  } else if (s->pick_rc == 0) {
    kv(p, "owner", "no tagged sprite owns this pixel");
    note(p, "(terrain, an untagged draw, or a native layer)");
  } else {
    const HdArtPick *k = &s->pick;
    kv(p, "layer", "%s", k->layer == HD_ART_TOP ? "golf's own UI (old interface)" : "course world");
    kv(p, "sheet", "%s", k->file);
    kv(p, "view / frame / variant", "%d / %d / %d  flip %d", k->view, k->frame, k->variant, k->flip);
    kv(p, "category / look", "%s / %s%s", k->category, k->look, k->hidden ? " (hidden)" : "");
    kv(p, "box (canvas px)", "%.0f,%.0f  %.0f x %.0f", k->x, k->y, k->w, k->h);
    if (k->shadow[0]) kv(p, "shadow sheet", "%s", k->shadow);
    kv(p, "entry / covering boxes", "%d of %d / %d", k->entry, k->entries, k->ncover);
  }
  head(p, "Native layers covering the point");
  if (!s->nlayer_hit) note(p, "none (the picture is golf's own)");
  for (int i = 0; i < s->nlayer_hit; i++) kv(p, s->layer_hit[i].name, "region id 0x%08x", s->layer_hit[i].id);
}

static int ci_contains(const char *hay, const char *needle) {
  if (!*needle) return 1;
  size_t n = strlen(needle);
  for (; *hay; hay++)
    if (!strncasecmp(hay, needle, n)) return 1;
  return 0;
}

static void filter_box(Pane *p, int which, const char *hint) {
  UiRect r = ui_rect(p->x, p->y, p->w, p->rowh * 1.5f);
  char label[80];
  snprintf(label, sizeof label, "%s%s", g_filter[which][0] ? g_filter[which] : hint, g_focus == which ? "_" : "");
  if (ui_button(p->c, ui_id(which ? "dbg.f1" : "dbg.f0"), r, label, UI_ICON_NONE, UI_BTN_GHOST, g_focus == which ? UI_SELECTED : 0)) g_focus = which;
  p->y += p->rowh * 1.7f;
}

static void tab_watch(Pane *p, const Snap *s, float bottom) {
  head(p, "Watching");
  if (!s->nwatch) note(p, "nothing yet: pick below, or [debug] watch= in mods.ini");
  for (int i = 0; i < s->nwatch; i++) {
    const char *nm = s->watch[i].name;
    char v[80];
    uint32_t val = s->watch[i].value;
    if (s->watch[i].sign) snprintf(v, sizeof v, "%d  (0x%x)", (int32_t)val, val);
    else snprintf(v, sizeof v, "%u  (0x%x)", val, val);
    if (row_vis(p)) {
      float kw = p->w * 0.46f;
      ui_text_fit(p->c, ui_rect(p->x, p->y, kw, p->rowh), UI_TYPE_CAPTION, p->t->text_secondary, nm, 0);
      ui_text_fit(p->c, ui_rect(p->x + kw, p->y, p->w - kw - ui_s(p->c, 52), p->rowh), UI_TYPE_CAPTION, p->t->text_primary, v, 0);
      if (ui_button(p->c, ui_idi(ui_id("dbg.wx"), i), ui_rect(p->x + p->w - ui_s(p->c, 48), p->y, ui_s(p->c, 48), p->rowh), "drop", UI_ICON_NONE, UI_BTN_GHOST, UI_COMPACT)) {
        for (int k = i; k < g_nwatch - 1; k++) g_watch[k] = g_watch[k + 1];
        g_nwatch--;
      }
    }
    p->y += p->rowh;
  }
  head(p, "Globals from the RE database");
  load_symbols();
  filter_box(p, 0, "type to filter, or 0xADDRESS");
  uint32_t a;
  if (parse_addr_text(g_filter[0], &a) && p->y + p->rowh < bottom) {
    char l[64];
    snprintf(l, sizeof l, "watch 0x%08x as 4 bytes", a);
    if (ui_button(p->c, ui_id("dbg.wadd"), ui_rect(p->x, p->y, p->w, p->rowh * 1.5f), l, UI_ICON_NONE, UI_BTN_PILL, 0)) {
      char nm[32];
      snprintf(nm, sizeof nm, "0x%08x", a);
      add_watch(nm, a, 4, 1);
    }
    p->y += p->rowh * 1.7f;
  }
  static int idx[2400];
  int n = 0;
  for (int i = 0; i < g_nglist && n < 2400; i++)
    if (ci_contains(g_sym[g_glist[i]].name, g_filter[0])) idx[n++] = g_glist[i];
  UiRect view = ui_rect(p->x - ui_s(p->c, 4), p->y, p->w + ui_s(p->c, 8), bottom - p->y);
  float rh = p->rowh * 1.35f;
  float off = ui_vscroll_begin(p->c, ui_id("dbg.wlist"), view, n * rh, rh * 3);
  int first = (int)(-off / rh);
  for (int i = first < 0 ? 0 : first; i < n && i < first + (int)(view.h / rh) + 2; i++) {
    const Sym *sy = &g_sym[idx[i]];
    UiRect rr = ui_rect(p->x, view.y + off + i * rh, p->w, rh);
    char l[96];
    snprintf(l, sizeof l, "%s", sy->name);
    if (ui_button(p->c, ui_idi(ui_id("dbg.wa"), (int)(sy - g_sym)), rr, l, UI_ICON_NONE, UI_BTN_GHOST, UI_COMPACT))
      add_watch(sy->name, sy->addr, sy->size == 1 || sy->size == 2 ? sy->size : 4, 1);
  }
  ui_vscroll_end(p->c);
  p->y = bottom;
}

static void tab_trace(Pane *p, const Snap *s, float bottom) {
  head(p, "Traced functions");
  if (!s->ntrace) note(p, "nothing yet: pick below, or [debug] trace= in mods.ini");
  for (int i = 0; i < s->ntrace; i++) {
    const Sym *sy = g_nsym ? sym_at(s->trace[i].addr, 'f') : NULL;
    char nm[64], v[64];
    snprintf(nm, sizeof nm, "%s", sy ? sy->name : "?");
    snprintf(v, sizeof v, "%u calls, %.0f/s", s->trace[i].calls, s->trace[i].rate);
    if (row_vis(p)) {
      float kw = p->w * 0.44f;
      ui_text_fit(p->c, ui_rect(p->x, p->y, kw, p->rowh), UI_TYPE_CAPTION, p->t->text_secondary, nm, 0);
      ui_text_fit(p->c, ui_rect(p->x + kw, p->y, p->w - kw - ui_s(p->c, 52), p->rowh), UI_TYPE_CAPTION, p->t->text_primary, v, 0);
      if (ui_button(p->c, ui_idi(ui_id("dbg.tx"), i), ui_rect(p->x + p->w - ui_s(p->c, 48), p->y, ui_s(p->c, 48), p->rowh), "drop", UI_ICON_NONE, UI_BTN_GHOST, UI_COMPACT))
        port_trace_select(s->trace[i].addr, 0);
    }
    p->y += p->rowh;
  }
  if (s->ntrace && ui_button(p->c, ui_id("dbg.tclear"), ui_rect(p->x, p->y, p->w * 0.4f, p->rowh * 1.4f), "Clear all", UI_ICON_NONE, UI_BTN_GHOST, UI_COMPACT)) port_trace_clear();
  if (s->ntrace) p->y += p->rowh * 1.5f;
  head(p, "Recent calls (frame, function, entered after)");
  for (int i = s->nlog - 1; i >= 0 && i > s->nlog - 9; i--) {
    const Sym *sy = g_nsym ? sym_at(s->log[i].addr, 'f') : NULL, *af = g_nsym ? sym_at(s->log[i].after, 'f') : NULL;
    char k[32], v[100];
    snprintf(k, sizeof k, "frame %u", s->log[i].frame);
    snprintf(v, sizeof v, "%s  <- %s", sy ? sy->name : "?", af ? af->name : "?");
    kv(p, k, "%s", v);
  }
  head(p, "Functions from the RE database");
  load_symbols();
  filter_box(p, 1, "type to filter, or 0xADDRESS");
  uint32_t a;
  if (parse_addr_text(g_filter[1], &a) && p->y + p->rowh < bottom) {
    char l[64];
    snprintf(l, sizeof l, "trace 0x%08x", a);
    if (ui_button(p->c, ui_id("dbg.tadd"), ui_rect(p->x, p->y, p->w, p->rowh * 1.5f), l, UI_ICON_NONE, UI_BTN_PILL, 0)) port_trace_select(a, 1);
    p->y += p->rowh * 1.7f;
  }
  static int idx[2400];
  int n = 0;
  for (int i = 0; i < g_nflist && n < 2400; i++)
    if (ci_contains(g_sym[g_flist[i]].name, g_filter[1])) idx[n++] = g_flist[i];
  UiRect view = ui_rect(p->x - ui_s(p->c, 4), p->y, p->w + ui_s(p->c, 8), bottom - p->y);
  float rh = p->rowh * 1.35f;
  float off = ui_vscroll_begin(p->c, ui_id("dbg.tlist"), view, n * rh, rh * 3);
  int first = (int)(-off / rh);
  for (int i = first < 0 ? 0 : first; i < n && i < first + (int)(view.h / rh) + 2; i++) {
    const Sym *sy = &g_sym[idx[i]];
    UiRect rr = ui_rect(p->x, view.y + off + i * rh, p->w, rh);
    if (ui_button(p->c, ui_idi(ui_id("dbg.ta"), (int)(sy - g_sym)), rr, sy->name, UI_ICON_NONE, UI_BTN_GHOST, UI_COMPACT)) port_trace_select(sy->addr, 1);
  }
  ui_vscroll_end(p->c);
  p->y = bottom;
}

static void tab_report(Pane *p, const Snap *s) {
  head(p, "Bug report");
  note(p, "Saves a screenshot, the game state and the log in one folder.");
  if (ui_button(p->c, ui_id("dbg.report"), ui_rect(p->x, p->y, p->w, p->rowh * 2.2f), "Save bug report", UI_ICON_NONE, UI_BTN_PRIMARY, 0)) g_report_req = 1;
  p->y += p->rowh * 2.6f;
  kv(p, "reports this session", "%d", g_reports);
  if (g_last_report[0]) {
    kv(p, "last saved", "%s", "");
    if (row_vis(p)) ui_text_fit(p->c, ui_rect(p->x, p->y, p->w, p->rowh), UI_TYPE_CAPTION, p->t->accent_soft_text, g_last_report, 0);
    p->y += p->rowh;
  }
  head(p, "About");
  note(p, "State comes from golf's memory, named by the RE database.");
  kv(p, "symbols", "%s", g_nsym ? g_sym_path : "none (run tools/re/build.sh)");
  kv(p, "toggle key", "%s%s%s%s", g_key_none ? "none" : "", g_mod_shift ? "Shift+" : "", g_mod_ctrl ? "Ctrl+" : "", g_mod_alt ? "Alt+" : "");
  kv(p, "frame", "%u", s->frame);
}

static void draw(const HdUiFrame *f, void *user) {
  (void)user;
  if (g_failed || !g_vis) {
    if (g_ui) ui_skip_frame(g_ui);
    return;
  }
  SDL_Renderer *r = f->renderer;
  if (!g_ui) {
    UiTokens t;
    char base[1024];
    snprintf(base, sizeof base, "%s", port_host_path("/ui/frosted_mint.toml"));
    ui_tokens_load_all(&t, base, NULL, 0);
    char fonts[1024];
    snprintf(fonts, sizeof fonts, "%s", port_host_path("/fonts/plus-jakarta-sans"));
    g_ui = ui_create(r, &t, fonts);
    if (!g_ui) {
      g_failed = 1;
      return;
    }
    ui_debug_name(g_ui, "debug");
    fprintf(stderr, "debug: overlay layer on\n");
  }
  int w = 0, h = 0;
  if (f->target) SDL_QueryTexture(f->target, NULL, NULL, &w, &h);
  else SDL_GetRendererOutputSize(r, &w, &h);
  g_view_x = (float)f->view.x, g_view_y = (float)f->view.y, g_view_px = f->px > 0 ? f->px : 1;
  Snap s;
  SDL_LockMutex(g_lock);
  s = g_snap;
  SDL_UnlockMutex(g_lock);
  double scale_cfg = mods_get_num("ui.scale", 0);
  float scale = scale_cfg >= 0.5 && scale_cfg <= 4 ? (float)scale_cfg : ui_scale_for_height(h);
  // the box around the picked sprite, on the game picture
  if (s.valid && s.pick_rc == 1 && f->target) {
    SDL_SetRenderTarget(r, f->target);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 255, 80, 80, 230);
    SDL_Rect rc = {(int)(f->view.x + s.pick.x * f->px), (int)(f->view.y + s.pick.y * f->px), (int)(s.pick.w * f->px), (int)(s.pick.h * f->px)};
    SDL_RenderDrawRect(r, &rc);
  }
  ui_begin(g_ui, f->target, w, h, scale, f->target, f->now_ms);
  const UiTokens *t = ui_tokens(g_ui);
  float m = ui_s(g_ui, 12);
  float pw = ui_s(g_ui, 500);
  if (pw > w * 0.55f) pw = w * 0.55f;
  UiRect panel = ui_rect(w - pw - m, m, pw, h - 2 * m);
  g_panel_x0 = (int)((panel.x - g_view_x) / g_view_px);
  ui_panel(g_ui, panel, ui_s(g_ui, t->radius_panel), UI_PANEL_READABLE);
  float pad = ui_s(g_ui, 14);
  UiRect in = ui_inset_all(panel, pad);
  UiFont tf = ui_font(g_ui, UI_TYPE_LABEL);
  ui_text_in(g_ui, ui_rect(in.x, in.y, in.w * 0.5f, tf.px * 1.6f), UI_TYPE_HEADING, t->text_primary, "Debug", 0);
  char fps[64];
  snprintf(fps, sizeof fps, "%s  %u", s.valid ? "live" : "waiting for a course frame", s.frame);
  ui_text_in(g_ui, ui_rect(in.x + in.w * 0.4f, in.y, in.w * 0.6f, tf.px * 1.6f), UI_TYPE_CAPTION, t->text_tertiary, fps, 1);
  float ty = in.y + tf.px * 2.0f;
  UiRect tabs = ui_rect(in.x, ty, in.w, ui_s(g_ui, 34));
  int sel = g_tab;
  if (ui_segmented(g_ui, ui_id("dbg.tabs"), tabs, k_tabs, NULL, NTABS, &sel)) {
    g_tab = sel;
    g_focus = -1;
  }
  float top = ty + ui_s(g_ui, 34) + ui_s(g_ui, 10);
  UiRect body = ui_rect(in.x, top, in.w, in.y + in.h - top);
  UiFont cf = ui_font(g_ui, UI_TYPE_CAPTION);
  Pane p = {g_ui, t, body.x, body.y, body.w, cf.px * 1.55f, body.y, body.y + body.h};
  if (!s.valid) {
    note(&p, "Waiting for a course frame. Load or start a course.");
    if (g_tab == TAB_REPORT) tab_report(&p, &s);
    else if (g_tab == TAB_WATCH) tab_watch(&p, &s, body.y + body.h);
    else if (g_tab == TAB_TRACE) tab_trace(&p, &s, body.y + body.h);
  } else if (g_tab == TAB_GOLFERS) {
    tab_golfers(&p, &s, body.y + body.h);
  } else if (g_tab == TAB_STAFF) {
    tab_staff(&p, &s, body.y + body.h);
  } else if (g_tab == TAB_WATCH) {
    tab_watch(&p, &s, body.y + body.h);
  } else if (g_tab == TAB_TRACE) {
    tab_trace(&p, &s, body.y + body.h);
  } else {
    float off = ui_vscroll_begin(g_ui, ui_idi(ui_id("dbg.scroll"), g_tab), body, (float)g_content_h[g_tab], p.rowh * 3);
    p.y = body.y + off;
    float y0 = p.y;
    if (g_tab == TAB_STATE) tab_state(&p, &s);
    else if (g_tab == TAB_CURSOR) tab_cursor(&p, &s);
    else tab_report(&p, &s);
    g_content_h[g_tab] = (int)(p.y - y0 + p.rowh);
    ui_vscroll_end(g_ui);
  }
  ui_end(g_ui);
  // what else is under the point the sprite pick looks at (native layers), for the next snapshot
  {
    float tx = g_view_x + (s.pick_x + 0.5f) * g_view_px, ty2 = g_view_y + (s.pick_y + 0.5f) * g_view_px;
    int n = 0;
    SDL_LockMutex(g_lock);
    for (int i = 0; i < ui_debug_count() && n < 8; i++) {
      char nm[24];
      uint32_t id = 0;
      UiRect rr = {0};
      if (ui_debug_probe(i, tx, ty2, g_ui, nm, sizeof nm, &id, &rr)) {
        snprintf(g_snap.layer_hit[n].name, sizeof g_snap.layer_hit[n].name, "%s", nm);
        g_snap.layer_hit[n].id = id, g_snap.layer_hit[n].r = rr;
        n++;
      }
    }
    g_snap.nlayer_hit = n;
    SDL_UnlockMutex(g_lock);
  }
}

// Game thread, before golf sees the event; mouse positions in canvas pixels.
static int input(SDL_Event *e, int course, void *user) {
  (void)user, (void)course;
  if (!g_ui || !g_vis) return 0;
  UiEvent u = {0};
  int cx = 0, cy = 0;
  switch (e->type) {
    case SDL_MOUSEMOTION:
      if (!e->motion.windowID) return 0;
      u.kind = UI_EV_MOVE;
      cx = e->motion.x, cy = e->motion.y;
      break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      if (!e->button.windowID) return 0;
      u.kind = e->type == SDL_MOUSEBUTTONDOWN ? UI_EV_DOWN : UI_EV_UP;
      u.button = e->button.button;
      cx = e->button.x, cy = e->button.y;
      break;
    case SDL_MOUSEWHEEL:
      if (!e->wheel.windowID) return 0;
      u.kind = UI_EV_WHEEL;
      u.wheel = (float)e->wheel.y;
      {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        cx = hd_raw_mouse_x, cy = hd_raw_mouse_y;
        (void)mx, (void)my;
      }
      break;
    case SDL_WINDOWEVENT:
      if (e->window.event == SDL_WINDOWEVENT_LEAVE || e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) ui_input_reset(g_ui);
      return 0;
    case SDL_TEXTINPUT:
      if (g_focus >= 0) {
        char *fb = g_filter[g_focus];
        size_t n = strlen(fb);
        for (const char *c = e->text.text; *c && n + 1 < sizeof g_filter[0]; c++)
          if (*c >= 32 && *c < 127) fb[n++] = *c;
        fb[n] = 0;
        return 1;
      }
      return 0;
    case SDL_KEYDOWN:
      if (g_focus >= 0) {
        SDL_Keycode k = e->key.keysym.sym;
        char *fb = g_filter[g_focus];
        if (k == SDLK_BACKSPACE && fb[0]) fb[strlen(fb) - 1] = 0;
        else if (k == SDLK_ESCAPE || k == SDLK_RETURN) g_focus = -1;
        return 1;   // typing in the filter box: the game gets no keys
      }
      return 0;
    case SDL_KEYUP:
      return g_focus >= 0;
    default:
      return 0;
  }
  u.x = g_view_x + (cx + 0.5f) * g_view_px;
  u.y = g_view_y + (cy + 0.5f) * g_view_px;
  int to = ui_route_event(g_ui, &u);
  if (u.kind == UI_EV_DOWN && (to & UI_ROUTE_GAME) && g_focus >= 0) g_focus = -1;
  return !(to & UI_ROUTE_GAME);
}

// ------------------------------------------------------------------ the switch

static void show(int on) {
  if (!g_lock) g_lock = SDL_CreateMutex();
  if (on && !g_registered && !g_failed) {
    g_registered = hd_ui_add_layer(draw, NULL) && hd_ui_add_input(input, NULL);
    if (!g_registered) {
      g_failed = 1;
      fprintf(stderr, "debug: overlay could not register a UI layer (all %d slots taken)\n", 12);
    }
  }
  g_vis = on && !g_failed;
  fprintf(stderr, "debug: overlay %s\n", g_vis ? "shown" : "hidden");
}

// Game thread, every event pump (win32.c), before the UI layers see the event.
int dbg_overlay_event(SDL_Event *e) {
  if (!g_lock) g_lock = SDL_CreateMutex();
  act();
  if (g_started && !g_vis && !g_failed && !g_registered) show(1);   // [debug] overlay=1
  else if (g_started && g_vis && !g_registered) show(1);
  if (g_swallow_up && e->type == SDL_KEYUP && e->key.keysym.sym == g_key) {
    g_swallow_up = 0;
    return 1;
  }
  if (e->type == SDL_KEYDOWN && !g_key_none && e->key.keysym.sym == g_key) {
    int mods = e->key.keysym.mod;
    int shift = (mods & KMOD_SHIFT) != 0, ctrl = (mods & KMOD_CTRL) != 0, alt = (mods & KMOD_ALT) != 0;
    if (shift == g_mod_shift && ctrl == g_mod_ctrl && alt == g_mod_alt) {
      if (!e->key.repeat) show(!g_vis);
      g_swallow_up = 1;
      return 1;
    }
  }
  return 0;
}
