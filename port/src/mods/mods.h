// Mod platform, the game side (simgolf mod, task-11). What game code, the
// bridge and the later mod cards (data tables, hooks, assets, the in-game
// manager) call. docs/modding/CORE.md has the full picture.
//
//   if (mods_enabled("camera_controls")) ...          is a mod loaded?
//   int holes = mods_get_int("course.max_holes", 18);   a setting (any layer)
//   mods_touch("my_mod", "asset", "Interface/x.pcx", "replace");   registry
//
// Settings come from four layers, the highest set one wins:
//   live      mods_set() (the bridge's mods_set, tools)
//   mods.ini  --mods FILE / SIMGOLF_MODS / mods.ini next to the exe / data/mods.ini
//   mods      [settings] and [patch] of the loaded mods, folded in load order
//   golf.ini  golf.ini next to the exe, as "golf.<key>"

#ifndef SIMGOLF_MODS_H
#define SIMGOLF_MODS_H

#ifdef __cplusplus
extern "C" {
#endif

// Startup (host_win32.c, once, before the game thread): reads the options,
// golf.ini and mods.ini, scans mods/, resolves the active profile and logs
// the load order, conflicts and problems to golf.log.
//   --mods FILE        mods.ini (also SIMGOLF_MODS)
//   --mods-dir DIR     the mods folder (also SIMGOLF_MODS_DIR)
//   --profile NAME     profile from mods/load_order.toml (also SIMGOLF_PROFILE)
//   --no-mods          the original game: profile "vanilla", no mods.ini switches
void mods_init(int argc, char **argv);

// Loaded mods.
int mods_enabled(const char *id);          // loaded (by id, or by something it provides)
int mods_count(void);                      // loaded mods
const char *mods_id_at(int i);             // i-th in load order
const char *mods_version(const char *id);  // NULL if not loaded
const char *mods_dir(const char *id);      // its folder ('/' separated), NULL if not loaded
const char *mods_root(void);               // the mods folder
const char *mods_profile(void);            // active profile name

// Settings ("section.key").
const char *mods_get(const char *key);     // NULL when no layer sets it
int mods_get_int(const char *key, int def);
double mods_get_num(const char *key, double def);
const char *mods_origin(const char *key);  // "live", "mods.ini", "mod <id>", "golf.ini"
int mods_set(const char *key, const char *value);   // live layer; 0 if the store is full
unsigned mods_serial(void);                // changes whenever any value does
int mods_value_count(void);                // keys with a value, for listing
const char *mods_value_key(int i);
const char *mods_ini_path(void);
int mods_ini_save(void);
// The Options page (card task-24): sets a value now and keeps it in options.ini (next to golf.exe, or
// SIMGOLF_OPTIONS_FILE), which is read over mods.ini at the next start. Returns 0 if validation, table
// capacity, or persistence fails. A persistence failure leaves the new value active for this run.
int mods_option_set(const char *key, const char *value);
// Mod-scoped choices load only while their owning mod is loaded. They share
// the regular Options controls, with the latest player change taking priority.
int mods_config_set(const char *mod, const char *key, const char *value);
// The same without writing the file (a slider being dragged); mods_option_flush writes it.
int mods_option_put(const char *key, const char *value);
// Returns 0 when settings were not persisted, including when --no-mods suppresses the write.
int mods_option_flush(void);
const char *mods_option_path(void);                   // writes the mods.ini and live layers back

// Registry of what mods touch at run time (data rows, hooks, assets): the
// later cards register here so conflicts are reported in one place.
// kind: "setting", "data", "hook", "asset"; op: "set", "replace", "remove",
// "add", "mul", "min", "max", "append", "merge", "before", "after".
// Returns 1 when this mod's touch takes effect (it wins or merges), 0 when a
// mod later in load order overrides it, -1 on a bad argument.
int mods_touch(const char *mod_id, const char *kind, const char *key, const char *op);
// The mod whose exclusive touch decides a key (NULL: none, or all merge).
const char *mods_winner(const char *kind, const char *key);

// Saves (fs_native.c): every .sve the game writes gets a record of the
// loaded mods next to it; every .sve it opens for reading is checked
// against the running mods (a clear warning in golf.log when they differ).
void mods_save_written(const char *path);
void mods_save_opened(const char *path);
// The result of the last save check, one line ("" before any), for tools.
const char *mods_last_save_check(void);

// The report lines from startup (and later conflicts), for the bridge.
int mods_report_count(void);
const char *mods_report_line(int i);
void mods_report_add(const char *line);   // a line from a later layer (data tables)

#ifdef __cplusplus
}
#endif
#endif  // SIMGOLF_MODS_H
