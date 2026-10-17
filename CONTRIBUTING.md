# Contributing to SimGolf Remastered

Forks, fixes, new features, mods and bug reports are all welcome.

## Easiest ways to help

- **Report a bug or suggest an idea:** open an [issue](../../issues). Say what you did, what you saw, and attach a screenshot and your `golf.log` (next to `golf.exe`) if the game misbehaved.
- **Make a mod without building anything:** prices, wages, personalities, difficulty and more live in plain text tables. See [the modding guide](docs/modding/README.md) and the examples in `mods-examples` inside the release zip. Share a mod as a pull request into `mods/`, or as its own repo.

## Modding and source development

The [modding guide](docs/modding/README.md) covers working text-file mods,
recipes, manifests, data fields, testing and packaging. Start with
[Your first mod](docs/modding/FIRST_MOD.md); it needs no compiler.

For engine changes, read [Source development](docs/modding/SOURCE.md).
It includes a tested standalone build of the mod validation helper and
explains the current gaps in the public full-game build. The public export
omits internal test sources and some build/QA helpers, so do not treat the
older upstream instructions as a verified clean-build recipe for this fork.

## Rules for pull requests

1. **No game files, no decompiler output, no upscaled art** in commits (nothing from `game/`, `build/`, `.pcx`, `.flc`, `.sve`, `golf.bin` and so on). The release tools refuse them anyway.
2. **Change behaviour through overrides**, never by editing generated code: a hand-written function in `port/src` marked `// @override FUN_xxxxxxxx`, or a patch entry in the translator json.
3. **Keep the original reachable:** every change gets a switch in `data/mods.ini` (and an Options row if players will want it), so the 2002 behaviour is always one setting away. Tuning numbers go in `data/`, not in code.
4. **Show it works:** say how you tested it, and add a screenshot or a short clip for anything players will see. If game logic is involved, check the game plays the same with your switch off.
5. One feature or fix per pull request, with a short plain description.

## How pull requests get in

Fork the repo, make a branch, and open a pull request against `main`. The maintainer reviews it, tests it with the full test suite, and brings it into the development line by hand, so your commit may land in a combined release commit. You are credited by name in the CHANGELOG and the release notes of the release that includes it.

## What is planned

Open an issue before starting a large feature so its scope and compatibility
can be discussed. The [SDK design](docs/modding/SDK_DESIGN.md) is a historical
proposal, not a list of implemented mod APIs.

## Licence

Code is MIT (see LICENSE), like the upstream browser port it builds on. SimGolf itself and its art belong to Firaxis, Electronic Arts and Take-Two; this project ships none of their files.
