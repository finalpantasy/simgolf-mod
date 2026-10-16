# Your first mod

[Modding guide](README.md) / Your first mod

Make the Snack Bar cost $5,000 instead of its $15,000 base price, and raise its per-visit income from $500 to $600. You need an installed copy of SimGolf Remastered and a plain text editor. You do not need Python or a compiler.

## 1. Find the right folder

Open the folder containing `Sim Golf Remastered.exe` and `golf.ini`. Put the mod in its `mods` folder, beside `builtin`. Do not put it inside the original `game` folder.

Close the game before editing. In Explorer, enable **View > Show > File name extensions** so the files do not accidentally end in `.txt`.

Create this layout:

```text
mods/
  cheaper_snacks/
    mod.toml
    data/
      buildings.json
```

## 2. Describe the mod

Save this as `mods/cheaper_snacks/mod.toml`:

```toml
id = "cheaper_snacks"
name = "Cheaper Snacks"
version = "1.0.0"
authors = ["YourHandle"]
description = "Lower Snack Bar construction cost and raise its visit income."
platform = "^1.0"
game = ">=1.2.0"
tags = ["economy"]

[touches.data]
"buildings.snack_bar.cost" = "set"
"buildings.snack_bar.income" = "add"
```

The `game` version describes the original executable: v1.02 is represented as `1.2.0`. It is separate from the Remastered release number and the mod platform version.

## 3. Change the data

Save this as `mods/cheaper_snacks/data/buildings.json`:

```json
{
  "set": {"snack_bar.cost": 50},
  "add": {"snack_bar.income": 1}
}
```

Building money values use units of $100. `50` means $5,000. `add` increases the original income by $100. The game can add level and site-work costs to construction, so a placement quote is not always the table's base price.

JSON requires double quotes, no comments, and no trailing comma. Save both files as UTF-8 plain text.

## 4. Enable it in a test profile

Start the game and open **Mods** from the start menu. Duplicate your usual profile, name the copy `snack_test`, and enable **Cheaper Snacks** in that copy. Apply the changes, exit, and restart the game. A profile change takes effect at startup.

If you prefer to edit `mods/load_order.toml`, add a separate profile without removing the existing ones:

```toml
[profiles.snack_test]
mods = ["cheaper_snacks"]
```

Change the file's existing top-level `active` line to `active = "snack_test"`. There must be only one `active` line, before the profile sections. This minimal profile loads only the example mod; duplicating a profile in the Mods screen keeps your usual presentation mods too.

## 5. Check the result

Open `golf.log` beside the executable after startup. Look for the mod in the resolved load order and `data:` lines for `buildings.snack_bar.cost` and `buildings.snack_bar.income`. In a clean test configuration, they should resolve to `50` and `6`. Fix any reported errors before continuing.

Start a disposable Sandbox course so the Snack Bar is available. Compare its cost with the mod enabled and disabled, using the same building level and an equivalent site. Let golfers visit it to check income separately. Successful parsing alone does not prove the income path works in play.

Disable the mod, apply, restart and repeat the comparison. Keep your normal saves out of this test. If nothing changes, follow [Troubleshooting](TROUBLESHOOTING.md).

## 6. Share it

Zip the `cheaper_snacks` folder so extraction into another player's `mods` folder produces the layout above. Include a short README with installation, tested version, known conflicts and removal instructions. Follow the [sharing checklist](SHARING.md).

For another example, try [a golfer personality](RECIPES.md#add-a-golfer-personality). For offline validation and command-line profile management, see [Testing](TESTING.md).
