// mod.toml manifests, the installed-mod scan and mods/load_order.toml
// (simgolf mod, task-11). TOML through the vendored tomlc99 (MIT).

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "mods/mods_core.h"
#include "tomlc99/toml.h"

// ------------------------------------------------------------------ small list helpers

static void push_str(char ***arr, int *n, const char *s) {
  char **a = (char **)realloc(*arr, (size_t)(*n + 1) * sizeof *a);
  if (!a) return;
  a[*n] = mods_strdup(s);
  *arr = a;
  (*n)++;
}

static void push_dep(ModDep **arr, int *n, const char *id, const char *range) {
  ModDep *a = (ModDep *)realloc(*arr, (size_t)(*n + 1) * sizeof *a);
  if (!a) return;
  a[*n].id = mods_strdup(id);
  a[*n].range = mods_strdup(range && *range ? range : "*");
  *arr = a;
  (*n)++;
}

static void push_touch(ModManifest *m, int kind, int op, const char *key, const char *value) {
  ModTouch *a = (ModTouch *)realloc(m->touches, (size_t)(m->ntouches + 1) * sizeof *a);
  if (!a) return;
  a[m->ntouches].kind = kind;
  a[m->ntouches].op = op;
  a[m->ntouches].key = mods_strdup(key);
  a[m->ntouches].value = mods_strdup(value);
  m->touches = a;
  m->ntouches++;
}

// A scalar as text: strings as they are, booleans 1/0, numbers as written
// (integers exactly, floats with up to 15 significant digits).
static int scalar_text(toml_raw_t raw, char *out, size_t outlen) {
  if (!raw) return 0;
  char *s;
  int b;
  int64_t i;
  double d;
  if (!toml_rtos(raw, &s)) {
    snprintf(out, outlen, "%s", s);
    free(s);
    return 1;
  }
  if (!toml_rtob(raw, &b)) { snprintf(out, outlen, "%d", b ? 1 : 0); return 1; }
  if (!toml_rtoi(raw, &i)) { snprintf(out, outlen, "%lld", (long long)i); return 1; }
  if (!toml_rtod(raw, &d)) { snprintf(out, outlen, "%.15g", d); return 1; }
  return 0;
}

// A string array as a comma list (append values in [patch]).
static int array_text(toml_array_t *a, char *out, size_t outlen) {
  size_t k = 0;
  out[0] = 0;
  for (int i = 0; i < toml_array_nelem(a); i++) {
    char one[256];
    if (!scalar_text(toml_raw_at(a, i), one, sizeof one)) return 0;
    k += (size_t)snprintf(out + k, k < outlen ? outlen - k : 0, "%s%s", i ? "," : "", one);
  }
  return k < outlen;
}

static char *str_in(toml_table_t *t, const char *key) {
  toml_datum_t d = toml_string_in(t, key);
  return d.ok ? d.u.s : NULL;   // malloc'd by tomlc99 (plain malloc)
}

// A list of ids given as an array of strings, or a single string.
static int read_id_list(ModManifest *m, toml_table_t *t, const char *key, char ***arr, int *n) {
  if (!toml_key_exists(t, key)) return 1;
  toml_array_t *a = toml_array_in(t, key);
  if (!a) {
    char *s = str_in(t, key);
    if (!s) return 0;
    push_str(arr, n, s);
    free(s);
    return 1;
  }
  for (int i = 0; i < toml_array_nelem(a); i++) {
    toml_datum_t d = toml_string_at(a, i);
    if (!d.ok) return 0;
    push_str(arr, n, d.u.s);
    free(d.u.s);
  }
  (void)m;
  return 1;
}

// [dependencies] other = ">=1.0" (or an array of ids, any version).
static int read_deps(ModManifest *m, toml_table_t *root, const char *key, ModDep **arr, int *n) {
  if (!toml_key_exists(root, key)) return 1;
  toml_table_t *t = toml_table_in(root, key);
  if (!t) {
    toml_array_t *a = toml_array_in(root, key);
    if (!a) return 0;
    for (int i = 0; i < toml_array_nelem(a); i++) {
      toml_datum_t d = toml_string_at(a, i);
      if (!d.ok) return 0;
      push_dep(arr, n, d.u.s, "*");
      free(d.u.s);
    }
    return 1;
  }
  for (int i = 0; toml_key_in(t, i); i++) {
    const char *id = toml_key_in(t, i);
    char range[128];
    if (!scalar_text(toml_raw_in(t, id), range, sizeof range)) {
      mdiag_add(&m->warns, MD_ERROR, "manifest", m->id, "%s.%s: the version range must be a string", key, id);
      return 0;
    }
    if (!semver_range_valid(range)) {
      mdiag_add(&m->warns, MD_ERROR, "manifest", m->id, "%s.%s: \"%s\" is not a version range", key, id, range);
      return 0;
    }
    push_dep(arr, n, id, range);
  }
  return 1;
}

// [settings]: "section.key" = value, or [settings.section] key = value.
static int read_settings(ModManifest *m, toml_table_t *t, const char *prefix) {
  for (int i = 0; toml_key_in(t, i); i++) {
    const char *k = toml_key_in(t, i);
    char full[192], val[512];
    snprintf(full, sizeof full, "%s%s%s", prefix, *prefix ? "." : "", k);
    toml_table_t *sub = toml_table_in(t, k);
    if (sub) {
      if (*prefix) {
        mdiag_add(&m->warns, MD_ERROR, "manifest", m->id, "settings.%s: settings nest one level (section.key)", full);
        return 0;
      }
      if (!read_settings(m, sub, k)) return 0;
      continue;
    }
    toml_array_t *a = toml_array_in(t, k);
    if (a ? !array_text(a, val, sizeof val) : !scalar_text(toml_raw_in(t, k), val, sizeof val)) {
      mdiag_add(&m->warns, MD_ERROR, "manifest", m->id, "settings.%s: unsupported value", full);
      return 0;
    }
    push_touch(m, MT_SETTING, MO_SET, full, val);
  }
  return 1;
}

// [patch]: "section.key" = { op = "mul", value = 1.2 } (or [patch.section]).
static int read_patch(ModManifest *m, toml_table_t *t, const char *prefix) {
  for (int i = 0; toml_key_in(t, i); i++) {
    const char *k = toml_key_in(t, i);
    char full[192], val[512] = "";
    snprintf(full, sizeof full, "%s%s%s", prefix, *prefix ? "." : "", k);
    toml_table_t *spec = toml_table_in(t, k);
    if (!spec) {
      mdiag_add(&m->warns, MD_ERROR, "manifest", m->id, "patch.%s: expected { op = \"...\", value = ... }", full);
      return 0;
    }
    if (!toml_key_exists(spec, "op")) {   // [patch.section] key = { ... }
      if (*prefix) {
        mdiag_add(&m->warns, MD_ERROR, "manifest", m->id, "patch.%s: missing op", full);
        return 0;
      }
      if (!read_patch(m, spec, k)) return 0;
      continue;
    }
    char *ops = str_in(spec, "op");
    int op = mods_op_parse(ops);
    free(ops);
    if (op < 0 || op == MO_BEFORE || op == MO_AFTER || op == MO_REPLACE) {
      mdiag_add(&m->warns, MD_ERROR, "manifest", m->id,
                "patch.%s: op must be set, add, mul, min, max, append or remove", full);
      return 0;
    }
    toml_array_t *a = toml_array_in(spec, "value");
    int ok = a ? array_text(a, val, sizeof val) : scalar_text(toml_raw_in(spec, "value"), val, sizeof val);
    if (!ok && op != MO_REMOVE) {
      mdiag_add(&m->warns, MD_ERROR, "manifest", m->id, "patch.%s: missing or unsupported value", full);
      return 0;
    }
    if ((op == MO_ADD || op == MO_MUL || op == MO_MIN || op == MO_MAX)) {
      char *end;
      strtod(val, &end);
      if (end == val || *end) {
        mdiag_add(&m->warns, MD_ERROR, "manifest", m->id, "patch.%s: %s needs a number", full, mods_op_name(op));
        return 0;
      }
    }
    push_touch(m, MT_SETTING, op, full, val);
  }
  return 1;
}

// [touches.data] / [touches.hooks] / [touches.assets]: key = "op". What the
// mod changes outside the settings store, declared so conflicts are known
// before anything runs. Later cards also register touches at run time.
static int read_touches(ModManifest *m, toml_table_t *t) {
  for (int i = 0; toml_key_in(t, i); i++) {
    const char *kname = toml_key_in(t, i);
    int kind = mods_kind_parse(kname);
    toml_table_t *sub = toml_table_in(t, kname);
    if (kind < 0 || kind == MT_SETTING || !sub) {
      mdiag_add(&m->warns, MD_ERROR, "manifest", m->id, "touches.%s: expected [touches.data], [touches.hooks] or [touches.assets]", kname);
      return 0;
    }
    for (int j = 0; toml_key_in(sub, j); j++) {
      const char *key = toml_key_in(sub, j);
      char *ops = str_in(sub, key);
      int op = mods_op_parse(ops);
      free(ops);
      int okop = op >= 0 && (kind == MT_HOOK ? (op == MO_BEFORE || op == MO_AFTER || op == MO_REPLACE)
                             : kind == MT_ASSET ? (op == MO_REPLACE || op == MO_ADD || op == MO_REMOVE)
                                                : op != MO_BEFORE && op != MO_AFTER && op != MO_REPLACE);
      if (!okop) {
        mdiag_add(&m->warns, MD_ERROR, "manifest", m->id, "touches.%s.%s: %s is not an operation for %s", kname, key,
                  op >= 0 ? mods_op_name(op) : "that", kname);
        return 0;
      }
      push_touch(m, kind, op, key, NULL);
    }
  }
  return 1;
}

static const char *const k_known[] = {"id", "name", "version", "description", "authors", "platform", "game",
                                      "load_after", "load_before", "provides", "dependencies",
                                      "optional_dependencies", "incompatibilities", "settings", "patch",
                                      "touches", "homepage", "license", "icon", "tags", "links", "changes_saves", NULL};

int manifest_parse_text(ModManifest *m, char *text, const char *dir, int builtin) {
  memset(m, 0, sizeof *m);
  m->dir = mods_strdup(dir);
  m->builtin = builtin;
  char err[256];
  toml_table_t *t = toml_parse(text, err, sizeof err);
  if (!t) {
    char msg[300];
    snprintf(msg, sizeof msg, "mod.toml: %s", err);
    m->error = mods_strdup(msg);
    return 0;
  }
  m->id = str_in(t, "id");
  m->name = str_in(t, "name");
  m->version = str_in(t, "version");
  m->description = str_in(t, "description");
  m->platform = str_in(t, "platform");
  m->game = str_in(t, "game");
  m->icon = str_in(t, "icon");
  m->homepage = str_in(t, "homepage");
  {
    toml_datum_t cs = toml_bool_in(t, "changes_saves");
    m->changes_saves = cs.ok && cs.u.b;
    toml_table_t *links = toml_table_in(t, "links");
    for (int i = 0; links && toml_key_in(links, i); i++) {
      char *u = str_in(links, toml_key_in(links, i));
      if (!u) continue;
      m->link_names = (char **)realloc(m->link_names, sizeof(char *) * (size_t)(m->nlinks + 1));
      m->link_urls = (char **)realloc(m->link_urls, sizeof(char *) * (size_t)(m->nlinks + 1));
      m->link_names[m->nlinks] = mods_strdup(toml_key_in(links, i));
      m->link_urls[m->nlinks++] = u;
    }
  }
  if (!m->platform) m->platform = mods_strdup("*");
  if (!m->game) m->game = mods_strdup("*");
  const char *bad = NULL;
  SemVer v;
  if (!m->id) bad = "id is missing";
  else if (!mods_valid_id(m->id)) bad = "id must be lowercase letters, digits, '_', '-' or '.'";
  else if (!m->version) bad = "version is missing";
  else if (!semver_parse(m->version, &v)) bad = "version is not a version (use 1.0.0)";
  else if (!semver_range_valid(m->platform)) bad = "platform is not a version range";
  else if (!semver_range_valid(m->game)) bad = "game is not a version range";
  else if (!read_id_list(m, t, "authors", &m->authors, &m->nauthors)) bad = "authors must be a list of names";
  else if (!read_id_list(m, t, "load_after", &m->after, &m->nafter)) bad = "load_after must be a list of mod ids";
  else if (!read_id_list(m, t, "load_before", &m->before, &m->nbefore)) bad = "load_before must be a list of mod ids";
  else if (!read_id_list(m, t, "provides", &m->provides, &m->nprovides)) bad = "provides must be a list of ids";
  else if (!read_id_list(m, t, "tags", &m->tags, &m->ntags)) bad = "tags must be a list of words";
  else if (!read_deps(m, t, "dependencies", &m->deps, &m->ndeps)) bad = "bad [dependencies]";
  else if (!read_deps(m, t, "optional_dependencies", &m->opt, &m->nopt)) bad = "bad [optional_dependencies]";
  else if (!read_deps(m, t, "incompatibilities", &m->incompat, &m->nincompat)) bad = "bad [incompatibilities]";
  else if (toml_table_in(t, "settings") && !read_settings(m, toml_table_in(t, "settings"), "")) bad = "bad [settings]";
  else if (toml_table_in(t, "patch") && !read_patch(m, toml_table_in(t, "patch"), "")) bad = "bad [patch]";
  else if (toml_table_in(t, "touches") && !read_touches(m, toml_table_in(t, "touches"))) bad = "bad [touches]";
  if (!bad) {
    for (int i = 0; toml_key_in(t, i); i++) {
      const char *k = toml_key_in(t, i);
      int known = 0;
      for (int j = 0; k_known[j]; j++) known |= !strcmp(k, k_known[j]);
      if (!known) mdiag_add(&m->warns, MD_WARN, "unknown_key", m->id, "mod.toml: unknown key \"%s\" (ignored)", k);
    }
    for (int i = 0; i < m->ndeps; i++)
      if (!strcmp(m->deps[i].id, m->id)) bad = "a mod cannot depend on itself";
  }
  if (bad) {
    // The detailed reason (if any) is the last error diagnostic.
    char msg[600];
    const char *detail = NULL;
    for (int i = m->warns.n - 1; i >= 0 && !detail; i--)
      if (m->warns.d[i].level == MD_ERROR) detail = m->warns.d[i].msg;
    snprintf(msg, sizeof msg, "mod.toml: %s%s%s", bad, detail ? ": " : "", detail ? detail : "");
    m->error = mods_strdup(msg);
    // The detail lives in m->error now; keep only the warnings.
    int k = 0;
    for (int i = 0; i < m->warns.n; i++) {
      if (m->warns.d[i].level == MD_ERROR) { free(m->warns.d[i].mod); free(m->warns.d[i].msg); continue; }
      m->warns.d[k++] = m->warns.d[i];
    }
    m->warns.n = k;
  }
  if (!m->name) m->name = mods_strdup(m->id ? m->id : "?");
  toml_free(t);
  return m->error == NULL;
}

int manifest_load(ModManifest *m, const char *dir, int builtin) {
  char path[600];
  snprintf(path, sizeof path, "%s/mod.toml", dir);
  char *text = mods_read_file(path, NULL);
  if (!text) {
    memset(m, 0, sizeof *m);
    m->dir = mods_strdup(dir);
    m->error = mods_strdup("mod.toml cannot be read");
    return 0;
  }
  int ok = manifest_parse_text(m, text, dir, builtin);
  free(text);
  return ok;
}

static void free_strs(char **a, int n) {
  for (int i = 0; i < n; i++) free(a[i]);
  free(a);
}

static void free_deps(ModDep *a, int n) {
  for (int i = 0; i < n; i++) {
    free(a[i].id);
    free(a[i].range);
  }
  free(a);
}

void manifest_free(ModManifest *m) {
  free(m->id); free(m->name); free(m->version); free(m->description);
  free(m->platform); free(m->game); free(m->dir); free(m->error);
  free(m->icon); free(m->homepage);
  free_strs(m->tags, m->ntags);
  free_strs(m->link_names, m->nlinks);
  free_strs(m->link_urls, m->nlinks);
  free_strs(m->authors, m->nauthors);
  free_strs(m->after, m->nafter);
  free_strs(m->before, m->nbefore);
  free_strs(m->provides, m->nprovides);
  free_deps(m->deps, m->ndeps);
  free_deps(m->opt, m->nopt);
  free_deps(m->incompat, m->nincompat);
  for (int i = 0; i < m->ntouches; i++) {
    free(m->touches[i].key);
    free(m->touches[i].value);
  }
  free(m->touches);
  mdiags_free(&m->warns);
  memset(m, 0, sizeof *m);
}

// ------------------------------------------------------------------ installed set

int modset_find(const ModSet *s, const char *id) {
  for (int i = 0; id && i < s->n; i++)
    if (s->m[i].id && !strcmp(s->m[i].id, id)) return i;
  return -1;
}

int modset_add(ModSet *s, ModManifest *m) {
  if (m->id && modset_find(s, m->id) >= 0) {
    mdiag_add(&s->diags, MD_ERROR, "duplicate_id", m->id, "two mods use the id \"%s\": %s is ignored (the first is %s)",
              m->id, m->dir ? m->dir : "?", s->m[modset_find(s, m->id)].dir);
    manifest_free(m);
    return 0;
  }
  if (s->n == s->cap) {
    int cap = s->cap ? s->cap * 2 : 16;
    ModManifest *a = (ModManifest *)realloc(s->m, (size_t)cap * sizeof *a);
    if (!a) return 0;
    s->m = a;
    s->cap = cap;
  }
  s->m[s->n++] = *m;
  memset(m, 0, sizeof *m);
  return 1;
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void scan_root(ModSet *s, const char *root, int builtin, int missing_only) {
  DIR *d = opendir(root);
  if (!d) return;
  char **names = NULL;
  int n = 0;
  struct dirent *e;
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.') continue;
    char p[700];
    snprintf(p, sizeof p, "%s/%s/mod.toml", root, e->d_name);
    FILE *f = fopen(p, "rb");
    if (!f) continue;
    fclose(f);
    push_str(&names, &n, e->d_name);
  }
  closedir(d);
  qsort(names, (size_t)n, sizeof *names, cmp_str);   // the same order on every machine
  for (int i = 0; i < n; i++) {
    char dir[700];
    snprintf(dir, sizeof dir, "%s/%s", root, names[i]);
    ModManifest m;
    manifest_load(&m, dir, builtin);
    if (!m.id) {
      // Unreadable manifest without an id: keep it under its folder name so
      // it shows up in reports (it can never load).
      m.id = mods_strdup(names[i]);
      if (!m.name) m.name = mods_strdup(names[i]);
    }
    // Compatibility defaults never replace an installed or customized mod.
    if (missing_only && modset_find(s, m.id) >= 0) {
      manifest_free(&m);
      continue;
    }
    // Folder names relative to the mods folder: the same text wherever it lives.
    if (m.error)
      mdiag_add(&s->diags, MD_ERROR, "manifest_error", m.id, "%s (%s%s/mod.toml): %s", m.id, builtin ? "builtin/" : "",
                names[i], m.error);
    else if (strcmp(m.id, names[i])) {
      mdiag_add(&m.warns, MD_WARN, "folder_name", m.id, "the folder is \"%s\" but the id is \"%s\" (the id counts)",
                names[i], m.id);
    }
    modset_add(s, &m);
  }
  free_strs(names, n);
}

void modset_scan(ModSet *s, const char *mods_dir) {
  char p[700];
  snprintf(p, sizeof p, "%s/builtin", mods_dir);
  scan_root(s, p, 1, 0);
  scan_root(s, mods_dir, 0, 0);
  // Older updaters refresh ui/ but deliberately leave player mods untouched.
  // Discover missing built-ins there without copying files or enabling them.
  snprintf(p, sizeof p, "%s/../ui/builtin-mods", mods_dir);
  scan_root(s, p, 1, 1);
}

void modset_free(ModSet *s) {
  for (int i = 0; i < s->n; i++) manifest_free(&s->m[i]);
  free(s->m);
  mdiags_free(&s->diags);
  memset(s, 0, sizeof *s);
}

// ------------------------------------------------------------------ load_order.toml

ModProfile *loadorder_profile(ModLoadOrder *lo, const char *name, int create) {
  for (int i = 0; i < lo->n; i++)
    if (!strcmp(lo->p[i].name, name)) return &lo->p[i];
  if (!create) return NULL;
  ModProfile *a = (ModProfile *)realloc(lo->p, (size_t)(lo->n + 1) * sizeof *a);
  if (!a) return NULL;
  lo->p = a;
  ModProfile *p = &lo->p[lo->n++];
  memset(p, 0, sizeof *p);
  p->name = mods_strdup(name);
  return p;
}

int loadorder_parse_text(ModLoadOrder *lo, char *text) {
  memset(lo, 0, sizeof *lo);
  lo->exists = 1;
  char err[256];
  toml_table_t *t = toml_parse(text, err, sizeof err);
  if (!t) {
    char msg[300];
    snprintf(msg, sizeof msg, "load_order.toml: %s", err);
    lo->error = mods_strdup(msg);
    lo->active = mods_strdup("vanilla");
    return 0;
  }
  lo->active = str_in(t, "active");
  if (!lo->active) lo->active = mods_strdup("vanilla");
  toml_table_t *profiles = toml_table_in(t, "profiles");
  for (int i = 0; profiles && toml_key_in(profiles, i); i++) {
    const char *name = toml_key_in(profiles, i);
    toml_table_t *pt = toml_table_in(profiles, name);
    if (!pt) continue;
    ModProfile *p = loadorder_profile(lo, name, 1);
    toml_array_t *a = toml_array_in(pt, "mods");
    for (int j = 0; a && j < toml_array_nelem(a); j++) {
      toml_datum_t d = toml_string_at(a, j);
      if (!d.ok) continue;
      push_str(&p->mods, &p->nmods, d.u.s);
      free(d.u.s);
    }
  }
  toml_free(t);
  return 1;
}

int loadorder_read(ModLoadOrder *lo, const char *path) {
  char *text = mods_read_file(path, NULL);
  if (!text) {
    memset(lo, 0, sizeof *lo);
    lo->active = mods_strdup("vanilla");
    return 1;
  }
  int ok = loadorder_parse_text(lo, text);
  free(text);
  return ok;
}

// A TOML basic string: names typed by hand may hold quotes, backslashes or spaces.
static void toml_str(FILE *f, const char *s) {
  fputc('"', f);
  for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
    if (*c == '"' || *c == '\\') fprintf(f, "\\%c", *c);
    else if (*c < 0x20 || *c == 0x7f) fprintf(f, "\\u%04X", *c);
    else fputc(*c, f);
  }
  fputc('"', f);
}

// Bare keys stay bare so ordinary files read the same as before.
static void toml_key(FILE *f, const char *s) {
  int bare = *s != 0;
  for (const char *c = s; *c && bare; c++) bare = isalnum((unsigned char)*c) || *c == '_' || *c == '-';
  if (bare) fputs(s, f);
  else toml_str(f, s);
}

int loadorder_write(const ModLoadOrder *lo, const char *path) {
  char tmp[700];
  if (!path || snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return 0;
  FILE *f = fopen(tmp, "wb");
  if (!f) return 0;
  fprintf(f, "# Which mods load, in which order (docs/modding/CORE.md).\n"
             "# Written by tools/modctl; editing by hand is fine.\n"
             "# The profile \"vanilla\" is always there and always empty: the original game.\n\n");
  fputs("active = ", f);
  toml_str(f, lo->active ? lo->active : "vanilla");
  fputc('\n', f);
  for (int i = 0; i < lo->n; i++) {
    if (!strcmp(lo->p[i].name, "vanilla")) continue;
    fputs("\n[profiles.", f);
    toml_key(f, lo->p[i].name);
    fputs("]\nmods = [", f);
    for (int j = 0; j < lo->p[i].nmods; j++) {
      if (j) fputs(", ", f);
      toml_str(f, lo->p[i].mods[j]);
    }
    fprintf(f, "]\n");
  }
  // A failed write/replacement must never destroy the previous profile.
  int ok = !ferror(f);
  if (fflush(f) != 0) ok = 0;
  if (fclose(f) != 0) ok = 0;
  if (!ok) {
    remove(tmp);
    return 0;
  }
#ifdef _WIN32
  // C rename() will not replace an existing destination on Windows.
  ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  ok = rename(tmp, path) == 0;
#endif
  if (!ok) remove(tmp);
  return ok;
}

void loadorder_free(ModLoadOrder *lo) {
  for (int i = 0; i < lo->n; i++) free_strs(lo->p[i].mods, lo->p[i].nmods), free(lo->p[i].name);
  free(lo->p);
  free(lo->active);
  free(lo->error);
  memset(lo, 0, sizeof *lo);
}
