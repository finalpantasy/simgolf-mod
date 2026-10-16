# Modding SimGolf Remastered

Change prices, wages, difficulty and golfer dialogue with text files. Use C or C++ for changes that need new game logic. This guide covers the Windows version of [SimGolf Remastered](../../README.md).

**Start with [Your first mod](FIRST_MOD.md).** It makes the Snack Bar cheaper, takes two small files, and needs no compiler.

## Choose a path

| You want to... | Read |
|---|---|
| Make and install a working mod | [Your first mod](FIRST_MOD.md) |
| Change wages, upkeep or golfer dialogue | [Recipes](RECIPES.md) |
| Customize Living Club or understand optional content | [Optional content](CONTENT.md) |
| Define metadata, dependencies and load order | [Manifest and profiles](MANIFEST.md) |
| Find table names, fields, limits and operations | [Data reference](DATA.md) |
| Change game logic or work on the engine | [Source development](SOURCE.md) |
| Check a mod and compare its on/off behavior | [Testing](TESTING.md) |
| Package a mod for other players | [Sharing a mod](SHARING.md) |
| Diagnose a missing mod, unchanged value or conflict | [Troubleshooting](TROUBLESHOOTING.md) |

## What works today

| Area | Current support |
|---|---|
| Game data | JSON patches for the ten tables listed in the [data reference](DATA.md#tables) |
| Personalities | New personality rows and replacement thought lines for existing thought events |
| Existing features | Settings read by the game, such as UI choices and the staff limit |
| Mod management | Folders, manifests, profiles, dependency resolution, load order and conflict reports |
| Save tracking | A companion file records the loaded mods and versions |
| New game logic | Source changes compiled into a custom build |
| General asset replacement | No general drop-in asset loader for arbitrary mod folders |
| Lua, runtime hooks and DLL plugins | Proposed, not a supported mod-author API |

Declaring `[touches.assets]` or `[touches.hooks]` records a conflict claim. It does not load an image, run a script or install a function hook. Likewise, a setting does nothing until game code reads that key. The [SDK proposal](SDK_DESIGN.md) describes future work, not features you can install now.

## Keep a separate test install

Use a copy of your installation and disposable saves. Keep the mod off in your normal profile until you have tested it. A mod that changes costs or difficulty should require the player to opt in.

You need your own game files to play or build the game. Share your original mod files and source changes, not the original game, extracted data, decompiler exports or derived executable images.

## Further reference

- [Core implementation](CORE.md): resolver rules, settings API and mod records.
- [Mods screen](MANAGER.md): player controls and optional manifest metadata.
- [Staff and save formats](STAFF.md): expanded staff records and compatibility limits.
- [Engine limits](../LIMITS.md): research notes for changes to fixed capacities.
- [Contributing](../../CONTRIBUTING.md): bug reports and source pull requests.

These instructions were checked against the public source at `639a3e3` (Windows release 0.1.17). They distinguish shipped behavior from design proposals. See [testing](TESTING.md) for what each check can establish.
