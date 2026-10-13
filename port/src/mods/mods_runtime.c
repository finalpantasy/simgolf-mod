// Mod platform, the game side (simgolf mod, task-11): startup, the loaded-mod
// queries, the run-time touch registry and the save records. Native build;
// everything that decides (parsing, resolving, conflicts) is in the portable
// files shared with tools/modctl.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mods/mods.h"
#include "mods/mods_core.h"

#ifndef SIMGOLF_DEFAULT_MODS
#define SIMGOLF_DEFAULT_MODS ""
#endif
#ifndef SIMGOLF_DEFAULT_MODS_INI
#define SIMGOLF_DEFAULT_MODS_INI ""
#endif

static ModSession g_ss;
#define g_set g_ss.set
#define g_res g_ss.res
static char g_root[MAX_PATH];
static char g_ini[MAX_PATH];
static char g_profile[64] = "vanilla";
static int g_no_mods, g_inited;
static char **g_lines;
static int g_nlines;
static char g_save_check[600];

// The player's own choices (card task-24, the Options page): options.ini next to golf.exe (SIMGOLF_OPTIONS_FILE
// names another), "[section]" and "key=value" lines like mods.ini. Loaded over mods.ini, rewritten whole
// whenever the page changes something, so the file only ever holds what the player chose.
#define OPT_MAX 256
static char g_opt_path[MAX_PATH];
static char *g_okey[OPT_MAX], *g_oval[OPT_MAX];
static int g_nopt;

// ------------------------------------------------------------------ log

static void log_line(void *ctx, const char *line) {
  (void)ctx;
  fprintf(stderr, "mods: %s\n", line);
  char **a = (char **)realloc(g_lines, (size_t)(g_nlines + 1) * sizeof *a);
  if (!a) return;
  g_lines = a;
  g_lines[g_nlines++] = mods_strdup(line);
}

int mods_report_count(void) { return g_nlines; }
// Later layers (the data tables, task-19) add their lines to the same report.
void mods_report_add(const char *line) {
  char **a = (char **)realloc(g_lines, (size_t)(g_nlines + 1) * sizeof *a);
  if (!a) return;
  g_lines = a;
  g_lines[g_nlines++] = mods_strdup(line);
}
const char *mods_report_line(int i) { return i >= 0 && i < g_nlines ? g_lines[i] : NULL; }

// ------------------------------------------------------------------ paths

static void slashes(char *p) {
  for (; *p; p++)
    if (*p == '\\') *p = '/';
}

static int is_dir(const char *p) {
  DWORD a = GetFileAttributesA(p);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int is_file(const char *p) {
  DWORD a = GetFileAttributesA(p);
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void full_path(char *dst, size_t len, const char *src) {
  char tmp[MAX_PATH];
  if (GetFullPathNameA(src, MAX_PATH, tmp, NULL)) snprintf(dst, len, "%s", tmp);
  else snprintf(dst, len, "%s", src);
  slashes(dst);
  size_t n = strlen(dst);
  while (n > 3 && dst[n - 1] == '/') dst[--n] = 0;
}

static const char *env(const char *name) {
  const char *v = getenv(name);
  return v && *v ? v : NULL;
}

// ------------------------------------------------------------------ the mods layer

// Puts every setting the loaded mods decide into the store's mods layer.
static void apply_settings(void) {
  for (int i = 0; i < g_res.nkeys; i++) {
    const ModKeyUse *k = &g_res.keys[i];
    if (k->kind != MT_SETTING) continue;
    const char *origin = k->winner >= 0 ? g_set.m[k->winner].id : k->n ? g_set.m[k->mod[k->n - 1]].id : NULL;
    mods_store_put(MS_MODS, k->key, k->result, origin);
  }
}

void mods_init(int argc, char **argv) {
  if (g_inited) return;
  g_inited = 1;
  const char *ini_arg = env("SIMGOLF_MODS"), *dir_arg = env("SIMGOLF_MODS_DIR"), *prof_arg = env("SIMGOLF_PROFILE");
  const char *nm = env("SIMGOLF_NO_MODS");
  g_no_mods = nm && *nm == '1';
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--mods") && i + 1 < argc) ini_arg = argv[++i];
    else if (!strcmp(argv[i], "--mods-dir") && i + 1 < argc) dir_arg = argv[++i];
    else if (!strcmp(argv[i], "--profile") && i + 1 < argc) prof_arg = argv[++i];
    else if (!strcmp(argv[i], "--no-mods")) g_no_mods = 1;
  }
  char exe[MAX_PATH], p[MAX_PATH + 32];
  GetModuleFileNameA(NULL, exe, MAX_PATH);
  slashes(exe);
  char *slash = strrchr(exe, '/');
  if (slash) *slash = 0;

  // golf.ini as "golf.<key>"; it may also name the mods folder, profile and mods.ini.
  snprintf(p, sizeof p, "%s/golf.ini", exe);
  char *gi = mods_read_file(p, NULL);
  if (gi) {
    size_t n = strlen(gi);
    char *text = (char *)malloc(n + 8);
    memcpy(text, "[golf]\n", 7);
    memcpy(text + 7, gi, n + 1);
    mods_store_parse_ini(MS_GOLFINI, text);
    free(text);
    free(gi);
  }
  const char *gi_dir = mods_get("golf.mods_dir"), *gi_prof = mods_get("golf.profile"), *gi_ini = mods_get("golf.mods_ini");

  // The mods folder.
  if (dir_arg) full_path(g_root, sizeof g_root, dir_arg);
  else if (gi_dir && *gi_dir) full_path(g_root, sizeof g_root, gi_dir);
  else {
    snprintf(p, sizeof p, "%s/mods", exe);
    if (is_dir(p)) full_path(g_root, sizeof g_root, p);
    else if (*SIMGOLF_DEFAULT_MODS) full_path(g_root, sizeof g_root, SIMGOLF_DEFAULT_MODS);
  }

  // mods.ini: the player's switches and values.
  if (ini_arg) full_path(g_ini, sizeof g_ini, ini_arg);
  else if (gi_ini && *gi_ini) full_path(g_ini, sizeof g_ini, gi_ini);
  else {
    snprintf(p, sizeof p, "%s/mods.ini", exe);
    if (is_file(p)) full_path(g_ini, sizeof g_ini, p);
    else if (*SIMGOLF_DEFAULT_MODS_INI && is_file(SIMGOLF_DEFAULT_MODS_INI))
      full_path(g_ini, sizeof g_ini, SIMGOLF_DEFAULT_MODS_INI);
  }
  int nini = -1;
  if (g_ini[0] && !g_no_mods) nini = mods_store_load_ini(MS_INI, g_ini);
  {   // options.ini: the player's choices from the Options page, over mods.ini
    const char *of = env("SIMGOLF_OPTIONS_FILE");
    if (of && *of) full_path(g_opt_path, sizeof g_opt_path, of);
    else snprintf(g_opt_path, sizeof g_opt_path, "%s/options.ini", exe);
    char *text = g_no_mods ? NULL : mods_read_file(g_opt_path, NULL);
    if (text) {
      char section[64] = "";
      for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
          *next++ = 0;
          while (*next == '\r' || *next == '\n') next++;
        }
        char *sl = line;
        line = next;
        while (*sl == ' ' || *sl == '\t') sl++;
        if (!*sl || *sl == ';' || *sl == '#') continue;
        if (*sl == '[') {
          char *e = strchr(sl, ']');
          if (e) *e = 0;
          snprintf(section, sizeof section, "%s", sl + 1);
          continue;
        }
        char *eq = strchr(sl, '=');
        if (!eq) continue;
        *eq = 0;
        char full[160];
        snprintf(full, sizeof full, "%s.%s", section, sl);
        if (g_nopt < OPT_MAX) {
          g_okey[g_nopt] = mods_strdup(full);
          g_oval[g_nopt++] = mods_strdup(eq + 1);
          mods_store_put(MS_INI, full, eq + 1, NULL);
        }
      }
      free(text);
    }
  }

  // Profile, switches, scan, resolve: the same code as tools/modctl.
  if (g_no_mods) prof_arg = NULL;
  else if (!prof_arg && gi_prof && *gi_prof) prof_arg = gi_prof;
  mods_session_open(&g_ss, g_root, prof_arg, g_no_mods);
  snprintf(g_profile, sizeof g_profile, "%s", g_ss.profile);
  apply_settings();

  char line[1200];
  if (g_no_mods) log_line(NULL, "--no-mods: the original game, mods.ini ignored");
  else if (g_ini[0]) {
    snprintf(line, sizeof line, "mods.ini %s (%s)", g_ini, nini < 0 ? "missing, created on persist" : "loaded");
    log_line(NULL, line);
  }
  mods_session_report(&g_ss, g_root, log_line, NULL);
}

// ------------------------------------------------------------------ queries

static int loaded_index(const char *id) {
  if (!id) return -1;
  for (int i = 0; i < g_res.norder; i++) {
    const ModManifest *m = &g_set.m[g_res.order[i]];
    if (!strcmp(m->id, id)) return g_res.order[i];
  }
  for (int i = 0; i < g_res.norder; i++) {
    const ModManifest *m = &g_set.m[g_res.order[i]];
    for (int k = 0; k < m->nprovides; k++)
      if (!strcmp(m->provides[k], id)) return g_res.order[i];
  }
  return -1;
}

int mods_enabled(const char *id) { return loaded_index(id) >= 0; }
int mods_count(void) { return g_res.norder; }
const char *mods_id_at(int i) { return i >= 0 && i < g_res.norder ? g_set.m[g_res.order[i]].id : NULL; }

const char *mods_version(const char *id) {
  int i = loaded_index(id);
  return i >= 0 ? g_set.m[i].version : NULL;
}

const char *mods_dir(const char *id) {
  int i = loaded_index(id);
  return i >= 0 ? g_set.m[i].dir : NULL;
}

const char *mods_root(void) { return g_root; }
const char *mods_profile(void) { return g_profile; }
const char *mods_ini_path(void) { return g_ini; }

const char *mods_option_path(void) { return g_opt_path; }

static void option_write(void) {
  if (!g_opt_path[0]) return;
  FILE *f = fopen(g_opt_path, "w");
  if (!f) return;
  fprintf(f, "; options.ini: your choices from the in-game Options page (delete this file to go back to the defaults)\n");
  char done[OPT_MAX] = {0};
  for (int i = 0; i < g_nopt; i++) {
    if (done[i]) continue;
    const char *dot = strchr(g_okey[i], '.');
    int sl = dot ? (int)(dot - g_okey[i]) : 0;
    if (sl) fprintf(f, "\n[%.*s]\n", sl, g_okey[i]);
    for (int j = i; j < g_nopt; j++) {
      const char *d2 = strchr(g_okey[j], '.');
      int l2 = d2 ? (int)(d2 - g_okey[j]) : 0;
      if (done[j] || l2 != sl || _strnicmp(g_okey[j], g_okey[i], (size_t)sl)) continue;
      fprintf(f, "%s=%s\n", g_okey[j] + (sl ? sl + 1 : 0), g_oval[j]);
      done[j] = 1;
    }
  }
  fclose(f);
}

// The Options page sets a value: it takes effect now (the store); the file follows with mods_option_flush.
int mods_option_put(const char *key, const char *value) {
  if (!key || !*key || !value) return 0;
  if (!mods_store_put(MS_INI, key, value, NULL)) return 0;
  int i = 0;
  while (i < g_nopt && _stricmp(g_okey[i], key)) i++;
  if (i == g_nopt) {
    if (g_nopt >= OPT_MAX) return 0;
    g_okey[g_nopt] = mods_strdup(key);
    g_oval[g_nopt++] = mods_strdup(value);
  } else {
    free(g_oval[i]);
    g_oval[i] = mods_strdup(value);
  }
  return 1;
}

void mods_option_flush(void) {
  if (!g_no_mods) option_write();
}

// ... and kept in options.ini at once.
int mods_option_set(const char *key, const char *value) {
  if (!mods_option_put(key, value)) return 0;
  mods_option_flush();
  return 1;
}

int mods_ini_save(void) {
  if (g_no_mods || !g_ini[0]) return 0;   // under --no-mods the file is not ours to rewrite
  return mods_store_save_ini(g_ini);
}

// ------------------------------------------------------------------ run-time touches

int mods_touch(const char *mod_id, const char *kind, const char *key, const char *op) {
  int k = mods_kind_parse(kind), o = mods_op_parse(op), i = -1;
  for (int j = 0; mod_id && j < g_res.norder; j++)
    if (!strcmp(g_set.m[g_res.order[j]].id, mod_id)) i = g_res.order[j];
  if (k < 0 || o < 0 || i < 0 || !key || !*key) return -1;
  ModManifest *m = &g_set.m[i];
  // The same touch twice (declared in mod.toml and registered at run time,
  // task-19) counts once.
  int dup = 0;
  for (int t = 0; t < m->ntouches && !dup; t++)
    dup = m->touches[t].kind == k && m->touches[t].op == o && !_stricmp(m->touches[t].key, key);
  if (!dup) {
    ModTouch *a = (ModTouch *)realloc(m->touches, (size_t)(m->ntouches + 1) * sizeof *a);
    if (!a) return -1;
    m->touches = a;
    memset(&a[m->ntouches], 0, sizeof *a);
    a[m->ntouches].kind = k;
    a[m->ntouches].op = o;
    a[m->ntouches].key = mods_strdup(key);
    m->ntouches++;
  }

  // Recompute who wins every key; log what changed for this key.
  int keep = 0;
  for (int d = 0; d < g_res.diags.n; d++) {
    ModDiag *dg = &g_res.diags.d[d];
    if (!strcmp(dg->code, "conflict") || !strcmp(dg->code, "merged")) {
      free(dg->mod);
      free(dg->msg);
      continue;
    }
    g_res.diags.d[keep++] = *dg;
  }
  g_res.diags.n = keep;
  for (int q = 0; q < g_res.nkeys; q++) {
    ModKeyUse *u = &g_res.keys[q];
    for (int j = 0; j < u->n; j++) free(u->value[j]);
    free(u->key); free(u->mod); free(u->op); free(u->value); free(u->result);
  }
  free(g_res.keys);
  g_res.keys = NULL;
  g_res.nkeys = 0;
  int before = g_res.diags.n;
  mods_compute_keys(&g_set, g_res.order, g_res.norder, &g_res);
  for (int d = before; d < g_res.diags.n; d++) {
    const ModDiag *dg = &g_res.diags.d[d];
    char line[1200];
    if (!strstr(dg->msg, key)) continue;   // only the key this touch is about
    snprintf(line, sizeof line, "%s %s: %s", mdiag_level_name(dg->level), dg->code, dg->msg);
    log_line(NULL, line);
  }
  if (k == MT_SETTING) apply_settings();

  for (int q = 0; q < g_res.nkeys; q++) {
    ModKeyUse *u = &g_res.keys[q];
    if (u->kind != k || _stricmp(u->key, key)) continue;
    // This touch is the mod's last one on the key: it takes effect unless an
    // exclusive touch of another mod comes after it.
    int last = -1;
    for (int j = 0; j < u->n; j++)
      if (u->mod[j] == i) last = j;
    for (int j = last + 1; j < u->n; j++)
      if (mods_op_exclusive(u->op[j]) && u->mod[j] != i) return 0;
    return 1;
  }
  return 1;
}

const char *mods_winner(const char *kind, const char *key) {
  int k = mods_kind_parse(kind);
  for (int q = 0; key && q < g_res.nkeys; q++)
    if (g_res.keys[q].kind == k && !_stricmp(g_res.keys[q].key, key))
      return g_res.keys[q].winner >= 0 ? g_set.m[g_res.keys[q].winner].id : NULL;
  return NULL;
}

// ------------------------------------------------------------------ saves

// A saved game: "Saved Games/<name>.sve" (top10.sve, the high scores, is not one).
static int is_save(const char *path) {
  size_t n = path ? strlen(path) : 0;
  if (n <= 4 || _stricmp(path + n - 4, ".sve")) return 0;
  char low[MAX_PATH + 32];
  snprintf(low, sizeof low, "%s", path);
  slashes(low);
  _strlwr(low);
  return strstr(low, "/saved games/") != NULL;
}

static const char *base_name(const char *path) {
  const char *s = strrchr(path, '/'), *b = strrchr(path, '\\');
  if (b > s) s = b;
  return s ? s + 1 : path;
}

void mods_save_written(const char *path) {
  if (!g_inited || !is_save(path)) return;
  ModSaveRec rec;
  memset(&rec, 0, sizeof rec);
  rec.format = MODS_SAVEREC_VERSION;
  snprintf(rec.platform, sizeof rec.platform, "%s", MODS_PLATFORM_VERSION);
  snprintf(rec.profile, sizeof rec.profile, "%s", g_profile);
  if (!mods_file_crc(path, &rec.save_size, &rec.save_crc)) return;
  for (int i = 0; i < g_res.norder && rec.n < 64; i++) {
    const ModManifest *m = &g_set.m[g_res.order[i]];
    snprintf(rec.id[rec.n], sizeof rec.id[0], "%s", m->id);
    snprintf(rec.version[rec.n], sizeof rec.version[0], "%s", m->version);
    rec.n++;
  }
  static char text[16384];
  char side[MAX_PATH + 32], tmp[MAX_PATH + 40];
  snprintf(side, sizeof side, "%s.mods.toml", path);
  snprintf(tmp, sizeof tmp, "%s.tmp", side);
  if (!saverec_format(&rec, text, sizeof text)) return;
  FILE *f = fopen(tmp, "wb");
  if (!f) {
    fprintf(stderr, "mods: save %s: cannot write the mod record\n", base_name(path));
    return;
  }
  int written = fputs(text, f) != EOF && !ferror(f);
  if (fclose(f) != 0) written = 0;
  if (!written || !MoveFileExA(tmp, side, MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileA(tmp);
    fprintf(stderr, "mods: save %s: cannot replace the mod record\n", base_name(path));
    return;
  }
  fprintf(stderr, "mods: save %s: recorded %d mods (profile \"%s\") in %s\n", base_name(path), rec.n, g_profile,
          base_name(side));
}

void mods_save_opened(const char *path) {
  if (!g_inited || !is_save(path)) return;
  uint32_t size, crc;
  if (!mods_file_crc(path, &size, &crc)) return;
  // The game opens a save several times in a row (list entry, details, the
  // load itself): one check per file per burst, a later load checks again.
  static struct { uint32_t size, crc; ULONGLONG at; char name[64]; } seen[32];
  static int nseen;
  const char *name = base_name(path);
  ULONGLONG now = GetTickCount64();
  int slot = -1;
  for (int i = 0; i < nseen && slot < 0; i++)
    if (seen[i].size == size && seen[i].crc == crc && !_stricmp(seen[i].name, name)) slot = i;
  if (slot >= 0 && now - seen[slot].at < 3000) {
    seen[slot].at = now;
    return;
  }
  if (slot < 0) slot = nseen < 32 ? nseen++ : (int)(crc % 32);
  seen[slot].size = size;
  seen[slot].crc = crc;
  seen[slot].at = now;
  snprintf(seen[slot].name, sizeof seen[slot].name, "%s", name);

  char side[MAX_PATH + 32];
  snprintf(side, sizeof side, "%s.mods.toml", path);
  char *text = mods_read_file(side, NULL);
  ModSaveRec rec;
  int have = text && saverec_parse(&rec, text);
  free(text);
  if (have && (rec.save_size != size || rec.save_crc != crc)) {
    snprintf(g_save_check, sizeof g_save_check,
             "save %s: its mod record belongs to an earlier version of the file; treated as a save without mods", name);
    have = 0;
  } else if (!have) {
    snprintf(g_save_check, sizeof g_save_check,
             "save %s: no mod record (made by the original game or before mods existed); it loads as it is", name);
  }
  if (!have) {
    if (g_res.norder)
      snprintf(g_save_check + strlen(g_save_check), sizeof g_save_check - strlen(g_save_check),
               ", with the %d mods loaded now", g_res.norder);
    fprintf(stderr, "mods: %s\n", g_save_check);
    return;
  }
  ModDiags ds = {0};
  int warns = saverec_check(&rec, &g_set, &g_res, &ds);
  snprintf(g_save_check, sizeof g_save_check, "save %s: made with %d mods (profile \"%s\"): %s", name, rec.n,
           rec.profile, warns ? "WARNING, the mods differ" : "the same mods are loaded");
  fprintf(stderr, "mods: %s\n", g_save_check);
  for (int i = 0; i < ds.n; i++)
    fprintf(stderr, "mods: %s save %s: %s\n", ds.d[i].level == MD_WARN ? "WARNING" : "note", name, ds.d[i].msg);
  mdiags_free(&ds);
}

const char *mods_last_save_check(void) { return g_save_check; }
