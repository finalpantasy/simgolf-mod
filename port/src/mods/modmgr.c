// Mod manager model (simgolf mod, card task-22), see modmgr.h.

#include "mods/modmgr.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const char *const modmgr_sort_names[MM_SORT__COUNT] = {"Name", "Author", "Recently added", "Load order", "Enabled first"};

struct ModMgr {
  char dir[512];
  ModSet set;
  ModLoadOrder disk, draft;
  char **running;
  int nrunning;   // -1: unknown
  ModResolution res;
  ModInfo *info;
};

// ------------------------------------------------------------------ small helpers

static char *dup(const char *s) { return mods_strdup(s ? s : ""); }

static void push(char ***a, int *n, const char *s) {
  *a = (char **)realloc(*a, sizeof(char *) * (size_t)(*n + 1));
  (*a)[(*n)++] = dup(s);
}

static void drop_at(char **a, int *n, int k) {
  free(a[k]);
  memmove(&a[k], &a[k + 1], sizeof(char *) * (size_t)(*n - k - 1));
  (*n)--;
}

static void lo_copy(ModLoadOrder *to, const ModLoadOrder *from) {
  memset(to, 0, sizeof *to);
  to->exists = from->exists;
  to->active = dup(from->active ? from->active : "vanilla");
  if (from->error) to->error = dup(from->error);
  for (int i = 0; i < from->n; i++) {
    if (!strcmp(from->p[i].name, "vanilla")) continue;
    ModProfile *p = loadorder_profile(to, from->p[i].name, 1);
    for (int j = 0; j < from->p[i].nmods; j++) push(&p->mods, &p->nmods, from->p[i].mods[j]);
  }
}

static int lo_equal(const ModLoadOrder *a, const ModLoadOrder *b) {
  if (strcmp(a->active ? a->active : "vanilla", b->active ? b->active : "vanilla")) return 0;
  int na = 0, nb = 0;
  for (int i = 0; i < a->n; i++) na += strcmp(a->p[i].name, "vanilla") != 0;
  for (int i = 0; i < b->n; i++) nb += strcmp(b->p[i].name, "vanilla") != 0;
  if (na != nb) return 0;
  for (int i = 0; i < a->n; i++) {
    if (!strcmp(a->p[i].name, "vanilla")) continue;
    int k = -1;
    for (int j = 0; j < b->n; j++)
      if (!strcmp(a->p[i].name, b->p[j].name)) k = j;
    if (k < 0 || a->p[i].nmods != b->p[k].nmods) return 0;
    for (int j = 0; j < a->p[i].nmods; j++)
      if (strcmp(a->p[i].mods[j], b->p[k].mods[j])) return 0;
  }
  return 1;
}

static ModProfile *cur_profile(ModMgr *m, int create) {
  const char *a = m->draft.active ? m->draft.active : "vanilla";
  if (!strcmp(a, "vanilla")) return NULL;
  return loadorder_profile(&m->draft, a, create);
}

static int ieq(const char *a, const char *b) {
  for (; *a && *b; a++, b++)
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
  return *a == *b;
}

static int icontains(const char *hay, const char *needle) {
  size_t nl = strlen(needle);
  if (!nl) return 1;
  for (; *hay; hay++) {
    size_t k = 0;
    while (k < nl && hay[k] && tolower((unsigned char)hay[k]) == tolower((unsigned char)needle[k])) k++;
    if (k == nl) return 1;
  }
  return 0;
}

// ------------------------------------------------------------------ resolve

static void refresh(ModMgr *m) {
  resolution_free(&m->res);
  free(m->info);
  m->info = NULL;
  ModProfile *pr = cur_profile(m, 0);
  const char *keys[256], *vals[256];
  int nsw = mods_store_layer_keys(MS_INI, "mods.", keys, vals, 256);
  ModSwitch sw[256];
  for (int i = 0; i < nsw; i++) sw[i].id = keys[i] + 5, sw[i].on = atoi(vals[i]) != 0;
  mods_resolve(&m->set, pr ? (const char *const *)pr->mods : NULL, pr ? pr->nmods : 0, sw, nsw, &m->res);

  int n = m->set.n;
  m->info = (ModInfo *)calloc((size_t)(n ? n : 1), sizeof(ModInfo));
  for (int i = 0; i < n; i++) {
    ModInfo *f = &m->info[i];
    const ModManifest *mf = &m->set.m[i];
    f->pos = f->load_pos = -1;
    f->broken = mf->error != NULL;
    f->changes_saves = mf->changes_saves;
    for (int j = 0; pr && j < pr->nmods; j++)
      if (mf->id && !strcmp(pr->mods[j], mf->id)) f->on = 1, f->pos = j;
    for (int j = 0; j < m->nrunning; j++)
      if (mf->id && !strcmp(m->running[j], mf->id)) f->running = 1;
    char path[700];
    snprintf(path, sizeof path, "%s/mod.toml", mf->dir ? mf->dir : "");
    struct stat st;
    f->added = stat(path, &st) == 0 ? (long)st.st_mtime : 0;
  }
  for (int j = 0; j < m->res.norder; j++) m->info[m->res.order[j]].load_pos = j;
  for (int k = 0; k < m->res.nkeys; k++) {
    const ModKeyUse *u = &m->res.keys[k];
    if (!u->conflict) continue;
    for (int x = 0; x < u->n; x++) m->info[u->mod[x]].conflict = 1;
  }
  for (int d = 0; d < m->res.diags.n; d++) {
    const ModDiag *g = &m->res.diags.d[d];
    int i = g->mod ? modset_find(&m->set, g->mod) : -1;
    if (i < 0) continue;
    if (!strcmp(g->code, "missing_dependency") || !strcmp(g->code, "dependency_version")) m->info[i].missing_dep = 1;
    else if (!strcmp(g->code, "incompatible")) m->info[i].incompatible = 1;
    else if (!strcmp(g->code, "auto_enabled")) m->info[i].auto_enabled = 1;
  }
  // restart: loads differently from the running game, or in another relative order among the shared ones
  if (m->nrunning >= 0) {
    int common_r[256], nr = 0;
    for (int j = 0; j < m->nrunning && nr < 256; j++) {
      int i = modset_find(&m->set, m->running[j]);
      if (i >= 0 && m->info[i].load_pos >= 0) common_r[nr++] = i;
    }
    int common_d[256], nd = 0;
    for (int j = 0; j < m->res.norder && nd < 256; j++)
      if (m->info[m->res.order[j]].running) common_d[nd++] = m->res.order[j];
    for (int i = 0; i < n; i++) {
      ModInfo *f = &m->info[i];
      if ((f->load_pos >= 0) != f->running) f->needs_restart = 1;
    }
    for (int j = 0; j < nr && j < nd; j++)
      if (common_r[j] != common_d[j]) m->info[common_d[j]].needs_restart = 1;
  }
}

static void load_all(ModMgr *m) {
  char p[800];
  snprintf(p, sizeof p, "%s/load_order.toml", m->dir);
  ModLoadOrder raw;
  loadorder_read(&raw, p);
  lo_copy(&m->disk, &raw);
  lo_copy(&m->draft, &raw);
  loadorder_free(&raw);
  modset_scan(&m->set, m->dir);
  refresh(m);
}

ModMgr *modmgr_open(const char *mods_dir, const char *const *running, int nrunning) {
  ModMgr *m = (ModMgr *)calloc(1, sizeof *m);
  snprintf(m->dir, sizeof m->dir, "%s", mods_dir ? mods_dir : "");
  m->nrunning = running ? nrunning : -1;
  int cnt = 0;
  for (int i = 0; running && i < nrunning; i++) push(&m->running, &cnt, running[i]);
  load_all(m);
  return m;
}

void modmgr_reload(ModMgr *m) {
  modset_free(&m->set);
  loadorder_free(&m->disk);
  loadorder_free(&m->draft);
  load_all(m);
}

void modmgr_close(ModMgr *m) {
  if (!m) return;
  modset_free(&m->set);
  loadorder_free(&m->disk);
  loadorder_free(&m->draft);
  resolution_free(&m->res);
  for (int i = 0; i < (m->nrunning > 0 ? m->nrunning : 0); i++) free(m->running[i]);
  free(m->running);
  free(m->info);
  free(m);
}

// ------------------------------------------------------------------ the mods

int modmgr_count(const ModMgr *m) { return m->set.n; }
const ModManifest *modmgr_manifest(const ModMgr *m, int i) { return &m->set.m[i]; }
const ModInfo *modmgr_info(const ModMgr *m, int i) { return &m->info[i]; }
int modmgr_find(const ModMgr *m, const char *id) { return modset_find(&m->set, id); }

const char *modmgr_author(const ModMgr *m, int i, char *out, size_t n) {
  const ModManifest *mf = &m->set.m[i];
  size_t o = 0;
  if (n) out[0] = 0;
  for (int k = 0; k < mf->nauthors && o + 1 < n; k++)
    o += (size_t)snprintf(out + o, n - o, "%s%s", k ? ", " : "", mf->authors[k]);
  return out;
}

int modmgr_notes(const ModMgr *m, int i, ModNote *out, int max) {
  const ModManifest *mf = &m->set.m[i];
  int n = 0;
  if (mf->error && n < max) {
    out[n].level = MD_ERROR;
    snprintf(out[n].code, sizeof out[n].code, "manifest_error");
    snprintf(out[n].msg, sizeof out[n].msg, "%s", mf->error);
    n++;
  }
  for (int lvl = MD_ERROR; lvl >= MD_NOTE; lvl--)
    for (int d = 0; d < m->res.diags.n && n < max; d++) {
      const ModDiag *g = &m->res.diags.d[d];
      if (g->level != lvl || !g->mod || !mf->id || strcmp(g->mod, mf->id) || !strcmp(g->code, "merged")) continue;
      if (!strcmp(g->code, "switched_off") || !strcmp(g->code, "switched_on")) continue;
      out[n].level = g->level;
      snprintf(out[n].code, sizeof out[n].code, "%s", g->code);
      snprintf(out[n].msg, sizeof out[n].msg, "%s", g->msg);
      n++;
    }
  for (int d = 0; d < mf->warns.n && n < max; d++) {
    out[n].level = mf->warns.d[d].level;
    snprintf(out[n].code, sizeof out[n].code, "%s", mf->warns.d[d].code);
    snprintf(out[n].msg, sizeof out[n].msg, "%s", mf->warns.d[d].msg);
    n++;
  }
  return n;
}

int modmgr_conflict_lines(const ModMgr *m, int i, char (*out)[320], int max) {
  int n = 0;
  for (int k = 0; k < m->res.nkeys && n < max; k++) {
    const ModKeyUse *u = &m->res.keys[k];
    if (!u->conflict) continue;
    int mine = 0;
    for (int x = 0; x < u->n; x++) mine |= u->mod[x] == i;
    if (!mine) continue;
    char line[320];
    int o = snprintf(line, sizeof line, "%s %s: ", mods_kind_name(u->kind), u->key);
    for (int x = 0; x < u->n && o < (int)sizeof line - 40; x++)
      o += snprintf(line + o, sizeof line - (size_t)o, "%s%s %s", x ? ", " : "", m->set.m[u->mod[x]].name,
                    mods_op_name(u->op[x]));
    if (u->winner >= 0 && o < (int)sizeof line - 40)
      snprintf(line + o, sizeof line - (size_t)o, "; %s wins (loads later)", m->set.m[u->winner].name);
    snprintf(out[n++], 320, "%s", line);
  }
  return n;
}

// ------------------------------------------------------------------ view

static const ModMgr *g_cmp_m;
static int g_cmp_sort;

static const char *first_author(const ModManifest *mf) { return mf->nauthors ? mf->authors[0] : ""; }

static int cmp_name(const ModMgr *m, int a, int b) {
  const ModManifest *x = &m->set.m[a], *y = &m->set.m[b];
  const char *p = x->name ? x->name : x->id, *q = y->name ? y->name : y->id;
  while (*p && *q && tolower((unsigned char)*p) == tolower((unsigned char)*q)) p++, q++;
  int c = tolower((unsigned char)*p) - tolower((unsigned char)*q);
  return c ? c : strcmp(x->id, y->id);
}

static int cmp_view(const void *pa, const void *pb) {
  const ModMgr *m = g_cmp_m;
  int a = *(const int *)pa, b = *(const int *)pb;
  const ModInfo *x = &m->info[a], *y = &m->info[b];
  int c = 0;
  switch (g_cmp_sort) {
    case MM_SORT_AUTHOR: {
      const char *p = first_author(&m->set.m[a]), *q = first_author(&m->set.m[b]);
      if (!*p != !*q) c = *p ? -1 : 1;   // mods without an author last
      else {
        while (*p && *q && tolower((unsigned char)*p) == tolower((unsigned char)*q)) p++, q++;
        c = tolower((unsigned char)*p) - tolower((unsigned char)*q);
      }
      break;
    }
    case MM_SORT_RECENT:
      c = x->added < y->added ? 1 : x->added > y->added ? -1 : 0;
      break;
    case MM_SORT_LOAD: {
      // loaded mods in load order, then enabled-but-not-loaded in list order, then the rest
      int ka = x->load_pos >= 0 ? 0 : x->on ? 1 : 2, kb = y->load_pos >= 0 ? 0 : y->on ? 1 : 2;
      if (ka != kb) c = ka - kb;
      else if (ka == 0) c = x->load_pos - y->load_pos;
      else if (ka == 1) c = x->pos - y->pos;
      break;
    }
    case MM_SORT_ENABLED:
      c = (y->on != 0) - (x->on != 0);
      break;
    default:
      break;
  }
  return c ? c : cmp_name(m, a, b);
}

static int passes(const ModMgr *m, int i, const ModView *v) {
  const ModInfo *f = &m->info[i];
  const ModManifest *mf = &m->set.m[i];
  if (v->only_enabled && !f->on) return 0;
  if (v->only_disabled && f->on) return 0;
  if (v->only_conflicts && !(f->conflict || f->missing_dep || f->incompatible || f->broken)) return 0;
  if (v->tag[0]) {
    int has = 0;
    for (int k = 0; k < mf->ntags; k++) has |= ieq(mf->tags[k], v->tag);
    if (!has) return 0;
  }
  char hay[2048];
  int o = snprintf(hay, sizeof hay, "%s %s %s ", mf->id, mf->name ? mf->name : "", mf->description ? mf->description : "");
  for (int k = 0; k < mf->nauthors && o < (int)sizeof hay - 80; k++) o += snprintf(hay + o, sizeof hay - (size_t)o, "%s ", mf->authors[k]);
  for (int k = 0; k < mf->ntags && o < (int)sizeof hay - 40; k++) o += snprintf(hay + o, sizeof hay - (size_t)o, "%s ", mf->tags[k]);
  const char *p = v->search;   // every word must appear (no strtok: callers may be in the middle of their own)
  while (*p) {
    while (*p == ' ' || *p == '\t') p++;
    char word[64];
    int n = 0;
    while (*p && *p != ' ' && *p != '\t' && n < 63) word[n++] = *p++;
    word[n] = 0;
    if (n && !icontains(hay, word)) return 0;
  }
  return 1;
}

int modmgr_view(const ModMgr *m, const ModView *v, int *out, int max) {
  int n = 0;
  for (int i = 0; i < m->set.n && n < max; i++)
    if (passes(m, i, v)) out[n++] = i;
  g_cmp_m = m;
  g_cmp_sort = v->sort;
  qsort(out, (size_t)n, sizeof *out, cmp_view);
  return n;
}

int modmgr_tags(const ModMgr *m, char (*out)[24], int max) {
  int n = 0;
  for (int i = 0; i < m->set.n; i++)
    for (int k = 0; k < m->set.m[i].ntags; k++) {
      int have = 0;
      for (int j = 0; j < n; j++) have |= ieq(out[j], m->set.m[i].tags[k]);
      if (!have && n < max) {
        snprintf(out[n], 24, "%s", m->set.m[i].tags[k]);
        for (char *c = out[n]; *c; c++) *c = (char)tolower((unsigned char)*c);
        n++;
      }
    }
  for (int a = 1; a < n; a++)   // insertion sort
    for (int b = a; b > 0 && strcmp(out[b - 1], out[b]) > 0; b--) {
      char t[24];
      memcpy(t, out[b], 24), memcpy(out[b], out[b - 1], 24), memcpy(out[b - 1], t, 24);
    }
  return n;
}

// ------------------------------------------------------------------ edits

int modmgr_set_enabled(ModMgr *m, int i, int on) {
  if (i < 0 || i >= m->set.n) return 0;
  ModProfile *pr = cur_profile(m, 1);
  if (!pr) return 0;   // vanilla is read only
  const char *id = m->set.m[i].id;
  int at = -1;
  for (int j = 0; j < pr->nmods; j++)
    if (!strcmp(pr->mods[j], id)) at = j;
  if (on && at < 0) push(&pr->mods, &pr->nmods, id);
  else if (!on && at >= 0) drop_at(pr->mods, &pr->nmods, at);
  else return 0;
  refresh(m);
  return 1;
}

int modmgr_move_to(ModMgr *m, int i, int pos) {
  if (i < 0 || i >= m->set.n) return 0;
  ModProfile *pr = cur_profile(m, 0);
  if (!pr) return 0;
  int at = m->info[i].pos;
  if (at < 0) return 0;
  if (pos < 0) pos = 0;
  if (pos >= pr->nmods) pos = pr->nmods - 1;
  if (pos == at) return 0;
  char *s = pr->mods[at];
  if (pos < at) memmove(&pr->mods[pos + 1], &pr->mods[pos], sizeof(char *) * (size_t)(at - pos));
  else memmove(&pr->mods[at], &pr->mods[at + 1], sizeof(char *) * (size_t)(pos - at));
  pr->mods[pos] = s;
  refresh(m);
  return 1;
}

int modmgr_move(ModMgr *m, int i, int delta) {
  if (i < 0 || i >= m->set.n || m->info[i].pos < 0) return 0;
  return modmgr_move_to(m, i, m->info[i].pos + delta);
}

// ------------------------------------------------------------------ profiles

int modmgr_profile_count(const ModMgr *m) {
  int n = 1;
  for (int i = 0; i < m->draft.n; i++) n += strcmp(m->draft.p[i].name, "vanilla") != 0;
  return n;
}

const char *modmgr_profile_name(const ModMgr *m, int k) {
  if (k <= 0) return "vanilla";
  int c = 0;
  for (int i = 0; i < m->draft.n; i++) {
    if (!strcmp(m->draft.p[i].name, "vanilla")) continue;
    if (++c == k) return m->draft.p[i].name;
  }
  return "";
}

const char *modmgr_profile(const ModMgr *m) { return m->draft.active ? m->draft.active : "vanilla"; }
int modmgr_profile_readonly(const ModMgr *m, const char *name) {
  (void)m;
  return !strcmp(name, "vanilla");
}

int modmgr_clean_name(const char *in, char *out, size_t n) {
  size_t o = 0;
  for (; *in && o + 1 < n && o < 32; in++) {
    unsigned char c = (unsigned char)*in;
    if (c == ' ') out[o++] = '_';
    else if (isalnum(c) || c == '_' || c == '-') out[o++] = (char)c;
    else return (out[0] = 0), 0;
  }
  out[o] = 0;
  return o > 0 && !*in;
}

static int name_taken(const ModMgr *m, const char *name) {
  if (ieq(name, "vanilla")) return 1;
  for (int i = 0; i < m->draft.n; i++)
    if (ieq(m->draft.p[i].name, name)) return 1;
  return 0;
}

static int bad_name(const ModMgr *m, const char *name, char *clean, char *err, size_t en) {
  if (!modmgr_clean_name(name, clean, 40)) {
    snprintf(err, en, "Use 1 to 32 letters, digits, - or _.");
    return 1;
  }
  if (name_taken(m, clean)) {
    snprintf(err, en, "A profile called \"%s\" already exists.", clean);
    return 1;
  }
  return 0;
}

int modmgr_profile_use(ModMgr *m, const char *name) {
  if (strcmp(name, "vanilla") && !loadorder_profile(&m->draft, name, 0)) return 0;
  free(m->draft.active);
  m->draft.active = dup(name);
  refresh(m);
  return 1;
}

int modmgr_profile_create(ModMgr *m, const char *name, int copy, char *err, size_t en) {
  char clean[40];
  if (bad_name(m, name, clean, err, en)) return 0;
  ModProfile *src = copy ? cur_profile(m, 0) : NULL;
  char **ids = NULL;
  int n = 0;
  for (int j = 0; src && j < src->nmods; j++) push(&ids, &n, src->mods[j]);
  ModProfile *p = loadorder_profile(&m->draft, clean, 1);
  p->mods = ids;
  p->nmods = n;
  return modmgr_profile_use(m, clean);
}

int modmgr_profile_duplicate(ModMgr *m, char *err, size_t en) {
  char base[48], name[56];
  snprintf(base, sizeof base, "%.26s_copy", modmgr_profile(m));
  snprintf(name, sizeof name, "%s", base);
  for (int k = 2; name_taken(m, name) && k < 100; k++) snprintf(name, sizeof name, "%s%d", base, k);
  return modmgr_profile_create(m, name, 1, err, en);
}

int modmgr_profile_rename(ModMgr *m, const char *to, char *err, size_t en) {
  const char *cur = modmgr_profile(m);
  if (!strcmp(cur, "vanilla")) {
    snprintf(err, en, "Vanilla is the original game and cannot be renamed.");
    return 0;
  }
  char clean[40];
  if (ieq(cur, to)) {   // only the case changes
    if (!modmgr_clean_name(to, clean, sizeof clean)) {
      snprintf(err, en, "Use 1 to 32 letters, digits, - or _.");
      return 0;
    }
  } else if (bad_name(m, to, clean, err, en)) return 0;
  ModProfile *p = loadorder_profile(&m->draft, cur, 0);
  if (!p) return 0;
  free(p->name);
  p->name = dup(clean);
  free(m->draft.active);
  m->draft.active = dup(clean);
  return 1;
}

int modmgr_profile_delete(ModMgr *m, const char *name, char *err, size_t en) {
  if (!strcmp(name, "vanilla")) {
    snprintf(err, en, "Vanilla is the original game and cannot be deleted.");
    return 0;
  }
  for (int i = 0; i < m->draft.n; i++)
    if (!strcmp(m->draft.p[i].name, name)) {
      free(m->draft.p[i].name);
      for (int j = 0; j < m->draft.p[i].nmods; j++) free(m->draft.p[i].mods[j]);
      free(m->draft.p[i].mods);
      memmove(&m->draft.p[i], &m->draft.p[i + 1], sizeof(ModProfile) * (size_t)(m->draft.n - i - 1));
      m->draft.n--;
      if (!strcmp(modmgr_profile(m), name)) {
        free(m->draft.active);
        m->draft.active = dup("vanilla");
      }
      refresh(m);
      return 1;
    }
  snprintf(err, en, "No profile called \"%s\".", name);
  return 0;
}

// ------------------------------------------------------------------ apply

int modmgr_dirty(const ModMgr *m) { return !lo_equal(&m->disk, &m->draft); }

int modmgr_restart_needed(const ModMgr *m) {
  if (m->nrunning < 0) return 0;
  for (int i = 0; i < m->set.n; i++)
    if (m->info[i].needs_restart) return 1;
  return 0;
}

int modmgr_apply(ModMgr *m, char *err, size_t en) {
  char p[800];
  snprintf(p, sizeof p, "%s/load_order.toml", m->dir);
  if (m->disk.error) {
    // The file on disk did not parse: keep the player's copy before replacing it.
    char bak[820];
    snprintf(bak, sizeof bak, "%s.unreadable", p);
    size_t len = 0;
    char *text = mods_read_file(p, &len);
    FILE *f = text ? fopen(bak, "wb") : NULL;
    int saved = f && fwrite(text, 1, len, f) == len;
    if (f && fclose(f) != 0) saved = 0;
    free(text);
    if (!saved) {
      snprintf(err, en, "%s cannot be read and could not be backed up; fix or remove it first", p);
      return 0;
    }
  }
  if (!loadorder_write(&m->draft, p)) {
    snprintf(err, en, "Could not write %s", p);
    return 0;
  }
  loadorder_free(&m->disk);
  lo_copy(&m->disk, &m->draft);
  m->disk.exists = m->draft.exists = 1;
  return 1;
}

int modmgr_error(const ModMgr *m, char *out, size_t n) {
  snprintf(out, n, "%s", m->disk.error ? m->disk.error : "");
  return m->disk.error != NULL;
}

int modmgr_resolution_errors(const ModMgr *m) { return mdiags_count(&m->res.diags, MD_ERROR); }

int modmgr_list(const ModMgr *m, const char **ids, int max) {
  ModMgr *mm = (ModMgr *)m;
  ModProfile *pr = cur_profile(mm, 0);
  int n = 0;
  for (int j = 0; pr && j < pr->nmods && n < max; j++) ids[n++] = pr->mods[j];
  return n;
}
