# Manifest and profiles

[Modding guide](README.md) / Manifest and profiles

Each mod is a folder containing `mod.toml`. The loader scans built-in mod folders first, then other folders in name order. Use the same lowercase identifier for the folder and the manifest's `id`.

## Manifest template

```toml
id = "my_mod"
name = "My Mod"
version = "1.0.0"
authors = ["YourHandle"]
description = "Say exactly what changes."
platform = "^1.0"
game = ">=1.2.0"
tags = ["economy"]
# icon = "preview.png"
# homepage = "https://example.com/my-mod"
# changes_saves = true

[touches.data]
"economy.game.hole_upkeep" = "set"
```

`id` and `version` are required. Use a semantic version such as `1.0.0`, not a label such as `latest`. The scaffolding tool accepts identifiers of 1 to 63 characters, beginning with a lowercase letter or digit and continuing with lowercase letters, digits, `_`, `-` or `.`.

Use `changes_saves = true` when saves depend on what your mod adds. It is a warning badge, not an automatic save converter or a compatibility guarantee. Optional icons stay inside the mod folder; the Mods screen accepts images up to 2048 pixels. See [Mods screen](MANAGER.md) for links and filters.

## Dependencies and ordering

```toml
[dependencies]
course_rules = "^1.1"

[optional_dependencies]
extra_dialogue = "~1.2"

[incompatibilities]
other_economy = "*"
```

Required dependencies must be installed and compatible. The resolver can enable an installed dependency automatically, unless a player's `[mods]` switch explicitly turns it off. It does not download dependencies. Optional dependencies add an ordering edge when loaded and in range. Incompatible mods cannot both load; the later profile entry is rejected.

Ordering hints are top-level keys, placed before table headers:

```toml
load_after = ["other_mod"]
load_before = ["another_mod"]
provides = ["shared_feature"]
```

Hints do not install or enable the named mods. `provides` supplies virtual identifiers for dependency resolution and `mods_enabled()` queries. Do not advertise a feature your mod does not implement.

Version ranges include `*`, exact versions, comparisons, `^1.2`, `~1.2`, `1.x` and `1.2.x`. Space- or comma-separated terms all apply; `||` separates alternatives. Prerelease matching has additional restrictions. The [core reference](CORE.md#modtoml) documents them.

## Profiles

```toml
active = "default"

[profiles.default]
mods = ["hd_presentation", "camera_controls"]

[profiles.economy_test]
mods = ["hd_presentation", "camera_controls", "my_mod"]
```

The `vanilla` profile always exists and has no loaded mods. The player's list is an ordering preference; dependencies and ordering hints take precedence. Resolution uses a stable topological sort. Do not rely on a cycle being silently resolved: fix its reported dependency path.

A player's `mods.ini` can override the profile:

```ini
[mods]
my_mod=0
```

This explicitly turns the mod off, even when the profile or another mod requests it. Changing enabled mods or their order requires a restart. The Mods screen's Apply button writes the profile; it does not reload running game code or data tables.

## Settings and data are different

`[settings]` supplies keys read by game code. `[patch]` combines such settings numerically or as lists. Neither section creates new game behavior merely by naming a key.

```toml
[settings]
"ui.tags" = "always"

[patch]
"example.multiplier" = { op = "mul", value = 1.2 }
```

`ui.tags` has a game consumer. `example.multiplier` is deliberately illustrative and has no effect unless you implement a consumer. To change a Snack Bar price, use `data/buildings.json`, not a made-up setting such as `buildings.snack_bar.cost` in `[settings]`.

Settings precedence, highest first:

1. Live values set through `mods_set()`.
2. Player Options in `options.ini`, loaded over `mods.ini` in the same store layer.
3. Player values in `mods.ini`.
4. Loaded manifests' settings and patches, folded in load order.
5. `golf.ini`, exposed as `golf.<key>`, or the caller's fallback.

The CLI resolver does not reproduce the game's separate `options.ini` loading step. Account for that when comparing a CLI report with a running game.

## Touches and conflicts

Declare each known change under `[touches.data]`, `[touches.hooks]` or `[touches.assets]`. For example:

```toml
[touches.data]
"buildings.snack_bar.cost" = "set"
"employees.groundskeeper.wage" = "mul"
```

A later exclusive operation such as `set` wins a reported conflict. Composing operations such as `add` and `mul` fold in resolved order. They are not interchangeable: applying `add 2` then `mul 3` differs from the reverse order. A warning is not evidence that the chosen winner suits the player's intended setup.

Touch declarations describe changes; the JSON file or source implementation performs them. The registry's hook and asset kinds do not provide a runtime hook or asset loading API. See [current support](README.md#what-works-today).

The [core reference](CORE.md#how-the-resolver-decides) lists resolution steps and diagnostic codes. [Testing](TESTING.md) shows how to inspect the resolved order and actual data changes.
