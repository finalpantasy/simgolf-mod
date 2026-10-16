// Versions and version ranges for mod manifests (simgolf mod, task-11), plus
// the small shared helpers (diagnostics, names of kinds and operations).

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mods/mods_core.h"

// ------------------------------------------------------------------ helpers

char *mods_strdup(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *d = (char *)malloc(n);
  if (d) memcpy(d, s, n);
  return d;
}

char *mods_read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
  long n = ftell(f);
  if (n < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
  char *buf = (char *)malloc((size_t)n + 1);
  if (!buf) { fclose(f); return NULL; }
  size_t got = fread(buf, 1, (size_t)n, f);
  int ok = got == (size_t)n && !ferror(f);
  if (fclose(f)) ok = 0;
  if (!ok) { free(buf); return NULL; }
  buf[got] = 0;
  if (len) *len = got;
  return buf;
}

// Lowercase letters, digits, '_', '-' and '.', starting with a letter or digit.
int mods_valid_id(const char *id) {
  if (!id || !*id || strlen(id) > 63 || !isalnum((unsigned char)id[0])) return 0;
  for (const char *p = id; *p; p++)
    if (!(islower((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '_' || *p == '-' || *p == '.'))
      return 0;
  return 1;
}

static const char *const k_kinds[MT__COUNT] = {"setting", "data", "hook", "asset"};
static const char *const k_ops[MO__COUNT] = {"set", "replace", "remove", "add", "mul", "min",
                                             "max", "append", "merge", "before", "after"};

const char *mods_kind_name(int kind) { return kind >= 0 && kind < MT__COUNT ? k_kinds[kind] : "?"; }
const char *mods_op_name(int op) { return op >= 0 && op < MO__COUNT ? k_ops[op] : "?"; }

int mods_kind_parse(const char *s) {
  for (int i = 0; s && i < MT__COUNT; i++)
    if (!strcmp(s, k_kinds[i])) return i;
  if (s && !strcmp(s, "settings")) return MT_SETTING;
  if (s && !strcmp(s, "hooks")) return MT_HOOK;
  if (s && !strcmp(s, "assets")) return MT_ASSET;
  return -1;
}

int mods_op_parse(const char *s) {
  for (int i = 0; s && i < MO__COUNT; i++)
    if (!strcmp(s, k_ops[i])) return i;
  if (s && !strcmp(s, "multiply")) return MO_MUL;
  return -1;
}

int mods_op_exclusive(int op) { return op == MO_SET || op == MO_REPLACE || op == MO_REMOVE; }

void mdiag_add(ModDiags *ds, int level, const char *code, const char *mod, const char *fmt, ...) {
  if (ds->n == ds->cap) {
    int cap = ds->cap ? ds->cap * 2 : 16;
    ModDiag *d = (ModDiag *)realloc(ds->d, (size_t)cap * sizeof *d);
    if (!d) return;
    ds->d = d;
    ds->cap = cap;
  }
  ModDiag *d = &ds->d[ds->n++];
  d->level = level;
  snprintf(d->code, sizeof d->code, "%s", code);
  d->mod = mods_strdup(mod);
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  d->msg = mods_strdup(buf);
}

void mdiags_free(ModDiags *ds) {
  for (int i = 0; i < ds->n; i++) {
    free(ds->d[i].mod);
    free(ds->d[i].msg);
  }
  free(ds->d);
  memset(ds, 0, sizeof *ds);
}

int mdiags_count(const ModDiags *ds, int level) {
  int n = 0;
  for (int i = 0; i < ds->n; i++) n += ds->d[i].level == level;
  return n;
}

const char *mdiag_level_name(int level) {
  return level == MD_ERROR ? "error" : level == MD_WARN ? "warning" : "note";
}

// ------------------------------------------------------------------ versions

// Reads up to three dot-separated numbers; '*', 'x' or 'X' in a place makes
// that place and the rest wild (*wild gets the count of fixed places).
static const char *parse_parts(const char *s, int part[3], int *fixed) {
  part[0] = part[1] = part[2] = 0;
  *fixed = 0;
  for (int i = 0; i < 3; i++) {
    if (*s == '*' || *s == 'x' || *s == 'X') {
      s++;
      while (*s == '.' && (s[1] == '*' || s[1] == 'x' || s[1] == 'X')) s += 2;
      return s;
    }
    if (!isdigit((unsigned char)*s)) return i ? s : NULL;
    long v = 0;
    while (isdigit((unsigned char)*s)) {
      v = v * 10 + (*s++ - '0');
      if (v > 1000000) return NULL;
    }
    part[i] = (int)v;
    *fixed = i + 1;
    if (*s != '.') return s;
    s++;
    if (i == 2) return NULL;
  }
  return s;
}

int semver_parse(const char *s, SemVer *v) {
  memset(v, 0, sizeof *v);
  if (!s) return 0;
  while (isspace((unsigned char)*s)) s++;
  if (*s == 'v') s++;
  int part[3], fixed;
  const char *e = parse_parts(s, part, &fixed);
  if (!e || fixed == 0) return 0;
  // Wildcards belong to dependency ranges, not an installed mod's version.
  if (e > s && e[-1] == '.') return 0;
  for (const char *p = s; p < e; p++) if (*p == '*' || *p == 'x' || *p == 'X') return 0;
  v->major = part[0];
  v->minor = part[1];
  v->patch = part[2];
  if (*e == '-') {
    e++;
    int n = 0;
    while (*e && (isalnum((unsigned char)*e) || *e == '.' || *e == '-') && n < (int)sizeof v->pre - 1)
      v->pre[n++] = *e++;
    v->pre[n] = 0;
    if (!n || v->pre[0] == '.' || v->pre[n - 1] == '.' || strstr(v->pre, "..")) return 0;
  }
  if (*e == '+') {   // build metadata: validate, but ignore for ordering
    const char *start = ++e;
    int empty = 1;
    while (*e && !isspace((unsigned char)*e)) {
      if (*e == '.') { if (empty) return 0; empty = 1; }
      else if (isalnum((unsigned char)*e) || *e == '-') empty = 0;
      else return 0;
      e++;
    }
    if (e == start || empty) return 0;
  }
  while (isspace((unsigned char)*e)) e++;
  return *e == 0;
}

// Prerelease ordering as semver: dot-separated identifiers, numeric ones
// compare as numbers and sort before alphanumeric ones.
static int pre_cmp(const char *a, const char *b) {
  if (!*a || !*b) return !*a && !*b ? 0 : !*a ? 1 : -1;   // a release sorts after its prerelease
  while (*a && *b) {
    char ia[32], ib[32];
    int na = 0, nb = 0;
    while (*a && *a != '.' && na < 31) ia[na++] = *a++;
    while (*b && *b != '.' && nb < 31) ib[nb++] = *b++;
    ia[na] = ib[nb] = 0;
    if (*a == '.') a++;
    if (*b == '.') b++;
    int da = na && strspn(ia, "0123456789") == (size_t)na, db = nb && strspn(ib, "0123456789") == (size_t)nb;
    int c;
    if (da && db) {
      // Numeric identifiers have no machine-integer size limit. Compare their
      // significant digits instead of overflowing/saturating a Windows long.
      const char *pa = ia, *pb = ib;
      while (*pa == '0') pa++;
      while (*pb == '0') pb++;
      size_t la = strlen(pa), lb = strlen(pb);
      c = la != lb ? (la > lb ? 1 : -1) : strcmp(pa, pb);
    }
    else if (da || db) c = da ? -1 : 1;
    else c = strcmp(ia, ib);
    if (c) return c < 0 ? -1 : 1;
  }
  return *a ? 1 : *b ? -1 : 0;
}

int semver_cmp(const SemVer *a, const SemVer *b) {
  if (a->major != b->major) return a->major < b->major ? -1 : 1;
  if (a->minor != b->minor) return a->minor < b->minor ? -1 : 1;
  if (a->patch != b->patch) return a->patch < b->patch ? -1 : 1;
  return pre_cmp(a->pre, b->pre);
}

// One comparator term ("^1.2", ">=1.0.0", "1.x"). Returns 1/0, -1 if malformed.
static int term_match(const SemVer *v, const char *t) {
  char op[3] = "";
  int n = 0;
  while (n < 2 && (*t == '<' || *t == '>' || *t == '=' || *t == '^' || *t == '~')) op[n++] = *t++;
  op[n] = 0;
  if (*t == 'v') t++;
  int part[3], fixed;
  const char *e = parse_parts(t, part, &fixed);
  if (!e) return -1;
  SemVer lo = {part[0], part[1], part[2], ""};
  if (*e == '-') {
    e++;
    int k = 0;
    while (*e && k < (int)sizeof lo.pre - 1) lo.pre[k++] = *e++;
    lo.pre[k] = 0;
  }
  if (*e) return -1;
  // Prereleases only match ranges that name a prerelease of the same version.
  if (v->pre[0] && !(lo.pre[0] && v->major == lo.major && v->minor == lo.minor && v->patch == lo.patch))
    return 0;
  int c = semver_cmp(v, &lo);
  if (!strcmp(op, "") || !strcmp(op, "=")) {
    if (fixed == 0) return 1;   // "*"
    if (fixed == 3) return c == 0;
    if (v->major != lo.major) return 0;
    return fixed == 1 || v->minor == lo.minor;
  }
  if (!strcmp(op, ">=")) return c >= 0;
  if (!strcmp(op, ">")) {
    if (fixed < 3) {   // ">1.2" means above every 1.2.x
      if (v->major != lo.major) return v->major > lo.major;
      return fixed == 1 ? 0 : v->minor > lo.minor;
    }
    return c > 0;
  }
  if (!strcmp(op, "<")) return c < 0;
  if (!strcmp(op, "<=")) {
    if (fixed < 3) {   // "<=1.2" includes every 1.2.x
      if (v->major != lo.major) return v->major < lo.major;
      return fixed == 1 ? 1 : v->minor <= lo.minor;
    }
    return c <= 0;
  }
  if (!strcmp(op, "~")) {   // same major.minor (same major if only the major is given)
    if (c < 0) return 0;
    if (v->major != lo.major) return 0;
    return fixed < 2 || v->minor == lo.minor;
  }
  if (!strcmp(op, "^")) {   // no change in the leftmost non-zero place
    if (c < 0) return 0;
    if (lo.major > 0 || fixed == 1) return v->major == lo.major;
    if (lo.minor > 0 || fixed == 2) return v->major == 0 && v->minor == lo.minor;
    return v->major == 0 && v->minor == 0 && v->patch == lo.patch;
  }
  return -1;
}

// Evaluates a whole range for v (NULL v: syntax check only).
static int range_eval(const SemVer *v, const char *range) {
  if (!range) return -1;
  SemVer zero = {0, 0, 0, ""};
  const char *p = range;
  int any_alt = 0, result = 0;
  while (1) {
    // One alternative: up to "||" or the end.
    const char *end = strstr(p, "||");
    size_t len = end ? (size_t)(end - p) : strlen(p);
    char alt[256];
    if (len >= sizeof alt) return -1;
    memcpy(alt, p, len);
    alt[len] = 0;
    int ok = 1, terms = 0;
    char *s = alt;
    while (*s) {
      while (*s == ' ' || *s == ',' || *s == '\t') s++;
      if (!*s) break;
      char term[64];
      int k = 0;
      // Allow "> = 1.0" style spacing between an operator and its version.
      while (*s && (*s == '<' || *s == '>' || *s == '=' || *s == '^' || *s == '~') && k < 3) term[k++] = *s++;
      while (k && *s == ' ') s++;
      while (*s && *s != ' ' && *s != ',' && *s != '\t' && k < 63) term[k++] = *s++;
      term[k] = 0;
      int m = term_match(v ? v : &zero, term);
      if (m < 0) return -1;
      if (!m) ok = 0;
      terms++;
    }
    if (!terms) {
      if (!end && !any_alt) return 1;   // an empty range means any version
      return -1;
    }
    any_alt = 1;
    if (ok) result = 1;
    if (!end) break;
    p = end + 2;
  }
  return result;
}

int semver_satisfies(const char *version, const char *range) {
  SemVer v;
  if (range_eval(NULL, range) < 0) return -1;
  if (!semver_parse(version, &v)) return 0;
  return range_eval(&v, range);
}

int semver_range_valid(const char *range) { return range_eval(NULL, range) >= 0; }
