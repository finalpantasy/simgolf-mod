# Testing a mod

[Modding guide](README.md) / Testing

Use three checks: a valid manifest, valid data changes, and the intended result in the running game. Each catches different failures.

## Check without command-line tools

1. Use a separate install and a copy of the save. Keep the original save and its companion files elsewhere.
2. Enable the mod in a test profile, apply and restart.
3. Read `golf.log`: confirm the selected profile, resolved mod order, data changes and absence of errors.
4. Perform the action your mod changes. Use a new course for starting cash or generation settings, a charge boundary for upkeep, and a relevant thought event for dialogue.
5. Disable the mod, restart and repeat with the same starting conditions.
6. Save, close the game, reopen and load the test save. Check the result and the mod-record warnings. If your mod affects courses exported separately, test that path too.

For repeatable automated runs, set `SIMGOLF_MUTE=1`; do not prove sound by playing it through the desktop speakers. Capture evidence from the exact build and profile you intend to distribute. A screenshot proves appearance at that moment, not save compatibility or simulation correctness.

## Get the validation helper

The standard release package does not include `modctl_core.exe` or the Python front end. In a source checkout, [build the helper independently](SOURCE.md#build-only-the-mod-validation-helper), then set this in Git Bash:

```bash
export MODCTL_CORE="$PWD/build/modctl-only/modctl_core.exe"
```

The Python tool's global options go **before** the command. For example, `--mods-dir` is global; `--profile` belongs to commands that accept it. `--json` is accepted anywhere.

## Validate and resolve

These examples use a scratch mod collection under `build/mod-test/mods`; replace it with your test install's absolute mods path when appropriate.

```bash
mkdir -p build/mod-test/mods
python tools/modctl/modctl.py --mods-dir build/mod-test/mods new cheaper_snacks
# Add the tutorial's data/buildings.json and touch declarations.
python tools/modctl/modctl.py --mods-dir build/mod-test/mods validate build/mod-test/mods/cheaper_snacks
python tools/modctl/modctl.py --mods-dir build/mod-test/mods profile create test
python tools/modctl/modctl.py --mods-dir build/mod-test/mods profile use test
python tools/modctl/modctl.py --mods-dir build/mod-test/mods enable cheaper_snacks
python tools/modctl/modctl.py --mods-dir build/mod-test/mods check --profile test
python tools/modctl/modctl.py --mods-dir build/mod-test/mods order --profile test
python tools/modctl/modctl.py --mods-dir build/mod-test/mods conflicts --profile test
```

`validate` checks the manifest. `check` resolves manifests, versions, dependencies, settings and declared touches. Neither validates the contents of `data/*.json`. A conflict is usually a warning, so inspect warnings as well as the exit code.

Profile edits rewrite `load_order.toml`; use a scratch copy if you want to preserve comments and formatting in a hand-edited file. To model a player's explicit switches, add `--ini path/to/mods.ini` to `check`, `order` or `conflicts`. The CLI does not load `options.ini` like the game does.

## Validate data against the original image

For a source build, the native post-build step puts the original image at `build/native/image/golf.bin`. A prepared release install has `image/golf.bin` beside its executable. This is a mapped image, not the raw PE executable. Do not substitute `game/golf_nocd.exe` for it.

```bash
build/modctl-only/modctl_core.exe data check \
  --tables data/tables \
  --image build/native/image/golf.bin \
  --mods-dir build/mod-test/mods --profile test --text

build/modctl-only/modctl_core.exe data show buildings \
  --tables data/tables \
  --image build/native/image/golf.bin \
  --mods-dir build/mod-test/mods --profile test
```

Replace the image path with the one from your test install if you built only the helper. Use the table definitions matching that installed build. Run with `--no-mods` for the unmodified baseline. `data check` reports file/line errors, changed values and conflicts; it does not execute the game.

You can inspect schema coverage without game files:

```bash
build/modctl-only/modctl_core.exe data coverage --tables data/tables
```

To inspect base values, create the output folder first:

```bash
mkdir -p build/data_base
build/modctl-only/modctl_core.exe data dump \
  --tables data/tables --image build/native/image/golf.bin --out build/data_base
```

These dumps come from your game. Keep them private and ignored by git. Check a save's mod record with:

```bash
python tools/modctl/modctl.py --mods-dir build/mod-test/mods \
  save-info "path/to/Test Course.sve" --profile test
```

## Compare on and off

Use `--profile vanilla` to choose an empty profile, but remember that player INI switches can still enable mods. Use `--no-mods` for a stricter comparison: it selects vanilla and ignores both `mods.ini` and `options.ini`. It does not undo source-level fixes or turn an expanded save into an original-format save.

Use the same initial save, random seed, difficulty, course size and observation interval on both sides. The existing `SIMGOLF_SEED` override controls new-game seeding; it does not reset every piece of state in an already running game. Do not compare unrelated random courses and attribute every difference to your mod.

Record your mod version, Remastered version or source commit, resolved order, relevant settings, expected result and observed result. Check with other likely mods installed, especially any that touch the same keys.

## Internal suite versus public checkout

The development repository runs `bash tools/check.sh`. Its exit codes are 0 for pass, 1 for failure and 3 for cannot run. Read skipped stages: a pass with no generated game or native runtime is not a gameplay pass.

The public 0.1.17 tree does not contain that script or the complete internal `tests/` tree. Do not invent a successful full-suite result. For a public-source contribution, provide the commands you could run, their outputs, the gameplay comparison, and any build/test blockers. The maintainer can run the internal suite during integration.
