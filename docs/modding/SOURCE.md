# Source development

[Modding guide](README.md) / Source development

Use source changes when the existing data fields and settings cannot express your feature. These changes produce a custom game build. They are not DLL plugins installed through `mod.toml`.

## Source map

| Path | Responsibility |
|---|---|
| `port/src/mods/` | Manifest parsing, dependency resolution, settings, data patches and save mod records |
| `port/src/mods/tool/` | Offline resolver and data commands |
| `port/src/port/golf/` | Hand-written overrides of original game functions |
| `port/src/port/jgld/` | Overrides of the original graphics library |
| `port/src/port/` | Runtime, Win32 compatibility, input, presentation and UI integration |
| `port/src/platform/` | Platform rendering and related backends |
| `port/tools/translate/` | Translation rules and small patches applied during generation |
| `data/tables/` | Table schemas, original row IDs and field bindings |
| `data/mods.ini` | Documented player settings and off switches |
| `mods/builtin/` | Built-in feature manifests |
| `build/gen/` | Locally generated game C and images; never edit or distribute |

The original game uses 32-bit addresses, pointers and layouts. The native target is **x86**, even on 64-bit Windows. It reserves the original image ranges and links the host executable elsewhere. Preserve integer widths, signedness, record strides and calling conventions. A descriptive function name does not establish its ABI or all of its callers.

## Build only the mod validation helper

This helper needs no original game files, Ghidra, SDL or generated game C. You need CMake, Ninja, LLVM with `clang-cl` and `lld-link`, and Visual Studio Build Tools with MSVC x86 libraries and a Windows SDK. Install these through their official distributions.

Create `build/modctl-src/CMakeLists.txt` with this content, relative to the repository root:

```cmake
cmake_minimum_required(VERSION 3.20)
project(SimGolfModTools C)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CORE semver manifest resolve session store modmgr saverec relocate json data)
set(SOURCES)
foreach(NAME IN LISTS CORE)
  list(APPEND SOURCES "${REPO}/port/src/mods/${NAME}.c")
endforeach()
add_executable(modctl_core ${SOURCES}
  "${REPO}/port/src/mods/tool/modctl_core.c"
  "${REPO}/port/src/mods/tool/modctl_data.c"
  "${REPO}/port/src/compat/msvc/dirent.c"
  "${REPO}/port/third_party/tomlc99/toml.c")
target_include_directories(modctl_core PRIVATE
  "${REPO}/port/src" "${REPO}/port/third_party"
  "${REPO}/port/src/compat/msvc")
target_compile_definitions(modctl_core PRIVATE
  _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
target_compile_options(modctl_core PRIVATE /clang:-std=gnu11)
```

In Git Bash, from the repository root, set your actual LLVM location and build:

```bash
export SIMGOLF_LLVM="C:/simgolf-tools/llvm"
cmake -S build/modctl-src -B build/modctl-only -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/port/cmake/clang-cl-x86.cmake"
cmake --build build/modctl-only
python tools/modctl/modctl.py --core build/modctl-only/modctl_core.exe list
```

Keep CMake and Ninja on `PATH`. In the [testing commands](TESTING.md), use `--core build/modctl-only/modctl_core.exe` or set `MODCTL_CORE` to that path. Do not commit the generated helper or build folder.

## Full game build: prerequisites and current gaps

The public source includes translation and native build code, but **the 0.1.16 source snapshot is not a self-contained, tested clean-build kit**. In particular:

- Native CMake files reference internal `tests/` sources that are absent from the public tree. A clean configure can fail on missing test sources.
- `tools/check.sh`, the native hidden-desktop runner and several scripts referenced by older documents are not exported.
- Ghidra preparation scripts assume pre-existing analyzed projects and prepared exports. The upstream build notes explain the process, but are not a complete Windows bootstrap script for this fork.

Do not delete test declarations merely to call the full build verified. For a reproducible engine-development setup, open a build-support issue with your source commit, tool versions, failing command and first meaningful error. The standalone helper above bypasses these game-build dependencies without changing the game build.

The full pipeline, once those prerequisites are available, is:

1. Supply your own installed game under ignored `game/`, including the matching v1.02 executable as `game/golf_nocd.exe` and `jgld.dll`.
2. Analyze/export both modules using the [upstream porting notes](../../port/docs/porting.md) and `port/tools/ghidra/`. Keep the export under an ignored directory. Do not download replacement executables as part of the build.
3. Set tool locations before sourcing `tools/env.sh`. Relevant variables include `TOOLS_EXT`, `SIMGOLF_LLVM`, `GHIDRA_HOME`, `JAVA_HOME_GHIDRA`, `GHIDRA_HEADLESS`, `W` and `EMSDK_DIR`. These are machine paths, not files to commit.
4. Translate the prepared exports into an owned output directory:

```bash
export PORT_EXPORTS_OVERRIDE="$PWD/build/ghidra_work"
bash tools/translate_gen.sh
```

The export root needs `port/` and `port_jgld/`, with `functions.jsonl`, `types.h`, `image.bin` and the other expected export products, plus the root-level externals files. The [translator driver](../../port/tools/translate/run.sh) defines the exact inputs. Reading an existing export does not require another Ghidra run.

5. After resolving the public-build gaps, `bash tools/build_native.sh` configures the native preset and builds the default ALL target. Use the full build: `cmake --build build/native --target golf` alone omits the separate `ui_font_files` target and can leave modern UI text blank.
6. Run `build/native/golf.exe` with the original game folder as its working directory and the configured mod/data paths. Preserve the companion images, SDL library, fonts and UI files next to the executable. A lone executable is not a complete runnable build.

The [architecture notes](../ARCHITECTURE.md) explain the browser/native split. Browser tooling is inherited from upstream; a successful native test does not verify a browser change.

## Override a function

Start with the original address and cached function body from your own export. Follow callers and state writers before replacing behavior. Check the exact target module and version.

Existing overrides are examples of the mechanism. [seed.c](../../port/src/port/golf/seed.c) replaces the seeding function at `FUN_00467110`:

```c
// @override FUN_00467110
```

The translator scans hand-written sources under `port/src`, suppresses the generated definition for that symbol, and takes the override's prototype from the following definition. Preserve the expected signature. Search for an existing override first; edit that implementation rather than defining the same symbol twice.

After changing an override marker or signature, retranslate and rebuild. A normal C rebuild alone can leave an old generated definition or prototype behind. Never repair that by editing `build/gen`.

## Use a translator patch for a small substitution

The `patches` object in [golf.json](../../port/tools/translate/golf.json) maps a function name to pairs of original and replacement text. For example, this describes the structure, not a patch to paste unchanged:

```json
{
  "patches": {
    "FUN_REPLACE_WITH_VERIFIED_ADDRESS": [
      ["exact original expression", "replacement expression"]
    ]
  }
}
```

Match the actual export, keep the replacement narrow, and inspect the regenerated function to confirm the intended occurrence changed. Retranslate after every patch change. Keep structural changes in an override when text substitution becomes fragile.

## Add a setting or data field

Use `mods_enabled("feature_id")` to gate a named feature and `mods_get_int()` / `mods_get_num()` for tuning values. Return the original behavior when disabled. Document the default, valid range, restart requirements and save effects in `data/mods.ini` and the feature's manifest. Arbitrary manifest keys have no effect without a reader.

For a data field, use `x-image` when it maps to an existing in-memory table field. Use `x-code` plus `MDATA` / `MDATA_AT` patches when the original value is a code literal. [Adding a table](DATA.md#adding-a-table) covers the schema and binding steps. Verify both the modified value and the zero-mod baseline.

Keep platform code behind the existing runtime/platform layers. Raising an array limit requires auditing allocation, pointers, loops, UI iteration, serialization and failure paths, not just changing a maximum. See [staff capacity](STAFF.md) for a worked technical record of that scope.
