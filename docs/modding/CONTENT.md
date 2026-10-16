# Optional content in 0.1.17

[Modding guide](README.md) / Optional content

The JSON data-patch layer is not the only content system in Remastered. Release 0.1.17 adds Living Club, an authored roster and optional environment features. They have separate loaders and switches. Putting these files in a mod's `data` folder does not make the ordinary table loader merge them.

The [current switch table](CORE.md#optional-additions-in-0117) lists their defaults. Player Options can override manifest and INI settings; restart after changing startup content.

## Living Club dialogue and stories

[data/living_club.json](../../data/living_club.json) contains authored personalities, story arcs, trait dialogue, event groups, context lines and tuning. It is distinct from the extensible `data/tables/personalities.json` table used in the [personality recipe](RECIPES.md#add-a-golfer-personality).

The runtime normally finds the content beside the selected tables directory, with a release-data fallback for updated installations. For an isolated content experiment, copy the whole shipped file to a private test location, edit that copy, and launch your test installation with an explicit override. For example, in PowerShell from that installation:

```powershell
$env:SIMGOLF_LIVING_CONTENT = 'C:/simgolf-tests/living_club.json'
& '.\Sim Golf Remastered.exe'
Remove-Item Env:SIMGOLF_LIVING_CONTENT
```

Use your actual file path. This changes the selected content file for that launch; it does not create a per-mod merge or an installer. Read `golf.log` for the `living-club:` load result. Test with the Living Club feature enabled.

The [content schema](../../data/living_club.schema.json) is a full JSON Schema document. Its contract includes exactly 25 personalities, 1 to 16 stories, five trait banks, version 1, and required tuning and context sections. Dialogue banks contain printable ASCII lines of at most 110 characters. Preserve IDs, array order and cross-references during a wording-only experiment; personality and story identity matter to persistent state.

Use a standards-compliant JSON Schema validator for that schema. The data-table validator deliberately supports a smaller schema subset and `modctl_core data check` does not validate this separate content file. The runtime loader also performs its own structural checks; schema validation does not replace a startup and save/reload test.

Relevant implementation:

- [living_club.c](../../port/src/mods/living_club.c): content parsing, dialogue selection and story state.
- [living_runtime.c](../../port/src/mods/living_runtime.c): settings, content path, game events and save integration.
- [data_runtime.c](../../port/src/mods/data_runtime.c): Living Club dialogue is considered before ordinary table personalities.

To test a table personality independently, set `living=0` under `[golfers]` in the test install's `mods.ini`, check that no higher-priority Options value enables it, and restart. Living Club reactions are separately opt-in through `golfers.reactions`; changing text does not require enabling mood changes.

## Authored roster

[data/roster/new-golfers.json](../../data/roster/new-golfers.json) is authored roster input. The running game uses compiled records from [new_roster_data.h](../../data/roster/new_roster_data.h), included by [roster.c](../../port/src/port/golf/roster.c). Editing the JSON in a release installation alone does not regenerate those records or replace portraits.

Treat roster changes as a source-build task until a public generation and packaging workflow is available. Keep original slot meanings intact and test profile identity, progression, portraits and saves. `gameplay.new_roster=0` selects the original roster path. Authored visitor skills are a separate option, off by default.

## Environment and terrain

The shipped data includes [weather-climate.csv](../../data/weather-climate.csv), [weather-surfaces.csv](../../data/weather-surfaces.csv), [weather-air.csv](../../data/weather-air.csv), [weather-atmosphere.csv](../../data/weather-atmosphere.csv) and [lighting-windows.csv](../../data/lighting-windows.csv). Their header comments describe the columns. Climate weights are authored gameplay tendencies, not measured weather forecasts.

These are feature-specific tuning inputs, not extra tables accepted by `modctl_core data check`. Inspect the relevant weather and rendering consumers before changing ranges or adding entries. Keep experiments in a separate install, retain the shipped inputs, and test each affected season, surface or view. There is no general CSV hot-reload or per-mod merge contract.

Lighting/weather presentation (`environment.enabled`) and weather gameplay effects (`environment.gameplay`) are separate opt-in settings. The optional Terrain Mod uses a separate art pack; arbitrary files under `[touches.assets]` still do not become replacement art automatically.

## Save rules

The size/CRC-bound `.sve.mods.toml` companion records a versioned `[rules]` table for authored visitor skills, Living Club reactions and weather gameplay. It documents those rules at save time. Loading a save does not silently apply its recorded settings to the current player configuration. Include the companion in compatibility tests and explain mismatches to players.
