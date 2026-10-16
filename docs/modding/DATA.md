# Mod data layer

[Modding guide](README.md) / Data reference

Change supported game tables without writing code. A mod is a
folder with a `mod.toml`; its data changes live next to it in
`data/<table>.json`.

Code: `port/src/mods/json.c` (JSON with line numbers), `data.c` (tables,
schema checks, operations, shared by the game and the tools),
`data_runtime.c` (the game side), `tool/modctl_data.c` (`modctl_core data`).
Tables: `data/tables/*.json`. Tests: `tests/mods/test_data.c`,
`tests/mods/data/`, `tests/mods/data_game.sh`, `tests/mods/data_ab.mjs`.

## Quick start

For complete manifest, installation and verification steps, use
[Your first mod](FIRST_MOD.md). This reference assumes an installed helper;
see [Testing](TESTING.md) for how to build it and set its path.

A mod that makes the Snack Bar cheaper:

```
mods/cheap_snack_bar/mod.toml
mods/cheap_snack_bar/data/buildings.json
```

```json
{
  "set": {"snack_bar.cost": 50},
  "add": {"snack_bar.income": 1}
}
```

```
python tools/modctl/modctl.py enable cheap_snack_bar
build/modctl-only/modctl_core.exe data check --tables data/tables --image build/native/image/golf.bin --mods-dir mods --text
```

The check uses the same data implementation as the game and reports
validation problems and changed values. Match the image, tables, profile
and player switches to the test install. Example output:

```
data: 10 tables, 111 game rows from data/tables
data: mod cheap_snack_bar: 2 changes
data: buildings.snack_bar.cost = 50 (was 150; game table at 0x4c274e)
data: buildings.snack_bar.income = 6 (was 5)
data: 2 values changed by mods, 1 written to the game's tables, 0 problems
```

The game writes the same lines to golf.log at startup (`data: ...`), and the
bridge's `mods_status` shows them with the mod report.

## How it works

- **Tables** are defined in `data/tables/<table>.json`: a JSON Schema
  (draft 2020-12) for one row, plus `x-table` (row ids in the game's order,
  where the rows live) and, per field, where its value lives. These files are
  ours: they hold no game data except a few small numbers the game keeps in
  its code (prices per visit, wage periods), which are checked against the
  code at run time.
- **Base data comes from the game itself.** Fields marked `x-image` are read
  from golf.exe's image at its original address (the running game's memory,
  or `golf.bin` for the tools), so names, prices and wages are whatever the
  installed game has, and nothing of the game is stored in this repository.
  `modctl_core data dump` writes the base rows as JSON for reading
  (`build/data_base/`, never committed).
- **Mods change rows** in load order (`mods_id_at(0..n)`), each operation
  registered with the core's conflict registry (`mods_touch`, kind `data`).
- **Back into the game**: changed image fields are written into the game's
  own tables at startup, after the static initializers and before WinMain,
  so every place the game reads them sees the mod's value. Fields marked
  `x-code` are constants in the code: a translator patch wraps each one in
  `MDATA(table, row, field, literal)` (or `MDATA_AT` with a row index such as
  the difficulty), which returns the mod's value when a mod changed that
  field and the original literal otherwise.
- **Zero mods leaves this layer unchanged**: nothing is written, every `MDATA` is
  its literal (tests/mods/data_ab.mjs measures it, see Tests). The layer can
  be switched off completely with `SIMGOLF_DATA=0` or `--no-data`.

Where the game finds the tables (first that applies): `--data-dir DIR`,
`SIMGOLF_DATA_DIR`, `data_dir=` in golf.ini, `data/tables` next to golf.exe,
the repo's `data/tables`.

## Mod data files

`mods/<id>/data/<table>.json`, one per table, a JSON object of operations.
They run in this order whatever their order in the file: `remove`,
`replace`, `merge`, then `set`, `add`, `mul`, `min`, `max`, `append`.
`$schema`, `description`, `comment` and `title` are allowed and ignored.

| Operation | Takes | Does | Registered as |
|---|---|---|---|
| `set` | `{"row.field": value}` | sets the value | `set` on `table.row.field` (exclusive: two mods conflict, the later wins) |
| `add` | `{"row.field": number}` | adds | `add` (combines with other mods) |
| `mul` | `{"row.field": number}` | multiplies; integer fields round half away from zero | `mul` |
| `min`, `max` | `{"row.field": number}` | caps or floors | `min`, `max` |
| `append` | `{"row.field": [items]}` | adds items to a list field | `append` |
| `merge` | `{"row": {fields}}` | sets those fields; object fields merge key by key; an id that does not exist adds a new row (tables marked extensible) | `merge` on `table.row`, `set` per field (`merge` plus `set` per key for object fields); a new row is `add` on `table.row` |
| `replace` | `{"row": {fields}}` | the whole row; read-only fields must stay as they are | `set` on `table.row` |
| `remove` | `["row", ...]` | removes a row a mod added (rows of the game stay) | `remove` on `table.row` |

- `"*.field"` applies a field operation to every row that has the field
  (`"mul": {"*.cost": 0.5}` halves every price).
- In a table with one row (`economy`) the row may be left out:
  `"set": {"hole_upkeep": 2}`.
- Every value is checked against the field's schema before it is used. A
  bad change is skipped (the value stays) and reported with file and line:

```
data: error data_error (my_mod): mods/my_mod/data/buildings.json:3: buildings.snack_bar.cost: 70000 is above the maximum 32767
data: error data_error (my_mod): mods/my_mod/data/buildings.json:4: buildings.snack_bar.footprint cannot be changed (read only)
data: error data_error (my_mod): mods/my_mod/data/buildings.json:5: buildings has no row "snakbar"
data: error data_error (my_mod): mods/my_mod/data/personalities.json:3: personalities.loud: thoughts: "shouting" is not a row of thought_events
data: error data_error (my_mod): mods/my_mod/data/terrain.json:4: trailing comma before '}'
```

- Conflicts and merges show in the same report as every other kind of
  touch (CORE.md), for example two mods setting one price:

```
mods: warning conflict: conflict on data buildings.snack_bar.cost: cheap_snack_bar set, pricey set; pricey wins (it loads later)
```

- Declare what a mod changes in `mod.toml` so `modctl conflicts` sees it
  without running anything: `[touches.data] "buildings.snack_bar.cost" = "set"`.

## Tables

Money is in the game's unit, $100 (a cost of 150 is $15,000). "Image" fields
are read from and written to golf.exe's own table at the address shown;
"code" fields are constants in the function shown, read through `MDATA`.

### buildings (20 rows, image 0x4c26b0, 0x14 bytes a row)

Rows in the game's order (also the unlock order): `pathway`, `benches`,
`flower_bed`, `ball_washer`, `landmark`, `home_site`, `putting_green`,
`snack_bar`, `pro_shop`, `swim_club`, `driving_range`, `cart_garage`,
`marina`, `resort_hotel`, `airstrip`, `clubhouse`, `willow_tree`, `tv_tower`,
`tv_booth`, `scenic_bridge`.

| Field | Type | Where | Notes |
|---|---|---|---|
| `name` | string, 1 to 15 (Latin-1) | image +0x00 | the dock, golfer card, messages |
| `footprint` | integer, read only | image +0x10 | square edge in tiles; the art has this size |
| `cost` | integer 0 to 32767 | image +0x12 | placing pays cost x (level + 2) / 2 plus site work; refunds and lot values use it |
| `income` | integer 0 to 1000 | code, FUN_004289e0 | per visit: putting_green 4, snack_bar 5, pro_shop 6, driving_range 8 |
| `income_upgraded` | integer 0 to 1000 | code, FUN_004289e0 | per visit at level 2 or more: 8, 10, 12 (snack bar has one price) |

### terrain (23 rows, image 0x4c1a40, 0x30 bytes a row)

`tees`, `green`, `fairway`, `firm_fairway`, `rough`, `deep_rough`, `mound`,
`sand_trap`, `waste_bunker`, `pot_bunker`, `ravine`, `brush`, `rocks`,
`tree`, `pine_tree`, `palm_tree`, `elm_tree`, `water`, `wetlands`, `marsh`,
`out_of_bounds`, `building`, `building_2`. The game copies this table to
0x578350 when a course is set up, so changes apply to every course.

| Field | Type | Where | Notes |
|---|---|---|---|
| `name` | string, 1 to 31 | image +0x00 | build tools, thoughts ("in the sand trap") |
| `cost` | integer 0 to 127 | image +0x23 | price per tile painted |
| `hazard` | integer, read only | image +0x22 | lie difficulty used by shots (not yet tested with other values) |

### employees (4 rows, image 0x4c2e2c, 2 bytes a row)

`club_pro` (Celebrity when skilled), `ranger` (Marshall), `groundskeeper`
(Technician), `soda_vendor` (Refresher).

| Field | Type | Where | Notes |
|---|---|---|---|
| `wage` | integer 0 to 127 | image +0 | per wage period (difficulty.wage_period), when a roll against the course grade passes |
| `wage_skilled` | integer 0 to 127 | image +1 | the skilled form |

### economy (1 row `game`, code)

| Field | Base | Where | Notes |
|---|---|---|---|
| `sandbox_cash` | 10000 | FUN_0040f5c0 | cash of a new Sandbox game |
| `hole_upkeep` | 1 | FUN_0040f5c0 | per open hole at every wage period |
| `debt_interest_divisor` | 50 | FUN_0040f5c0 | monthly: negative cash grows by cash / this |
| `fire_fee` | 25 | FUN_00436c00 | paid when an employee is fired |

### difficulty (4 rows, code, row = the game's difficulty)

`easy`, `moderate`, `difficult`, `impossible` (the game's own names). The game computes these from the
difficulty; the rows hold the results, so a mod can reshape the curve.

| Field | Base (easy, moderate, difficult, impossible) | Where | Notes |
|---|---|---|---|
| `label` | Easy, Moderate, Difficult, Impossible | data, read only | the menu's names |
| `start_cash` | 1000 each | FUN_0040f5c0 | cash of a new standard game (land is bought from it) |
| `wage_period` | 512, 341, 256, 204 | FUN_0040f5c0 | ticks between wage and upkeep charges (1024 / (difficulty + 2)) |
| `start_mood` | 4, 3, 3, 3 | FUN_00421bc0 | lowest mood of an arriving golfer (-10 to 10) |
| `start_mood_random` | 0, 2, 2, 2 | FUN_00421bc0 | a random 0 to this is added |
| `hole_fatigue` | 1, 2, 3, 4 | FUN_00427380 | weight of the mood drop after each hole (difficulty + 1) |

### traits (5 rows, image 0x4c2864, pointers)

`neat`, `outgoing`, `active`, `playful`, `nice`: `label` (1 to 20), the
character screen's trait buttons.

### skills (11 rows, image 0x4c2c3c, pointers)

`power_hitter`, `long_driver`, `accurate_driver`, `accurate_irons`,
`accurate_putter`, `draw_shot`, `fade_shot`, `high_backspin`, `recovery`,
`luck`, `positive_attitude`: `label` (1 to 24), the golfer card and the
pro's skill screen.

### thought_events (42 rows, data, read only)

Our names for the game's mood events (the number the thought routine
FUN_00469b00 switches on), with the usual mood change: `holed_tough_putt`
(1), `missed_easy_putt` (2), `bad_shot` (3), `next_shot_worry` (4),
`use_the_slope` (6), `no_safe_target` (8), `nearly_hit` (9), `wants_a_path`
(10), `scenic_view` (11), `hit_a_tree` (12), `in_a_hazard` (13), `thirsty`
(14), `hungry` (15), `amazed_draw` (16), `amazed_fade` (17), `eating_snack`
(18), `eyesore` (20), `slow_play` (21), `celebrity_home` (22), `weeds` (24),
`soda` (25), `tired` (26), `resting` (27), `likes_feature` (28),
`likes_hole` (29), `dislikes_hole` (30), `strategic_choice` (32),
`strategic_choice_2` (33), `quits` (35), `angry_golfer` (36), `wildlife`
(39), `new_driving_range` (51), `new_pro_shop` (52), `new_putting_green`
(53), `new_club` (54), `sets_up_draw` (55), `sets_up_fade` (56),
`sets_up_high` (57), `sets_up_low` (58), `sets_up_shot_5` (59),
`sets_up_shot_6` (60), `lost_sight` (65).

### trays (1 row `amenities`, code)

How many designs the Amenities panel's strips list (the
built-in mod `cut_content` sets all three; see the built-in cut_content manifest).

| Field | Base | Where | Notes |
|---|---|---|---|
| `landmark_designs` | 14 | FUN_00434cf0 | 14 to 16; kinds 14 (oriental house) and 15 (dinosaur tarpit) have sprites, names and prices but no strip slot |
| `bench_designs` | 5 | FUN_00434cf0, FUN_0040f5c0 | 5 to 6; also the modulus the placed bench design is stored with, so the sixth (lovers' bench) survives |
| `sandbox_landmark_mask` | 16383 | FUN_0040f5c0 | bit per landmark kind a Sandbox game starts with |

### personalities (extensible, starts empty)

Personalities mods give golfers, each with thoughts of its own.

| Field | Type | Notes |
|---|---|---|
| `name` | string 1 to 40, required | display name |
| `description` | string | |
| `share` | integer 0 to 100, default 10 | percent of golfers with it |
| `chance` | integer 0 to 100, default 100 | percent of their thoughts (with lines here) that use these lines |
| `traits` | list of trait ids | describes it (tools, later cards) |
| `thoughts` | `{thought_event: [lines]}` | 1 to 20 lines of up to 120 characters each |

A golfer's personality comes from its profile id (fixed per golfer, kept in
saves): the personalities in load order share out 100 percent of golfers by
`share` (shares past 100 percent are cut). When such a golfer has a thought
for which its personality has lines, one of them replaces the original line,
the same one every time for the same moment (the thought bubble is redrawn
every frame). `MYNAME` and `PARTNER` become the names, as in the game's own
lines. Golfers without a personality, and moments without lines, keep the
original thoughts. The Player Comments report (F2) shows the original
wording (it has no golfer to ask).

## Sample mods

`mods/examples/` (use it as the mods folder: `--mods-dir mods/examples`,
profiles `samples`, `grumpy`, `cheap`, `hard`; or copy a folder into `mods/`):

- `grumpy_golfers`: a new personality, the Grump, with 22 kinds of thoughts
  (share 100 so every golfer is one, for testing).
- `cheap_snack_bar`: Snack Bar $5,000 instead of $15,000, $600 a visit
  instead of $500.
- `hard_times`: a steeper difficulty curve (start cash 900 / 800 / 650 / 520,
  wage period 512 / 300 / 200 / 128 ticks, start mood 4 / 2 / 1 / 0, hole
  fatigue up to 7) and 4 percent debt interest.

## Tools

```
modctl_core data dump --tables data/tables --image build/native/image/golf.bin --out build/data_base
modctl_core data coverage --tables data/tables
modctl_core data check --tables data/tables --image build/native/image/golf.bin --mods-dir mods [--profile P] [--ini F] [--no-mods] [--text]
modctl_core data show buildings --tables data/tables --image build/native/image/golf.bin --mods-dir mods
```

`check` exits 1 when a mod has a problem. The bridge's MCP `launch` takes
`env` (for example `{"SIMGOLF_MODS_DIR": "mods/examples", "SIMGOLF_PROFILE": "cheap"}`)
and `exe` (another build, for A/B runs).

## Internal tests

These tests and evidence paths refer to the internal development tree. The
public source snapshot does not export the complete suite. See
[Testing](TESTING.md) for checks available to mod authors.

- `tools/check.sh` runs `data_tests.exe tests/mods/data` (JSON, schema,
  every operation, conflicts, errors with file:line, writing back, zero mods
  leave the image byte for byte) and `tests/mods/data_game.sh` (the bindings
  read the game's known names, prices and wages from golf.bin; zero mods
  change nothing in the live game; with the sample mods the game logs the
  same data lines as `modctl_core data check`).
- `node tests/mods/data_ab.mjs BASE_GOLF_EXE`: the zero-mods A/B. Every run
  pokes the same random seed at the title, loads the same copy of a mid-year
  save (default "Ocean's Edge 12 June 2001"), freezes at the same frame (`pause_at`, 80 ticks after the load), runs 2400 frames in turbo and
  compares money, golfers, fun, skill, stars and a checksum of all 152
  golfer records: the build before the data layer twice, this build with the
  layer on and off, and a mod that must change the result (hole upkeep 50).
  Evidence: `evidence/task-19-ab-<rev>.txt`.
- `node tests/mods/data_samples.mjs`: the three sample mods next to the
  original game, screenshots in `evidence/task-19-*-<rev>.png`.

## Adding a table

1. Find the table or constant in the decompile (build/gen, data/symbols.json,
   your own verified export for meanings).
2. A table in golf.exe's data: write `data/tables/<name>.json` with
   `x-table.image` (`base`, `stride`) and `x-image` per field (`offset`,
   `type` u8 i8 u16 i16 u32 i32 str strptr, `len` for str). Check it with
   `modctl_core data dump` (the rows must read as the game's values).
   The game must read the table where it is (not from a copy made before
   WinMain): look for copies like the terrain one.
3. A constant in code: add the field with `"x-code": true`, its base value in
   `x-table.rows`, and a patch in `port/tools/translate/golf.data.json` that
   wraps the literal in `MDATA(...)` / `MDATA_AT(...)`; re-translate
   (`tools/translate_gen.sh`). golf.log reports `WARNING drift` if the base
   value and the literal ever disagree.
4. Run `bash tools/check.sh` and the A/B (`node tests/mods/data_ab.mjs`).

Supported JSON Schema keywords: `type` (also a list), `enum`, `const`,
`minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `minLength`,
`maxLength` (characters), `items`, `minItems`, `maxItems`, `properties`,
`required`, `additionalProperties` (false or a schema), `propertyNames`,
`readOnly`, `default` (new rows); `description`, `title`, `$schema`, `$id`
are documentation. Our own: `x-table`, `x-image`, `x-code`, `x-where`
(documentation), `x-keys-from` (object keys must be row ids of a table).
Not supported: `$ref`, `pattern`, `oneOf`/`anyOf`/`allOf`, `format`.

## Coverage

Use `modctl_core data coverage --tables data/tables` for the current schema
inventory. The report describes bindings, not live-play verification.

`evidence/task-19-coverage-<rev>.md` lists every table and field with where
it lives, and what is still hard-coded with addresses (the next tables to
lift).
