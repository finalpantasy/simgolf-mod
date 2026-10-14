// The layered settings store (simgolf mod, task-11): golf.ini < loaded mods <
// mods.ini < live changes. Replaces the bridge's own key/value store (task-13):
// bridge_mod() and the bridge's mods_get/mods_set read and write this one.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "mods/mods.h"
#include "mods/mods_core.h"

typedef struct {
  char *key;
  char *val[MS__COUNT];
  char *origin;   // mod id behind the MS_MODS value
} Entry;

static Entry *g_e;
static int g_n, g_cap;
static unsigned g_serial;

static int ieq(const char *a, const char *b) {
  for (; *a && *b; a++, b++)
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
  return *a == *b;
}

static Entry *find(const char *key) {
  for (int i = 0; i < g_n; i++)
    if (ieq(g_e[i].key, key)) return &g_e[i];
  return NULL;
}

void mods_store_clear(void) {
  for (int i = 0; i < g_n; i++) {
    free(g_e[i].key);
    free(g_e[i].origin);
    for (int l = 0; l < MS__COUNT; l++) free(g_e[i].val[l]);
  }
  free(g_e);
  g_e = NULL;
  g_n = g_cap = 0;
  g_serial++;
}

int mods_store_put(int layer, const char *key, const char *value, const char *origin) {
  if (!key || !*key || layer < 0 || layer >= MS__COUNT || strlen(key) > 127) return 0;
  Entry *e = find(key);
  if (!e) {
    if (g_n == g_cap) {
      int cap = g_cap ? g_cap * 2 : 2048;
      Entry *a = (Entry *)realloc(g_e, (size_t)cap * sizeof *a);
      if (!a) return 0;
      g_e = a;
      g_cap = cap;
    }
    e = &g_e[g_n];
    memset(e, 0, sizeof *e);
    e->key = mods_strdup(key);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);   // complete before readers on other threads can see it
    g_n++;
  }
  // The Options page (render thread) and the game thread read the store at the same time: the replaced string
  // is kept (a few bytes per change a player makes), so a pointer mods_get handed out never dangles, and the
  // table starts big enough that it is never moved in practice.
  e->val[layer] = value ? mods_strdup(value) : NULL;
  if (layer == MS_MODS) {
    free(e->origin);
    e->origin = mods_strdup(origin);
  }
  g_serial++;
  return 1;
}

static const char *effective(const Entry *e, int *layer) {
  for (int l = MS__COUNT - 1; l >= 0; l--)
    if (e->val[l]) {
      if (layer) *layer = l;
      return e->val[l];
    }
  return NULL;
}

const char *mods_get(const char *key) {
  Entry *e = key ? find(key) : NULL;
  return e ? effective(e, NULL) : NULL;
}

int mods_get_int(const char *key, int def) {
  const char *v = mods_get(key);
  if (!v || !*v) return def;
  char *end;
  double d = strtod(v, &end);
  return end != v ? (int)d : def;
}

double mods_get_num(const char *key, double def) {
  const char *v = mods_get(key);
  if (!v || !*v) return def;
  char *end;
  double d = strtod(v, &end);
  return end != v ? d : def;
}

const char *mods_origin(const char *key) {
  static char buf[96];
  Entry *e = key ? find(key) : NULL;
  int l = -1;
  if (!e || !effective(e, &l)) return NULL;
  switch (l) {
    case MS_LIVE: return "live";
    case MS_INI: return "mods.ini";
    case MS_GOLFINI: return "golf.ini";
    default:
      snprintf(buf, sizeof buf, "mod %s", e->origin ? e->origin : "?");
      return buf;
  }
}

int mods_set(const char *key, const char *value) { return mods_store_put(MS_LIVE, key, value ? value : "", NULL); }

unsigned mods_serial(void) { return g_serial; }

int mods_value_count(void) {
  int n = 0;
  for (int i = 0; i < g_n; i++) n += effective(&g_e[i], NULL) != NULL;
  return n;
}

const char *mods_value_key(int i) {
  for (int k = 0; k < g_n; k++)
    if (effective(&g_e[k], NULL) && i-- == 0) return g_e[k].key;
  return NULL;
}

int mods_store_parse_ini(int layer, char *text) {
  char section[64] = "";
  int n = 0;
  for (char *line = text; line && *line;) {
    char *next = strpbrk(line, "\r\n");
    if (next) {
      *next++ = 0;
      while (*next == '\r' || *next == '\n') next++;
    }
    char *s = line;
    line = next;
    while (isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len && isspace((unsigned char)s[len - 1])) s[--len] = 0;
    if (!*s || *s == ';' || *s == '#') continue;
    if (*s == '[') {
      char *e = strchr(s, ']');
      if (e) *e = 0;
      snprintf(section, sizeof section, "%s", s + 1);
      continue;
    }
    char *eq = strchr(s, '=');
    if (!eq) continue;
    *eq = 0;
    char *k = s, *v = eq + 1;
    len = strlen(k);
    while (len && isspace((unsigned char)k[len - 1])) k[--len] = 0;
    while (isspace((unsigned char)*v)) v++;
    char full[160];
    if (section[0]) snprintf(full, sizeof full, "%s.%s", section, k);
    else snprintf(full, sizeof full, "%s", k);
    n += mods_store_put(layer, full, v, NULL);
  }
  return n;
}

int mods_store_load_ini(int layer, const char *path) {
  char *text = mods_read_file(path, NULL);
  if (!text) return -1;
  int n = mods_store_parse_ini(layer, text);
  free(text);
  return n;
}

// The value a key has in the file: live beats mods.ini.
static const char *ini_value(const Entry *e) { return e->val[MS_LIVE] ? e->val[MS_LIVE] : e->val[MS_INI]; }

int mods_store_save_ini(const char *path) {
  if (!path || !*path) return 0;
  char tmp[1024];
  if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return 0;
  char *done = (char *)calloc((size_t)g_n + 1, 1);
  if (!done) return 0;
  FILE *f = fopen(tmp, "w");
  if (!f) { free(done); return 0; }
  fprintf(f, "; written by the game (mods_set persist), docs/modding/CORE.md\n");
  for (int i = 0; i < g_n; i++) {
    if (done[i] || !ini_value(&g_e[i])) continue;
    const char *dot = strchr(g_e[i].key, '.');
    int slen = dot ? (int)(dot - g_e[i].key) : 0;
    if (slen) fprintf(f, "\n[%.*s]\n", slen, g_e[i].key);
    for (int j = i; j < g_n; j++) {
      const char *d2 = strchr(g_e[j].key, '.');
      int l2 = d2 ? (int)(d2 - g_e[j].key) : 0;
      if (done[j] || !ini_value(&g_e[j]) || l2 != slen) continue;
      int same = 1;
      for (int c = 0; c < slen; c++)
        same &= tolower((unsigned char)g_e[j].key[c]) == tolower((unsigned char)g_e[i].key[c]);
      if (!same) continue;
      fprintf(f, "%s=%s\n", g_e[j].key + (slen ? slen + 1 : 0), ini_value(&g_e[j]));
      done[j] = 1;
    }
  }
  free(done);
  int ok = !ferror(f);
  if (fflush(f) != 0) ok = 0;
  if (fclose(f) != 0) ok = 0;
  if (!ok) { remove(tmp); return 0; }
#ifdef _WIN32
  ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  ok = rename(tmp, path) == 0;
#endif
  if (!ok) remove(tmp);
  return ok;
}

int mods_store_layer_keys(int layer, const char *prefix, const char **keys, const char **vals, int max) {
  size_t pl = strlen(prefix);
  int n = 0;
  for (int i = 0; i < g_n && n < max; i++) {
    if (layer < 0 || layer >= MS__COUNT || !g_e[i].val[layer] || strlen(g_e[i].key) < pl) continue;
    int same = 1;
    for (size_t c = 0; c < pl; c++) same &= tolower((unsigned char)g_e[i].key[c]) == tolower((unsigned char)prefix[c]);
    if (!same) continue;
    keys[n] = g_e[i].key;
    vals[n] = g_e[i].val[layer];
    n++;
  }
  return n;
}
