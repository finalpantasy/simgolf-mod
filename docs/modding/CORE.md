# Mod platform core

The foundation every mod and every later feature plugs into (card task-11).
A mod is a folder with a `mod.toml`. A profile in `mods/load_order.toml` says
which mods load; the resolver puts them in a stable order, checks versions,
dependencies and incompatibilities, and reports every place two mods touch
the same thing, with the winner or the merge. Saves remember the mods they
were made with. With zero mods the game is the original.

Code: `port/src/mods/` (C, shared by the game and the tools),
`tools/modctl/` (CLI), tests in `tests/mods/`.

## Quick start

```
bash tools/build_native.sh                 # builds golf.exe, modctl_core.exe, mods_tests.exe
python tools/modctl/modctl.py list         # installed mods, which ones load
python tools/modctl/modctl.py new my_mod   # mods/my_mod/mod.toml from a commented template
python tools/modctl/modctl.py enable my_mod
python tools/modctl/modctl.py check        # the same report the game writes to golf.log
python tools/modctl/modctl.py conflicts
```

`tools/modctl/modctl.sh` is the same from Git Bash.

## Folders

```
mods/
  load_order.toml          profiles: which mods load
  builtin/<id>/mod.toml    our own features as mods (hd_presentation, camera_controls, bridge)
  <id>/mod.toml            any other mod
data/mods.ini              your own switches and values (all commented out by default)
```

A folder without `mod.toml` is not a mod (so `mods/examples/` can hold
anything). Folders are scanned in name order, built-ins first. The folder name
should equal the id (a warning otherwise; the id counts).

Where the game looks (first that applies):

| What | Command line | Environment | golf.ini | Default |
|---|---|---|---|---|
| mods folder | `--mods-dir DIR` | `SIMGOLF_MODS_DIR` | `mods_dir=` | `mods/` next to golf.exe, else the repo's `mods/` |
| mods.ini | `--mods FILE` | `SIMGOLF_MODS` | `mods_ini=` | `mods.ini` next to golf.exe, else the repo's `data/mods.ini` |
| profile | `--profile NAME` | `SIMGOLF_PROFILE` | `profile=` | `active` in load_order.toml |
| original game | `--no-mods` | `SIMGOLF_NO_MODS=1` | | |

`--no-mods` means profile `vanilla` and mods.ini ignored. Relative paths are
relative to the folder the game was started from.

## mod.toml

```toml
id = "pricier_greens"                 # required: lowercase letters, digits, _ - .
name = "Pricier greens"
version = "1.0.0"                     # required: semver
authors = ["Ann"]
description = "One sentence."
platform = "^1.0"                     # mod platform versions it works with (this is 1.0.0)
game = ">=1.2.0"                      # game versions (golf.exe v1.02 is 1.2.0)
load_after = ["other"]                # order hints, ignored when the other is not loaded
load_before = ["other"]
provides = ["renderer"]               # virtual ids that satisfy other mods' dependencies

[dependencies]                        # must be loaded (enabled automatically when installed)
core_lib = "^1.1"
[optional_dependencies]               # loaded first when present
fancy_ui = "*"
[incompatibilities]                   # never together
tiny_maps = "*"

[settings]                            # values this mod sets (exclusive)
"economy.green_fee_scale" = 1.5
[settings.course]                     # or one level of sections
max_holes = 36

[patch]                               # values combined with other mods' (no conflict)
"economy.income_scale" = { op = "mul", value = 1.2 }
"golfers.names" = { op = "append", value = ["Ann", "Bob"] }

[touches.data]                        # declared up front so conflicts show before anything runs
"buildings.clubhouse.cost" = "set"
[touches.hooks]
FUN_00412340 = "after"
[touches.assets]
"Interface/TitleBASE.pcx" = "replace"
```

Unknown top-level keys are warnings (a newer mod on an older platform still
loads if its `platform` range allows). Anything else wrong makes the mod
unusable, with the reason: `modctl validate DIR`.

Version ranges: `*`, `1.2.3` or `=1.2.3`, `>=1.2`, `>1`, `<2`, `<=2.1`,
`^1.2` (same major; same minor below 1.0), `~1.2` (same minor), `1.x`,
`1.2.x`; terms separated by spaces or commas all apply; `||` separates
alternatives. Prereleases (`1.0.0-beta.1`) match only ranges that name a
prerelease of the same version.

## Profiles and switches

```toml
# mods/load_order.toml
active = "default"

[profiles.default]
mods = ["hd_presentation", "camera_controls", "bridge"]

[profiles.big]
mods = ["hd_presentation", "bigger_maps"]
```

The profile `vanilla` always exists and is always empty: the original game.
No `load_order.toml` at all also means zero mods. The list order is the
player's preference; dependencies and `load_after`/`load_before` still apply.

mods.ini can switch single mods on top of the profile, per copy of the game:

```ini
[mods]
camera_controls=0      ; off even though the profile has it
bigger_maps=1          ; on even though the profile does not
```

`tests/mods/all_off.ini` switches every built-in off.

## How the resolver decides

1. The profile's mods in order, minus mods.ini switches set to 0, plus those set to 1.
   Unknown ids, broken manifests and `platform`/`game` ranges that do not match: error, not loaded.
2. Required dependencies that are installed but not enabled are enabled (a note says why),
   unless mods.ini switched them off.
3. Until nothing changes: a mod whose dependency is missing, not loaded or the wrong
   version is not loaded (and so are mods that need it); of two incompatible mods the
   one later in the profile is not loaded.
4. Order: a stable topological sort. Edges: dependency before dependent (optional ones
   too when present and in range), `load_after`, `load_before`. Among mods that are
   free to go next: built-ins first, then the profile order, then the id.
   A cycle is reported with its path (`a -> b -> a`) and broken by loading the
   first of its mods by that same rule, so the result never depends on luck.
5. Conflicts and merges (below).

Every message has a level (`error`, `warning`, `note`) and a code:
`manifest_error`, `duplicate_id`, `folder_name`, `unknown_key`, `missing_mod`,
`not_loaded`, `platform_version`, `game_version`, `auto_enabled`,
`switched_off`, `switched_on`, `missing_dependency`, `dependency_version`,
`incompatible`, `optional_missing`, `optional_version`, `cycle`, `conflict`,
`merged`, plus `missing_profile` and `load_order` from the session. The game
still starts with whatever resolved; `modctl check` exits 1 on any error.

## Conflicts and merge rules

Every touch has a kind (`setting`, `data`, `hook`, `asset`) and an operation.

| Operation | Kinds | Behaviour |
|---|---|---|
| `set` | setting, data | takes the key over (exclusive) |
| `replace` | hook, asset | takes it over (exclusive) |
| `remove` | setting, data, asset | removes it (exclusive) |
| `add`, `mul`, `min`, `max` | setting, data | combine numbers (unset starts at 0 for add, 1 for mul) |
| `append` | setting, data | joins lists (comma separated) |
| `merge` | data | combine records field by field (task-19 defines fields) |
| `before`, `after` | hook | chain around the function (task-20) |
| `add` | asset | a new file (unique path, no overlap) |

Folding runs in load order. An exclusive touch after another mod's touch on
the same key is a **conflict**: reported as a warning, the later mod wins.
Composing touches (or a set followed by composing ones) **merge**: all apply,
reported as a note. Keys compare without case.

The two sample mods in `tests/mods/sets/samples` show both:

```
order 1: pricier_greens 1.0.0
order 2: members_discount 1.0.0
warning conflict: conflict on setting economy.green_fee_scale: pricier_greens sets 1.5, members_discount sets 0.8; members_discount wins (it loads later), value 0.8
note merged: merged setting economy.income_scale: pricier_greens mul 1.2, members_discount mul 1.1; all apply, value 1.32
warning conflict: conflict on asset Interface/TitleBASE.pcx: pricier_greens replace, members_discount replace; members_discount wins (it loads later)
```

## Settings store (game code)

`port/src/mods/mods.h`. Four layers, the highest that has a key wins:

| Layer | Source |
|---|---|
| live | `mods_set()`, the bridge's `mods_set` |
| mods.ini | the player's file (see Folders) |
| mods | `[settings]`/`[patch]` of the loaded mods, folded in load order |
| golf.ini | golf.ini next to golf.exe, as `golf.<key>` |

```c
#include "mods/mods.h"
if (mods_enabled("camera_controls")) { ... }          // by id or by something it provides
int holes = mods_get_int("course.max_holes", 18);      // the default is the original game's
double fee = mods_get_num("economy.green_fee_scale", 1.0);
if (mods_serial() != seen) { ... }                      // a value changed (live tools)
```

`bridge_mod()`, `bridge_mod_int()` and `bridge_mods_serial()` (task-13) are
the same store. Changing which mods load needs a restart; values change live.

## For the next cards

- **Data tables (task-19, done: docs/modding/DATA.md)**: read base tables, then apply each loaded mod in
  `mods_id_at(0..mods_count()-1)` order from `mods_dir(id)`. Register every
  row or field a mod changes with
  `mods_touch(id, "data", "table.row.field", "set"|"add"|"merge"|...)`; the
  return value says whether that change takes effect (0: a later mod
  overrides it). Use `MO_*`/`mods_fold()` for the numeric operations so data
  and settings combine the same way. Declare known touches in
  `[touches.data]` so `modctl conflicts` sees them without running the game.
- **Hooks and scripts (task-20)**: register with
  `mods_touch(id, "hook", "FUN_xxxxxxxx", "before"|"after"|"replace")`;
  chain in load order; two `replace` hooks are a reported conflict with the
  later mod winning. Script errors become diagnostics with the mod's id.
- **Assets (task-21)**: resolve a game path by walking the loaded mods from
  last to first (`mods_dir(id)` + path), falling back to the game folder;
  register with `mods_touch(id, "asset", path, "replace"|"add")`;
  `mods_winner("asset", path)` names the mod whose file is used.
- **Mod manager (task-22, done: docs/modding/MANAGER.md)**: the Mods screen (start menu, Options page, Shift+F12) over
  `port/src/mods/modmgr.c`, which asks `mods_resolve` for every conflict and dependency and writes
  `load_order.toml` with `loadorder_write`, like modctl. New optional mod.toml keys it shows: `tags`, `icon`,
  `homepage`, `[links]`, `changes_saves`.
- **Modder kit (task-23)**: `modctl new`/`validate` are the start of
  validate/pack; the JSON from `modctl_core` is stable (keys only added).

## Saves

The `.sve` format is the original game's and stays byte for byte as the game
writes it: the original golf.exe and builds from before task-11 load our
saves, and we load theirs. The mods go into a record next to the save:

```toml
# Saved Games/Pines.sve.mods.toml
format = "simgolf-save-mods"
format_version = 1
platform = "1.0.0"
profile = "default"
save_size = 296973
save_crc32 = "8c0e7b1a"

[[mods]]
id = "hd_presentation"
version = "0.1.0"
```

Written whenever the game writes a `.sve` under "Saved Games" (high scores,
`top10.sve`, are not a save). Checked whenever the game opens one: the
record must match the save's size and CRC-32 (otherwise it belongs to an
older version of the file and is ignored). golf.log then says one of:

```
mods: save Pines.sve: made with 2 mods (profile "samples"): the same mods are loaded
mods: save Pines.sve: made with 2 mods (profile "samples"): WARNING, the mods differ
mods: WARNING save Pines.sve: this save was made with the mod pricier_greens 1.0.0, which is not loaded now; things it added may be missing or behave differently
mods: save Pines.sve: no mod record (made by the original game or before mods existed); it loads as it is
```

Newer record formats keep these fields; unknown fields are ignored.
`modctl save-info FILE.sve` shows the same check from the command line. The
MCP `load_game` copies a record along with a save it copies in.

A sidecar instead of a trailer inside the `.sve`: the original reader's
handling of extra bytes is unknown, and the original game must keep loading
our saves. Copying a save without its record only loses the warning.

## Relocation helper

For cards that raise fixed limits (bigger maps, more golfers): move a game
array to a bigger block and rebase the 32-bit pointers into it.

```c
Golfer *big = mods_grow_array(old, sizeof(Golfer), 64, 256);   // copy + zeroed rest
size_t links[] = {offsetof(Golfer, next), offsetof(Golfer, partner)};
mods_relocate_fields(big, sizeof(Golfer), 256, links, 2, (uint32_t)old, 64 * sizeof(Golfer), (uint32_t)big);
uint32_t *globals[] = {&g_first_golfer, &g_selected};
mods_relocate_slots(globals, 2, (uint32_t)old, 64 * sizeof(Golfer), (uint32_t)big);
```

## modctl and modctl_core

`tools/modctl/modctl.py` is the front end (arguments, profile edits,
scaffolding, readable output). Every decision is made by
`build/native/modctl_core.exe`, built from the same C files as the game, so
both always agree; `tests/mods/game_tests.sh` checks that the game's
golf.log report and `modctl check` are identical line for line.

```
modctl list | check | conflicts | order          (--profile P, --ini FILE, --no-mods)
modctl enable ID | disable ID                    (the active profile, or --profile P)
modctl order ID --before X | --after X | --first | --last
modctl profile [list | use P | create P [--from Q] | delete P]
modctl new ID [--name NAME] | validate DIR | save-info FILE.sve
--mods-dir DIR, --core EXE, --json (anywhere)
```

`modctl_core resolve|manifest|loadorder|set-profile|delete-profile|set-active|check-save|semver`
prints JSON (see the top of `port/src/mods/tool/modctl_core.c`).

## Tests

`tools/check.sh` runs:

- `mods_tests.exe` (`tests/mods/test_mods.c`): versions and ranges, manifest
  errors, ordering, cycles, missing dependencies, version ranges,
  incompatibilities, conflicts and merges, the store, save records,
  load_order.toml, relocation, the sample mods.
- `tests/mods/test_modctl.py`: the CLI on copies of `tests/mods/sets`.
- `tests/mods/game_tests.sh`: the live game on the hidden desktop, muted:
  identity with modctl on `tests/mods/sets/complex`, a save that records the
  sample mods, its warning when loaded without them, and a save without a
  record (the original format) loading.

`tests/mods/ab_original.sh OLD_GOLF_EXE` is the A/B against a build from
before the mod platform with every mod off (evidence/mods-ab-*.txt).

## Optional additions in 0.1.17

Player settings in `mods.ini` override mod defaults and survive updates.

| Addition | Independent switch | Default |
|---|---|---|
| Authored roster and portraits | `gameplay.new_roster` | 1, original progression |
| Authored ordinary visitor arrival skills | `gameplay.authored_visitor_skills` | 0 |
| Living Club dialogue, memories and stories | `mods.living_club` or `golfers.living` | 1 in the default profile |
| Living Club mood reactions | `golfers.reactions` | 0 |
| Lighting and weather presentation | `environment.enabled` | 0 |
| Weather effects on rolls and arrival mood | `environment.gameplay` | 0 |
| Approved Parkland clubhouse | `art.clubhouse` | 1; Original textures retain original art |
| Optional Terrain Mod | `art.terrain` | Active only when its separate pack is installed; 0 disables it |

Set an independent switch to 0 to retain its original behavior. The vanilla
profile remains empty. Saves retain the mod record and bound companions;
loading an original save remains supported. Manual par and drag-to-build are
unimplemented and have no effect on scoring or progression.

The existing size/CRC-bound `.sve.mods.toml` companion also records a versioned
`[rules]` table for authored visitor skills, Living Club reactions and weather
gameplay. These values document the rules at save time; loading does not
silently change the player's current settings. Older readers ignore the table.

For updates started by0.1.16, missing new optional data can load from
`data/tables/_release/`. Existing root data and explicit custom paths retain
priority. The table loader skips this compatibility folder.
