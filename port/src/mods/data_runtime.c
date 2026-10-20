// Mod data layer, the game side (simgolf mod, task-19): loads the tables at
// startup, writes the image fields mods change, answers MDATA reads from the
// translated code and gives golfers with a personality their own thoughts.
// Native build only. docs/modding/DATA.md.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "platform/exe_path.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mods/data.h"
#include "mods/mdata.h"
#include "mods/mods.h"
#include "mods/living_club.h"

#ifndef SIMGOLF_DEFAULT_TABLES
#define SIMGOLF_DEFAULT_TABLES ""
#endif

static DataSet g_ds;
static int g_on, g_errors;
static char g_dir[MAX_PATH];

static void say(const char *line) {
  fprintf(stderr, "data: %s\n", line);
  char tmp[1200];
  snprintf(tmp, sizeof tmp, "data: %s", line);
  mods_report_add(tmp);
}

static void say_cb(void *ctx, const char *line) {
  (void)ctx;
  say(line);
}

// The live image: golf.exe's ranges are mapped at their own addresses.
static unsigned char *live_at(void *ctx, uint32_t addr, uint32_t len) {
  (void)ctx;
  if (addr < 0x400000u || addr >= 0x900000u || len > 0x900000u - addr) return NULL;
  return (unsigned char *)(uintptr_t)addr;
}

static int touch_cb(void *ctx, const char *mod, const char *key, const char *op) {
  (void)ctx;
  return mods_touch(mod, "data", key, op);
}

// ------------------------------------------------------------------ personalities

typedef struct {
  int row;          // personalities row
  int share, chance;
  char **lines[256];   // per thought event: Latin-1 lines (NULL: none)
  int nlines[256];
} Persona;

static Persona *g_p;
static int g_np;
static int g_event_row[256];   // thought event -> thought_events row (-1)

static char *to_latin1(const char *s) {
  char *out = (char *)malloc(strlen(s) + 1), *o = out;
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    if (*p < 0x80) *o++ = (char)*p++;
    else if ((*p & 0xe0) == 0xc0 && p[1]) { *o++ = (char)((*p & 0x1f) << 6 | (p[1] & 0x3f)); p += 2; }
    else { *o++ = '?'; p++; while ((*p & 0xc0) == 0x80) p++; }
  }
  *o = 0;
  return out;
}

static void build_personas(void) {
  for (int i = 0; i < 256; i++) g_event_row[i] = -1;
  DataTable *ev = data_table(&g_ds, "thought_events"), *pt = data_table(&g_ds, "personalities");
  if (!ev || !pt) return;
  int fe = data_field_index(ev, "event");
  for (int r = 0; r < ev->nrows; r++) {
    const JsonVal *v = data_value(ev, r, fe);
    if (v && v->type == JS_NUM && v->n >= 0 && v->n < 256) g_event_row[(int)v->n] = r;
  }
  int fs = data_field_index(pt, "share"), fc = data_field_index(pt, "chance"), ft = data_field_index(pt, "thoughts");
  g_p = (Persona *)calloc((size_t)pt->nrows + 1, sizeof *g_p);
  for (int r = 0; r < pt->nrows; r++) {
    if (!pt->row[r]) continue;
    Persona *p = &g_p[g_np];
    const JsonVal *s = data_value(pt, r, fs), *c = data_value(pt, r, fc), *th = data_value(pt, r, ft);
    p->row = r;
    p->share = s && s->type == JS_NUM ? (int)s->n : 0;
    p->chance = c && c->type == JS_NUM ? (int)c->n : 100;
    for (int e = 0; e < 256; e++) {
      if (g_event_row[e] < 0 || !th) continue;
      const JsonVal *list = json_get(th, ev->row_id[g_event_row[e]]);
      if (!list || list->type != JS_ARR || !list->len) continue;
      p->lines[e] = (char **)calloc((size_t)list->len, sizeof(char *));
      for (int i = 0; i < list->len; i++)
        if (list->items[i]->type == JS_STR) p->lines[e][p->nlines[e]++] = to_latin1(list->items[i]->s);
    }
    g_np++;
  }
  int total = 0;
  for (int i = 0; i < g_np; i++) total += g_p[i].share;
  if (g_np) {
    char line[300];
    snprintf(line, sizeof line, "%d personalities, %d%% of golfers have one%s", g_np, total > 100 ? 100 : total,
             total > 100 ? " (shares add up to more than 100: the first ones in load order fill it)" : "");
    say(line);
  }
}

static uint32_t mix(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
  return x;
}

// The golfer's profile id (+0xb6 of the 0x100-byte golfer slot): stable for a
// golfer and kept in saves, so a golfer keeps a personality across sessions.
static int profile_of(int slot) { return *(int16_t *)(uintptr_t)(0x5794b8u + (uint32_t)slot * 0x100u + 0xb6u); }

static const Persona *persona_of(int slot) {
  if (!g_np || slot < 0 || slot >= 0x98) return NULL;
  uint32_t h = mix((uint32_t)profile_of(slot) * 2654435761u ^ 0x5ca1ab1eu) % 100u;
  int cum = 0;
  for (int i = 0; i < g_np; i++) {
    cum += g_p[i].share;
    if ((int)h < cum) return &g_p[i];
  }
  return NULL;
}

const char *mdata_personality_of(int slot) {
  const Persona *p = persona_of(slot);
  return p ? data_table(&g_ds, "personalities")->row_id[p->row] : NULL;
}

static size_t g_len0;

void mdata_thought_begin(char *buf) { g_len0 = buf ? strlen(buf) : 0; }

void mdata_thought_end(int event, int arg, int arg2, int slot, char *buf) {
  if (!g_on || !buf || event < 0 || event > 255) return;
  char contextual[LC_TEXT];
  if (living_thought(slot, event, arg, arg2, contextual, sizeof contextual)) {
    // The original global buffer is large, but a line always fits its known
    // 600-character append budget. Preserve the routine's pre-existing prefix.
    if (strlen(buf) >= g_len0 && g_len0 < 600) {
      snprintf(buf + g_len0, 601 - g_len0, "%s", contextual);
      return;
    }
  }
  const Persona *p = persona_of(slot);
  if (!p || !p->nlines[event]) return;
  uint32_t h = mix((uint32_t)profile_of(slot) * 31u + (uint32_t)event * 7919u + (uint32_t)arg * 104729u +
                   (uint32_t)arg2 * 15485863u);
  if ((int)(h % 100u) >= p->chance) return;
  const char *line = p->lines[event][(h >> 8) % (uint32_t)p->nlines[event]];
  if (strlen(buf) < g_len0) return;   // the routine rebuilt the buffer: leave it
  buf[g_len0] = 0;
  strncat(buf, line, 600);
}

// ------------------------------------------------------------------ code constants

typedef struct {
  int t, r, f;              // table, row (static rows), field; t < 0: not a table value
  uint64_t checked;         // rows whose base was compared with the literal
} Cell;

static Cell g_cells[2048];
static int g_ncells;

static int new_cell(const char *table, const char *row, const char *field) {
  if (g_ncells >= (int)(sizeof g_cells / sizeof g_cells[0])) return -2;
  Cell *c = &g_cells[g_ncells];
  c->t = c->r = c->f = -1;
  DataTable *t = data_table(&g_ds, table);
  if (t) {
    c->t = (int)(t - g_ds.t);
    c->f = data_field_index(t, field);
    if (row) c->r = data_row_index(t, row);
    if (c->f < 0 || (row && c->r < 0)) {
      char line[300];
      snprintf(line, sizeof line, "WARNING the code reads %s.%s.%s, which the tables do not define", table,
               row ? row : "*", field);
      say(line);
      c->t = -1;
    }
  }
  return g_ncells++;
}

static int value_at(Cell *c, int r, int lit) {
  if (!g_on || c->t < 0) return lit;
  DataTable *t = &g_ds.t[c->t];
  if (r < 0 || r >= t->nfixed) return lit;
  if (r < 64 && !(c->checked >> r & 1)) {
    c->checked |= 1ull << r;
    const JsonVal *b = t->base[r] ? json_get(t->base[r], t->f[c->f].name) : NULL;
    if (!b || b->type != JS_NUM || (int)b->n != lit) {
      char line[300];
      snprintf(line, sizeof line, "WARNING drift: %s.%s.%s is %g in the table but %d in the game's code", t->name,
               t->row_id[r], t->f[c->f].name, b && b->type == JS_NUM ? b->n : -1.0, lit);
      say(line);
      g_errors++;
    }
  }
  if (!t->touched[(size_t)r * (size_t)t->nf + (size_t)c->f]) return lit;
  const JsonVal *v = data_value(t, r, c->f);
  return v && v->type == JS_NUM ? (int)v->n : lit;
}

int mdata_cell(int *cache, const char *table, const char *row, const char *field, int lit) {
  if (*cache == -1) *cache = new_cell(table, row, field);
  if (*cache < 0) return lit;
  Cell *c = &g_cells[*cache];
  return value_at(c, c->r, lit);
}

int mdata_at(int *cache, const char *table, int row, const char *field, int lit) {
  if (*cache == -1) *cache = new_cell(table, NULL, field);
  if (*cache < 0) return lit;
  return value_at(&g_cells[*cache], row, lit);
}

// ------------------------------------------------------------------ startup

static int is_dir(const char *p) {
  DWORD a = GetFileAttributesA(p);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void mdata_init(int argc, char **argv) {
  const char *off = getenv("SIMGOLF_DATA"), *dir = getenv("SIMGOLF_DATA_DIR");
  int disabled = off && *off == '0';
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--no-data")) disabled = 1;
    else if (!strcmp(argv[i], "--data-dir") && i + 1 < argc) dir = argv[++i];
  }
  if (disabled) {
    say("off (SIMGOLF_DATA=0 or --no-data): the game's own values, no table is read");
    return;
  }
  char exe[MAX_PATH], p[MAX_PATH + 32];
  exe_path_ansi(exe, MAX_PATH);
  char *slash = strrchr(exe, '\\');
  if (slash) *slash = 0;
  const char *gi = mods_get("golf.data_dir");
  snprintf(p, sizeof p, "%s\\data\\tables", exe);
  if (dir && *dir) snprintf(g_dir, sizeof g_dir, "%s", dir);
  else if (gi && *gi) {
    if (gi[0] != '/' && gi[0] != '\\' && gi[1] != ':') snprintf(g_dir, sizeof g_dir, "%s\\%s", exe, gi);   // relative: next to golf.exe
    else snprintf(g_dir, sizeof g_dir, "%s", gi);
  }
  else if (is_dir(p)) snprintf(g_dir, sizeof g_dir, "%s", p);
  else snprintf(g_dir, sizeof g_dir, "%s", SIMGOLF_DEFAULT_TABLES);
  for (char *q = g_dir; *q; q++)
    if (*q == '\\') *q = '/';

  data_load_defs(&g_ds, g_dir);
  DataImage img = {live_at, NULL};
  data_build_base(&g_ds, &img);
  char line[1200];
  int rows = 0;
  for (int i = 0; i < g_ds.n; i++) rows += g_ds.t[i].nfixed;
  snprintf(line, sizeof line, "%d tables, %d game rows from %s", g_ds.n, rows, g_dir);
  say(line);
  int before = g_ds.diags.n;
  for (int i = 0; i < mods_count(); i++) {
    const char *id = mods_id_at(i);
    int n = data_apply_mod(&g_ds, id, mods_dir(id), touch_cb, NULL);
    if (n) {
      snprintf(line, sizeof line, "mod %s: %d changes", id, n);
      say(line);
    }
  }
  for (int d = 0; d < g_ds.diags.n; d++) {
    const ModDiag *dg = &g_ds.diags.d[d];
    snprintf(line, sizeof line, "%s %s%s%s%s: %s", mdiag_level_name(dg->level), dg->code, dg->mod ? " (" : "",
             dg->mod ? dg->mod : "", dg->mod ? ")" : "", dg->msg);
    say(line);
    if (dg->level == MD_ERROR) g_errors++;
  }
  (void)before;
  int w = data_write_image(&g_ds, &img, NULL, NULL);
  data_report_changes(&g_ds, say_cb, NULL);
  snprintf(line, sizeof line, "%d values changed by mods, %d written to the game's tables, %d problems", g_ds.changes,
           w, mdiags_count(&g_ds.diags, MD_ERROR));
  say(line);
  g_on = 1;
  build_personas();
  living_init(g_dir);
}

int mdata_enabled(void) { return g_on; }
int mdata_changes(void) { return g_ds.changes; }
int mdata_errors(void) { return g_errors; }

char *mdata_table_json(const char *table, int effective) {
  DataTable *t = data_table(&g_ds, table);
  if (!t) return NULL;
  JsonVal *v = data_table_json(t, effective);
  char *s = json_write_compact(v);
  json_free(v);
  return s;
}
