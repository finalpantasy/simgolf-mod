# Modding SDK design: easy at the top, deep at the bottom

**Historical proposal, not a mod-author API.** Some underlying pieces, such
as data patches and the Mods screen, now exist; the proposed Lua runtime,
plugin API, hot reload and content pipeline do not. Start with the
[current modding guide](README.md) for supported features.

The remaining text records the original design proposal (task-108).
Builds on: docs/modding/CORE.md (mod folders, resolver, profiles), docs/modding/DATA.md
(data tables), docs/LIMITS.md (engine caps), docs/ui/KIT.md (Frosted Mint kit).
It becomes the spec that cards task-20 (hooks, scripting, plugins), task-21 (assets),
task-22 (Mod Manager metadata), task-23 (modder kit) and task-36 (data part 2) are cut from.

## 0. Summary for the owner (plain English)

**What this is.** One mod system with four steps, from "change a number" to "rewrite part of
the game". A beginner starts on step 1 and never has to learn the later steps. An expert can
go all the way down. All four steps use the same mod folder and the same Mod Manager entry,
so one mod can mix them (a rainy-day mod can have a few numbers, a new golfer type, a script
and an engine plugin).

| Step | What you write | What you can do | How hard |
|---|---|---|---|
| 1. Numbers | A plain text file with `name = value` lines | Prices, wages, golfer moods, difficulty, economy, progression | 5 minutes, no programming |
| 2. Content | Folders with text files and pictures | New golfer personalities and types, new buildings and decorations, replaced art and sounds, new words in the UI, new UI colours | 30 minutes, no programming |
| 3. Scripts | Short programs in Lua (a small, friendly language) | New rules and features: a hole-in-one party, happy-hour pricing, a weather event, a custom panel or button on the screen, a tournament rule | A few hours for a first feature |
| 4. Engine plugins | A compiled Windows plugin (C or C++) | Anything: change how the game works inside, raise limits, new systems | Real programmer work, and the player is warned |

**What you get with it.**
- A command `modctl new` that creates a working starter mod for each step, and `modctl check`
  that explains every mistake in plain words, with the file, the line and the allowed values.
- Hot reload: save a script, a number or a picture and the running game changes in about a second.
- A console panel in the game (log, errors, a "how much time does each mod use" readout).
- Documentation that cannot go out of date, because it is generated from the same source file
  that builds the feature, and every example in it is run by the test suite.
- 10 minute tutorials for each step, a cookbook of 25+ recipes, a full reference, and 11 small
  example mods.
- Safety: steps 1 to 3 cannot touch your files, the network or the rest of your computer, and a
  broken script is switched off with a message instead of crashing the game. Step 4 is full
  power, so the Mod Manager shows a clear warning and asks you to trust each plugin once.
- The original game is always one switch away (the `vanilla` profile or `--no-mods`), and
  zero mods stays byte-for-byte the original behaviour, as it is today.

**Honest limits.**
- Some "new building" ideas are hard because the engine has fixed tables (256 placed objects, a
  fixed list of 20 building kinds in the saves). The design handles new buildings in two
  stages: first as new designs of the existing landmark and decoration kinds (feasible), and a
  research spike decides whether fully new kinds are safe. See section 3.2.
- The game has no weather and no real seasons (it runs March to October, 8 months a year), so a
  weather recipe needs card task-79 first, and "seasons" are an SDK convention built on the
  months. Tournaments do exist in the original game, so tournament scripts are possible now; the
  celebrity pro tour (task-55) would only add more events.
- Browser build: steps 1 to 3 can work there later; engine plugins (step 4) never can.

**Recommended first build (about 1 to 1.5 weeks of worker time):** the Lua runtime, 5 events, one
example mod (a hole-in-one party) and its 10 minute tutorial. It proves the whole chain
(manifest, sandbox, events, a game change you can see, docs, tests) at the smallest size. After
that: friendly step 1 files and error messages, hot reload and console, custom panels, save
state, content, then engine plugins.

**What I need from you.** Approve the plan, or tell me what to change. Three small decisions
are in the Asks block of the message that delivered this; none blocks reading.

---

## 1. Principles

1. **Easiest first, no cliff.** Each layer has a hello world of 1 to 20 lines. A mod author only
   meets the next layer when they choose to.
2. **One mod, one `mod.toml`, one resolver.** The four layers are not four systems. The manifest
   says which layers a mod uses (and the tools verify it), the resolver orders, checks and
   conflict-reports across all of them (CORE.md already does this for settings, data, hooks and
   assets).
3. **Plain words everywhere.** Money is dollars. Names are words (`golfer.mood`, not offsets).
   Every error says what is wrong, where (file and line), what is allowed and how to fix it, with
   a stable code (`SG-D012`) that links to a help page.
4. **The API is a contract, not an address book.** Mods use stable names (`golfer_holed_out`,
   `green_fee`); the engine side maps names to the decompiled functions and data. When golf's
   internals change or a function is re-found, mods do not break. Only the mapping changes.
5. **Write the API once.** A single declarative spec generates the C bindings, the Lua editor
   stubs, the reference docs, the unknown-function checker and the AI tool lists. Docs cannot drift.
6. **Safe by default, deep on request.** Layers 1 to 3 are sandboxed and validated. Layer 4 is
   honest full trust with warnings and a prompt.
7. **The original is one switch away.** Everything is switchable per mod (mods.ini, profiles,
   `--no-mods`), and the A/B harness (task-12) runs the same save with mods off and on.
8. **Built to be written by an AI too.** Machine-readable API (`api.json`), a `mods/AGENTS.md`
   scaffolded into each new mod, MCP tools (task-23), small files, one obvious way to do things.

## 2. The four layers at a glance

```
mods/my_mod/
  mod.toml                    one manifest for everything below
  data/*.toml                 layer 1  numbers            (tables, validated)
  content/ ... , assets/ ...  layer 2  content            (TOML + PNG/WAV)
  scripts/main.lua  lib/      layer 3  scripts            (Lua 5.4, sandboxed)
  plugins/my_mod.dll          layer 4  native plugin      (trust prompt)
  README.md  thumbnail.png    shown in the Mod Manager
```

| | 1 Numbers | 2 Content | 3 Scripts | 4 Native |
|---|---|---|---|---|
| Can change | any table value | adds rows, art, sound, text | behaviour through events, rules, UI | anything |
| Cannot | add behaviour | add behaviour | touch files, network, raw memory, other mods' private state | nothing technical |
| Sandbox | schema + ranges | schema + file checks | Lua sandbox + budgets + permissions | none (trust prompt) |
| Hot reload | yes (most fields) | yes (art, sound, text) | yes (state kept) | restart |
| Breaks saves | no | only if it adds placed content (declared) | only through saved state (versioned) | possibly (declared, backup taken) |
| Browser build | later | later | later | never |

## 3. The layers

### 3.1 Layer 1: numbers and data

**What exists (task-19, merged).** 10 tables in `data/tables/*.json` (buildings, terrain,
employees, economy, difficulty, traits, skills, thought_events, trays, personalities), 25
moddable fields, operations `set add mul min max append merge replace remove`, file:line errors,
base values read from the game's own image so no game data is committed. Mod files are JSON
today, in game units ($100).

**What the SDK adds.**
1. **TOML as the front door.** Same engine, nicer file. A plain value means "set"; an inline table
   means another operation; comments are allowed. The JSON form stays valid and is what the
   loader converts to.
2. **Dollars.** In TOML, money fields are dollars (`cost = 5000`). The schema marks each field's
   unit (`x-unit = "usd100"`), and the loader converts. JSON files keep game units for
   compatibility (`modctl convert` migrates). A wrong guess ("is 50 five thousand dollars or
   fifty?") is the single most likely beginner error, so this is fixed at the root.
3. **Friendly validation.** Every error carries file, line, the field's allowed range, the base
   value, and a "did you mean". Examples are in 3.1.2.
4. **Every tunable documented.** Each field in the schema gets `description`, `x-unit`,
   `x-effect` (what the player sees change) and `x-live` (true if a hot reload applies it
   without restart). `modctl docs` generates `docs/modding/TABLES.md` from them.
5. **Editor support.** `modctl schema` writes JSON Schemas so VS Code (Even Better TOML) gives
   autocomplete and red squiggles with no game running.
6. **Coverage grows by card, not by SDK.** Task-36 lifts mood amounts, thought lines, land
   prices, green fee formula, goals and progression. The SDK's `modctl coverage` shows what is
   moddable and what is still hard-coded, and a "lift a constant" guide (already in DATA.md,
   "Adding a table") is extended with a checklist for cards.

#### 3.1.1 Hello world (layer 1)

```toml
# mods/cheap_snack_bar/mod.toml
id = "cheap_snack_bar"
name = "Cheap Snack Bar"
version = "1.0.0"
```

```toml
# mods/cheap_snack_bar/data/buildings.toml
[snack_bar]
cost = 5000                 # dollars; the original is 15000
income = { add = 100 }      # dollars per visit; add to the original value
```

Run `modctl check`, start the game, the Snack Bar is cheaper. Other forms:

```toml
[economy]
debt_interest_divisor = 25            # set
[difficulty.impossible]
start_cash = { mul = 1.5 }            # multiply
[employees.groundskeeper]
wage = { min = 2 }                    # floor
["*"]                                  # every row that has the field
cost = { mul = 0.5 }                  # half price on everything
```

#### 3.1.2 What an error looks like

```
mods/cheap_snack_bar/data/buildings.toml:3  SG-D012
  snack_bar.cost = 70000 is too high. Allowed: 0 to 327670 dollars (the game stores prices up to 32767 x $10).
  The original value is 15000. Did you mean 7000?   Help: docs/modding/help/SG-D012.md
mods/cheap_snack_bar/data/buildings.toml:5  SG-D004
  There is no building called "snakbar". Did you mean "snack_bar"?   Rows: pathway, benches, flower_bed ...
```

The existing messages (DATA.md "Mod data files") already carry file:line; the SDK adds the
range in friendly units, the original value, the suggestion and the code.

**Open technical point.** tomlc99 (vendored, port/third_party/tomlc99) does not expose key line
numbers. Phase 3 either patches it to record them or runs a small line scanner alongside it. The
JSON reader already tracks lines (port/src/mods/json.c) and is the model.

### 3.2 Layer 2: content

Content is "more rows and more files", still no code. Everything is TOML plus pictures and sound.

| Content | How | State today |
|---|---|---|
| Golfer personalities (own thoughts, share of golfers) | `data/personalities.toml` | done (task-19) |
| Golfer types (skills, spending, looks, arrival chance) | extensible `golfers` table | task-36 |
| Mood and thought lines, building effects, land prices, fee formula, goals | tables | task-36 |
| Replace art, animation, sound, font | `assets/<game path>` mirrors the game's own path; PNG/TGA with alpha in any scale, WAV | task-21 (only the registry stubs exist: `mods_touch(.., "asset", ..)`, `mods_winner`) |
| New UI text and languages | `text/<lang>.toml` (key = string) overrides any game string, falls back to the original | task-64 (language) |
| UI colours and sizes | `ui/frosted_mint.toml` overlay | done (docs/ui/KIT.md) |
| Courses themes, new decorations, landmarks | designs inside the existing landmark/decoration kinds | needs the spike below |
| New buildings with their own income | extensible `buildings` rows | needs the spike below |

**New buildings: the honest design.** The engine has 20 fixed building kinds, 256 placed-object
slots (docs/LIMITS.md section 4), art keyed by kind, and placement/refund code that switches on
the kind. A brand-new kind is therefore engine work. The SDK takes it in two stages:

1. **Stage A (design the SDK for it now, build it first):** a new building is a *design* of an
   existing family (landmark, bench, decoration, amenity with income), exactly the way
   `cut_content` already enables 14 to 16 landmark designs and a sixth bench. The mod row says
   `family = "landmark"`, gives footprint, cost, art, name, and effects built from a small fixed
   vocabulary the engine already understands (income per visit like the Snack Bar's
   `FUN_004289e0` path, mood aura, thought text). The save stores the design index, so an
   uninstalled mod leaves a clear "missing design" marker, not a crash (see 5.2).
2. **Stage B (spike, decision after):** can a truly new kind be added by growing the building
   table (the relocation helper in CORE.md exists for this) and routing the kind switches
   through a table? The spike lists every `switch (kind)` and decides. If the answer is "too
   invasive", Stage A stays the supported path and Layer 3 scripts add behaviour on top
   (`sg.buildings.on_visit(design, fn)`).

#### Hello world (layer 2): a personality and a recoloured clubhouse

```toml
# mods/cheerful_golfers/data/personalities.toml
[cheerful]
name = "Cheerful"
share = 100                                    # percent of golfers; 100 for testing
[cheerful.thoughts]
holed_tough_putt = ["Best. Day. Ever.", "MYNAME does a little dance."]
```

Plus one picture: `mods/cheerful_golfers/assets/clubhouse.png` replaces the clubhouse sprite.
`modctl check` verifies the picture's size against the original and the alpha channel, and
reports "replaces the clubhouse; another mod (pretty_clubhouse) also replaces it, it loads
later and wins".

### 3.3 Layer 3: scripting

**Language: Lua 5.4.** Reasons: MIT, about 30 plain-C source files that build with the project's
existing 32-bit clang-cl toolchain (and with Emscripten, so the browser build can follow);
easiest language to sandbox (a library is only there if we open it); has coroutines, which make
"wait two days then do X" read naturally; instruction-count hooks and a custom allocator give
exact time and memory budgets; huge existing community and training data, so AI-written mods
come out right; already the decision recorded for task-20 (docs/DECISIONS.md 2026-10-09).
Alternatives considered: Luau (MIT, typed, built-in sandbox, but C++ and a bigger build, and
less documentation for beginners); QuickJS (JavaScript, familiar, but larger and slower to
sandbox tightly); Wren and Python (small community, or heavy and hard to sandbox). Lua is the
smallest risk. Luau stays a possible later swap because the API layer (5.1) is language-neutral.

#### 3.3.1 Hello world (layer 3)

```toml
# mods/hole_in_one_party/mod.toml
id = "hole_in_one_party"
name = "Hole-in-one Party"
version = "1.0.0"
platform = "^1.1"
[scripts]
main = "scripts/main.lua"
[permissions]
needs = ["money", "ui"]
```

```lua
-- mods/hole_in_one_party/scripts/main.lua
sg.on("golfer_holed_out", function(e)
  if e.strokes == 1 then
    sg.money.add(5000, "hole-in-one bonus")                  -- logged in the ledger
    sg.ui.toast("Hole in one!", e.golfer.name .. " aced hole " .. e.hole)
    sg.sound.play("cheer")                                   -- from assets/ if the mod ships one
  end
end)
```

Fifteen lines of explanation in the tutorial, one working feature. Save the file while the game
runs: it reloads.

#### 3.3.2 The shape of the API (namespace `sg`)

Everything below is defined once in the API spec (5.1). Names are the contract; the spec marks
each with `since`, `permission` and a cost class.

**Events (`sg.on(name, fn)`, `sg.once`, `sg.off`).** Notifications: the game has already done the
thing. Payload is a plain table.

| Event | Payload (main fields) | Fired from (details in Appendix A) |
|---|---|---|
| `tick` | `tick` (rare; budgeted, usually use the timers below) | the main loop's `tick++` (FUN_0040f5c0) |
| `day_start`, `month_start`, `year_start` | `date`, `month`, `year` | derived in the main loop: day = ((tick & 1023) * 30 >> 10) + 1, month = (tick >> 10) & 7, year = tick >> 13 (a game year is 8 months, March to October) |
| `season_start` | `season` | SDK convention: spring March to May, summer June to August, autumn September to October; the month-to-season table is data a mod may change |
| `golfer_arrived` | `golfer` | FUN_00421bc0 (claims a slot) |
| `golfer_left` | `golfer`, `reason` (`finished`, `quit`, `removed`) | FUN_00426670 (clears the slot) plus the mid-round quit inside FUN_004289e0 |
| `golfer_holed_out` | `golfer`, `hole`, `strokes`, `par` | FUN_00427380 (hole finished) |
| `golfer_shot` | `golfer`, `hole`, `stroke`, `lie`, `result` | stroke increments inside FUN_004289e0 (3 sites) with the landing from FUN_00409620; no single shot function exists |
| `golfer_mood` | `golfer`, `reaction`, `change` | FUN_00467a00 (about 60 callers); text from FUN_00469b00 is already hooked by the personalities |
| `money_changed` | `amount`, `balance`, `reason` | about 25 direct writes to the cash global 0x571fd4; see 7.2 |
| `wages_paid` | `holes`, `staff`, `total` | the wage block in the main loop |
| `building_placed` | `design`, `x`, `y`, `cost` | FUN_0040e000 (record creation; also used by course generation, so filtered by game state) |
| `building_removed` | `design`, `x`, `y` | FUN_0040e400 / FUN_0040a4e0 |
| `terrain_painted` | `tiles` (batched per frame) | tile writes in the main loop via FUN_00405e30 |
| `hole_opened` | `hole`, `par` | FUN_0040e720 |
| `employee_hired`, `employee_fired` | `employee` | FUN_00402970 (allocation), FUN_00436c00 case 5 |
| `tournament_started`, `tournament_finished` | `field`, `results`, `prizes` | FUN_0046c970 (start), FUN_0045a090 (results and payout) |
| `pro_round_started`, `pro_skill_gained` | `pro`, `skill` | FUN_0040f190, FUN_00407e00 |
| `celebrity_home_bought` | `celebrity`, `site` | the home-site block in the main loop (FUN_004011e0) |
| `game_started`, `game_loaded`, `game_saving`, `game_saved` | `name`, `is_new` | FUN_004315e0 / FUN_00470a60 / FUN_0042f7a0, FUN_0040b9b0, FUN_0040b4a0 (all with the port's existing `mods_save_opened/written`) |
| `message_posted` | `text`, `colour` | FUN_0040cb00 (about 55 callers) |
| `ui_click` | `id` | from script UI (3.3.4) |

**Rules (`sg.rule(name, fn)`).** A rule is a typed filter on a decision the game makes. The
function receives the value and context and returns the (possibly changed) value. Several mods
chain in load order (each sees the previous result), which is why two mods adjusting the same
price do not conflict. Examples: `green_fee(value, ctx)`, `golfer_mood_change(value, golfer, event)`,
`building_income(value, building, golfer)`, `wage(value, employee)`, `land_price(value, tile)`,
`arrival_rate(value, date)`, `shot_error(value, golfer, club)`. A mod that wants to fully take
over uses `sg.rule(name, fn, {exclusive = true})`, and that is a reported conflict if two do.

**Queries (read only, cheap).**
`sg.golfers.all()`, `.where{mood_below = 3}`, `.count()`, `.selected()`, golfer fields
(`name, mood, skills, hole, strokes, x, y, personality, profile_id, is_pro`; golfers carry no
wallet in the original game, so spending is a per-visit price, not a balance), `sg.course` (holes,
par, tiles, buildings, `tile(x,y)`), `sg.money.balance()`, `sg.time` (date, season, tick,
`day_of_week`), `sg.staff.all()`, `sg.pro`, `sg.weather` (once task-79 lands), `sg.stats`
(rating, fun, fame, stars).

**Actions (permission-checked, validated).** `sg.money.add(amount, reason)`,
`sg.golfers.spawn{}` (bounded), `golfer:set_mood(n)`, `golfer:say(text)` (a thought bubble),
`sg.course.place(design, x, y)`, `sg.staff.hire(kind)`, `sg.ui.toast`, `sg.sound.play`,
`sg.settings.set(key, value)` (the live settings store that already exists, `mods_set`),
`sg.data.set("buildings.snack_bar.cost", 4500)` (live data change, same validation as layer 1).
Every write is clamped to the legal range and reported if clamped; nothing hands out a raw pointer.

**Timers and flow.** `sg.every("1 day", fn)`, `sg.after("3 hours", fn)`, and coroutines:
`sg.run(function() sg.wait("2 days"); sg.ui.toast("Two days later") end)`. Timers use game time
and are saved and restored with the state (3.3.5).

**Randomness.** `sg.random(a, b)` is a deterministic stream seeded from the game seed and the mod
id, so replays and the A/B harness give the same result; `math.random` is removed.

**Logging.** `sg.log.info/warn/error(fmt, ...)` goes to golf.log and the console panel, tagged
with the mod id.

#### 3.3.3 Sandbox and budgets

- Lua libraries opened: base (without `dofile`, `loadfile`, `load` of binary chunks,
  `collectgarbage` control), `string`, `table`, `math` (without `random`), `utf8`, `coroutine`.
  Removed: `io`, `os`, `package`, `debug`. `require` is replaced by a loader that resolves only
  inside the mod's `scripts/` and the `lib/` of mods it depends on.
- File access: `sg.file.read("data/prices.csv")` reads inside the mod folder only;
  `sg.file.write("export.csv", text)` writes only into `mod_data/<id>/` next to the saves,
  size-capped (1 MB per mod). No network, no process start, no clock beyond game time and a
  coarse `sg.real_time_ms()` for profiling.
- Memory: custom allocator, 16 MB per mod by default (a mod may ask for up to 64 MB in
  `mod.toml`, shown in the Manager).
- Time: a count hook aborts a handler that runs longer than its budget (default 2 ms per event
  call, 4 ms total per frame across all mods, `tick` 0.5 ms). Three aborts in one minute switch
  that mod's scripts off with a toast and a log line naming the file, line and handler.
- Errors: every handler runs under a protected call. The message has the mod id, file:line,
  a plain description and a traceback in the console; the game continues.
- Permissions: the manifest lists what the scripts need (`money`, `golfers_write`, `course_write`,
  `ui`, `state`, `files`); a call outside the list fails with "mod X tried sg.money.add but
  mod.toml does not list the 'money' permission". The Mod Manager shows the list in plain words
  ("Can change your money, can add panels"). This is transparency and mistake prevention, not
  protection against a hostile script; the sandbox itself is the protection.

#### 3.3.4 Custom UI: panels, HUD buttons, toasts

The UI kit draws on the render thread; Lua must run on the game thread only (the same split the
HUD already uses: the game thread publishes a plain snapshot, the screen draws it and returns
actions, see `HudSnap`/`HudAction` in hud_frosted.h). Scripts therefore describe the UI as data
and receive clicks as events. No Lua runs on the render thread.

```lua
local panel = sg.ui.panel{
  id = "daily_ledger", title = "Daily ledger", anchor = "right", width = 320,
  view = function(ctx)                       -- called on change and about 10 times a second
    return {
      { "stat",   "Today",  sg.format.money(today) },
      { "meter",  "Busy",   busy / 100 },
      { "button", "Reset",  id = "reset" },
      { "list",   rows },                    -- rows = { {name=, line=, right=}, ... }
    }
  end,
}
sg.on("ui_click", function(e) if e.id == "reset" then today = 0 end end)
sg.ui.hud_button{ id = "ledger", icon = "chart", tooltip = "Daily ledger", panel = panel }
```

Widgets map one to one onto the kit (docs/ui/KIT.md): `stat`, `text`, `button`, `icon_button`,
`toggle`, `slider`, `segmented` (tabs), `meter`, `list` (`ui_list_row`), `tile`, `divider`,
`tooltip`, plus `toast` and `dialog`. The panel gets the frosted glass, tokens, UI scale and
input routing for free, so mods look native and a restyle mod (dusk_glass) restyles them too.
The game-side extension point (a HUD slot registry and a mod layer on `hd_ui_add_layer`) does not
exist yet: the dock items are hard-coded in `ui_hud.c` (about 2000 lines). Building that slot
registry is its own task (phase 6) and is also what the Mod Manager entry point needs.

#### 3.3.5 Saved mod state and migration

```lua
local S = sg.state({ version = 2, today = 0, regulars = {} })   -- defaults; loaded on game load
sg.migrate(function(old, from) if from < 2 then old.regulars = {} end return old end)
```

State is plain data (numbers, strings, booleans, nested tables; no functions or cycles), saved as
JSON inside the save's sidecar (5.2), 256 KB per mod. Timers started with `sg.every/after` are
part of it. If a save comes back without its sidecar, scripts start fresh and the console says so.

#### 3.3.6 Hot reload

A file watcher (mtime poll every 500 ms, no OS dependency) watches the loaded mods. Script save:
the module is re-run, its handlers and timers re-registered (the old ones are removed first by
mod id), `sg.state` is kept in memory. Data and UI token changes re-apply live where the schema
says `x-live`. Pictures and sounds reload their textures. The console prints
"reloaded hole_in_one_party (3 handlers) in 4 ms". `modctl watch` drives the same from an editor.

### 3.4 Layer 4: native plugins (deep engine mods)

A plugin is a 32-bit Windows DLL (the game is an i686 program because the translated code keeps
x86 addresses; docs/ARCHITECTURE.md). It uses the same hook mechanism as the project's own
overrides (section 7), through a C header with a versioned ABI, and can do anything the game's
own code can.

```c
// simgolf_plugin.h (shipped in the SDK zip, generated from the API spec)
#define SG_ABI_VERSION 1
typedef struct SgHost {
  uint32_t size, abi;                                  // size grows only at the end
  void (*log)(int level, const char *fmt, ...);
  int  (*hook)(const char *point, int when, SgHookFn fn, void *user);   // SG_BEFORE|SG_AFTER|SG_REPLACE
  int  (*call_original)(SgCall *call);                 // from a REPLACE hook
  int  (*get)(const char *symbol, SgValue *out);       // named state, data/symbols.json
  int  (*set)(const char *symbol, const SgValue *in);
  int  (*on)(const char *event, SgEventFn fn, void *user);   // same events as Lua
  int  (*ui_layer)(SgUiDraw draw, SgUiInput input, void *user); // a Frosted Mint layer
  int  (*state)(const char *key, void *buf, size_t cap, int write); // saved plugin state
} SgHost;
SG_EXPORT int  sg_plugin_init(const SgHost *host);       // return 0 = ok
SG_EXPORT void sg_plugin_shutdown(void);
SG_EXPORT const SgPluginInfo *sg_plugin_info(void);      // id, abi, touches_saves, ...
```

Hello world:

```c
#include "simgolf_plugin.h"
static void on_shot(SgCall *c, void *user) { host->log(SG_INFO, "shot by golfer %d", sg_arg_int(c, 0)); }
int sg_plugin_init(const SgHost *h) { host = h; return host->hook("golfer.shot", SG_AFTER, on_shot, 0); }
```

**Policy.**
- A plugin is full trust, like any program you download. The Mod Manager says so in plain words
  before the first load: "This mod runs code inside the game. It can change how the game works
  and could crash it or damage saves. Only trust mods from people you trust." Per mod, per exact
  DLL hash (SHA-256): a changed DLL asks again. Trust is stored in the player's user folder.
- The first load of a plugin that declares `touches_saves = true` makes a backup copy of Saved
  Games first, and the Manager says where it is.
- Callbacks run under a Windows structured exception handler. A crash in a plugin disables it
  for the next start and the Manager says "the game stopped while mod X was loaded". Memory
  corruption cannot always be recovered, so this is honesty rather than a guarantee.
- Plugins never auto-download and are never accepted by the course browser (task-46 trusted-mods
  rule); they install only from a file the player picks.
- Not available in the browser build.
- The ABI is versioned: `abi` in `mod.toml [native]`, a new host struct appends fields only, an
  incompatible change bumps `SG_ABI_VERSION` and old plugins are refused with a clear message.
- Templates: `modctl new native` writes a CMake project that builds with clang-cl i686 or MSVC,
  with the header, a debug log hook and a README for debugging (attach, log, symbols). Other
  languages work if they can export C functions for i686 (Rust target i686-pc-windows-msvc).

**Two ways to hook.** Curated shims (section 7.2) are stable, named and what Lua uses. For code
the curated list does not cover, `sg.detour("FUN_xxxxxxxx", ...)` patches the function at run
time by address through the function table the build exports. It is marked unsafe in the
reference (an inlined or recompiled function may not exist), refused unless the plugin says
`unsafe_detours = true`, and shown in the Manager. Whether the build can export that table
cheaply is a phase-2 spike.

## 4. One manifest, one resolver

All four layers share `mod.toml`. Existing keys (CORE.md) are unchanged. New keys are additive and
unknown keys stay warnings, so a mod made for the SDK still loads on an older platform if its
`platform` range allows.

```toml
id = "happy_hour"                          # existing
name = "Happy Hour"
version = "1.2.0"
authors = ["Ann"]
description = "Half-price greens from 4 pm, with a HUD button to toggle it."
platform = "^1.1"                          # SDK scripting arrives as platform 1.1
game = ">=1.2.0"
load_after = ["pricier_greens"]            # existing order hints and dependency tables
[dependencies]
core_lib = "^1.1"
[incompatibilities]
fixed_prices = "*"

# NEW: what a player sees in the Mod Manager
tags = ["economy", "ui", "scripts"]
category = "gameplay"                      # gameplay | content | visual | audio | ui | tools | engine
thumbnail = "thumbnail.png"                # 16:9, 640x360 recommended
screenshots = ["shots/button.png"]
homepage = "https://example.invalid/happy-hour"
license = "MIT"
readme = "README.md"                       # shown on the detail page
changelog = "CHANGELOG.md"
save_impact = "safe_to_remove"             # safe_to_remove | needs_mod_to_load | needs_mod_to_load_new_saves

# NEW: layers (checked by the tools against the folder, so the label cannot lie)
[layers]
data = true
content = false
scripts = true
native = false

[scripts]
main = "scripts/main.lua"
library = false                            # true: shared Lua modules for other mods (provides)
memory_mb = 16
[permissions]
needs = ["money", "ui", "state"]

[native]                                   # only if plugins/ exists
plugin = "plugins/happy_hour.dll"
abi = 1
touches_saves = false
unsafe_detours = false

# NEW touch kinds, for conflict reporting before anything runs
[touches.rules]
green_fee = "filter"                       # filters chain; "replace" is exclusive
[touches.events]
day_start = "on"
[touches.ui]
hud_button = "add"
[touches.data]                             # existing
"buildings.snack_bar.cost" = "set"
[touches.assets]                           # existing
"Interface/TitleBASE.pcx" = "replace"
```

**Resolver additions** (the algorithm in CORE.md is unchanged, kinds and operations grow):
- Kinds `rule`, `event`, `ui` join `setting`, `data`, `hook`, `asset`. Operations: `filter`
  (composes like `mul`, never conflicts), `on` (listeners, never conflict), `add` (a UI slot),
  and the exclusive `replace`. Two exclusive `replace` on the same rule: the later mod wins and a
  warning names both, exactly like hooks and assets today.
- `modctl check` scans the folder, derives the real layers, permissions used, rules and events
  subscribed, assets replaced and data keys touched, compares them with the manifest, and fills
  in missing `[touches]` as a suggestion (so authors cannot forget, and conflicts show before the
  game starts).
- `save_impact` feeds the save-compatibility rules (5.2).

**What the Mod Manager (task-22) gets.** `modctl_core resolve --json` and a new `info --json`
return, per mod: id, name, version, authors, description, tags, category, thumbnail path,
screenshots, readme text, changelog, license, homepage, derived layers, permissions in plain
words, native-plugin trust state, `save_impact`, memory and time use this session (from the
budget counters), conflicts with the exact counterpart, missing dependencies, and warnings. The
Manager screen is task-22's; this card only fixes the data it can rely on. Fields are only ever
added.

## 5. Single source of truth, compatibility and saves

### 5.1 The API spec generates everything

```
sdk/api/api.toml        functions: name, since, deprecated, params, returns, permission, cost,
                         summary, example, implemented_by (C function)
sdk/api/events.toml     events: name, since, payload fields, fired_by (hook point)
sdk/api/rules.toml      rules: name, since, value type, context, default
sdk/api/hooks.toml      hook points: name, FUN address, argument types, which are writable, notes
data/tables/*.json      tables (exists)
```

`modctl docs` (and the build) generate from them:
1. the C binding table that registers the Lua functions (a function that is in the docs is in the
   game, because both come from one line);
2. `docs/modding/API.md`, `EVENTS.md`, `RULES.md`, `HOOKS.md`, `TABLES.md`;
3. `sdk/lua/sg.d.lua`, type stubs so editors autocomplete and show docs;
4. `api.json` for `modctl check` (unknown `sg.foo` calls get "did you mean") and for AI tools;
5. `simgolf_plugin.h` (the native header);
6. `docs/modding/help/SG-*.md`, one page per error code.

`modctl doctest` extracts every fenced example in the docs marked `lua` or `toml`, runs it in a
headless game (the test harness, task-12) with the mod installed, and fails the check if it does
not load or errors. Documentation drift becomes a failing test.

### 5.2 Save compatibility

- The `.sve` format never changes (CORE.md: original builds load our saves and the reverse).
- Mod state goes into the existing sidecar `<save>.sve.mods.toml` as `[state.<id>]` blocks
  (JSON text, versioned, size-capped). The existing CRC tie to the save stays. Known weakness,
  same as today: copy a save without its sidecar and the state is lost. The Manager's save list
  and `modctl pack-save` copy both together.
- Removing a mod: `safe_to_remove` mods load fine without it. `needs_mod_to_load` mods (placed
  content, new designs, plugins that changed save data) make the load screen say which mods are
  missing and offer "Cancel" or "Load anyway (may be wrong)". Today the game only logs a warning
  (CORE.md, "Saves"); this makes it a choice, never a crash.
- A script's saved state is migrated by its own `sg.migrate`. A state it cannot read is kept
  untouched in the sidecar (so a later fixed version can read it) and the console says so.
- Content in saves is stored by stable string id, mapped to the engine's slot indexes at load, so
  reordering mods never scrambles a save. (Needs care in Stage A building designs.)

### 5.3 Versioning

- `platform` in `mod.toml` is the one compatibility range (as today); scripting arrives as 1.1.
  API functions have `since` and `deprecated`; a deprecated call still works for two minor
  versions and logs a warning with the replacement. Removal needs a major version.
- Events, rules, hook points and table fields follow the same rule: add freely, deprecate before
  remove, never silently change meaning.
- Native ABI is separate (`SG_ABI_VERSION`) and bumps rarely.

### 5.4 Performance budget

- No mods: no cost, and zero mods stays identical to the original (A/B, as tests/mods/data_ab.mjs
  does today). Hook shims check one flag per call when nothing listens.
- Events are dispatched only if someone subscribed. A subscribed per-frame `tick` is the only
  expensive habit, so it has its own tight budget and the docs steer to `sg.every`.
- Target: ten typical script mods use under 0.5 ms per frame on the development machine; the
  console shows each mod's time and memory. A Lua call is about 100 ns, so the cost is in how
  much a script iterates, which is why queries return lightweight handles and `where` filters run
  in C.
- Golfer arrays are 152 slots (docs/LIMITS.md); queries iterate only the live ones.

### 5.5 Safety summary

Layers 1 to 3 cannot reach files, network or other processes; layer 3 is also time and memory
capped and permission-declared; layer 4 is explicitly trusted. Malformed mods fail at `modctl check`
or at load with a message, and the game still starts without them. A mod can never write inside
the game folder (assets and data are read-only; `mod_data/<id>/` is the only writable place).

## 6. Tooling

| Command | Does |
|---|---|
| `modctl new <template> <id>` | Scaffolds a working mod. Templates: `data`, `personality`, `building`, `art-replace`, `text`, `script`, `script-ui`, `library`, `native`. Each builds, loads and has a README and a comment on every line that matters. Also writes `mods/<id>/AGENTS.md` so an AI chat knows the rules. |
| `modctl check [id]` | Manifest, data (existing checks plus friendly errors), scripts (syntax, unknown `sg.` calls, permissions used vs listed, budget lint such as `tick` handlers that iterate everyone), assets (size, alpha, missing, unused), native (header ABI, hash), dependencies and conflicts preview. Exit 1 on error. Same C code as the game. |
| `modctl run [id]` | Starts the game hidden or visible with that mod on, through the bridge (task-13), hot reload on. |
| `modctl watch` | Pushes file changes to a running game (for editors). |
| `modctl test [id]` | Runs `tests/*.lua` of a mod (`sg.test.expect`) in the headless harness; the template includes one. |
| `modctl docs`, `schema`, `doctest` | Generators and docs tests (5.1). |
| `modctl api search "golfer mood"` | Looks in the API from the terminal; `api.json` gives the same to AI tools. |
| `modctl pack <id>` | Zips to `<id>-<version>.sgmod` (manifest hash, file list); the Manager installs it by drag and drop. |
| `modctl convert` | JSON data to TOML (and dollars). |
| `modctl coverage` | What is moddable, what is hard-coded. |
| MCP tools (task-23) | `list_tables`, `describe_table`, `list_events`, `describe_api`, `scaffold_mod`, `validate_mod`, `enable_mod`, `hot_reload`, `run_mod_test`, all from the same spec. |

**In-game mod console** (a Frosted Mint panel, open from the Mod Manager and a key): the log
(filter by mod and level, errors in red with the file:line), per-mod time and memory, an event
trace switch ("show every event as it fires", with payload), a reload button per mod and, when
the mod enables `dev = true`, a one-line Lua prompt. It is the debugging answer for non-experts.

## 7. Grounding in the codebase

### 7.1 What exists and what each layer reuses

- Mod folders, `mod.toml`, resolver, profiles, mods.ini, settings store, save sidecar, conflict
  registry with `mods_touch`/`mods_winner`/`mods_fold`: port/src/mods/ (CORE.md). Reused as is.
- Data layer: port/src/mods/data.c, data_runtime.c, data/tables/*.json, `MDATA(...)` wrappers from
  translator patches in port/tools/translate/golf.data.json. Layer 1 and 2 data reuse it.
- Overrides today are build-time only: a hand-written function tagged `// @override FUN_xxxxxxxx`
  in port/src/port/golf/*.c replaces the generated one (18 of them now: seed.c, hud_hooks.c,
  map_save.c, overrides.c ...), and translator `patches` edit generated lines. There is no
  runtime chain and no way to call the original from an override. Layer 3 and 4 need the runtime
  mechanism in 7.2.
- Named state: data/symbols.json (money, tick, date, game_flags, difficulty, holes_built,
  golfers_on_course, golfers, fun_rating, skill_rating, stars, fame, map and camera, cash_history
  and others, 32 so far) with types, scale and confidence, read and written through the bridge.
  `sg.get/set` and the native `get/set` build on it; it grows as API functions need more.
- Golfer records: 152 live slots x 256 bytes at 0x5794b8, fields documented in docs/LIMITS.md
  section 2; employees 64 x 0x4c at 0x585850; placed objects 256 x 16 at 0x58bcb8 with a byte
  grid at 0x56988c; profiles, celebrities and pros as listed there. The query API wraps these.
- UI: docs/ui/KIT.md and port/src/port/ui/ (immediate-mode widgets, tokens, input routing);
  screens register with `hd_ui_add_layer` and `hd_ui_add_input` (up to 8 layers) in
  port/src/port/hd_native.h; the game thread publishes snapshots and the screen returns actions
  (`HudSnap`, `HudAction`). HUD dock items are hard-coded in ui/ui_hud.c: the HUD has no
  extension point yet.
- Build: 32-bit clang-cl, static CRT, SDL2, one `golf.exe` plus SDL2.dll (port/cmake/*,
  tools/build_native.sh). No Lua and no plugin loading exist yet; the only DLL calls are
  emulated Windows imports and the d3dcompiler load. The browser build has no mods layer at all
  today (the web target lists no src/mods).
- No hot reload and no in-game console exist; golf.log and the bridge's `log` command are the
  only diagnostics. No asset virtual file system exists (task-21 not built); `fs_native.c`
  resolves DOS paths and has only the save hooks.
- Tooling: tools/modctl/modctl.py (`list enable disable order check conflicts new validate
  profile save-info`) over build/native/modctl_core.exe; tools/check.sh stages for mods.

### 7.2 The runtime hook mechanism (new)

What the survey found. Overrides are compile-time swaps (the translator, port/tools/translate/
translate.py lines 72 to 100 and 870, skips the generated body of a function tagged
`// @override`); the original body is not callable from the override, so an override must
re-implement it. Nothing dispatches `before/after/replace` at run time: `mods_touch(.., "hook", ..)`
only records conflicts. Many of the moments mods care about are not functions at all: they are
inline code inside the 73 KB main loop FUN_0040f5c0 (the tick, month and year rollovers, wages,
building placement, terrain painting), and money has no central function (about 25 direct writes
to the cash global, 20 of them next to the floating-cash popup FUN_0040c890). So the mechanism has
three parts, all emitted by the translator at build time and all dormant when nothing listens:

1. **Function shims** for whole functions (arrive FUN_00421bc0, leave FUN_00426670, hole finished
   FUN_00427380, mood reaction FUN_00467a00, hire FUN_00402970, fire FUN_00436c00, tournament
   FUN_0046c970 and FUN_0045a090, save and load, message FUN_0040cb00 ...). The translator emits a
   wrapper under the original name; the body is renamed `FUN_xxxxxxxx__orig`. With no listener the
   wrapper tail-calls the body after one flag test. With listeners it runs the chain: `before`
   (may read and change arguments), at most one winning `replace` (receives a `call_original`),
   then `after` (sees the result).
2. **Inline probes** for events inside the main loop. The translator already supports text patches
   (`[old, new]` replacements in a function's decompiled C, golf*.json; `MDATA(...)` and the HUD
   hide/restore calls are examples). Each probe becomes a one-line call such as
   `sg_probe_day(tick)` or, for money, a rewrite of every `cash = cash + x` into
   `sg_money(x, SG_REASON_WAGES)` so the event carries a reason. This is mechanical (about 25
   sites), reviewed once, and covered by the zero-mods A/B. Where a probe is too risky, a cheap
   fallback exists: compare the global each frame (money, tick) and report the difference without a
   reason.
3. **Entry observers for plugins.** Every translated function starts with `PORT_TRACE(addr)`
   (port/src/port/ghidra_prelude.h:216). Plugins may ask the host for an observer on any function
   by address (read only, before the body). Whether this can also carry arguments, and whether a
   detour on `FUN_xxxxxxxx` is reliable under clang-cl optimisation, is the phase-2 spike.

The curated list in `sdk/api/hooks.toml` is the stable surface (about 40 points to start, growing by
request; adding one needs a rebuild of golf.exe, not a mod). Cost with no listeners: one
predictable branch per call. Zero mods stays identical, which the A/B harness proves per hook point
(phase 2 acceptance). Overrides stay as they are and coexist.

### 7.3 Which game functions the API wraps

Appendix A lists the functions and data per event and per query, with source lines and confidence.

## 8. Docs plan

Layout of `docs/modding/` (the existing CORE.md and DATA.md become reference pages):

```
README.md              start here: "what do you want to do?" decision list, links to the four tutorials
tutorials/1-numbers.md   10 minutes: make the Snack Bar cheaper, see it, break it on purpose, read the error
tutorials/2-content.md   10 minutes: a new golfer personality, a recoloured clubhouse, a new line of text
tutorials/3-scripts.md   10 minutes: the hole-in-one party, then a toast, then a HUD button
tutorials/4-native.md    10 minutes (for programmers): build the template, log every shot, break and recover
COOKBOOK.md            25+ recipes (below)
reference/             generated: TABLES.md, API.md, EVENTS.md, RULES.md, HOOKS.md, MANIFEST.md, PLUGIN_ABI.md
help/SG-*.md           one page per error code
CONCEPTS.md            money units, ticks and days, how load order and conflicts work, how saves work, for non-programmers
TROUBLESHOOTING.md     the 15 most common failures
SDK_DESIGN.md          this document
```

Tutorial rule: each step is a command or a file, shows the expected result (a real screenshot
from the hidden-desktop capture, never a mockup), ends with "what you just learned" and "next".
The tutorials are run by `modctl doctest` in CI.

**Cookbook (target 27 recipes).** Each has: goal, layer, files, code, what you see, ideas to tweak,
and a status line if it needs another card.

| # | Recipe | Layer | Needs |
|---|---|---|---|
| 1 | Cheaper snack bar | 1 | done |
| 2 | Cheaper running costs (hole upkeep, groundskeeper wages; fertilizer-style costs if the game has a row for them) | 1 | done |
| 3 | A harder economy (wage period, debt interest, start cash) | 1 | done |
| 4 | Start rich or start broke per difficulty | 1 | done |
| 5 | Golfers get angrier at slow play | 1 | task-36 |
| 6 | Cheaper land | 1 | task-36 |
| 7 | A new golfer personality with its own thoughts | 2 | done |
| 8 | A new golfer type (rich, slow, big tippers) | 2 | task-36 |
| 9 | Recolour the whole interface | 2 | done |
| 10 | Replace the clubhouse picture | 2 | task-21 |
| 11 | Replace a sound (the cheer) | 2 | task-21 |
| 12 | Rename things / a second language | 2 | task-64 |
| 13 | A new decoration with art | 2 | Stage A |
| 14 | A new building with its own income | 2+3 | Stage A + script |
| 15 | Hole-in-one celebration | 3 | slice |
| 16 | Happy hour: timed discount with a HUD button | 3 | UI slots |
| 17 | Dynamic pricing: busy days cost more | 3 | rules |
| 18 | Seasonal discount | 3 | events |
| 19 | A weather event (heatwave: golfers thirstier, snack bars earn more, a banner) | 3 | script-only version now; visuals need task-79 |
| 20 | A custom HUD panel: daily money chart | 3 | UI slots |
| 21 | Regulars: remember golfers who came back, give loyalty bonuses (saved state) | 3 | state |
| 22 | Staff bonus day | 3 | events |
| 23 | A tournament rule: prize money scales with stars | 3 | events (tournaments exist in the original game) |
| 24 | Daily CSV report to `mod_data` | 3 | files |
| 25 | A cheat-free "easy mode": no debt interest and a gentle first year | 1+3 | done |
| 26 | Raise the staff cap | 4 | task-104 |
| 27 | Log every shot (plugin) and change the shot error (replace hook) | 4 | plugin |

**Reference.** Generated from the spec (5.1) so every function shows signature, since, permission,
cost class, example and what it wraps. Tables show every field with range, unit, effect, live or not.

## 9. Example mods to ship (11)

Each is small (data files of under 30 lines, scripts under 80), heavily commented, tested by
`modctl test`, and used by a tutorial or recipe. They live in `mods/examples/`.

| # | Mod | Layer | Shows |
|---|---|---|---|
| 1 | `cheap_snack_bar` (exists, convert to TOML) | 1 | tutorial 1 |
| 2 | `hard_times` (exists, convert) | 1 | difficulty curve, several tables |
| 3 | `grumpy_golfers` (exists) and `cheerful_golfers` | 2 | personality with thoughts, tutorial 2 |
| 4 | `dusk_glass` | 2 | UI tokens overlay (a restyle) |
| 5 | `golden_clubhouse` | 2 | replaced art + sound |
| 6 | `lighthouse` | 2+3 | new landmark design with art, name, price, a script giving it income and a thought |
| 7 | `hole_in_one_party` | 3 | the vertical slice, tutorial 3 |
| 8 | `happy_hour` | 3 | a rule filter, timer, HUD button, saved state |
| 9 | `daily_ledger` | 3 | a HUD panel with a chart, events, CSV export |
| 10 | `shot_log` | 4 | native plugin template: logs every shot, tutorial 4 |
| 11 | `staff_cap_plus` | 4 | a replace hook that lifts the hire cap (pairs with task-104) |

(`heatwave`, the script-only weather event, joins once the rule filters exist; task-79 adds the visuals.)

## 10. Build plan

Cost units: S = one worker chat (about 0.5 to 1 day), M = 2 to 3 chats, L = 4 to 6 chats. A
chat that reaches 400k to 500k tokens hands off, per the worker rules. "Sonnet" means a Sonnet
worker can do it from the card; "Opus" means the root cause or the design is unclear enough to
need it (reason given).

| Phase | Deliverable | Effort | Model | Depends |
|---|---|---|---|---|
| 0 | API spec format (`api.toml`, `events.toml`, `rules.toml`, `hooks.toml`) and the generator skeleton (C table, Markdown, Lua stubs, `api.json`); spec for the first 5 events | S | Sonnet | none |
| 1 | **Vertical slice**: vendor Lua 5.4, sandbox, per-mod state/budgets/errors, event bus, `sg.on`, `sg.log`, `sg.golfers` basics, `sg.money.add`, `sg.ui.toast`, the 5 events `day_start` (a main-loop probe), `golfer_arrived`, `golfer_holed_out`, `building_placed` (function shims on FUN_00421bc0, FUN_00427380, FUN_0040e000) and `money_changed` (the 25-site cash rewrite of 7.2, the riskiest part of the slice), `hole_in_one_party` example, tutorial 3, tests in check.sh | M-L | Sonnet for runtime and docs; Opus review of the sandbox and of the hook shim generator (security and unclear translator interplay) | 0 |
| 2 | Hook shim generation for the curated list (about 40 points), the function-table export spike, zero-mods A/B per point, rules (`sg.rule`) | M | Opus (translator and calling-convention risk; unclear root cause if something is inlined) | 1 |
| 3 | TOML data front end, dollars, friendly errors with codes and help pages, generated TABLES.md, editor schemas, line numbers in TOML | M | Sonnet | none (parallel to 1) |
| 4 | modctl: templates, `check` for scripts/assets, `api search`, `pack`, `convert`, `doctest`, `AGENTS.md`; MCP tools with task-23 | M | Sonnet | 0, 1, 3 |
| 5 | Hot reload (watcher, script/data/asset), mod console panel, per-mod time/memory counters | M | Sonnet | 1 |
| 6 | Script UI: panel/widget bridge, HUD slot registry in `ui_hud.c`, buttons, tutorials 3b | M-L | Sonnet (Opus review of the thread split) | 1, task-26/30 |
| 7 | Saved state: `sg.state`, timers, migration, sidecar state, "missing mod" load choice | S-M | Sonnet | 1 |
| 8 | Content: asset layer (task-21), text/language (task-64), personalities pack, Stage A designs, Stage B spike, extensible roster (task-36) | L | Sonnet; Opus for the Stage B spike (switch-on-kind archaeology across the decompile) | 3, 7 |
| 9 | Native plugins: header from spec, loader, trust prompt (with task-22), crash isolation, backup, template, tutorial 4, examples 10 and 11 | M-L | Opus for ABI and crash handling design; Sonnet for template and docs | 2, task-22 |
| 10 | Docs completion: tutorials 1, 2, 4; cookbook 27 recipes; CONCEPTS; TROUBLESHOOTING; examples 1 to 11 | L, spread over phases | Sonnet | each layer |

Order: 0 → 1 → (2, 3 in parallel) → 4, 5 → 6, 7 → 8, 9 → 10 throughout. Roughly 14 to 20 worker
chats overall. With the worker limit (3 at a time) and one game run at a time, that is a few
weeks of elapsed time; the slice alone is 1 to 1.5 weeks.

**Fit with existing cards.** Phase 1, 2, 5, 7, 9 are what task-20 describes; phase 3 and 8 are
task-19 follow-up, task-21, task-36 and task-64; phase 4 is task-23; the Manager shows what
phase 4 and section 4 define (task-22). Proposal: keep those cards as the containers and put
their acceptance tests under this spec, rather than creating parallel work.

### The first vertical slice

Lua runtime + 5 events + 1 example mod + its tutorial (phase 0 + 1).
Acceptance:
- A fresh `modctl new script` mod builds and loads; `hole_in_one_party` pays a bonus and shows a
  toast when a golfer holes out in one (scripted shot sequence on the showcase course, screenshot
  as evidence).
- A script that loops forever is aborted, the mod is switched off, the game keeps running (test).
- A script that calls `io.open` or `os.execute` fails with the "not available" message (test).
- An error in a handler prints the mod id, file, line and keeps the game running (test).
- Zero mods: A/B identical over 2400 ticks (existing harness), with the hook shims compiled in.
- Tutorial 3 passes `modctl doctest`.
- `tools/check.sh` passes.

## 11. Decisions I made (technical) and open questions

Decided (my calls, logged for docs/DECISIONS.md if approved):
1. Lua 5.4 as the one scripting language; a TOML rules language is deferred (it would be a second
   language; add it only if tutorial testers stumble, compiled to Lua so there is one engine).
2. TOML is the front door for numbers; money is dollars in TOML, game units stay in JSON.
3. Scripts run on the game thread only; UI is data, never Lua on the render thread.
4. Hook points are a curated, named list with a rebuild to add; raw address detours are an
   explicit unsafe option for plugins only.
5. Mod state lives in the existing save sidecar, never in the `.sve`.
6. `platform` stays the single compatibility range; scripting is platform 1.1.
7. No real name or personal detail in any SDK file (public-identity rule).

Open, for the owner (game and risk choices only, each with my pick):
1. **How strict should "missing mod when loading a save" be?** Pick: a choice screen (Cancel or
   Load anyway) for mods that placed content, silent for the rest.
2. **Plugins (layer 4) in the first public release?** Pick: include, behind the trust prompt, but
   ship the Mod Manager warning and backup first (phase 9 after the Manager).
3. **Pace of new building kinds.** Pick: Stage A designs first, spike Stage B afterwards, accept
   that some building ideas stay scripts-on-landmarks.

## Appendix A: game functions and data the API wraps

Short forms: `gNN:line` is build/gen/golf/golf_NN.c (the decompile, never committed); "Doc" is
upstream/simgolf-native/docs. MAIN is FUN_0040f5c0. Confidence: high = read in the decompile and
cross-checked, medium = decompile or Doc only, low = guess. All of this comes from a read-only
survey for this card; no game was run.

### A.1 Events

| Moment | Function or place | Source | Conf. | Note |
|---|---|---|---|---|
| Tick / frame | MAIN `tick++`, tick at 0x834170 | g01:20257 (fast g01:20259) | high | Gated by flag 0x4 (paused); same block runs FUN_004289e0 (golfers), FUN_00402a40 (staff), FUN_00409bf0 |
| New day | none in the game | symbols.json `date` | high | Derive from tick: `((tick & 1023) * 30 >> 10) + 1` |
| New month | MAIN, `tick & 0x3ff == 0` | g01:20562-20760 | high | Autosave, per-hole stat decay, debt interest (g01:20747); mid-month autosave at `== 0x200` |
| Season / year | MAIN, `tick & 0x1fff == 0`, FUN_0044cff0 | g01:21055, g01:21090; g03:2147 | high | No seasons: March to October, 8 months; year = tick >> 13 |
| Weather | none | task-79 | high | "Rainy/Sunny/Windy" are course-name words only (g01:16120) |
| Golfer arrives | FUN_00421bc0 | call at g01:15525; Doc DECODE_EVENTS_NEEDS 1.1 | high | Claims a slot, mood 3 to 5, hole = 0xFF |
| Golfer leaves | FUN_00426670 | g01:32125; callers g01:36886, 25375, 13612 | high | Clears the slot. Mid-round quit is inline in FUN_004289e0 (g01:36850-36870, flag 0x20000000) |
| Special visitors | FUN_004266b0 | g01:32215 | high | CEO, commissioner, heiress, celebrity; pays an investor or donates a landmark |
| Hole finished | FUN_00427380 | g01:33922; callers 36303, 38096, 38260 | high | Green fee added at g01:34195 (global 0x4c2850) |
| Shot taken | stroke++ inline in FUN_004289e0; plan FUN_00424120 | g01:38089, 38259, 38511; g01:31023 | medium | No single shot function |
| Shot result | FUN_00409620 | g01:6765; calls g01:38257, 38646 | medium | Landing and skill roll; runs only when a photo or skill slot is set |
| Mood reaction | FUN_00467a00 (react), FUN_00469b00 (text) | g04:3474, g04:7099 | high | About 60 call sites; (golfer, type, location) |
| Money changes | no central function; cash 0x571fd4 | about 25 writes, e.g. g01:12025, 20317, 20332, 20747, 24477, 27400, 34195, 34532, 34809 | high | Floating-cash popup FUN_0040c890 (g01:10334) at about 20 of them. Year ledger 0x584210 (stride 0x14); `cash_history` 0x51b388 (500 i16) |
| Wages / upkeep | MAIN, every `1024 / (difficulty + 2)` ticks | g01:20313-20345 | high | Hole upkeep g01:20317, staff wage g01:20332 |
| Building placed | MAIN placement block; record by FUN_0040e000 | g01:27270-27440; g01:11823 | medium | Cost charged at g01:27400; FUN_0040e000 is also used by course generation |
| Building removed | FUN_0040a4e0, FUN_0040e400 | g01:7884, g01:12005 | high | FUN_0040e400 also refunds home sites |
| Terrain painted | MAIN via FUN_00405e30; edit record FUN_0040a130 | g01:27880-27975; g01:7384 | medium | 256-entry edit queue (docs/LIMITS.md) |
| Hole opened | FUN_0040e720 | g01:12422 | high | Logs event 0x20, unlocks buildings |
| Tournament start | FUN_0046c970 | g04:13654; Doc DECODE_TOURNAMENTS 2 | high | Sets flag 0x200000; the offer runs at `tick & 0x1fff == 0x1000` (g01:20809) via FUN_0044fb30(2) |
| Tournament results | FUN_0045a090 | g03:17415; Doc DECODE_TOURNAMENTS 4 | high | Per frame while flag 0x200000 is set; pays cash, fame, accomplishments when the course is empty |
| Employee hired | FUN_0040aa80 -> FUN_00402970 | g01:8395; g00:1580 | high | FUN_00402970 allocates the record |
| Employee fired | FUN_00436c00 case 5 | g02:8535 (fee g02:8583) | high | Clears active byte +0x12 |
| Pro round | FUN_0040f190 | g01:13567 | high | Practice, match or tournament pair |
| Pro skill gain | FUN_00407e00 (via FUN_00409620) | g01:4350 | high | Skills at 0x5a5a04 |
| Accomplishment | FUN_0046e7b0, FUN_0046e810 | g04:15705, 15806 | high | |
| Celebrity home bought | MAIN; FUN_004011e0 | g01:20346-20545; g00:139 | high | Home site state 5, logs event 0xc0 |
| Save | FUN_0040b4a0(name) -> FUN_0040afa0(0); field I/O FUN_0040af70 | g01:8855, 8738, 8719 | high | Autosaves g01:20560, 20568; port hooks `mods_save_written/opened` exist |
| Load | FUN_0040b9b0 -> FUN_0040afa0(1); also FUN_0040b840; courses FUN_0040bbf0 | g01:9481, 9614 | high | |
| New game / setup | MAIN start; FUN_004315e0 (reset), FUN_00470a60 (generate), FUN_0042f7a0 (post-setup) | g01:14904-15450; g02:1806; g04:18414; g02:592 | medium | FUN_0042f7a0 also runs after a load |
| Player messages | FUN_0040cb00(colour, mode, slot) | g01:10517 | high | About 55 callers; text staged in 0x51a068 |
| Milestone log | FUN_0040c6f0(code, arg) | g01:10147 | high | Codes: 0x20 hole opened, 0x40 built, 0x60 match won, 0xc0 home bought, 0xe0 tournament place ... |
| Sound | FUN_004481b0 | already an override (sound.c:31) | medium | About 90 callers; a weak "something happened" signal |

### A.2 Data the queries read

| Data | Where | Note |
|---|---|---|
| Golfer record | 0x5794b8, 256 B each, 152 live slots (0..0x97), 3 scratch, 156 saved | x +0, y +4 (1/1024 tile), flags +0x10, kind +0x18, hole +0x21, strokes +0x22, scorecard +0x23+hole, mood +0xa4 (i16), profile id +0xb6 (1 to 75), skills +0xf0 (16 B). Layout in docs/LIMITS.md |
| Golfer name and looks | profile table 0x4d6088 (0x230 each), roster stats 0x5849e0 (0x2c each) | golfers have no wallet |
| Buildings | 256 x 16 B at 0x58bcb8 | type i16 +0, x +2, y +4, flags +7, buyer +8, value +0xc; names at 0x4c26b0 (stride 0x14) |
| Employees | 64 x 0x4c at 0x585850 | active +0x12, job +0x13 |
| Holes | 20 x 0x208 at 0x575ab0 | par first byte |
| Map | ids 0x5722e8, flags 0x53caf0, elevation 0x5a4998 | index `x * map_w + y` (port/src/port/grid.h); never assume 50 |
| Time and money | tick 0x834170, year index 0x5a6d3c, cash 0x571fd4 ($100 units), fame 0x5787cc, fun 0x59ae78, skill rating 0x541cd8 | all in data/symbols.json |
| Pro | skills 0x5a5a04, pending slot 0x5a59f8, level 0x59b730, scratch slot 0x99 | |
| Other tables | celebrities 100 x 0x25 at 0x55d738; pro golfers 100 x 0x38 at 0x58dd50; event queue 100 at 0x5689e8 | docs/LIMITS.md section 4 |

### A.3 The bridge today (for the console and test tooling)

`ping pause resume step pause_at speed screenshot hdshot hddump pixel move click drag mousedown
mouseup wmove wclick wwheel key keydown keyup text peek poke find call mods_get mods_set mods_status
trace grid perf log quit`, polled on the game thread at each `win_yield` (port/src/port/bridge.c).
There is no event stream and no subscribe, only polling (`watch`). The SDK adds a push channel
(console and `modctl test` read script events and logs from it) in phase 5.
