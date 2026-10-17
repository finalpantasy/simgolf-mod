// Mod platform core (simgolf mod, task-11): the parts shared by the game, the
// native helper behind tools/modctl (modctl_core.exe) and the unit tests.
// Plain C, no game code and no Windows calls except in mods_runtime.c, so the
// CLI and the game resolve the same files to the same order by construction.
//
// docs/modding/CORE.md describes the files (mod.toml, load_order.toml, the
// save record) and the rules (resolver, conflicts, merge operations).

#ifndef SIMGOLF_MODS_CORE_H
#define SIMGOLF_MODS_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The mod platform's own version: manifests state the range they work with
// ("platform = \"^1.0\""). Bump the minor for additions, the major for breaks.
#define MODS_PLATFORM_VERSION "1.0.0"
// The game the platform runs: golf.exe v1.02 ("game = \">=1.2.0\"").
#define MODS_GAME_VERSION "1.2.0"
// The save record format (sidecar next to each save), see saverec.c.
#define MODS_SAVEREC_VERSION 1

// ------------------------------------------------------------------ versions

typedef struct {
  int major, minor, patch;
  char pre[32];   // prerelease tag ("beta.1"), empty for a release
} SemVer;

// "1", "1.2", "1.2.3", "1.2.3-beta.1" (missing parts are 0). 1 = ok.
int semver_parse(const char *s, SemVer *v);
int semver_cmp(const SemVer *a, const SemVer *b);
// Ranges: "*", "1.2.3" or "=1.2.3" (exact), ">=1.2", ">1", "<2", "<=2.1",
// "^1.2" (same major, or same minor below 1.0), "~1.2" (same minor),
// "1.x" / "1.2.x" (wildcards); terms joined by spaces or commas all apply,
// alternatives are joined by "||". Returns 1 match, 0 no match, -1 bad range.
int semver_satisfies(const char *version, const char *range);
int semver_range_valid(const char *range);

// ------------------------------------------------------------------ diagnostics

enum { MD_NOTE = 0, MD_WARN = 1, MD_ERROR = 2 };

typedef struct {
  int level;
  char code[24];   // stable machine-readable code, e.g. "missing_dependency"
  char *mod;       // mod id the message is about (may be NULL)
  char *msg;       // one plain sentence
} ModDiag;

typedef struct {
  ModDiag *d;
  int n, cap;
} ModDiags;

void mdiag_add(ModDiags *ds, int level, const char *code, const char *mod, const char *fmt, ...);
void mdiags_free(ModDiags *ds);
int mdiags_count(const ModDiags *ds, int level);
const char *mdiag_level_name(int level);

// ------------------------------------------------------------------ manifests

// What a mod touches: game settings (the key/value store), data table rows
// (task-19), hooked functions (task-20) and asset paths (task-21).
enum { MT_SETTING, MT_DATA, MT_HOOK, MT_ASSET, MT__COUNT };
// How it touches them. Exclusive operations (set, replace, remove) take the
// key over: two of them on one key are a conflict, the later mod in load
// order wins. Composing operations (add, mul, min, max, append, merge,
// before, after) combine with every other mod's, so mods coexist.
enum { MO_SET, MO_REPLACE, MO_REMOVE, MO_ADD, MO_MUL, MO_MIN, MO_MAX, MO_APPEND, MO_MERGE, MO_BEFORE,
       MO_AFTER, MO__COUNT };

const char *mods_kind_name(int kind);    // "setting", "data", "hook", "asset"
int mods_kind_parse(const char *s);      // -1 if unknown
const char *mods_op_name(int op);        // "set", "mul", ...
int mods_op_parse(const char *s);        // -1 if unknown
int mods_op_exclusive(int op);

typedef struct {
  char *id;
  char *range;   // version range, "*" when not given
} ModDep;

typedef struct {
  int kind, op;
  char *key;
  char *value;   // settings only: the value as text ("1.5", "1", "Ann,Bob")
} ModTouch;

typedef struct {
  char *id, *name, *version, *description;
  char *platform;   // platform version range ("^1.0"), "*" when not given
  char *game;       // game version range, "*" when not given
  char *dir;        // folder holding mod.toml ('/' separated)
  char **authors; int nauthors;
  int builtin;      // found under mods/builtin
  ModDep *deps; int ndeps;
  ModDep *opt; int nopt;
  ModDep *incompat; int nincompat;
  char **after; int nafter;     // load_after: these load before this mod
  char **before; int nbefore;   // load_before: these load after this mod
  char **provides; int nprovides;
  ModTouch *touches; int ntouches;
  // shown by the in-game Mods screen (task-22); all optional
  char *icon;       // picture inside the folder (default: preview.png when present)
  char *homepage;
  char **tags; int ntags;       // categories ("economy", "visual"), lowercase
  char **link_names; char **link_urls; int nlinks;   // [links] name = "https://..."
  int changes_saves;            // changes_saves = true: saves made with it need it to load right
  char *error;      // set when the manifest is unusable (the mod cannot load)
  ModDiags warns;   // problems that do not stop it (unknown keys, odd values)
} ModManifest;

typedef struct {
  ModManifest *m;
  int n, cap;
  ModDiags diags;   // scan problems (bad manifests, duplicate ids)
} ModSet;

// Parses one mod.toml. On failure m->error says why (id is still set when it
// could be read). Returns 1 when the manifest is usable.
int manifest_parse_text(ModManifest *m, char *text, const char *dir, int builtin);
int manifest_load(ModManifest *m, const char *dir, int builtin);
void manifest_free(ModManifest *m);

// Scans "<mods_dir>/builtin/*/mod.toml" then "<mods_dir>/*/mod.toml", each in
// name order, then missing built-ins from ../ui/builtin-mods (legacy updater).
// Existing IDs always win. Discovery never writes files or enables a mod.
// A folder without mod.toml is not a mod (builtin, examples).
void modset_scan(ModSet *s, const char *mods_dir);
int modset_add(ModSet *s, ModManifest *m);   // takes ownership; 0 on duplicate id
int modset_find(const ModSet *s, const char *id);
void modset_free(ModSet *s);

// ------------------------------------------------------------------ load order and profiles

typedef struct {
  char *name;
  char **mods; int nmods;   // enabled mods, in the player's preferred order
} ModProfile;

typedef struct {
  char *active;             // active profile name
  ModProfile *p; int n;
  char *error;              // parse error (the file is then ignored)
  int exists;
} ModLoadOrder;

// mods/load_order.toml: active = "name", [profiles.<name>] mods = [...].
// A missing file is valid: no profiles, active "vanilla" (zero mods). The
// profile "vanilla" always exists and is always empty.
int loadorder_read(ModLoadOrder *lo, const char *path);
int loadorder_parse_text(ModLoadOrder *lo, char *text);
int loadorder_write(const ModLoadOrder *lo, const char *path);
ModProfile *loadorder_profile(ModLoadOrder *lo, const char *name, int create);
void loadorder_free(ModLoadOrder *lo);

// ------------------------------------------------------------------ resolver

typedef struct {
  int kind;
  char *key;
  int n;            // touches, in load order
  int *mod;         // set index of each touch
  int *op;
  char **value;
  int conflict;     // 1: an exclusive touch overrode another mod's touch
  int winner;       // set index of the mod whose touch decides (-1: merged, all apply)
  char *result;     // settings: the value after folding every touch
} ModKeyUse;

typedef struct {
  int *order; int norder;   // set indexes, in load order
  ModDiags diags;
  ModKeyUse *keys; int nkeys;   // every key touched by 2+ mods, plus every setting
} ModResolution;

// Overrides from mods.ini "[mods] <id> = 0|1", applied on top of the profile.
typedef struct {
  const char *id;
  int on;
} ModSwitch;

// Resolves the requested mods (profile order) against the installed set:
// unknown ids, manifest errors, platform/game ranges, dependencies (enabled
// automatically when installed), versions, incompatibilities, then a stable
// topological order (dependencies and load_after/load_before; ties keep
// built-ins first, then the profile order, then id). Cycles are reported and
// broken deterministically. Then conflicts and merged values.
void mods_resolve(const ModSet *s, const char *const *requested, int nreq, const ModSwitch *sw, int nsw,
                  ModResolution *r);
void resolution_free(ModResolution *r);
int resolution_has(const ModResolution *r, int set_index);
// Conflicts and merges only (order given): used by mods_resolve and by the
// runtime registry when later cards register touches while the game runs.
void mods_compute_keys(const ModSet *s, const int *order, int norder, ModResolution *r);

// Folds a setting value: cur (NULL = unset) op val -> out. Numbers fold as
// numbers, append joins with commas. Returns out.
char *mods_fold(const char *cur, int op, const char *val, char *out, size_t outlen);

// Text report (the game log and `modctl check` print the same lines).
typedef void (*mods_print_fn)(void *ctx, const char *line);
void mods_report(const ModSet *s, const ModResolution *r, const char *profile, mods_print_fn fn, void *ctx);
// The same as JSON (modctl_core --json), one object.
char *mods_report_json(const ModSet *s, const ModResolution *r, const char *profile);

// ------------------------------------------------------------------ session

// Everything the game decides at startup, without the platform bits, so the
// game (mods_init) and tools/modctl (modctl_core) share one code path:
// load_order.toml in mods_dir, the profile (given, else the file's active
// one; no_mods: "vanilla"), the mods.ini switches ("mods.<id>" keys of the
// store's MS_INI layer), the scan and the resolution.
typedef struct {
  ModSet set;
  ModResolution res;
  ModLoadOrder lo;
  char profile[64];
  int missing_profile;   // the profile is not in load_order.toml
  int nswitches;
} ModSession;

void mods_session_open(ModSession *ss, const char *mods_dir, const char *profile, int no_mods);
// The startup report: folder, load order file, profile problems, then mods_report.
void mods_session_report(const ModSession *ss, const char *mods_dir, mods_print_fn fn, void *ctx);
void mods_session_close(ModSession *ss);

// ------------------------------------------------------------------ save record

// A sidecar next to each save ("<name>.sve.mods.toml") records the platform
// version, the profile and every active mod with its version, plus the size
// and CRC-32 of the save it belongs to (a stale sidecar is ignored). The .sve
// itself is never changed, so the original game and older builds load it.
typedef struct {
  int format;         // MODS_SAVEREC_VERSION when written by us
  char platform[32];
  char profile[64];
  uint32_t save_size, save_crc;
  int n;
  char id[64][64];
  char version[64][32];
} ModSaveRec;

uint32_t mods_crc32(uint32_t crc, const void *data, size_t len);
int mods_file_crc(const char *path, uint32_t *size, uint32_t *crc);
// Builds the record text for the given save bytes' size/crc.
int saverec_format(const ModSaveRec *rec, char *out, size_t outlen);
int saverec_parse(ModSaveRec *rec, char *text);   // 1 ok
// Compares a record with the running mods: missing, other version, extra.
// Adds one diagnostic per difference; returns the number of warnings.
int saverec_check(const ModSaveRec *rec, const ModSet *s, const ModResolution *r, ModDiags *out);

// ------------------------------------------------------------------ relocation

// Moving a fixed game array to a bigger block (bigger maps, more golfers):
// copies `old_n` elements into a new zeroed block of `new_n` and returns it
// (malloc; NULL on failure).
void *mods_grow_array(const void *old, size_t elem, size_t old_n, size_t new_n);
// Rebases every pointer that points into [old_base, old_base + old_size) to the
// same offset from new_base. `slots` are the addresses of 32-bit pointer
// variables (globals that refer into the array); returns how many changed.
int mods_relocate_slots(uint32_t *const *slots, int nslots, uint32_t old_base, uint32_t old_size,
                        uint32_t new_base);
// The same for pointer fields inside an array of structs (each element has
// pointers at the given byte offsets), e.g. "next" links between golfers.
int mods_relocate_fields(void *arr, size_t elem, size_t n, const size_t *offsets, int noff, uint32_t old_base,
                         uint32_t old_size, uint32_t new_base);

// ------------------------------------------------------------------ settings store (store.c)

// Layers, lowest first; mods_get returns the highest that has the key.
enum { MS_GOLFINI, MS_MODS, MS_INI, MS_LIVE, MS__COUNT };

void mods_store_clear(void);
// origin: the mod id for MS_MODS (shown by mods_origin), else NULL.
int mods_store_put(int layer, const char *key, const char *value, const char *origin);
// "[section]" + "key=value" lines into a layer as "section.key"; ';' and '#'
// start comments. Returns the number of values.
int mods_store_parse_ini(int layer, char *text);
int mods_store_load_ini(int layer, const char *path);   // -1 if the file is missing
// Writes the mods.ini and live layers (live wins) grouped by section.
int mods_store_save_ini(const char *path);
// Keys of one layer that start with `prefix` (case-insensitive), in insertion
// order: fills keys/vals (pointers into the store) up to max, returns the count.
int mods_store_layer_keys(int layer, const char *prefix, const char **keys, const char **vals, int max);

// ------------------------------------------------------------------ small helpers

char *mods_strdup(const char *s);
char *mods_read_file(const char *path, size_t *len);   // malloc, NUL-terminated
int mods_valid_id(const char *id);

#ifdef __cplusplus
}
#endif
#endif  // SIMGOLF_MODS_CORE_H
