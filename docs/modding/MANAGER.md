# Mods screen

[Modding guide](README.md) / Mods screen

The in-game mod manager is a Frosted Mint page to turn mods on and off, order them, see
conflicts and missing dependencies, and keep profiles. It is the screen version of `tools/modctl`:
every decision comes from the platform's resolver (`mods_resolve`), nothing is re-implemented in the UI,
and what it saves is the same `mods/load_order.toml` modctl writes.

## Opening it

- Start menu: the **Mods** pill, left of Options.
- Options page (wrench menu's "Options..." in a running game, or F12): the **Mods** button in its header.
- **Shift+F12** anywhere (rebindable in Options > Controls, action "Open the Mods screen").
- Esc closes it. With changes you have not applied it asks: Apply and close, Discard, or Keep editing.
- A running game stands still while it is open.

## What it shows

Per mod: picture (`icon` in mod.toml, else `preview.png` in the mod folder, else a letter), name, version,
author, one line of description, load position, an on/off switch, up and down buttons and a drag handle.
Badges: **Conflict**, **Missing dependency**, **Incompatible**, **Needs restart**, **Changes saves**,
**Needed by another mod**, **Broken mod.toml**.

Organise: search box (all words, any of name, id, author, description, tags), sort (Name, Author, Newest,
Load order, Enabled first), chips (All, Enabled, Disabled, Conflicts, and one chip per tag).
The Conflicts chip lists every mod with a conflict, a missing dependency, an incompatibility or a broken
mod.toml. Dragging the handle (or the up/down buttons) changes the order in the profile; the mod that loads
later wins a conflict. Keys: Up/Down select, Enter toggles, typing searches.

Details of the selected mod: full description, what it needs (with ready / turned off / not installed), what
it does not work with, the resolver's conflict lines (key, who sets what, who wins) and notes, what it
changes (`[touches]`), **Open folder**, and its links (`homepage`, `[links]`; http and https only).

Profiles: the pill at the top lists them (vanilla first, read only, always empty). **New** (empty, or a copy
of the mods that are on), **Duplicate**, **Rename**, **Delete**. Names are 1-32 letters, digits, `-` or `_`.

## Nothing is applied silently

Every change is a draft in memory. **Apply changes** writes `load_order.toml` (the active profile and every
profile's list); **Undo changes** drops the draft. Mods load at start, so after Apply the footer says the
change takes effect after a restart (also when a mod is already running with another order). mods.ini
switches are read like the game does at start, so the screen shows what the next start will load.

## mod.toml keys it reads (all optional)

```toml
tags = ["economy", "visual"]          # categories, filter chips
icon = "icon.png"                     # inside the mod folder (default preview.png), up to 2048 px
homepage = "https://example.com"
changes_saves = true                  # saves made with it need it to load right: shows a badge
[links]
"Report a problem" = "https://example.com/issues"
```

## Code and internal tests

Test and gallery paths below describe the internal development suite; some
are absent from the public export. See [Testing](TESTING.md).

- `port/src/mods/modmgr.c/.h`: the model (scan, draft, resolve, view, edits, profiles, apply). No drawing.
- `port/src/port/ui/ui_mods.c/.h`: the page (kit components). `port/src/port/mods_frosted.c`: icons,
  keys, folder and link opening (skipped in automated runs: `SIMGOLF_MUTE` or `SIMGOLF_NO_SHELL`), layout log
  (`SIMGOLF_MODS_LOG=1`). It rides on the Options layer, the presenter's top layer.
- `tests/ui/test_modmgr.c` (check.sh stage "mod manager: unit tests"): search, filters, sort, order and
  profile edits, apply. `tests/native/modsui.sh`: the hidden, muted run on a private copy of the mods folder.
- Gallery pages `mods-*` at 1080p and 4K (`ui_gallery --only mods`).
- Sample mods: `mods/sample_cheap_bar` (a data tweak), `sample_bar_tips` (needs it), `sample_pricey_bar`
  (conflicts with the cheap one), `sample_fancy_menu` (missing dependency, changes saves). All off by default.
