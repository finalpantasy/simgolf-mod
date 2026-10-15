// Art pack (simgolf mod, task-17): golf's sprites drawn from the 4x upscaled
// art (task-16: art_local/upscaled, Real-ESRGAN x4plus) at the screen's own
// resolution, per category, while golf keeps drawing its own 2002 pixels
// underneath (positions, hit boxes, animation timing and the save all stay
// the game's).
//
// How (game thread):
// 1. Tagging. golf prerenders every FLC frame once into its sprite cache
//    (port/docs/flic.md): golf/art_hooks.c reports each FLC it opens and
//    each rectangle it cuts out of the 480x480 scratch surface, so every
//    cached jgld sprite gets a tag: which file, view and frame, and where the
//    frame (and its shadow) sits inside the sprite.
// 2. Recording. jgld's sprite draws (vtable 0x1011d380, docs/graphsy-sprite.md)
//    are wrapped through icall_replace. A tagged sprite drawn on the back
//    buffer between the start of a course frame and hd_ui_course_done becomes
//    an entry of this frame's list (canvas position, scale and mirroring,
//    the palette in effect), and every canvas pixel the draw changed is
//    marked as that entry's (owner map, plus the value it wrote).
// 3. Publishing (hd_present): the list and the owner map go to the render
//    thread with the course layer they belong to.
// Render thread (hd_native.c's compose): over the HD terrain, the entries are
// drawn in golf's order from 4x textures, then golf's course layer with the
// pixels those entries own cut out (a pixel still holding what the entry
// wrote; anything golf drew over it later stays golf's). An entry whose
// texture is not loaded yet keeps golf's pixels, so nothing ever goes blank.
// PNGs decode on a loader thread (Windows Imaging Component); textures live
// in an LRU cache with a memory budget.
//
// Looks per category (people, buildings, trees, terrain, animals, ui,
// effects): original (golf's pixels, the default; nothing here runs, the
// picture is the plain build's), upscaled (the RGBA upscale; where golf
// recolours a sprite with another palette or a remap table, the 8-bit
// upscale through that palette), locked (always the 8-bit upscale through
// the palette golf draws with: crisp, only the game's colours).
//
// Settings: golf.ini art=<look> (all categories), art_<category>=<look>,
// art_dir=<folder with index.tsv> (tools/upscale/make_index.py), art_cache_mb
// (texture budget, default 1024), art_log=1; over it the mods store's
// art.<category> / art.look (mods.ini [art]), over both SIMGOLF_DISPLAY.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mods/mods.h"
#include "port/golf/art_hooks.h"
#include "port/golf/hd_ui.h"
#include "port/hd_art.h"
#include "port/hd_art_dialog_cutoff.h"
#include "port/hd_art_geom.h"
#include "port/hd_art_pixels.h"
#include "port/hd_native.h"
#include "port/host.h"
#include "port/runtime.h"
#include "port/parallel_jobs.h"
#include "port/water_native.h"

// ------------------------------------------------------------------ settings

enum { C_PEOPLE, C_BUILDINGS, C_TREES, C_TERRAIN, C_ANIMALS, C_UI, C_EFFECTS, NCAT };
enum { L_ORIGINAL, L_UPSCALED, L_LOCKED };
static const char *const k_cat[NCAT] = {"people", "buildings", "trees", "terrain", "animals", "ui", "effects"};
static const char *const k_look[] = {"original", "upscaled", "locked"};

static int g_look[NCAT];
static int g_pixel, g_pixel_base, g_pixel_env = -1, g_pixel_size;   // task-71: Pixel HD (a pixel grid over the course picture)
static int g_def_done;
static char g_dir[MAX_PATH], g_xdir[MAX_PATH];   // upscaled art, and the originals (art_local/extracted)
static int g_cache_mb = 1024, g_log = 1, g_on, g_selftest, g_loaders = 4, g_debug, g_any, g_pack = 1;
static int g_look_base[NCAT], g_look_env[NCAT] = {-1, -1, -1, -1, -1, -1, -1};
static int g_env_phase, g_base_done;   // set_key calls after the store are SIMGOLF_DISPLAY's

// HD is the default (card task-71, the maintainer 2026-10-10: "i want the game to be in hd, not pixelized"); scripted
// runs set SIMGOLF_ART_DEFAULT=original so their pictures stay the plain build's.
static void defaults(void) {
  if (g_def_done) return;
  g_def_done = 1;
  const char *e = getenv("SIMGOLF_ART_DEFAULT");
  int look = e && !_stricmp(e, "original") ? L_ORIGINAL : L_UPSCALED;
  for (int i = 0; i < NCAT; i++) g_look[i] = look;
}

static int parse_look(const char *v) {
  for (int i = 0; i < 3; i++)
    if (!_stricmp(v, k_look[i])) return i;
  if (!_stricmp(v, "palette") || !_stricmp(v, "palette_locked")) return L_LOCKED;
  if (!_stricmp(v, "on") || !strcmp(v, "1")) return L_UPSCALED;
  if (!_stricmp(v, "off") || !strcmp(v, "0")) return L_ORIGINAL;
  return -1;
}

static int cat_of(const char *name) {
  if (!_stricmp(name, "trees_plants") || !_stricmp(name, "plants")) return C_TREES;
  if (!_stricmp(name, "golfers")) return C_PEOPLE;
  for (int i = 0; i < NCAT; i++)
    if (!_stricmp(name, k_cat[i])) return i;
  return -1;
}

// The Textures choice: hd (the art pack, smooth), pixel_hd (the pack's game-colour look through a pixel grid),
// original (the 2002 pixels). Sets every category and the grid.
static int set_textures(const char *v) {
  int look, pixel = 0;
  if (!_stricmp(v, "hd") || !_stricmp(v, "upscaled")) look = L_UPSCALED;
  else if (!_stricmp(v, "pixel_hd") || !_stricmp(v, "pixel") || !_stricmp(v, "pixelated")) look = L_LOCKED, pixel = 1;
  else if (!_stricmp(v, "original")) look = L_ORIGINAL;
  else return 0;
  for (int i = 0; i < NCAT; i++) {
    g_look[i] = look;
    if (g_env_phase) g_look_env[i] = look;
  }
  g_pixel = pixel;
  if (g_env_phase) g_pixel_env = pixel;
  return 1;
}

void hd_art_set_key(const char *k, const char *v) {
  defaults();
  int look = parse_look(v);
  if (!_stricmp(k, "art_textures") || !_stricmp(k, "textures")) {
    set_textures(v);
  } else if (!_stricmp(k, "art_pixel")) {
    g_pixel = atoi(v) != 0;
    if (g_env_phase) g_pixel_env = g_pixel;
  } else if (!_stricmp(k, "art_pixel_size")) {
    g_pixel_size = atoi(v) < 0 ? 0 : atoi(v) > 16 ? 16 : atoi(v);
  } else if (!_stricmp(k, "art") || !_stricmp(k, "art_look")) {
    if (look >= 0)
      for (int i = 0; i < NCAT; i++) {
        g_look[i] = look;
        if (g_env_phase) g_look_env[i] = look;
      }
  } else if (!_stricmp(k, "art_pack")) {
    g_pack = atoi(v) != 0;
  } else if (!_stricmp(k, "art_dir")) {
    snprintf(g_dir, sizeof g_dir, "%s", v);
  } else if (!_stricmp(k, "art_cache_mb")) {
    g_cache_mb = atoi(v) < 64 ? 64 : atoi(v);
  } else if (!_stricmp(k, "art_log")) {
    g_log = atoi(v);
  } else if (!_stricmp(k, "art_debug")) {
    g_debug = atoi(v);
  } else if (!_stricmp(k, "art_selftest")) {
    g_selftest = atoi(v);
  } else if (!_stricmp(k, "art_loaders")) {
    g_loaders = atoi(v) < 1 ? 1 : atoi(v) > 16 ? 16 : atoi(v);
  } else if (!_strnicmp(k, "art_", 4)) {
    int c = cat_of(k + 4);
    if (c >= 0 && look >= 0) {
      g_look[c] = look;
      if (g_env_phase) g_look_env[c] = look;
    }
  }
}

// The mods store over golf.ini (hd_native.c calls this between the two).
void hd_art_read_store(void) {
  defaults();
  if (!g_base_done) {   // the first call: what golf.ini said
    memcpy(g_look_base, g_look, sizeof g_look);
    g_pixel_base = g_pixel;
    g_base_done = 1;
  }
  int env = g_env_phase;
  g_env_phase = 0;
  const char *v = mods_get("art.textures");
  if (v && *v) hd_art_set_key("art_textures", v);
  v = mods_get("art.pixel");
  if (v && *v) hd_art_set_key("art_pixel", v);
  v = mods_get("art.pixel_size");
  if (v && *v) hd_art_set_key("art_pixel_size", v);
  v = mods_get("art.look");
  if (v && *v) hd_art_set_key("art", v);
  for (int i = 0; i < NCAT; i++) {
    char key[64];
    snprintf(key, sizeof key, "art.%s", k_cat[i]);
    v = mods_get(key);
    if (v && *v) {
      char k2[64];
      snprintf(k2, sizeof k2, "art_%s", k_cat[i]);
      hd_art_set_key(k2, v);
    }
  }
  v = mods_get("art.dir");
  if (v && *v) hd_art_set_key("art_dir", v);
  (void)env;
  g_env_phase = 1;
}

int hd_art_active(void) { return g_on; }

// The Textures choice now in effect: "hd", "pixel_hd", "original" or "custom" (a mix set by the per-category keys).
const char *hd_art_textures_name(void) {
  int same = 1;
  for (int i = 1; i < NCAT; i++) same &= g_look[i] == g_look[0];
  if (!same) return "custom";
  if (g_look[0] == L_ORIGINAL) return g_pixel ? "custom" : "original";
  if (g_look[0] == L_LOCKED) return g_pixel ? "pixel_hd" : "custom";
  return g_pixel ? "custom" : "hd";
}

// Pixel HD: the course picture is drawn on a grid of this many screen pixels (0: off). k: screen px per canvas px.
// Auto: one canvas pixel (3 screen pixels at 4K, 2 at 1440p and 1080p): at 2 pixels at 4K the grid is too fine to
// read as pixel art (judged on 4K captures, card task-71), at 4 it is coarser than the original.
int hd_art_pixel_grid(double k) {
  if (!g_pixel) return 0;
  if (g_pixel_size >= 1) return g_pixel_size;
  int p = (int)floor(k + 0.5);
  return p < 2 ? 2 : p;
}

// ------------------------------------------------------------------ index

enum { K_SPRITE, K_SHADOW, K_STILL, K_TEXTURE, K_OTHER };

typedef struct {
  char *src;         // as in index.tsv ("Flics/Animals/Crane.flc")
  char *norm;        // lower case, forward slashes
  char *out;         // its output name in the art folders: no extension, spaces as _
  int kind, cat;
  int w4, h4;        // the upscaled frame size (4x)
  int crop_x, crop_y;   // 1x: the frame's top-left in the 480x480 canvas
  int views, fpv;
  int shadow;        // index of <name>Shadow.flc, -1
} ArtFile;

static ArtFile *g_files;
static int g_nfiles;
static int *g_fhash;   // open addressing, size g_fhash_n, -1 empty
static int g_fhash_n;

static uint32_t hash_str(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

static void normalize(const char *in, char *out, size_t n) {
  size_t i = 0;
  for (; *in && i + 1 < n; in++) {
    char c = *in == '\\' ? '/' : *in;
    if (c >= 'A' && c <= 'Z') c += 32;
    if (c == '/' && i && out[i - 1] == '/') continue;
    out[i++] = c;
  }
  out[i] = 0;
}

static int find_file_norm(const char *norm) {
  if (!g_fhash_n) return -1;
  for (uint32_t h = hash_str(norm) & (g_fhash_n - 1);; h = (h + 1) & (g_fhash_n - 1)) {
    int i = g_fhash[h];
    if (i < 0) return -1;
    if (!strcmp(g_files[i].norm, norm)) return i;
  }
}

// A path as golf passes it (relative, any case, back slashes, maybe without
// extension) -> index entry.
static int find_file(const char *path, const char *ext) {
  char n[MAX_PATH];
  normalize(path, n, sizeof n);
  const char *p = n;
  while (p[0] == '.' && p[1] == '/') p += 2;
  int i = find_file_norm(p);
  if (i < 0 && ext && !strrchr(p, '.')) {
    char m[MAX_PATH + 8];
    snprintf(m, sizeof m, "%s%s", p, ext);
    i = find_file_norm(m);
  }
  // absolute or game-folder paths: try from each folder level
  for (const char *q = strchr(p, '/'); i < 0 && q; q = strchr(q + 1, '/')) i = find_file_norm(q + 1);
  return i;
}

static int load_index(void) {
  char path[MAX_PATH + 16];
  snprintf(path, sizeof path, "%s/index.tsv", g_dir);
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  char line[1024];
  int cap = 0;
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#') continue;
    char *col[10];
    int n = 0;
    for (char *t = strtok(line, "\t\r\n"); t && n < 10; t = strtok(NULL, "\t\r\n")) col[n++] = t;
    if (n < 10) continue;
    if (g_nfiles == cap) g_files = realloc(g_files, (cap = cap ? cap * 2 : 8192) * sizeof *g_files);
    ArtFile *a = &g_files[g_nfiles++];
    a->src = _strdup(col[0]);
    char nm[MAX_PATH];
    normalize(col[0], nm, sizeof nm);
    a->norm = _strdup(nm);
    a->out = _strdup(col[0]);
    char *dot = strrchr(a->out, '.');
    if (dot && !strchr(dot, '/')) *dot = 0;
    for (char *c = a->out; *c; c++)
      if (*c == ' ') *c = '_';
    a->kind = !strcmp(col[1], "sprite") ? K_SPRITE : !strcmp(col[1], "shadow") ? K_SHADOW
            : !strcmp(col[1], "still") ? K_STILL : !strcmp(col[1], "texture") ? K_TEXTURE : K_OTHER;
    a->cat = cat_of(col[2]);
    a->w4 = atoi(col[3]);
    a->h4 = atoi(col[4]);
    a->crop_x = atoi(col[5]);
    a->crop_y = atoi(col[6]);
    a->views = atoi(col[7]);
    a->fpv = atoi(col[8]);
    a->shadow = -1;
  }
  fclose(f);
  g_fhash_n = 1;
  while (g_fhash_n < g_nfiles * 2) g_fhash_n <<= 1;
  g_fhash = malloc(g_fhash_n * sizeof(int));
  memset(g_fhash, 0xff, g_fhash_n * sizeof(int));
  for (int i = 0; i < g_nfiles; i++) {
    uint32_t h = hash_str(g_files[i].norm) & (g_fhash_n - 1);
    while (g_fhash[h] >= 0) h = (h + 1) & (g_fhash_n - 1);
    g_fhash[h] = i;
  }
  for (int i = 0; i < g_nfiles; i++) {
    ArtFile *a = &g_files[i];
    size_t L = strlen(a->norm);
    if (a->kind != K_SPRITE || L < 4 || strcmp(a->norm + L - 4, ".flc")) continue;
    char s[MAX_PATH + 16];
    snprintf(s, sizeof s, "%.*sshadow.flc", (int)(L - 4), a->norm);
    a->shadow = find_file_norm(s);
  }
  return g_nfiles;
}

// ------------------------------------------------------------------ jgld access

#define SPRITE_VTBL 0x1011d380u
#define SURFACE_VTBL 0x1011d0b0u
#define SCALE_X (*(volatile int32_t *)0x10122dc0)
#define SCALE_Y (*(volatile int32_t *)0x10122dc4)
#define SCALE_DEN (*(volatile int32_t *)0x10122dc8)
#define DEFAULT_PAL_HOLDER 0x1012873cu
#define SCRATCH 0x5aa778u            // golf's 480x480 prerender surface (wrapper)
#define FLIC_PLAYER(slot) (0x50fc20u + (uint32_t)(slot) * 0x44u)
#define FLIC_PAL(slot) (0x4e9aa0u + (uint32_t)(slot) * 0x58u)
#define SHADOW_SLOT 0x24e

#define U32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define I32(a) (*(volatile int32_t *)(uintptr_t)(a))

static int mem_ok(uint32_t p, uint32_t n) {
  if (p < 0x10000 || p + n < p) return 0;
  MEMORY_BASIC_INFORMATION mi;
  for (uint32_t a = p; a < p + n;) {
    if (!VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi) || mi.State != MEM_COMMIT ||
        (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
      return 0;
    a = (uint32_t)((uintptr_t)mi.BaseAddress + mi.RegionSize);
  }
  return 1;
}

// jgld palette -> its index -> RGB555 table (vtable +0x18), NULL if none.
static const uint16_t *pal16(uint32_t pal) {
  if (!pal || !mem_ok(pal, 8)) return NULL;
  uint32_t fn = U32(U32(pal) + 0x18), flags = 0;
  IcallFn f = icall_find(fn, &flags);
  if (!f) return NULL;
  uint32_t r = (flags & 1) ? f(pal, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) : f(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  return r && mem_ok(r, 512) ? (const uint16_t *)(uintptr_t)r : NULL;
}

static uint32_t hash_bytes(const void *p, size_t n, uint32_t h) {
  const uint8_t *b = p;
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}

static void ensure_hooks(void);
static void selftest_cut(uint32_t sprite, int file, int view, int frame, int lx, int ly);

// ------------------------------------------------------------------ tags (game thread)

typedef struct {
  uint32_t sprite, pixels;   // the jgld sprite and its pixel buffer when tagged
  int16_t w, h;              // its size
  int16_t file, view, frame;
  int16_t lx, ly;            // the frame's top-left inside the sprite (1x)
  uint8_t shadow;            // the shadow frame was drawn into it too
  uint32_t pal_hash;         // the file's own palette (RGB555), 0 unknown
} Tag;

#define TAG_N (1 << 17)
static Tag *g_tags;   // open addressing by sprite pointer; sprite 0 = empty
static int g_ntags;

static Tag *tag_slot(uint32_t sprite, int make) {
  uint32_t h = (sprite >> 3) * 2654435761u >> 15;
  for (int n = 0; n < TAG_N; n++, h = (h + 1) & (TAG_N - 1)) {
    Tag *t = &g_tags[h];
    if (t->sprite == sprite) return t;
    if (!t->sprite) {
      if (!make || g_ntags > TAG_N * 3 / 4) return NULL;
      g_ntags++;
      t->sprite = sprite;
      return t;
    }
  }
  return NULL;
}

static struct {
  int file, slot;          // the object FLC being prerendered
  int shadow_file;         // the shadow FLC opened just before it
  char shadow_norm[MAX_PATH];
} g_flic = {-1, -1, -1, ""};

static void on_flic_open(const char *path, int slot) {
  if (slot == SHADOW_SLOT) {
    g_flic.shadow_file = find_file(path, ".flc");
    return;
  }
  g_flic.file = find_file(path, ".flc");
  g_flic.slot = slot;
  // a shadow opened for another object does not belong to this one
  if (g_flic.file >= 0 && g_flic.shadow_file >= 0 && g_files[g_flic.file].shadow != g_flic.shadow_file) g_flic.shadow_file = -1;
  static int logged;
  if (g_log >= 2 || (g_flic.file < 0 && logged++ < 20)) fprintf(stderr, "art: flic %s slot %d -> %s\n", path, slot, g_flic.file >= 0 ? g_files[g_flic.file].src : "not in the index");
}

static struct { int cuts, tagged, untagged; } g_tagstat;

// Surfaces golf loaded a PCX into (FUN_00475840): wrapper -> index entry.
typedef struct { uint32_t surf; int file; char name[64]; } SurfFile;
#define NSURF 1024
static SurfFile g_surf[NSURF];
static int g_nsurf;

static void on_pcx_load(void *surface, const char *path) {
  uint32_t s = (uint32_t)(uintptr_t)surface;
  int i = 0;
  while (i < g_nsurf && g_surf[i].surf != s) i++;
  if (i == g_nsurf) {
    if (g_nsurf == NSURF) memmove(g_surf, g_surf + 1, (NSURF - 1) * sizeof *g_surf), i = NSURF - 1;
    else g_nsurf++;
  }
  g_surf[i].surf = s;
  g_surf[i].file = find_file(path, ".pcx");
  snprintf(g_surf[i].name, sizeof g_surf[i].name, "%s", path);
  if (g_log >= 3) fprintf(stderr, "art: pcx %s into 0x%08x -> %s\n", path, s, g_surf[i].file >= 0 ? g_files[g_surf[i].file].src : "not in the index");
}

static const SurfFile *surf_file(uint32_t s) {
  for (int i = g_nsurf - 1; i >= 0; i--)
    if (g_surf[i].surf == s) return &g_surf[i];
  return NULL;
}

typedef struct { uint32_t sprite, src; int x, y, w, h; } CutLog;   // art_log=3: untagged cuts
static CutLog g_cutlog[8192];
static int g_ncutlog;

static void on_cut(void *wrap, int src, int x, int y, int w, int h) {
  (void)w;
  (void)h;
  g_tagstat.cuts++;
  ensure_hooks();
  uint32_t sprite = U32((uintptr_t)wrap + 4);
  const SurfFile *sf = sprite && (uint32_t)src != SCRATCH ? surf_file((uint32_t)src) : NULL;
  if (sf && sf->file >= 0 && (g_files[sf->file].kind == K_STILL || g_files[sf->file].kind == K_OTHER)) {
    // a rectangle of a PCX sheet: the sprite's pixel (0, 0) is (x, y) + trim
    Tag *t = tag_slot(sprite, 1);
    if (!t) return;
    t->pixels = U32(sprite + 0x14);
    t->w = (int16_t)I32(sprite + 0x30);
    t->h = (int16_t)I32(sprite + 0x34);
    t->file = (int16_t)sf->file;
    t->view = -1;
    t->frame = 0;
    t->lx = (int16_t)(x + I32((uintptr_t)wrap + 0x20));
    t->ly = (int16_t)(y + I32((uintptr_t)wrap + 0x24));
    t->shadow = 0;
    const uint16_t *p = pal16(U32(sprite + 0x10));
    t->pal_hash = p ? hash_bytes(p, 512, 2166136261u) | 1 : 0;
    g_tagstat.tagged++;
    return;
  }
  if (!sprite || (uint32_t)src != SCRATCH || g_flic.file < 0 || g_flic.slot < 0) {
    g_tagstat.untagged++;
    if (g_log >= 3 && g_ncutlog < 8192) g_cutlog[g_ncutlog++] = (CutLog){sprite, (uint32_t)src, x, y, w, h};
    return;
  }
  ArtFile *a = &g_files[g_flic.file];
  Tag *t = tag_slot(sprite, 1);
  if (!t) return;
  t->pixels = U32(sprite + 0x14);
  t->w = (int16_t)I32(sprite + 0x30);
  t->h = (int16_t)I32(sprite + 0x34);
  t->file = (int16_t)g_flic.file;
  t->view = (int16_t)I32(FLIC_PLAYER(g_flic.slot) + 0x40);
  t->frame = (int16_t)I32(FLIC_PLAYER(g_flic.slot) + 0x3c);
  int tx = I32((uintptr_t)wrap + 0x20), ty = I32((uintptr_t)wrap + 0x24);
  t->lx = (int16_t)(a->crop_x - x - tx);
  t->ly = (int16_t)(a->crop_y - y - ty);
  t->shadow = g_flic.shadow_file >= 0;
  const uint16_t *p = pal16(U32(FLIC_PAL(g_flic.slot) + 4));
  t->pal_hash = p ? hash_bytes(p, 512, 2166136261u) | 1 : 0;
  g_tagstat.tagged++;
  if (g_selftest > 0 && g_tagstat.tagged % 7 == 1) selftest_cut(sprite, t->file, t->view, t->frame, t->lx, t->ly);
}

// ------------------------------------------------------------------ frame lists (game thread)
//
// Two lists per presented frame: LC, the course (from the last present to
// hd_ui_course_done), and LT, everything on top of it (golf's UI over the
// course; on title, menu and report pages the whole page). Each owns the
// canvas pixels its draws changed, checked against the picture at its end.

enum { V_RGBA, V_IDX, V_SHADOW, V_RECOLOR, V_MASKED, V_SHADE, V_INDEXED_SHADOW };
enum { LC, LT };

typedef struct {
  int16_t file, view, frame;   // view -1: a still (PCX), frame unused
  uint8_t variant, flip;       // flip: 1 horizontal, 2 vertical
  uint8_t hidden;              // task-81: under the Frosted Mint HUD's hide, never drawn
  uint32_t pal_hash;          // V_IDX/V_RECOLOR: the palette; V_MASKED: the mask file
  float x, y, w, h;            // the frame's place on the canvas
  int16_t shadow_file;         // -1: no shadow part
  uint32_t shadow_hash;
  int16_t sx, sy, sw, sh;      // stills: the source rectangle (1x pixels)
  uint8_t shadow_max_index;    // V_INDEXED_SHADOW: last source index treated as a LUT plane
} Entry;

typedef struct {
  Entry *e;
  int n, cap;
  uint16_t *owner;   // canvas: entry index + 1 that wrote the pixel, 0 none
  int w, h;
} List;

static void preview_init(void);
static void preview_look_changed(void);
static void preview_reject(void);
static void preview_record(const Entry *, const uint16_t *, const uint16_t *);

static List g_rec;                 // being recorded (game thread)
static int g_rec_which = LC;
static unsigned g_rec_generation; // changes whenever the recording list is reset and reused
static int g_issue44_trace_enabled, g_issue44_trace_pending;
static unsigned g_issue44_trace_seq;
static uint16_t *g_val;            // the value each owner wrote
static List g_closed[2];           // finished lists of this frame (game thread)
static int g_closed_ok[2];

static void issue44_cutoff_trace(const char *event) {
  if (!g_issue44_trace_enabled || !g_issue44_trace_pending) return;
  fprintf(stderr, "issue44-cutoff #%u %s generation=%u which=%d count=%d\n",
          ++g_issue44_trace_seq, event, g_rec_generation, g_rec_which, g_rec.n);
}

static void list_reset(List *L, int w, int h) {
  if (L == &g_rec) issue44_cutoff_trace("rec_reset_enter");
  if (L->w != w || L->h != h || !L->owner) {
    free(L->owner);
    L->owner = calloc((size_t)w * h, 2);
    L->w = w;
    L->h = h;
  } else {
    memset(L->owner, 0, (size_t)w * h * 2);
  }
  L->n = 0;
  if (L == &g_rec) {
    g_rec_generation++;
    issue44_cutoff_trace("rec_reset_exit");
  }
}

// Ends the list being recorded: a pixel stays an entry's only if it still
// holds what the entry wrote (bits: the canvas, pitch in pixels).
static void list_close(int which, const uint16_t *bits, int pitch) {
  List *L = &g_rec;
  if (!L->owner || L->w != HD_FRAME_W || L->h != HD_FRAME_H) {
    list_reset(L, HD_FRAME_W, HD_FRAME_H);
    g_closed_ok[which] = 0;
    return;
  }
  for (int y = 0; y < L->h; y++) {
    uint16_t *o = L->owner + (size_t)y * L->w;
    const uint16_t *v = g_val + (size_t)y * L->w, *b = bits + (size_t)y * pitch;
    for (int x = 0; x < L->w; x++)
      if (o[x] && b[x] != v[x]) o[x] = 0;
  }
  List t = g_closed[which];
  g_closed[which] = *L;
  *L = t;
  g_closed_ok[which] = 1;
  list_reset(L, HD_FRAME_W, HD_FRAME_H);
}

// ------------------------------------------------------------------ loader

typedef struct { uint32_t a, b; } Key;   // a: file << 16 | view << 8 | frame; b: variant << 28 | hash

typedef struct {
  Key key;
  int file, view, frame, variant;
  int sx, sy, sw, sh, mask;   // stills: the rectangle (1x) and the mask file (-1 none)
  int shadow_max_index;       // V_INDEXED_SHADOW: last source index treated as a LUT plane
  uint16_t pal[256];   // V_IDX/V_RECOLOR: RGB555 colours; shadows: black alpha per index from 0xf8
} Req;

typedef struct {
  Key key;
  int w, h;
  uint8_t *rgba;   // NULL: failed
  double ms;
} Done;

enum { S_NONE, S_QUEUED, S_READY, S_FAILED };
typedef struct { Key key; uint8_t used, state; } KeyState;
#define KS_N (1 << 19)
static KeyState *g_ks;   // shared, under g_lock
static int g_nks;
static CRITICAL_SECTION g_lock;
static HANDLE g_wake;

#define QN 4096
static Req *g_q;           // ring, under g_lock
static int g_qh, g_qt;
static Done *g_done;       // ring, under g_lock
static int g_dh, g_dt;
static volatile LONG g_loads, g_fails;
static double g_load_ms;

static int key_eq(Key x, Key y) { return x.a == y.a && x.b == y.b; }
static uint32_t key_hash(Key k) { return k.a * 2654435761u ^ k.b * 2246822519u ^ (k.b >> 13); }

static KeyState *ks_find(Key k, int make) {
  uint32_t h = key_hash(k) & (KS_N - 1);
  for (int n = 0; n < KS_N; n++, h = (h + 1) & (KS_N - 1)) {
    KeyState *s = &g_ks[h];
    if (!s->used) {
      if (!make || g_nks > KS_N * 3 / 4) return NULL;
      s->used = 1;
      s->key = k;
      s->state = S_NONE;
      g_nks++;
      return s;
    }
    if (key_eq(s->key, k)) return s;
  }
  return NULL;
}

static Key make_key(int file, int view, int frame, int variant, uint32_t hash) {
  Key k = {(uint32_t)file << 16 | (uint32_t)(view & 0xff) << 8 | (uint32_t)(frame & 0xff),
           (uint32_t)variant << 28 | (hash & 0x0fffffff)};
  return k;
}

// The texture an entry needs (shadow: its shadow part). Stills: one texture
// per rectangle of the sheet (and mask), view/frame 0xff.
static Key entry_key(const Entry *e, int shadow) {
  if (shadow) return make_key(e->shadow_file, e->view, e->frame, V_SHADOW, e->shadow_hash);
  if (e->view < 0) {
    uint32_t h = hash_bytes(&e->sx, 8, 2166136261u);
    h = (h ^ (uint32_t)(e->variant == V_MASKED || e->variant == V_SHADE || e->variant == V_INDEXED_SHADOW ? e->pal_hash + 1 : 0)) * 16777619u;
    if (e->variant == V_INDEXED_SHADOW) h = (h ^ e->shadow_max_index) * 16777619u;
    return make_key(e->file, 0xff, 0xff, e->variant, h);
  }
  return make_key(e->file, e->view, e->frame, e->variant, e->pal_hash);
}

// Game thread: asks for an entry's texture unless it is there or on its way.
static void want(const Entry *e, int shadow, const uint16_t *pal) {
  Key k = entry_key(e, shadow);
  EnterCriticalSection(&g_lock);
  KeyState *s = ks_find(k, 1);
  if (s && s->state == S_NONE && (g_qt - g_qh) < QN) {
    s->state = S_QUEUED;
    Req *r = &g_q[g_qt++ % QN];
    memset(r, 0, sizeof *r);
    r->key = k;
    r->file = shadow ? e->shadow_file : e->file;
    r->view = e->view;
    r->frame = e->frame;
    r->variant = shadow ? V_SHADOW : e->variant;
    r->sx = e->sx, r->sy = e->sy, r->sw = e->sw, r->sh = e->sh;
    r->shadow_max_index = shadow ? 0 : e->shadow_max_index;
    r->mask = e->variant == V_MASKED ? (int)e->pal_hash : -1;
    if (pal) memcpy(r->pal, pal, 512);
    SetEvent(g_wake);
  }
  LeaveCriticalSection(&g_lock);
}

static __declspec(thread) IWICImagingFactory *g_wic;   // one per loader thread

static uint8_t *decode(const char *path, int want_idx, int *w, int *h, uint8_t *pal_rgb) {
  wchar_t wp[MAX_PATH + 16];
  if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, MAX_PATH + 16)) return NULL;
  IWICBitmapDecoder *dec = NULL;
  IWICBitmapFrameDecode *fr = NULL;
  IWICFormatConverter *conv = NULL;
  uint8_t *out = NULL;
  if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(g_wic, wp, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)))
    return NULL;
  if (SUCCEEDED(IWICBitmapDecoder_GetFrame(dec, 0, &fr))) {
    UINT W, H;
    IWICBitmapFrameDecode_GetSize(fr, &W, &H);
    *w = (int)W;
    *h = (int)H;
    if (want_idx > 0) {
      WICPixelFormatGUID fmt;
      IWICBitmapFrameDecode_GetPixelFormat(fr, &fmt);
      if (IsEqualGUID(&fmt, &GUID_WICPixelFormat8bppIndexed)) {
        out = malloc((size_t)W * H);
        if (FAILED(IWICBitmapFrameDecode_CopyPixels(fr, NULL, W, W * H, out))) { free(out); out = NULL; }
        if (out && pal_rgb) {
          IWICPalette *p = NULL;
          if (SUCCEEDED(IWICImagingFactory_CreatePalette(g_wic, &p)) && SUCCEEDED(IWICBitmapFrameDecode_CopyPalette(fr, p))) {
            WICColor c[256] = {0};
            UINT n = 0;
            IWICPalette_GetColors(p, 256, c, &n);
            for (int i = 0; i < 256; i++) {
              pal_rgb[i * 3] = (uint8_t)(c[i] >> 16);
              pal_rgb[i * 3 + 1] = (uint8_t)(c[i] >> 8);
              pal_rgb[i * 3 + 2] = (uint8_t)c[i];
            }
          }
          if (p) IWICPalette_Release(p);
        }
      }
    } else if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter(g_wic, &conv)) &&
               SUCCEEDED(IWICFormatConverter_Initialize(conv, (IWICBitmapSource *)fr,
                                                        want_idx < 0 ? &GUID_WICPixelFormat8bppGray : &GUID_WICPixelFormat32bppRGBA,
                                                        WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) {
      UINT bpp = want_idx < 0 ? 1 : 4;   // want_idx -1: 8-bit grey
      out = malloc((size_t)W * H * bpp);
      if (FAILED(IWICFormatConverter_CopyPixels(conv, NULL, W * bpp, W * H * bpp, out))) { free(out); out = NULL; }
    }
  }
  if (conv) IWICFormatConverter_Release(conv);
  if (fr) IWICBitmapFrameDecode_Release(fr);
  IWICBitmapDecoder_Release(dec);
  return out;
}

static void frame_path(char *out, size_t n, int file, int view, int frame, const char *suffix) {
  snprintf(out, n, "%s/%s/v%d_f%02d%s", g_dir, g_files[file].out, view, frame, suffix);
}

// Decoded sheets shared by the loaders (UI and building sheets are up to
// 3200x2400; one decode serves every rectangle cut from it).
enum { SHEET_RGBA, SHEET_MASK, SHEET_INDEXED };
typedef struct { int file, format, w, h, refs; uint8_t *px; uint32_t used; } Sheet;
#define NSHEET 3
static Sheet g_sheet[NSHEET];
static CRITICAL_SECTION g_sheet_lock;
static uint32_t g_sheet_tick;

// The original 8-bit sheet straight from the player's game folder (BMP through
// WIC, PCX with a small RLE reader), for installs without an extracted/ folder.
static uint8_t *load_game_original(const char *src, int *w, int *h, uint8_t *pal) {
  char path[MAX_PATH * 2];
  snprintf(path, sizeof path, "%s/%s", host_game_dir(), src);
  const char *dot = strrchr(src, '.');
  if (!dot) return NULL;
  if (!_stricmp(dot, ".bmp")) return decode(path, 1, w, h, pal);
  if (_stricmp(dot, ".pcx")) return NULL;
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = n > 896 && n < (256 << 20) ? malloc((size_t)n) : NULL;
  if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
  fclose(f);
  if (!b) return NULL;
  uint8_t *out = NULL;
  int W = b[8] | b[9] << 8, H = b[10] | b[11] << 8, bpl = b[66] | b[67] << 8;
  W = W - (b[4] | b[5] << 8) + 1;
  H = H - (b[6] | b[7] << 8) + 1;
  if (b[0] == 10 && b[2] == 1 && b[3] == 8 && b[65] == 1 && W > 0 && H > 0 && bpl >= W && n > 769 && b[n - 769] == 12) {
    out = malloc((size_t)W * H);
    long p = 128;
    int ok = 1;
    for (int y = 0; y < H && ok; y++) {
      int x = 0;
      while (x < bpl) {
        if (p >= n - 769) { ok = 0; break; }
        int c = b[p++], run = 1;
        if (c >= 0xc0) {
          run = c & 0x3f;
          if (p >= n - 769) { ok = 0; break; }
          c = b[p++];
        }
        for (; run--; x++)
          if (x < W) out[(size_t)y * W + x] = (uint8_t)c;
      }
    }
    if (!ok) { free(out); out = NULL; }
    else {
      memcpy(pal, b + n - 768, 768);
      *w = W;
      *h = H;
    }
  }
  free(b);
  return out;
}

// Some upscaled sheets are plain RGB: golf's transparent indices (0xfe,
// 0xff) came out as their palette colour. Their alpha then comes from the
// original 8-bit sheet (art_local/extracted), its opaque/clear map sampled
// bilinearly at 4x and cut at one half, so edges are smooth, not blocky.
static void sheet_alpha(int file, uint8_t *px, int w, int h) {
  size_t n = (size_t)w * h;
  for (size_t i = 0; i < n; i++)
    if (px[i * 4 + 3] != 255) return;   // it has its own alpha
  char path[MAX_PATH * 2];
  snprintf(path, sizeof path, "%s/%s.png", g_xdir, g_files[file].out);
  int ow = 0, oh = 0;
  uint8_t pal[768] = {0};
  uint8_t *idx = decode(path, 1, &ow, &oh, pal);
  if (!idx) idx = load_game_original(g_files[file].src, &ow, &oh, pal);   // a player install has no extracted/ folder
  if (!idx || ow <= 0 || oh <= 0) {
    free(idx);
    return;
  }
  double kx = (double)ow / w, ky = (double)oh / h;
  for (int y = 0; y < h; y++) {
    double fy = (y + 0.5) * ky - 0.5;
    int y0 = (int)floor(fy);
    double ty = fy - y0;
    int ya = y0 < 0 ? 0 : y0 >= oh ? oh - 1 : y0, yb = y0 + 1 >= oh ? oh - 1 : y0 + 1 < 0 ? 0 : y0 + 1;
    for (int x = 0; x < w; x++) {
      double fx = (x + 0.5) * kx - 0.5;
      int x0 = (int)floor(fx);
      double tx = fx - x0;
      int xa = x0 < 0 ? 0 : x0 >= ow ? ow - 1 : x0, xb = x0 + 1 >= ow ? ow - 1 : x0 + 1 < 0 ? 0 : x0 + 1;
      double v = (idx[ya * ow + xa] < 0xfe) * (1 - tx) * (1 - ty) + (idx[ya * ow + xb] < 0xfe) * tx * (1 - ty) +
                 (idx[yb * ow + xa] < 0xfe) * (1 - tx) * ty + (idx[yb * ow + xb] < 0xfe) * tx * ty;
      uint8_t *o = px + ((size_t)y * w + x) * 4;
      int a = v >= 0.5;
      if (a && v < 0.999) {   // an edge: the upscaler blended the key colour in, drop pixels close to it
        for (int k = 0xfe; k <= 0xff && a; k++) {
          int dr = o[0] - pal[k * 3], dg = o[1] - pal[k * 3 + 1], db = o[2] - pal[k * 3 + 2];
          if (dr * dr + dg * dg + db * db < 70 * 70) a = 0;
        }
      }
      o[3] = a ? 255 : 0;
    }
  }
  free(idx);
}

static Sheet *sheet_get(int file, int format) {
  EnterCriticalSection(&g_sheet_lock);
  for (int i = 0; i < NSHEET; i++)
    if (g_sheet[i].px && g_sheet[i].file == file && g_sheet[i].format == format) {
      g_sheet[i].refs++;
      g_sheet[i].used = ++g_sheet_tick;
      LeaveCriticalSection(&g_sheet_lock);
      return &g_sheet[i];
    }
  LeaveCriticalSection(&g_sheet_lock);
  char path[MAX_PATH * 2];
  int w = 0, h = 0;
  uint8_t *px = NULL;
  if (format == SHEET_INDEXED) {   // original 8-bit source indices for palette-driven still effects
    snprintf(path, sizeof path, "%s/%s.png", g_xdir, g_files[file].out);
    px = decode(path, 1, &w, &h, NULL);
    if (!px) {
      uint8_t ignored_pal[768];
      px = load_game_original(g_files[file].src, &w, &h, ignored_pal);
    }
  } else {
    snprintf(path, sizeof path, "%s/%s.png", g_dir, g_files[file].out);
    px = decode(path, format == SHEET_RGBA ? 0 : -1, &w, &h, NULL);
  }
  if (!px) return NULL;
  if (format == SHEET_RGBA) sheet_alpha(file, px, w, h);
  EnterCriticalSection(&g_sheet_lock);
  Sheet *s = NULL;
  for (int i = 0; i < NSHEET; i++)   // a free slot, else the least recently used one nobody holds
    if (!g_sheet[i].refs && (!s || !g_sheet[i].px || (s->px && g_sheet[i].used < s->used))) s = &g_sheet[i];
  if (!s) {   // all held: this decode stays private
    LeaveCriticalSection(&g_sheet_lock);
    Sheet *p = calloc(1, sizeof *p);
    *p = (Sheet){file, format, w, h, -1, px, 0};
    return p;
  }
  free(s->px);
  *s = (Sheet){file, format, w, h, 1, px, ++g_sheet_tick};
  LeaveCriticalSection(&g_sheet_lock);
  return s;
}

static void sheet_put(Sheet *s) {
  if (s->refs < 0) {   // private
    free(s->px);
    free(s);
    return;
  }
  EnterCriticalSection(&g_sheet_lock);
  s->refs--;
  LeaveCriticalSection(&g_sheet_lock);
}

// A rectangle of a still (4x), with the soft alpha of its mask for UI art
// (golf's DrawMasked: colour premultiplied over black, mask grey = alpha).
static uint8_t *load_still(const Req *r, int *w, int *h) {
  Sheet *c = sheet_get(r->file, SHEET_RGBA);
  if (!c) {
    if (g_fails < 20) fprintf(stderr, "art: sheet %s cannot decode\n", g_files[r->file].src);
    return NULL;
  }
  int k = c->w / (g_files[r->file].w4 / 4 > 0 ? g_files[r->file].w4 / 4 : c->w);   // 4
  if (k < 1) k = 1;
  int x0 = r->sx * k, y0 = r->sy * k, W = r->sw * k, H = r->sh * k;
  uint8_t *out = NULL;
  if (W > 0 && H > 0 && x0 >= 0 && y0 >= 0 && x0 + W <= c->w && y0 + H <= c->h) {
    out = malloc((size_t)W * H * 4);
    for (int y = 0; y < H; y++) memcpy(out + (size_t)y * W * 4, c->px + ((size_t)(y0 + y) * c->w + x0) * 4, (size_t)W * 4);
    if (r->variant == V_SHADE)   // black, as dark as golf's first shadow level, where the sheet has pixels
      for (int i = 0; i < W * H; i++) {
        uint8_t *o = out + (size_t)i * 4;
        o[3] = (uint8_t)(o[3] * r->pal[0xf8] / 255);
        o[0] = o[1] = o[2] = 0;
      }
    if (r->variant == V_INDEXED_SHADOW) {
      Sheet *idx = sheet_get(r->file, SHEET_INDEXED);
      int ow = g_files[r->file].w4 / 4, oh = g_files[r->file].h4 / 4;
      int ok = idx && idx->w == ow && idx->h == oh &&
               hd_art_shadow_indexed(out, W, H, idx->px, idx->w, idx->h, r->sx, r->sy, r->sw, r->sh,
                                     r->pal, r->shadow_max_index, 1);
      if (!ok) {
        if (g_fails < 20)
          fprintf(stderr, "art: original indices unavailable for palette shadow %s\n", g_files[r->file].src);
        free(out);
        out = NULL;
      }
      if (idx) sheet_put(idx);
    }
    if (out && r->mask >= 0) {
      Sheet *m = sheet_get(r->mask, SHEET_MASK);
      if (!m || m->w != c->w || m->h != c->h) {
        if (g_fails < 20)
          fprintf(stderr, "art: mask %s for %s: %s\n", g_files[r->mask].src, g_files[r->file].src,
                  !m ? "cannot decode" : "size differs");
        free(out);
        out = NULL;
      } else {
        for (int y = 0; y < H; y++)
          for (int x = 0; x < W; x++) {
            uint8_t *o = out + ((size_t)y * W + x) * 4;
            uint8_t a = m->px[(size_t)(y0 + y) * m->w + x0 + x];
            if (!o[3]) a = 0;   // colour key: never drawn
            if (!a) o[0] = o[1] = o[2] = 0;
            o[3] = a;
          }
      }
      if (m) sheet_put(m);
    }
  }
  sheet_put(c);
  *w = W;
  *h = H;
  return out;
}

static void load_one(const Req *r, Done *d) {
  double t0 = host_now_ms();
  char path[MAX_PATH * 2];
  int w = 0, h = 0;
  uint8_t *rgba = NULL;
  snprintf(path, sizeof path, "%s", g_files[r->file].src);
  if (r->view < 0 && r->variant != V_SHADOW) {
    rgba = load_still(r, &w, &h);
  } else if (r->variant == V_RGBA) {
    frame_path(path, sizeof path, r->file, r->view, r->frame, ".png");
    rgba = decode(path, 0, &w, &h, NULL);
  } else if (r->variant == V_RECOLOR) {
    // The smooth upscale with golf's palette: each pixel moves by the change
    // of its palette entry (the 8-bit upscale says which entry it belongs to).
    frame_path(path, sizeof path, r->file, r->view, r->frame, ".png");
    rgba = decode(path, 0, &w, &h, NULL);
    uint8_t old[768] = {0};
    int iw = 0, ih = 0;
    frame_path(path, sizeof path, r->file, r->view, r->frame, ".idx.png");
    uint8_t *idx = rgba ? decode(path, 1, &iw, &ih, old) : NULL;
    if (!idx || iw != w || ih != h) {
      free(rgba);
      rgba = NULL;
    } else {
      int delta[256][3];
      for (int i = 0; i < 256; i++) {
        uint16_t c = r->pal[i];
        int nv[3] = {(c >> 10) & 31, (c >> 5) & 31, c & 31};
        for (int k = 0; k < 3; k++) {
          int o = old[i * 3 + k], n5 = nv[k];
          delta[i][k] = (o >> 3) == n5 ? 0 : (n5 << 3 | n5 >> 2) - o;   // same 5-bit colour: no change
        }
      }
      for (int i = 0; i < w * h; i++) {
        uint8_t v = idx[i], *o = rgba + (size_t)i * 4;
        if (v >= 0xf8) continue;
        for (int k = 0; k < 3; k++) {
          int x = o[k] + delta[v][k];
          o[k] = (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x);
        }
      }
    }
    free(idx);
  } else {
    frame_path(path, sizeof path, r->file, r->view, r->frame, ".idx.png");
    uint8_t *idx = decode(path, 1, &w, &h, NULL);
    if (idx) {
      rgba = malloc((size_t)w * h * 4);
      for (int i = 0; i < w * h; i++) {
        uint8_t v = idx[i], *o = rgba + (size_t)i * 4;
        if (r->variant == V_SHADOW) {
          o[0] = o[1] = o[2] = 0;
          o[3] = v >= 0xf8 && v <= 0xfb ? (uint8_t)r->pal[v] : 0;
        } else if (v >= 0xf8) {
          o[0] = o[1] = o[2] = o[3] = 0;
        } else {
          uint16_t c = r->pal[v];
          int R = (c >> 10) & 31, G = (c >> 5) & 31, B = c & 31;
          o[0] = (uint8_t)(R << 3 | R >> 2);
          o[1] = (uint8_t)(G << 3 | G >> 2);
          o[2] = (uint8_t)(B << 3 | B >> 2);
          o[3] = 255;
        }
      }
      free(idx);
    }
  }
  d->key = r->key;
  d->w = w;
  d->h = h;
  d->rgba = rgba;
  d->ms = host_now_ms() - t0;
  if (!rgba && g_fails < 20) fprintf(stderr, "art: cannot load %s\n", path);
}

// ------------------------------------------------------------------ self-test (art_selftest=N)
//
// The first N tags (every 7th cut) are checked against the original frames
// in art_local/extracted: each opaque pixel of the tagged frame must be in
// the cached sprite at the tag's place. Frames f-1 and f+1 are scored too,
// so an off-by-one shows up as a better neighbour.

typedef struct Verify {
  struct Verify *next;
  int file, view, frame, lx, ly, w, h;
  uint8_t *px;
} Verify;

static Verify *g_verify;   // under g_lock
static struct { volatile LONG n, best[3], low, missing; double score; } g_vstat;

static double frame_score(const Verify *v, int frame, int *missing) {
  char path[MAX_PATH * 2];
  snprintf(path, sizeof path, "%s/%s/v%d_f%02d.png", g_xdir, g_files[v->file].out, v->view, frame);
  int w = 0, h = 0;
  uint8_t *o = decode(path, 1, &w, &h, NULL);
  if (!o) {
    *missing = 1;
    return -1;
  }
  int tot = 0, ok = 0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      uint8_t c = o[y * w + x];
      if (c >= 0xfe) continue;
      int sx = v->lx + x, sy = v->ly + y;
      tot++;
      if (sx >= 0 && sy >= 0 && sx < v->w && sy < v->h && v->px[sy * v->w + sx] == c) ok++;
    }
  free(o);
  return tot ? (double)ok / tot : 1.0;
}

static void verify_one(Verify *v) {
  double s[3];
  int miss = 0, m = 0;
  for (int d = -1; d <= 1; d++) {
    int mm = 0;
    s[d + 1] = v->frame + d >= 0 ? frame_score(v, v->frame + d, &mm) : -1;
    if (d == 0) miss = mm;
    m |= mm && d == 0;
  }
  int best = 1;
  for (int i = 0; i < 3; i++)
    if (s[i] > s[best] + 0.02) best = i;
  EnterCriticalSection(&g_lock);
  g_vstat.n++;
  if (miss) g_vstat.missing++;
  else {
    g_vstat.best[best]++;
    g_vstat.score += s[1];
    if (s[1] < 0.9) {
      g_vstat.low++;
      if (g_vstat.low <= 12)
        fprintf(stderr, "art: selftest %s v%d f%d (%dx%d at %d,%d): %.3f (f-1 %.3f, f+1 %.3f)\n", g_files[v->file].src, v->view,
                v->frame, v->w, v->h, v->lx, v->ly, s[1], s[0], s[2]);
    }
  }
  LeaveCriticalSection(&g_lock);
  (void)m;
  free(v->px);
  free(v);
}

static void selftest_cut(uint32_t sprite, int file, int view, int frame, int lx, int ly) {
  int w = I32(sprite + 0x30), h = I32(sprite + 0x34);
  uint32_t px = U32(sprite + 0x14);
  if ((U32(sprite + 0x18) & 1) || w <= 0 || h <= 0 || !mem_ok(px, (uint32_t)(w * h))) return;   // RLE: skip
  g_selftest--;
  Verify *v = calloc(1, sizeof *v);
  v->file = file, v->view = view, v->frame = frame, v->lx = lx, v->ly = ly, v->w = w, v->h = h;
  v->px = malloc((size_t)w * h);
  memcpy(v->px, (void *)(uintptr_t)px, (size_t)w * h);
  EnterCriticalSection(&g_lock);
  v->next = g_verify;
  g_verify = v;
  SetEvent(g_wake);
  LeaveCriticalSection(&g_lock);
}

static DWORD WINAPI loader_main(void *arg) {
  (void)arg;
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&g_wic))) {
    fprintf(stderr, "art: no Windows Imaging Component; the art pack is off\n");
    g_on = 0;
    return 0;
  }
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  for (;;) {
    Req r;
    EnterCriticalSection(&g_lock);
    Verify *v = g_verify;
    if (v) g_verify = v->next;
    LeaveCriticalSection(&g_lock);
    if (v) {
      verify_one(v);
      continue;
    }
    EnterCriticalSection(&g_lock);
    int have = g_qh != g_qt;
    if (have) r = g_q[g_qh++ % QN];
    LeaveCriticalSection(&g_lock);
    if (!have) {
      WaitForSingleObject(g_wake, 100);
      continue;
    }
    Done d;
    load_one(&r, &d);
    EnterCriticalSection(&g_lock);
    while (g_dt - g_dh >= QN) {   // the render thread is behind (minimised): wait
      LeaveCriticalSection(&g_lock);
      Sleep(5);
      EnterCriticalSection(&g_lock);
    }
    g_done[g_dt++ % QN] = d;
    if (d.rgba) {
      g_loads++;
      g_load_ms += d.ms;
    } else {
      g_fails++;
    }
    LeaveCriticalSection(&g_lock);
  }
}

// ------------------------------------------------------------------ recording (game thread)

static IcallFn g_orig[41], g_sorig[64];
static uint32_t g_flags[41], g_sflags[64];
static struct { int calls[41], back[41], rec, owned, untagged[41], bycat[NCAT], blits, blits_rec; } g_stat;

static int pal_for(uint32_t sprite, uint32_t over, uint16_t *out, const uint8_t *remap) {
  uint32_t pal = over ? over : U32(sprite + 0x10);
  if (!pal && mem_ok(DEFAULT_PAL_HOLDER, 4) && U32(DEFAULT_PAL_HOLDER) && mem_ok(U32(DEFAULT_PAL_HOLDER), 8))
    pal = U32(U32(DEFAULT_PAL_HOLDER) + 4);
  const uint16_t *p = pal16(pal);
  if (!p) return 0;
  for (int i = 0; i < 256; i++) out[i] = p[remap ? remap[i] : i];
  return 1;
}

// The game's light/shadow planes: black alpha that matches each LUT plane on
// white. Slots 27 and 25 use the first plane; slot 30 has seven planes.
static uint32_t shadow_levels(uint32_t lut, uint16_t *pal, int planes) {
  uint32_t h = 2166136261u;
  if (planes < 1) planes = 1;
  if (planes > 7) planes = 7;
  for (int p = 0; p < planes; p++) {
    int a = 128;   // golf halves the colour when there is no table
    if (lut && mem_ok(lut + ((uint32_t)p << 16), 0x10000)) {
      uint16_t c = *(const uint16_t *)(uintptr_t)(lut + ((uint32_t)p << 16) + 0x7fff * 2);
      int g = (c >> 5) & 31;
      a = 255 - (g * 255 + 15) / 31;
      if (a < 0) a = 0;
    }
    pal[0xf8 + p] = (uint16_t)a;
    h = (h ^ (uint32_t)a) * 16777619u;
  }
  return h;
}

static int lut_has_additive_plane(uint32_t lut, int planes) {
  if (!lut || planes < 1) return 0;
  if (planes > 7) planes = 7;
  for (int p = 0; p < planes; p++) {
    uint32_t base = lut + ((uint32_t)p << 16);
    if (!mem_ok(base, 0x10000)) continue;
    const uint16_t *plane = (const uint16_t *)(uintptr_t)base;
    if (hd_art_lut_plane_is_additive(plane[0], plane[0x3def], plane[0x7fff])) return 1;
  }
  return 0;
}

// The canvas rectangle a draw at (x, y) of a w x h source may touch, copied
// before the draw so the pixels it changes can be found after it.
typedef struct {
  uint32_t base;
  int pitch, ox, oy, rx, ry, rw, rh;
  uint16_t *before;
} Snap;

static int snap_begin(Snap *s, uint32_t dst, int x, int y, double fw, double fh) {
  int bw, bh;
  memset(s, 0, sizeof *s);
  // The FLC decoder draws into its private 480x480 scratch surface. These
  // draws cannot affect the window, so no back-buffer snapshot is needed.
  // Confirm it is distinct from the live back surface before taking the path.
  uint32_t back = U32(0x4c1570u);
  if (port_loading_fast() && dst && dst == U32(SCRATCH + 4) && back >= 0x10000 && dst != U32(back + 4))
    return 0;
  if (!g_rec.owner || !mem_ok(dst, 0x4d0) || !hd_ui_back_buffer(&s->base, &s->pitch, &bw, &bh) || bw != g_rec.w || bh != g_rec.h)
    return 0;
  uint32_t dbits = U32(dst + 0x4c0);
  if (dbits < s->base || dbits >= s->base + (uint32_t)s->pitch * bh * 2) return 0;   // not the back buffer
  int off = (int)((dbits - s->base) / 2);
  s->ox = off % s->pitch;
  s->oy = off / s->pitch;
  int x0 = s->ox + x, y0 = s->oy + y, x1 = x0 + (int)ceil(fw) + 1, y1 = y0 + (int)ceil(fh) + 1;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > bw) x1 = bw;
  if (y1 > bh) y1 = bh;
  s->rx = x0, s->ry = y0, s->rw = x1 - x0, s->rh = y1 - y0;
  if (s->rw <= 0 || s->rh <= 0) return 0;
  s->before = malloc((size_t)s->rw * s->rh * 2);
  const uint16_t *src = (const uint16_t *)(uintptr_t)s->base;
  for (int j = 0; j < s->rh; j++) memcpy(s->before + j * s->rw, src + (size_t)(s->ry + j) * s->pitch + s->rx, (size_t)s->rw * 2);
  return 1;
}

// After the draw: the changed pixels become entry e's; asks for its textures.
static void snap_end(Snap *s, Entry *e, const uint16_t *pal, const uint16_t *lev) {
  List *L = &g_rec;
  if (L->n == L->cap) L->e = realloc(L->e, (L->cap = L->cap ? L->cap * 2 : 1024) * sizeof(Entry));
  int idx = L->n;
  if (idx >= 0xfffe) return;
  const uint16_t *now = (const uint16_t *)(uintptr_t)s->base;
  int owned = 0;
  for (int j = 0; j < s->rh; j++) {
    int cy = s->ry + j;
    for (int i = 0; i < s->rw; i++) {
      int cx = s->rx + i;
      uint16_t v = now[(size_t)cy * s->pitch + cx];
      if (v != s->before[j * s->rw + i]) {
        L->owner[cy * L->w + cx] = (uint16_t)(idx + 1);
        g_val[cy * L->w + cx] = v;
        owned++;
      }
    }
  }
  preview_record(e, pal, lev);   // previews also need white/black parts that leave the scratch fill unchanged
  if (!owned) return;
  L->e[L->n++] = *e;
  g_stat.rec++;
  g_stat.owned += owned;
  if (e->file >= 0 && g_files[e->file].cat >= 0) g_stat.bycat[g_files[e->file].cat]++;
  want(e, 0, pal);
  if (e->shadow_file >= 0) want(e, 1, lev);
}

static int look_of(int file) {
  int c = g_files[file].cat;
  return c >= 0 ? g_look[c] : L_ORIGINAL;
}

// A tagged sprite drawn with jgld slot `slot` at (x, y) of dst.
static void record_sprite(int slot, uint32_t sprite, uint32_t mask, uint32_t dst, int x, int y, uint32_t pal_over,
                          const uint8_t *remap, uint32_t lut, Snap *s) {
  Tag *t = tag_slot(sprite, 0);
  if (!t || t->pixels != U32(sprite + 0x14) || t->w != I32(sprite + 0x30) || t->h != I32(sprite + 0x34)) {
    preview_reject();
    g_stat.untagged[slot]++;
    return;
  }
  ArtFile *a = &g_files[t->file];
  int look = look_of(t->file);
  if (look == L_ORIGINAL) { preview_reject(); return; }
  int sx = SCALE_X, sy = SCALE_Y, den = SCALE_DEN ? SCALE_DEN : 1;
  double fx = fabs((double)sx / den), fy = fabs((double)sy / den);
  Entry e = {0};
  e.file = t->file;
  e.view = t->view;
  e.frame = t->frame;
  e.flip = (sx < 0 ? 1 : 0) | (sy < 0 ? 2 : 0);
  e.shadow_file = -1;
  uint16_t pal[256], lev[256] = {0};
  int have_pal = pal_for(sprite, pal_over, pal, remap);
  uint32_t ph = have_pal ? hash_bytes(pal, 512, 2166136261u) | 1 : 0;
  if (t->view < 0) {   // a still: the sprite is a rectangle of the PCX
    if (remap || (have_pal && t->pal_hash && ph != t->pal_hash)) { preview_reject(); return; }   // recoloured: the upscale has no 8-bit twin
    if (slot != 17 && slot != 23 && slot != 21 && slot != 20 && slot != 27 && slot != 30) { preview_reject(); return; }
    e.sx = t->lx, e.sy = t->ly, e.sw = t->w, e.sh = t->h;
    e.x = (float)(s->ox + x);
    e.y = (float)(s->oy + y);
    e.w = (float)(t->w * fx);
    e.h = (float)(t->h * fy);
    e.variant = V_RGBA;
    // task-78c: cliffs01.pcx holds the rock chips golf draws at cliff corners through these slots; they are
    // real rock pictures, not shading, and drawing them as shade painted dark wedges (the art pack's rocks stay)
    // Still effects are selected by source file because these slots also draw
    // ordinary course art, not only shade masks.
    int course_shadow = mods_get_int("fixes.course_shadow", 1);
    int still_effect = hd_art_still_effect(slot, a->norm, course_shadow);
    if (still_effect == HD_ART_STILL_INDEXED_SHADOW &&
        hd_art_course_target_cue_original(slot, a->norm, e.sx, e.sy, e.sw, e.sh, 1) && lut_has_additive_plane(lut, 7)) {
      static int logged_cue_fallback;
      if (getenv("SIMGOLF_ART_SLOT_LOG") && !logged_cue_fallback++) {
        fprintf(stderr, "art: course target cue keeps original draw; slot 30 LUT has additive plane\n");
        fflush(stderr);
      }
      return;  // this active cue blinks in golf's original draw; do not flatten its light plane to black alpha
    }
    // task-119 (tester F30): holemark.pcx is a colored plaque with 0xf8..0xfb shadow ink.
    // Keep the upscaled plaque pixels and turn only those original indexed inks into black alpha.
    if (still_effect == HD_ART_STILL_OPTIONAL_INDEXED_SHADOW) {
      if (mods_get_int("fixes.award_shadow", 1)) {
        e.variant = V_INDEXED_SHADOW;
        e.shadow_max_index = (uint8_t)hd_art_still_shadow_max_index(slot, a->norm);
        e.pal_hash = shadow_levels(lut, lev, e.shadow_max_index - 0xf8 + 1);
        memcpy(pal, lev, sizeof pal);
      }
    } else if (still_effect == HD_ART_STILL_INDEXED_SHADOW) {
      e.variant = V_INDEXED_SHADOW;
      e.shadow_max_index = (uint8_t)hd_art_still_shadow_max_index(slot, a->norm);
      e.pal_hash = shadow_levels(lut, lev, e.shadow_max_index - 0xf8 + 1);
      memcpy(pal, lev, sizeof pal);
    } else if (still_effect == HD_ART_STILL_SHADE) {   // index 0xf8: darken what is under it, black at golf's level
      e.variant = V_SHADE;
      e.pal_hash = shadow_levels(lut, lev, 4);
      memcpy(pal, lev, sizeof pal);
    }
    static int slot_log = -1;
    static unsigned char logged_effect[64][4];
    if (slot_log < 0) slot_log = getenv("SIMGOLF_ART_SLOT_LOG") != NULL;
    if (slot_log && strstr(a->norm, "course1.pcx") && e.sx == 549 && e.sy == 215 && e.sw == 43 && e.sh == 21 &&
        slot >= 0 && slot < 64 && still_effect >= 0 && still_effect < 4 && !logged_effect[slot][still_effect]) {
      const char *effect = still_effect == HD_ART_STILL_SHADE       ? "shade"
                           : still_effect == HD_ART_STILL_INDEXED_SHADOW ? "indexed-shadow"
                           : still_effect == HD_ART_STILL_OPTIONAL_INDEXED_SHADOW ? "optional-indexed-shadow"
                                                                               : "normal";
      logged_effect[slot][still_effect] = 1;
      fprintf(stderr, "art: course1 probe slot %d path %s src %d,%d %dx%d course_shadow %d effect %s variant %d\n", slot,
              a->norm, e.sx, e.sy, e.sw, e.sh, course_shadow, effect, e.variant);
      fflush(stderr);
    }
    if (slot == 21 || slot == 20) {   // soft-edged UI art: its mask is a tagged still too
      Tag *m = tag_slot(mask, 0);
      if (!m || m->view >= 0 || m->lx != t->lx || m->ly != t->ly) { preview_reject(); return; }
      e.variant = V_MASKED;
      e.pal_hash = (uint32_t)m->file;
    }
  } else {
    int fw = a->w4 / 4, fh = a->h4 / 4;
    double lx = t->lx, ly = t->ly;
    if (sx < 0) lx = t->w - t->lx - fw;
    if (sy < 0) ly = t->h - t->ly - fh;
    e.x = (float)(s->ox + x + lx * fx);
    e.y = (float)(s->oy + y + ly * fy);
    e.w = (float)(fw * fx);
    e.h = (float)(fh * fy);
    // upscaled: the RGBA upscale, recoloured where golf draws with another
    // palette or a remap; locked: the 8-bit upscale through golf's palette
    e.variant = look == L_LOCKED ? V_IDX : (!have_pal || ph == t->pal_hash) ? V_RGBA : V_RECOLOR;
    if (a->kind == K_SHADOW) {
      // A shadow FLC golf draws as a sprite of its own (the blimps' 0xf3 and 0xf5): its frames hold only
      // the shadow levels 0xf8..0xfb, so it is black at golf's level alpha like a shadow part, never
      // the opaque RGBA upscale (a black box with a white silhouette, task-72).
      uint32_t lut_for = slot == 30 || slot == 27 || slot == 25 ? lut : 0;
      e.variant = V_SHADOW;
      e.pal_hash = shadow_levels(lut_for, lev, 4);
      memcpy(pal, lev, sizeof pal);
    } else {
      if (e.variant != V_RGBA && !have_pal) { preview_reject(); return; }
      e.pal_hash = e.variant != V_RGBA ? ph : 0;
    }
    if (t->shadow && a->shadow >= 0) {
      e.shadow_file = (int16_t)a->shadow;
      e.shadow_hash = shadow_levels(slot == 30 || slot == 27 || slot == 25 ? lut : 0, lev, 4);
    }
  }
  snap_end(s, &e, pal, lev);
}

// dst argument position per slot (after `this`), -1: none.
static int dst_arg(int slot) {
  switch (slot) {
    case 15: case 16: case 17: case 18: case 19: case 23: case 25: case 27: case 29: case 30: case 31: case 33:
    case 36: case 37: case 38: case 39: case 40: case 34: case 35: return 0;
    case 21: case 22: return 1;
    case 20: return 2;
    default: return -1;
  }
}

static uint32_t hooked(int slot, uint32_t *a) {
  // a[0] = this (thiscall), then the method's arguments
  g_stat.calls[slot]++;
  int da = dst_arg(slot);
  uint32_t sprite = a[0], dst = da >= 0 ? a[1 + da] : 0;
  int follow = g_on && g_any && sprite && dst && (g_flags[slot] & 1) &&
               (slot == 17 || slot == 16 || slot == 23 || slot == 25 || slot == 27 || slot == 30 || slot == 21 || slot == 20);
  Snap s;
  int x = follow ? (int)a[2 + da] : 0, y = follow ? (int)a[3 + da] : 0;
  if (follow) {
    int sx = SCALE_X, sy = SCALE_Y, den = SCALE_DEN ? SCALE_DEN : 1;
    follow = snap_begin(&s, dst, x, y, I32(sprite + 0x30) * fabs((double)sx / den), I32(sprite + 0x34) * fabs((double)sy / den));
    if (follow) g_stat.back[slot]++;
  }
  uint32_t r = g_orig[slot](a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
  if (follow) {
    uint32_t pal_over = 0, lut = 0, mask = 0;
    const uint8_t *remap = NULL;
    switch (slot) {
      case 17: case 23: pal_over = a[4]; break;
      case 16: remap = (const uint8_t *)(uintptr_t)a[4]; pal_over = a[5]; break;
      case 25: remap = (const uint8_t *)(uintptr_t)a[4]; lut = a[5]; pal_over = a[6]; break;
      case 27: case 30: lut = a[4]; pal_over = a[5]; break;
      case 21: mask = a[1]; pal_over = a[5]; break;
      case 20: mask = a[1]; pal_over = a[6]; break;
    }
    if (remap && !mem_ok((uint32_t)(uintptr_t)remap, 256)) remap = NULL;
    record_sprite(slot, sprite, mask, dst, x, y, pal_over, remap, lut, &s);
    free(s.before);
  }
  return r;
}

// Whole-surface draws (graphsy surface vtable 0x1011d0b0, `this` = source):
// a PCX page or picture golf loaded with FUN_00475840 drawn on the back buffer.
static int surf_file_of(uint32_t jsurf) {
  for (int i = g_nsurf - 1; i >= 0; i--)
    if (g_surf[i].file >= 0 && mem_ok(g_surf[i].surf, 8) && U32(g_surf[i].surf + 4) == jsurf) return g_surf[i].file;
  return -1;
}

static uint32_t shooked(int slot, uint32_t *a) {
  uint32_t src = a[0], dst = a[1];
  int file = -1, follow = 0, sx = 0, sy = 0, w = 0, h = 0, x = 0, y = 0;
  double fx = 1, fy = 1;
  if (g_on && g_any && (g_sflags[slot] & 1) && src && dst && mem_ok(src, 0x4d0)) {
    file = surf_file_of(src);
    if (file >= 0 && look_of(file) != L_ORIGINAL && g_files[file].kind == K_STILL) {
      g_stat.blits++;
      if (slot == 15) {   // BlitTo(dst, srcX, srcY, dstX, dstY, w, h)
        sx = (int)a[2], sy = (int)a[3], x = (int)a[4], y = (int)a[5], w = (int)a[6], h = (int)a[7];
      } else {            // DrawTransparentTo / DrawShadowedTo (dst, x, y[, lut]): the whole surface, scaled
        x = (int)a[2], y = (int)a[3], w = I32(src + 0x38), h = I32(src + 0x3c);
        int den = SCALE_DEN ? SCALE_DEN : 1;
        fx = fabs((double)SCALE_X / den), fy = fabs((double)SCALE_Y / den);
      }
      if (sx < 0) x -= sx, w += sx, sx = 0;
      if (sy < 0) y -= sy, h += sy, sy = 0;
      follow = w > 0 && h > 0;
    }
  }
  Snap s;
  if (follow) follow = snap_begin(&s, dst, x, y, w * fx, h * fy);
  uint32_t r = g_sorig[slot](a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
  if (follow) {
    Entry e = {0};
    e.file = (int16_t)file;
    e.view = -1;
    e.shadow_file = -1;
    e.variant = V_RGBA;
    e.sx = (int16_t)sx, e.sy = (int16_t)sy, e.sw = (int16_t)w, e.sh = (int16_t)h;
    e.x = (float)(s.ox + x);
    e.y = (float)(s.oy + y);
    e.w = (float)(w * fx);
    e.h = (float)(h * fy);
    int before = g_rec.n;
    snap_end(&s, &e, NULL, NULL);
    g_stat.blits_rec += g_rec.n > before;
    free(s.before);
  }
  return r;
}

#define HOOK(n) static uint32_t hk_##n(ICALL_PARAMS) { \
    uint32_t a[16] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15}; return hooked(n, a); }
#define SHOOK(n) static uint32_t shk_##n(ICALL_PARAMS) { \
    uint32_t a[16] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15}; return shooked(n, a); }
HOOK(15) HOOK(16) HOOK(17) HOOK(18) HOOK(19) HOOK(20) HOOK(21) HOOK(22) HOOK(23) HOOK(25) HOOK(27) HOOK(29)
HOOK(30) HOOK(31) HOOK(33) HOOK(35) HOOK(36) HOOK(37) HOOK(38) HOOK(39) HOOK(40)
SHOOK(15) SHOOK(33) SHOOK(35)
static const struct { int slot; IcallFn fn; } k_hooks[] = {
  {15, hk_15}, {16, hk_16}, {17, hk_17}, {18, hk_18}, {19, hk_19}, {20, hk_20}, {21, hk_21}, {22, hk_22},
  {23, hk_23}, {25, hk_25}, {27, hk_27}, {29, hk_29}, {30, hk_30}, {31, hk_31}, {33, hk_33}, {35, hk_35},
  {36, hk_36}, {37, hk_37}, {38, hk_38}, {39, hk_39}, {40, hk_40}};
static const struct { int slot; IcallFn fn; } k_shooks[] = {{15, shk_15}, {33, shk_33}, {35, shk_35}};

// Wraps jgld's draws once jgld is there (its vtables are empty until golf
// has loaded it; the first prerender or present calls this).
static int g_hooks_done;
static void ensure_hooks(void) {
  if (g_hooks_done || !mem_ok(SPRITE_VTBL, 41 * 4) || !U32(SPRITE_VTBL + 17 * 4) || !mem_ok(SURFACE_VTBL, 60 * 4) ||
      !U32(SURFACE_VTBL + 15 * 4))
    return;
  g_hooks_done = 1;
  int n = 0, m = 0;
  for (size_t i = 0; i < sizeof k_hooks / sizeof k_hooks[0]; i++) {
    int s = k_hooks[i].slot;
    uint32_t fn = U32(SPRITE_VTBL + (uint32_t)s * 4);
    g_orig[s] = icall_replace(fn, k_hooks[i].fn, &g_flags[s]);
    if (g_orig[s]) n++;
    else fprintf(stderr, "art: sprite slot %d (0x%08x) not hookable\n", s, fn);
  }
  for (size_t i = 0; i < sizeof k_shooks / sizeof k_shooks[0]; i++) {
    int s = k_shooks[i].slot;
    uint32_t fn = U32(SURFACE_VTBL + (uint32_t)s * 4);
    g_sorig[s] = icall_replace(fn, k_shooks[i].fn, &g_sflags[s]);
    if (g_sorig[s]) m++;
    else fprintf(stderr, "art: surface slot %d (0x%08x) not hookable\n", s, fn);
  }
  fprintf(stderr, "art: %d sprite and %d surface draw slots wrapped\n", n, m);
}

// ------------------------------------------------------------------ settings while running

static unsigned g_seen_serial;

static void update_any(void) {
  g_any = 0;
  for (int i = 0; i < NCAT; i++) g_any |= g_look[i] != L_ORIGINAL;
}

// The mods store changed (Options screen, the bridge's mods_set): looks
// follow it live, golf.ini under it and SIMGOLF_DISPLAY over it.
static void live_settings(void) {
  if (mods_serial() == g_seen_serial) return;
  g_seen_serial = mods_serial();
  int before[NCAT];
  memcpy(before, g_look, sizeof before);
  memcpy(g_look, g_look_base, sizeof g_look);
  g_pixel = g_pixel_base;
  hd_art_read_store();
  for (int i = 0; i < NCAT; i++)
    if (g_look_env[i] >= 0) g_look[i] = g_look_env[i];
  if (g_pixel_env >= 0) g_pixel = g_pixel_env;
  update_any();
  if (memcmp(before, g_look, sizeof before)) {
    preview_look_changed();
    fprintf(stderr, "art: looks now");
    for (int i = 0; i < NCAT; i++) fprintf(stderr, " %s=%s", k_cat[i], k_look[g_look[i]]);
    fprintf(stderr, " pixel=%d textures=%s\n", g_pixel, hd_art_textures_name());
  }
  if ((before[C_TERRAIN] == L_ORIGINAL) != (g_look[C_TERRAIN] == L_ORIGINAL)) hdt_resend();
}

// ------------------------------------------------------------------ terrain textures (game thread)

static struct { int n; double ms; } g_terr;

uint32_t *hd_art_load_png(const char *path, int *w, int *h) {
  if (!path || !*path) return NULL;
  if (!g_wic) {   // this thread's own imaging factory
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&g_wic);
    if (FAILED(hr)) {
      fprintf(stderr, "hd_art: no imaging factory (0x%08lx) for %s\n", (unsigned long)hr, path);
      return NULL;
    }
  }
  uint8_t *px = decode(path, 0, w, h, NULL);   // RGBA bytes
  if (!px) fprintf(stderr, "hd_art: cannot read %s\n", path);
  if (!px) return NULL;
  uint32_t *o = (uint32_t *)px;
  for (size_t i = 0, n = (size_t)*w * *h; i < n; i++) {
    const uint8_t *p = px + i * 4;
    o[i] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
  }
  return o;
}

uint8_t *hd_art_terrain(const char *src, const uint8_t *rgba, int w, int h, int *ow, int *oh) {
  if (!g_on || !src || !*src || g_look[C_TERRAIN] == L_ORIGINAL) return NULL;
  int f = find_file(src, NULL);
  if (f < 0 || g_files[f].kind != K_TEXTURE) return NULL;
  if (!g_wic) {   // this thread's own imaging factory
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&g_wic)))
      return NULL;
  }
  double t0 = host_now_ms();
  char path[MAX_PATH * 2];
  snprintf(path, sizeof path, "%s/%s.png", g_dir, g_files[f].out);
  int W = 0, H = 0;
  uint8_t *up = decode(path, 0, &W, &H, NULL);
  if (!up || W < w || H < h || W % w || H % h) {
    free(up);
    return NULL;
  }
  // The terrain keeps BMP and TGA rows as stored (bottom-up as a rule), the
  // PNG is top-down: take the orientation that matches the original better.
  int kx = W / w, ky = H / h;
  double same = 0, flip = 0;
  for (int y = 0; y < h; y += 3)
    for (int x = 0; x < w; x += 3) {
      const uint8_t *o = rgba + ((size_t)y * w + x) * 4;
      const uint8_t *a = up + ((size_t)(y * ky + ky / 2) * W + x * kx + kx / 2) * 4;
      const uint8_t *b = up + ((size_t)((h - 1 - y) * ky + ky / 2) * W + x * kx + kx / 2) * 4;
      for (int c = 0; c < 3; c++) {
        same += abs(o[c] - a[c]);
        flip += abs(o[c] - b[c]);
      }
    }
  if (flip < same)
    for (int y = 0; y < H / 2; y++) {
      uint8_t *a = up + (size_t)y * W * 4, *b = up + (size_t)(H - 1 - y) * W * 4;
      for (int i = 0; i < W * 4; i++) {
        uint8_t t = a[i];
        a[i] = b[i];
        b[i] = t;
      }
    }
  int alpha = 0, up_alpha = 0;   // paths are TGAs with alpha: the upscale's own, else the original's in blocks
  for (size_t i = 0; i < (size_t)w * h && !alpha; i++) alpha = rgba[i * 4 + 3] != 255;
  for (size_t i = 0; i < (size_t)W * H && !up_alpha; i++) up_alpha = up[i * 4 + 3] != 255;
  if (alpha && !up_alpha)
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) up[((size_t)y * W + x) * 4 + 3] = rgba[((size_t)(y / ky) * w + x / kx) * 4 + 3];
  EnterCriticalSection(&g_lock);
  int count = ++g_terr.n;
  g_terr.ms += host_now_ms() - t0;
  double average = g_terr.ms / count;
  LeaveCriticalSection(&g_lock);
  if (g_log >= 2 || count % 50 == 1)
    fprintf(stderr, "art: terrain %d textures upscaled, %.1f ms each (%s %dx%d%s)\n", count, average, g_files[f].src, W, H,
            flip < same ? ", flipped" : "");
  *ow = W;
  *oh = H;
  return up;
}

static void terrain_job(void *user, int i) {
  HdTerrainJob *j = (HdTerrainJob *)user + i;
  j->up = hd_art_terrain(j->src, j->rgba, j->w, j->h, &j->uw, &j->uh);
}
static void terrain_job_finish(void) {
  if (g_wic) { IWICImagingFactory_Release(g_wic); g_wic = NULL; CoUninitialize(); }
}
void hd_art_terrain_jobs(HdTerrainJob *jobs, int count) {
  double start = host_now_ms();
  int workers = sg_run_jobs(count, terrain_job, terrain_job_finish, jobs);
  if (getenv("SIMGOLF_TERRAIN_VERIFY")) {
    for (int i = 0; i < count; i++) {
      HdTerrainJob *j = jobs + i;
      int w = 0, h = 0;
      uint8_t *reference = hd_art_terrain(j->src, j->rgba, j->w, j->h, &w, &h);
      if ((reference != NULL) != (j->up != NULL) ||
          (reference && (w != j->uw || h != j->uh || memcmp(reference, j->up, (size_t)w * h * 4)))) {
        fprintf(stderr, "terrain-loading: pixel comparison FAILED for %s\n", j->src);
        free(reference);
        port_abort("parallel terrain texture differs from serial decode");
      }
      free(reference);
    }
    fprintf(stderr, "terrain-loading: %d serial/parallel pixel comparisons PASS\n", count);
  }
  if (getenv("SIMGOLF_COURSE_TRACE"))
    fprintf(stderr, "terrain-loading: %d textures, %d worker threads, %.3f ms\n", count, workers, host_now_ms() - start);
}

// ------------------------------------------------------------------ hand-over

static CRITICAL_SECTION g_pub_lock;
static List g_pub[2];       // published (under g_pub_lock)
static int g_pub_ok[2];
static volatile LONG g_pub_serial;

void hd_art_course_done(const uint16_t *bits, int pitch) {
  if (!g_on) return;
  issue44_cutoff_trace("course_done_enter");
  list_close(LC, bits, pitch);
  g_rec_which = LT;
  issue44_cutoff_trace("course_done_exit");
}

// task-81: the Frosted Mint HUD hides golf's old HUD by putting back the
// pixels under its rectangles (hud_game.c). golf's pixels go, but the
// hi-res copies of the sprites it drew there would still be drawn under
// golf's UI layer: hd_art_hide_rects marks every entry recorded since
// hd_art_hide_begin that touches one of the rectangles (x, y, w, h each)
// as hidden.
static int g_hide_from = -1, g_hide_which;

void hd_art_hide_begin(void) {
  g_hide_from = g_on ? g_rec.n : -1;
  g_hide_which = g_rec_which;
}

void hd_art_hide_rects(const int *xywh, int nrects) {
  if (!g_on || g_hide_from < 0) return;
  int from = g_hide_from;
  if (g_hide_which != g_rec_which || from > g_rec.n) {   // a present came between (a modal dialog's loop)
    if (g_rec_which != LT) return;                       // course draws are the course's
    from = 0;
  }
  g_hide_from = g_rec.n;
  g_hide_which = g_rec_which;
  for (int i = from; i < g_rec.n; i++) {
    Entry *e = &g_rec.e[i];
    for (int r = 0; r < nrects; r++) {
      const int *q = xywh + r * 4;
      if (e->x < q[0] + q[2] && e->x + e->w > q[0] && e->y < q[1] + q[3] && e->y + e->h > q[1]) {
        e->hidden = 1;
        break;
      }
    }
  }
}

// task-88: a native dialog replaces golf's own popup (frame, rows) that the original draws each frame of
// its loop: every entry recorded since the dialog opened (hd_art_dlg_begin) is hidden, or the art pack's
// hi-res copy of the old panel frame shows beside the native dialog. After a present (the loop's own)
// the list starts empty again, so everything in it is the dialog's.
static int g_dlg_n = -1, g_dlg_which;
static HdArtDialogPreCutoff g_dlg_pre;

void hd_art_dlg_pre(void) {
  hd_art_dialog_capture_pre(&g_dlg_pre, g_on, g_rec.n, g_rec_which, g_rec_generation);
  g_issue44_trace_enabled = getenv("SIMGOLF_SAVE_CUTOFF_TRACE") != NULL;
  g_issue44_trace_pending = g_issue44_trace_enabled;
  g_issue44_trace_seq = 0;
  if (g_issue44_trace_pending) {
    fprintf(stderr, "issue44-cutoff #0 dlg_pre on=%d any=%d pack=%d index_ready=%d indexed=%d hash=%d dir=%s looks=",
            g_on, g_any, g_pack, g_files && g_fhash && g_nfiles > 0, g_nfiles, g_fhash_n, g_dir);
    for (int i = 0; i < NCAT; i++) fprintf(stderr, "%s%s=%s", i ? " " : "", k_cat[i], k_look[g_look[i]]);
    fprintf(stderr, " generation=%u which=%d count=%d\n", g_rec_generation, g_rec_which, g_rec.n);
  }
}

static void hd_art_dlg_begin_at(int use_pre) {
  HdArtDialogCutoff current = {g_on ? g_rec.n : -1, g_rec_which};
  if (g_issue44_trace_pending)
    fprintf(stderr, "issue44-cutoff #%u begin_before use_pre=%d pending=%d pre_generation=%u pre_which=%d pre_count=%d current_generation=%u current_which=%d current_count=%d\n",
            ++g_issue44_trace_seq, use_pre, g_dlg_pre.pending, g_dlg_pre.generation,
            g_dlg_pre.mark.which, g_dlg_pre.mark.count, g_rec_generation,
            current.which, current.count);
  HdArtDialogCutoff selected = hd_art_dialog_begin_cutoff(&g_dlg_pre, use_pre, current, g_rec_generation);
  if (g_issue44_trace_pending)
    fprintf(stderr, "issue44-cutoff #%u begin_after selected_which=%d selected_count=%d\n",
            ++g_issue44_trace_seq, selected.which, selected.count);
  g_issue44_trace_pending = 0;
  g_dlg_n = selected.count;
  g_dlg_which = selected.which;
}

void hd_art_dlg_begin(void) {
  hd_art_dlg_begin_at(0);
}

void hd_art_dlg_begin_pre(void) {
  hd_art_dlg_begin_at(1);
}

void hd_art_hide_dialog(void) {
  if (!g_on) return;
  int from = (g_dlg_n >= 0 && g_dlg_which == g_rec_which && g_dlg_n <= g_rec.n) ? g_dlg_n : 0;
  for (int i = from; i < g_rec.n; i++) g_rec.e[i].hidden = 1;
}

// task-89: the art pack's hi-res copy of golf's ball and shadow sprites (a dark pixelated blob on the ground)
// must not show under the round HD ball, which has its own soft shadow. hdball_item takes a mark before
// golf's sprite draw and hides every entry recorded since.
int hd_art_mark(void) { return g_on ? g_rec.n : -1; }

void hd_art_hide_from(int mark) {
  if (!g_on || mark < 0 || mark > g_rec.n) return;
  for (int i = mark; i < g_rec.n; i++) g_rec.e[i].hidden = 1;
  static int logged;
  if (g_rec.n > mark && !logged++) fprintf(stderr, "art: golf's ball and shadow sprite copies hidden under the round ball (task-89)\n");
}

void hd_art_discard_pending(void) {
  if (!g_on) return;
  issue44_cutoff_trace("discard_enter");
  list_reset(&g_rec, HD_FRAME_W, HD_FRAME_H);
  g_closed_ok[LC] = g_closed_ok[LT] = 0;
  g_rec_which = LC;
  g_hide_from = -1;
  issue44_cutoff_trace("discard_exit");
}

static void publish_one(int which, int ok) {
  g_pub_ok[which] = ok && g_closed_ok[which] && g_closed[which].n;
  if (!g_pub_ok[which]) return;
  List *c = &g_closed[which], *p = &g_pub[which];
  if (p->w != c->w || p->h != c->h || !p->owner) {
    free(p->owner);
    p->owner = malloc((size_t)c->w * c->h * 2);
    p->w = c->w;
    p->h = c->h;
  }
  memcpy(p->owner, c->owner, (size_t)c->w * c->h * 2);
  if (p->cap < c->n) p->e = realloc(p->e, (p->cap = c->n + 256) * sizeof(Entry));
  memcpy(p->e, c->e, (size_t)c->n * sizeof(Entry));
  p->n = c->n;
}

int hd_art_pick(int x, int y, HdArtPick *o) {
  memset(o, 0, sizeof *o);
  o->entry = -1;
  o->category = o->look = "";
  if (!g_on) return -1;
  int found = 0;
  EnterCriticalSection(&g_pub_lock);
  for (int w = LT; w >= LC; w--) {   // golf's UI is over the course
    List *p = &g_pub[w];
    if (!g_pub_ok[w] || !p->owner || x < 0 || y < 0 || x >= p->w || y >= p->h) continue;
    for (int i = 0; i < p->n; i++) {
      const Entry *e = &p->e[i];
      if (!e->hidden && x >= e->x && x < e->x + e->w && y >= e->y && y < e->y + e->h) o->ncover++;
    }
    int idx = p->owner[(size_t)y * p->w + x];
    if (!found && idx > 0 && idx <= p->n) {
      const Entry *e = &p->e[idx - 1];
      const ArtFile *a = &g_files[e->file];
      found = 1;
      o->layer = w;
      o->entry = idx - 1;
      o->entries = p->n;
      snprintf(o->file, sizeof o->file, "%s", a->src);
      if (e->shadow_file >= 0) snprintf(o->shadow, sizeof o->shadow, "%s", g_files[e->shadow_file].src);
      o->category = a->cat >= 0 ? k_cat[a->cat] : "";
      o->look = k_look[look_of(e->file)];
      o->view = e->view, o->frame = e->frame, o->variant = e->variant, o->flip = e->flip, o->hidden = e->hidden;
      o->x = e->x, o->y = e->y, o->w = e->w, o->h = e->h;
    }
  }
  LeaveCriticalSection(&g_pub_lock);
  return found;
}

void hd_art_publish(int layered, const uint16_t *screen) {
  live_settings();   // also with the pack off: Pixel HD is a grid over whatever the course shows
  if (!g_on) return;
  issue44_cutoff_trace("publish_enter");
  ensure_hooks();
  if (!g_val) g_val = calloc((size_t)HD_FRAME_W * HD_FRAME_H, 2);
  // what golf drew since the course (or, on a page, since the last present)
  list_close(LT, screen, HD_FRAME_W);
  EnterCriticalSection(&g_pub_lock);
  publish_one(LC, layered && g_rec_which == LT);
  publish_one(LT, 1);
  InterlockedIncrement(&g_pub_serial);
  LeaveCriticalSection(&g_pub_lock);
  g_closed_ok[LC] = 0;
  g_rec_which = LC;
  issue44_cutoff_trace("publish_exit");
  // periodic report
  static double last;
  double now = host_now_ms();
  if (g_log && now - last > 5000) {
    if (last > 0 && g_any) {
      fprintf(stderr, "art: %d tags (%d cuts, %d untagged); last lists %d course + %d on top; %d draws recorded (%d page blits), %d px owned in 5 s; loads %ld ok %ld failed, %.2f ms avg\n",
              g_ntags, g_tagstat.cuts, g_tagstat.untagged, g_pub[LC].n, g_pub[LT].n, g_stat.rec, g_stat.blits_rec, g_stat.owned,
              (long)g_loads, (long)g_fails, g_loads ? g_load_ms / g_loads : 0.0);
      if (g_vstat.n)
        fprintf(stderr, "art: selftest %ld frames checked, mean match %.4f, best f-1/f/f+1 %ld/%ld/%ld, %ld under 0.9, %ld without an original\n",
                (long)g_vstat.n, g_vstat.n > g_vstat.missing ? g_vstat.score / (g_vstat.n - g_vstat.missing) : 0.0,
                (long)g_vstat.best[0], (long)g_vstat.best[1], (long)g_vstat.best[2], (long)g_vstat.low, (long)g_vstat.missing);
      if (g_log >= 2) {
        fprintf(stderr, "art: slot calls (back buffer, untagged):");
        for (int i = 15; i <= 40; i++)
          if (g_stat.calls[i]) fprintf(stderr, " %d:%d(%d,%d)", i, g_stat.calls[i], g_stat.back[i], g_stat.untagged[i]);
        fprintf(stderr, "; recorded by category:");
        for (int i = 0; i < NCAT; i++) fprintf(stderr, " %s %d", k_cat[i], g_stat.bycat[i]);
        fprintf(stderr, "\n");
      }
    }
    memset(&g_stat, 0, sizeof g_stat);
    last = now;
  }
  // art_log=4: a few lists in the log; SIMGOLF_ART_LIST=<file>: the latest
  // lists, rewritten every frame (evidence: where things are in a frozen frame)
  static int dumps;
  static const char *list_file;
  if (!list_file) list_file = getenv("SIMGOLF_ART_LIST") ? getenv("SIMGOLF_ART_LIST") : "";
  int to_log = g_log >= 4 && dumps < 3 && g_pub[LC].n + g_pub[LT].n > 20 && (now - last) > 2000;
  FILE *lf = *list_file ? fopen(list_file, "w") : NULL;
  if (to_log) dumps++;
  if (to_log || lf)
    for (int w = 0; w < 2; w++)
      for (int i = 0; i < g_pub[w].n; i++) {
        const Entry *e = &g_pub[w].e[i];
        char line[MAX_PATH + 160];
        snprintf(line, sizeof line, "art: %s entry %d %s v%d f%d src %d,%d %dx%d at %.0f,%.0f %.0fx%.0f variant %d flip %d shadow %d\n",
                 w ? "top" : "course", i, g_files[e->file].src, e->view, e->frame, e->sx, e->sy, e->sw, e->sh, e->x, e->y, e->w,
                 e->h, e->variant, e->flip, e->shadow_file >= 0);
        if (to_log) fputs(line, stderr);
        if (lf) fputs(line, lf);
      }
  if (lf) fclose(lf);
}

// ------------------------------------------------------------------ render thread

typedef struct {
  Key key;
  SDL_Texture *tex;
  int w, h, used_slot;
  uint32_t used;
} Tex;

#define TEX_N (1 << 16)
static Tex *g_tex;   // open addressing, render thread; slots stay used after eviction (tombstones)
static int g_ntex, g_tex_slots;
static size_t g_tex_bytes, g_tex_peak;
static uint32_t g_tick;
static List g_cur[2];   // the lists being shown
static int g_cur_ok[2];
static uint8_t *g_cut[2];
static uint8_t *g_drawable[2];
static LONG g_cur_serial = -1;
static SDL_BlendMode g_premul;
static struct { int frames, entries, drawn; double ms; } g_rstat;

static Tex *tex_find(Key k, int make) {
  uint32_t h = key_hash(k) & (TEX_N - 1);
  for (int n = 0; n < TEX_N; n++, h = (h + 1) & (TEX_N - 1)) {
    Tex *t = &g_tex[h];
    if (!t->used_slot) {
      if (!make) return NULL;
      t->used_slot = 1;
      t->key = k;
      g_tex_slots++;
      return t;
    }
    if (key_eq(t->key, k)) return t;
  }
  return NULL;
}

// Drops the tombstones when the table gets full.
static void tex_rehash(void) {
  if (g_tex_slots < TEX_N * 3 / 4) return;
  Tex *old = g_tex;
  g_tex = calloc(TEX_N, sizeof(Tex));
  g_tex_slots = 0;
  for (int i = 0; i < TEX_N; i++)
    if (old[i].tex) *tex_find(old[i].key, 1) = old[i];
  free(old);
}

static void tex_evict(void) {
  size_t budget = (size_t)g_cache_mb << 20;
  while (g_tex_bytes > budget && g_ntex > 0) {
    Tex *old = NULL;   // the least recently used
    for (int i = 0; i < TEX_N; i++)
      if (g_tex[i].tex && (!old || g_tex[i].used < old->used)) old = &g_tex[i];
    if (!old || old->used + 2 >= g_tick) break;   // all in use right now
    SDL_DestroyTexture(old->tex);
    g_tex_bytes -= (size_t)old->w * old->h * 4;
    g_ntex--;
    EnterCriticalSection(&g_lock);
    KeyState *s = ks_find(old->key, 0);
    if (s) s->state = S_NONE;
    LeaveCriticalSection(&g_lock);
    old->tex = NULL;   // the key stays as a tombstone so probing goes on past it
  }
}

static int upload_done(SDL_Renderer *r) {
  int up = 0;
  for (int n = 0; n < 64; n++) {
    Done d;
    EnterCriticalSection(&g_lock);
    int have = g_dh != g_dt;
    if (have) d = g_done[g_dh++ % QN];
    LeaveCriticalSection(&g_lock);
    if (!have) break;
    SDL_Texture *t = d.rgba ? SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, d.w, d.h) : NULL;
    if (t) {
      SDL_UpdateTexture(t, NULL, d.rgba, d.w * 4);
      SDL_SetTextureBlendMode(t, (d.key.b >> 28) == V_MASKED ? g_premul : SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
      Tex *x = tex_find(d.key, 1);
      if (x) {
        if (x->tex) { SDL_DestroyTexture(x->tex); g_tex_bytes -= (size_t)x->w * x->h * 4; g_ntex--; }
        x->key = d.key;
        x->used_slot = 1;
        x->tex = t;
        x->w = d.w;
        x->h = d.h;
        x->used = g_tick;
        g_ntex++;
        g_tex_bytes += (size_t)d.w * d.h * 4;
        if (g_tex_bytes > g_tex_peak) g_tex_peak = g_tex_bytes;
      } else {
        SDL_DestroyTexture(t);
        t = NULL;
      }
    }
    EnterCriticalSection(&g_lock);
    KeyState *s = ks_find(d.key, 1);
    if (s) s->state = t ? S_READY : S_FAILED;
    LeaveCriticalSection(&g_lock);
    free(d.rgba);
    up += t != NULL;
  }
  tex_evict();
  tex_rehash();
  return up;
}

static SDL_Texture *tex_of(const Entry *e, int shadow) {
  Tex *t = tex_find(entry_key(e, shadow), 0);
  if (!t || !t->tex) return NULL;
  t->used = g_tick;
  return t->tex;
}

void hd_art_take(void) {
  if (!g_on || g_pub_serial == g_cur_serial) return;
  EnterCriticalSection(&g_pub_lock);
  g_cur_serial = g_pub_serial;
  for (int w = 0; w < 2; w++) {
    g_cur_ok[w] = g_pub_ok[w];
    if (!g_cur_ok[w]) continue;
    List *c = &g_cur[w], *p = &g_pub[w];
    if (c->w != p->w || c->h != p->h || !c->owner) {
      free(c->owner);
      free(g_cut[w]);
      c->owner = malloc((size_t)p->w * p->h * 2);
      g_cut[w] = malloc((size_t)p->w * p->h);
      c->w = p->w;
      c->h = p->h;
    }
    memcpy(c->owner, p->owner, (size_t)p->w * p->h * 2);
    if (c->cap < p->n) c->e = realloc(c->e, (c->cap = p->n + 256) * sizeof(Entry));
    memcpy(c->e, p->e, (size_t)p->n * sizeof(Entry));
    c->n = p->n;
  }
  LeaveCriticalSection(&g_pub_lock);
}

int hd_art_prepare(SDL_Renderer *r, int new_frame) {
  if (!g_on) return 0;
  if (!g_premul)
    g_premul = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                          SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
  g_tick++;
  int up = upload_done(r);
  if (!new_frame && !up) return 0;
  int changed = 0;
  for (int w = 0; w < 2; w++) {
    if (!g_cur_ok[w]) continue;
    List *c = &g_cur[w];
    // an entry is drawn when all its textures are here; only those own pixels
    if (new_frame) {
      g_drawable[w] = realloc(g_drawable[w], (size_t)c->n + 1);
      memset(g_drawable[w], 0, (size_t)c->n + 1);
    }
    int diff = new_frame;
    for (int i = 0; i < c->n; i++) {
      const Entry *e = &c->e[i];
      uint8_t ok = !e->hidden && tex_of(e, 0) && (e->shadow_file < 0 || tex_of(e, 1));
      diff |= ok != g_drawable[w][i + 1];
      g_drawable[w][i + 1] = ok;
    }
    if (!diff) continue;
    changed = 1;
    size_t n = (size_t)c->w * c->h;
    for (size_t i = 0; i < n; i++) g_cut[w][i] = g_drawable[w][c->owner[i]];
  }
  return changed;
}

const uint8_t *hd_art_cut_mask(int which) { return g_on && g_cur_ok[which] && g_cur[which].n ? g_cut[which] : NULL; }

// The attached shadow's place (task-112): see hd_art_geom.h.
static void shadow_rect(const Entry *e, const SDL_FRect *d, SDL_FRect *out) {
  const ArtFile *a = &g_files[e->file], *b = &g_files[e->shadow_file];
  float dv[4] = {d->x, d->y, d->w, d->h}, o[4];
  hd_art_shadow_rect(a->crop_x, a->crop_y, a->w4 / 4, a->h4 / 4, b->crop_x, b->crop_y, b->w4 / 4, b->h4 / 4, e->flip, dv, o);
  *out = (SDL_FRect){o[0], o[1], o[2], o[3]};
}

#include "port/hd_art_preview.inc"

void hd_art_draw(int which, SDL_Renderer *r, double ox, double oy, double k) {
  if (!g_on || !g_cur_ok[which]) return;
  double t0 = host_now_ms();
  int drawn = 0;
  List *c = &g_cur[which];
  for (int i = 0; i < c->n; i++) {
    if (!g_drawable[which][i + 1]) continue;
    const Entry *e = &c->e[i];
    SDL_FRect d = {(float)(ox + e->x * k), (float)(oy + e->y * k), (float)(e->w * k), (float)(e->h * k)};
    SDL_RendererFlip f = (e->flip & 1 ? SDL_FLIP_HORIZONTAL : 0) | (e->flip & 2 ? SDL_FLIP_VERTICAL : 0);
    if (e->shadow_file >= 0) {
      SDL_Texture *s = tex_of(e, 1);
      if (s) {
        SDL_FRect sd;
        shadow_rect(e, &d, &sd);
        SDL_RenderCopyExF(r, s, NULL, &sd, 0, NULL, f);
      }
    }
    SDL_Texture *tx = tex_of(e, 0);
    float uw[3], uwa;   // shoals, rocks and coral (task-78): seen through the realistic water, not painted on it
    const char *nm = g_files[e->file].norm;
    int under = 0;
    if (nm && !strncmp(nm, "flics/water/", 12) && water_underwater_tint(uw, &uwa)) {
      int reef = !strncmp(nm + 12, "uw", 2) && strstr(nm, "coral");   // coral lies on the bed: the full effect
      int bar = (!strncmp(nm + 12, "uw", 2) || (!strncmp(nm + 12, "as", 2) && strstr(nm, "sand")));   // rocks and sand bars stand up: half, enough to cool the shallow ring painted around them
      if (reef || bar) {
        under = 1;
        if (!reef) {
          for (int c = 0; c < 3; c++) uw[c] = 1 - (1 - uw[c]) * 0.5f;
          uwa = 1 - (1 - uwa) * 0.5f;
        }
      }
    }
    if (under) {
      SDL_SetTextureColorMod(tx, (Uint8)(uw[0] * 255 + 0.5f), (Uint8)(uw[1] * 255 + 0.5f), (Uint8)(uw[2] * 255 + 0.5f));
      SDL_SetTextureAlphaMod(tx, (Uint8)(uwa * 255 + 0.5f));
    }
    SDL_RenderCopyExF(r, tx, NULL, &d, 0, NULL, f);
    if (under) {
      SDL_SetTextureColorMod(tx, 255, 255, 255);
      SDL_SetTextureAlphaMod(tx, 255);
    }
    if (g_debug) {   // art_debug=1: a box per hi-res sprite, coloured by category
      static const Uint8 col[NCAT][3] = {{255, 0, 0}, {0, 0, 255}, {0, 255, 0}, {255, 255, 0}, {255, 0, 255}, {0, 255, 255}, {255, 128, 0}};
      int cc = g_files[e->file].cat < 0 ? 0 : g_files[e->file].cat;
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      SDL_SetRenderDrawColor(r, col[cc][0], col[cc][1], col[cc][2], 255);
      SDL_RenderDrawRectF(r, &d);
    }
    drawn++;
  }
  g_rstat.frames += which == LC;
  int shown = 0;   // task-81: entries under the HUD hide do not count (the log's entries/frame is what can be drawn)
  for (int i = 0; i < c->n; i++) shown += !c->e[i].hidden;
  g_rstat.entries += shown;
  g_rstat.drawn += drawn;
  g_rstat.ms += host_now_ms() - t0;
  static double last;
  double now = host_now_ms();
  if (g_log && now - last > 5000) {
    if (last > 0 && g_rstat.frames)
      fprintf(stderr, "art: render %d frames, %.1f entries/frame, %.1f drawn/frame, %.3f ms/frame drawing; %d textures %.1f MB (peak %.1f MB)\n",
              g_rstat.frames, (double)g_rstat.entries / g_rstat.frames, (double)g_rstat.drawn / g_rstat.frames,
              g_rstat.ms / g_rstat.frames, g_ntex, g_tex_bytes / 1048576.0, g_tex_peak / 1048576.0);
    memset(&g_rstat, 0, sizeof g_rstat);
    last = now;
  }
}

// ------------------------------------------------------------------ start

void hd_art_init(int presenter_on) {
  g_on = 0;
  update_any();
  if (!g_pack) {
    fprintf(stderr, "art: pack off (art_pack=0)\n");
    return;
  }
  if (!presenter_on) {
    if (g_any) fprintf(stderr, "art: needs the hd presentation with layers; original art\n");
    return;
  }
  if (!g_dir[0]) {
    const char *env = getenv("SIMGOLF_ART_DIR");
#ifdef SIMGOLF_DEFAULT_ART_DIR
    snprintf(g_dir, sizeof g_dir, "%s", env && *env ? env : SIMGOLF_DEFAULT_ART_DIR);
#else
    snprintf(g_dir, sizeof g_dir, "%s", env && *env ? env : "art/upscaled");
#endif
  }
  if (g_dir[0] != '/' && g_dir[0] != '\\' && g_dir[1] != ':') {   // relative: next to golf.exe, not the game folder
    char full[MAX_PATH];
    snprintf(full, sizeof full, "%s/%s", host_exe_dir(), g_dir);
    snprintf(g_dir, sizeof g_dir, "%s", full);
  }
  double t0 = host_now_ms();
  int n = load_index();
  if (!n) {
    fprintf(stderr, "art: no index.tsv in %s (tools/upscale/make_index.py); original art\n", g_dir);
    return;
  }
  // Loader threads use this source tree for alpha masks and palette-indexed
  // still effects, so finish the path before publishing the workers.
  snprintf(g_xdir, sizeof g_xdir, "%s", g_dir);
  char *up = strstr(g_xdir, "upscaled");
  if (up) memcpy(up, "extracted", 9), up[9] = 0;
  g_tags = calloc(TAG_N, sizeof(Tag));
  g_ks = calloc(KS_N, sizeof(KeyState));
  g_tex = calloc(TEX_N, sizeof(Tex));
  g_q = calloc(QN, sizeof(Req));
  g_done = calloc(QN, sizeof(Done));
  InitializeCriticalSection(&g_lock);
  InitializeCriticalSection(&g_pub_lock);
  InitializeCriticalSection(&g_sheet_lock);
  preview_init();
  g_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
  art_hooks.flic_open = on_flic_open;
  art_hooks.cut = on_cut;
  art_hooks.pcx_load = on_pcx_load;
  g_seen_serial = mods_serial();
  g_on = 1;
  for (int i = 0; i < g_loaders; i++) {
    HANDLE th = CreateThread(NULL, 1 << 20, loader_main, NULL, 0, NULL);
    if (th) CloseHandle(th);
  }
  fprintf(stderr, "art: %d files indexed from %s in %.1f ms; looks:", n, g_dir, host_now_ms() - t0);
  for (int i = 0; i < NCAT; i++) fprintf(stderr, " %s=%s", k_cat[i], k_look[g_look[i]]);
  fprintf(stderr, "; texture budget %d MB, %d loaders\n", g_cache_mb, g_loaders);
}
