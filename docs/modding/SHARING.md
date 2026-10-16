# Sharing a mod

[Modding guide](README.md) / Sharing

Ship one mod folder, an installation README and a clear version. Players should be able to install, identify, disable and remove it without replacing their whole mod setup.

## Archive layout

```text
cheaper-snacks-1.0.0.zip
  cheaper_snacks/
    mod.toml
    README.md
    LICENSE
    data/
      buildings.json
```

Extraction into the install's `mods` folder must produce `mods/cheaper_snacks/mod.toml`. Avoid a second wrapper folder such as `mods/cheaper-snacks-1.0.0/cheaper_snacks/mod.toml`.

There is no `modctl pack` or automatic mod downloader in the current tool. Use an ordinary ZIP archive. Do not include someone else's `load_order.toml`, `mods.ini`, `options.ini` or `golf.ini`: those are the player's choices.

## Mod README template

```markdown
# Cheaper Snacks

Version: 1.0.0
Tested with: SimGolf Remastered 0.1.16 on Windows

## Changes
Snack Bar base construction cost becomes $5,000.
Visit income rises from $500 to $600.
Site preparation and building-level adjustments still apply.

## Install
Extract cheaper_snacks into the mods folder beside the game executable.
Enable Cheaper Snacks in the Mods screen, apply, and restart.

## Compatibility
Changes buildings.snack_bar.cost and buildings.snack_bar.income.
Another mod setting either field can change the result through load order.

## Saves and removal
Back up your save and its companion files before testing.
Disable the mod, apply and restart before removing its folder.
Removing it restores the original table values; it does not refund purchases
or reverse money already earned while it was enabled.

## Support
Report the game version, mod version, resolved load order and relevant log lines.
```

Replace the compatibility and save sections with facts you have tested. State dependencies, required restart, new-course requirements, language/encoding limits and any irreversible effects. Include a screenshot only when it helps show the actual change, captured from the version you shipped.

## Save compatibility

The `.sve.mods.toml` companion records mod IDs and versions and binds to the save by size and CRC-32. It enables warnings; it does not store the mod itself, replay old settings or migrate content. Copy it with the save. Other companion files, such as camera metadata, should travel with the save too.

Table-only mods do not by themselves require a new save format, but their effects can persist in a course's finances or state. Expanded maps and staff are separate engine features that can add format extensions. Original executables and older builds may not read those saves. See [staff save compatibility](STAFF.md) and test the actual save you intend others to use. Disabling mods cannot remove format extensions already written to a save.

## Before uploading

- Install the ZIP into a clean test mods folder and verify its actual extracted layout.
- Check the manifest, data, conflicts, enabled behavior, disabled behavior and save/reload.
- Keep only your mod's files. Exclude game data, saves, extracted sprites, original sounds, mapped images, binaries derived from the game and decompiler output.
- Give your original contributions a license, and credit dependencies. The port's MIT license does not grant permission to redistribute SimGolf's assets.
- Use a public handle in authorship fields. Remove personal paths, private logs, credentials and other people's data.
- Publish the tested mod version and state known limits. If distributing a custom program build, include its source changes and the relevant license notices.

Host the mod in your own repository or propose it through a pull request to this project. Source contributions follow [CONTRIBUTING.md](../../CONTRIBUTING.md).
