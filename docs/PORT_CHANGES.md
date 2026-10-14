# Changes to port/ since upstream

port/ is RichardAtCT/simgolf-web at f985773 (see docs/UPSTREAM.md). Every
change we make inside port/ is listed here so it can be diffed against upstream
or offered back. Our own scripts outside port/ (tools/) are not listed.

## task-1: Windows build of the browser version

All are portability edits: with no environment set, each script behaves exactly
as upstream (macOS paths).

| File | Change | Why |
|---|---|---|
| tools/ghidra/prepare-golf.sh, prepare-jgld.sh, export-parallel.sh, reexport-golf.sh | `H=${GHIDRA_HEADLESS:-<homebrew path>}` | analyzeHeadless location is configurable. On Windows it is tools/ghidra_headless.sh (the .bat launcher splits arguments on `,` and `=`, breaking `purges=a,b` and `from=1`). |
| tools/ghidra/prepare-golf.sh, prepare-jgld.sh, reexport-golf.sh | `$root/build/gen` becomes `${PORT_GEN:-$root/build/gen}` | Our generated code lives in build/gen at the project root, not port/build/gen. |
| tools/ghidra/prepare-golf.sh, prepare-jgld.sh | VtablePurges writes `purges_*.tmp`, then `mv` to `purges_*.txt` | Each prepare script reads the other's purges file; the atomic rename lets both run in parallel (saves the whole jgld prepare time). tools/ghidra_import.sh writes both files once before either prepare starts. |
| tools/translate/run.sh | `gen=${PORT_GEN:-...}`; `python3` becomes `${PYTHON:-python3}` | Output location; on Windows `python3` is the Microsoft Store stub. |
| tools/translate/check.sh, tools/port/rebuild.sh | emsdk path `${EMSDK_DIR:-~/projects/emsdk}`; rebuild.sh build dir `${PORT_WEB:-build/web}` | Configurable toolchain and build locations. |
| CMakeLists.txt | `file(CREATE_LINK ... SYMBOLIC)` for `game` and `assets` goes through `simgolf_link()`, which falls back to a directory junction (`mklink /J`) on Windows | Symbolic links need Developer Mode or admin on Windows; junctions need neither. Same result for the dev server. |

Repository-wide (not in port/): `.gitattributes` forces LF for `*.sh` and for
every text file under port/. `core.autocrlf=true` had checked port/ out with
CRLF: bash rejects CRLF scripts, and web/preload.txt (read by the page at run
time) produced URLs ending in `%0D` (404 on every preloaded file).

## Differences in how we run the upstream tools

- Ghidra runs through tools/ghidra_headless.sh with `-Dline.separator=\n` (LF). On
  Windows the decompiled C otherwise has CRLF line breaks, and the translator's
  patterns for calls through `code *` then miss (compile errors such as
  "called object type 'code' is not a function").
- prepare-golf.sh and prepare-jgld.sh run in parallel (upstream ran them one
  after the other). Both finish in about 14 minutes here, not 1.5 hours.
- The void_used loop is automatic: tools/build_web.sh translates, and while
  build/gen/<module>/void_used.txt lists functions SetReturnInt has not fixed
  yet, it runs reexport-golf.sh / prepare-jgld.sh and translates again
  (VOID_PASSES, default 4). Upstream did this by hand and kept the accumulated
  lists in ~/ghidra_work. From a clean start, the first translation traps at
  startup (FUN_004a01d0 reads the result of FUN_004a00f0, typed void); one pass
  (1130 golf + 291 jgld functions) fixes it and the next translation adds nothing.
- Our jgld image.bin matches upstream's web/pages/jgld-nops.json exactly
  (3227 NOP runs), a byte-level check that the jgld prepare reproduces upstream.

## Environment (tools/env.sh)

`GHIDRA_HEADLESS`, `JAVA_HOME`, `W` (Ghidra work dir, default build/ghidra_work),
`PORT_EXPORTS` (= `W`), `PORT_GEN` (build/gen), `PORT_WEB` (build/web),
`SIMGOLF_GAME` (game/), `PYTHON`, `EMSDK_DIR`, `CHROME_PATH`, plus cmake, ninja
and emsdk on PATH and `PYTHONUTF8=1` for the translator.

## Not scripted upstream, now scripted (tools/ghidra_import.sh)

Upstream's prepare scripts start from Ghidra projects that were made by hand.
tools/ghidra_import.sh recreates them:

- `$W/projects/SimGolfDeps`: golf_nocd.exe, default auto-analysis, then
  RecoverFunctions.java (saved). Gives 2359 functions before recovery and 3109
  after (+750), matching docs/function-recovery.md (2358, 3108, +750).
- `$W/projects_copy/SimGolfDeps`: jgld.dll at its own image base 0x10000000
  (no rebase needed), auto-analysis, then RecoverFunctions.java (1708 to 1988, +280).
- DumpExternals.java into `$W/golf_externals.txt` and `$W/jgld_externals.txt`.
- VtablePurges.java into `$W/purges_golf.txt` and `$W/purges_jgld.txt`.

## task-2: native Windows build

`cmake --preset native` (port/CMakePresets.json) or tools/build_native.sh builds
build/native/golf.exe: 32-bit x86, clang-cl 23 + lld-link (official LLVM
release), static MSVC CRT, SDL2 2.32 (SDL2.dll next to the exe, delay-loaded).
Every shared-file change is behind `#ifdef __EMSCRIPTEN__` (or `_MSC_VER`), so
the browser build compiles and behaves as before.

| File | Change | Why |
|---|---|---|
| CMakeLists.txt | without emcmake on Windows, include cmake/native.cmake and return | One source tree for both builds. |
| CMakePresets.json, cmake/clang-cl-x86.cmake, cmake/native.cmake (new) | the native preset, toolchain and target | Generated code gets the browser build's flags (-O1, -fwrapv, -fwrapv-pointer, -fno-strict-aliasing, -fno-delete-null-pointer-checks, -fno-finite-loops) plus `-ftrivial-auto-var-init=zero` (see below). Linked at /BASE:0x60000000, no ASLR, no large-address awareness. |
| src/port/host.h, host_win32.c (new) | process setup: image ranges, game thread (64 MB stack), golf.ini, golf.log, crash and wild-access handlers, mute | See "Address space" and "Differences" below. Also the native stand-ins for hd.c (no upscaler) and video.c (videos skipped). |
| src/port/fs_native.c (new) | fs.h over the real game folder | Windows paths are case-insensitive already; no manifest, no fetch, no IndexedDB; saves are plain files in "Saved Games". |
| src/port/harness.c (new) | `--script` runner: input as SDL events, waits, screenshots from the back buffer, pixel checks | Tests and evidence on the hidden desktop (tools/ghost_run.ps1); the base for the task-13 bridge. |
| src/platform/sound_native.cpp (new) | the snd_js_* back end over SDL2 audio: WAV/smpl decoder, 44.1 kHz float mixer | Same rules as sound_web.js (gain, pan law, pitch, 16 voices, one for dedicated/streamed sounds, loops, fades). Reads the original WAVs from the game folder. |
| src/compat/msvc (new) | strings.h, unistd.h, dirent.h/.c for the MSVC CRT | The port's sources include POSIX headers. |
| src/port/runtime.c/.h | `port_now_ms()`, `port_host_path()`; native port_abort ends through host_fatal | emscripten_get_now and the /image, /fonts FS paths are browser-only. |
| src/port/win32.c | emscripten bits behind #ifdef; native waits sleep instead of yielding to JS; GetMessage/WaitMessage idle with a 1 ms sleep natively; harness_tick at each yield; SDL_QUIT (window close) exits; `win_closing` set by DestroyWindow / WM_QUIT | Native has no event loop to yield to; without the sleep GetMessage spun a core. |
| src/port/gdi.c, port_main.c, crt.c | clock and paths through runtime.h; native main is host_main; crt `exit()` ends the process natively | In the browser `exit()` aborts (the tab stays). |
| src/platform/sound.cpp | native: resolve the game's sound path with fs_resolve instead of the assets manifest | The native build plays the original files. |
| src/port/ghidra_prelude.h | `#define wchar_t ghidra_wchar_t` under _MSC_VER | The Ghidra types redeclare wchar_t as short; the MSVC headers already define it. |
| tools/port/run.mjs | Chrome launched with --mute-audio | Test runs are silent (the maintainer, 2026-10-09). |

### Address space (native)

The translated game keeps every global at its original address: golf.exe's
image needs 0x00400000-0x00900000 and jgld.dll's 0x10000000-0x10200000. With
the exe at 0x60000000 the loader puts its own heaps in the low range before
main() runs, so golf.exe starts itself again suspended, reserves both ranges
in the new process with VirtualAllocEx before its loader runs, and waits for
it (a job object kills the child with the parent). Two golf.exe processes in
Task Manager is expected.

### Differences from the browser build (native)

- Wild memory accesses. wasm memory is readable and zero everywhere below its
  size, and some decompiled functions depend on it (FUN_00462020 on the
  course-site screen indexes a table with a stack slot Ghidra mis-tracked, a
  read the original never makes). Natively the game thread gets the same: an
  access violation on free address space below 2 GB commits a zero page and
  retries (logged as "memory: ..."). Locals of generated code start at zero
  (`-ftrivial-auto-var-init=zero`, wasm locals are zero too). The null page
  cannot be mapped on Windows; a crash after the game started shutting down
  (FUN_00402870 with this=1 after quitting) exits normally.
- No intro or closing video (logged as skipped). The browser build plays WebM
  conversions; a native player is future work.
- No HD upscaler (hd.c): the window is the original 800x600 (task-3).
- Mute: `--mute`, SIMGOLF_MUTE=1, mute=1 in golf.ini, and always under
  `--headless`/`--script`: SDL's dummy audio driver, nothing reaches a sound
  card; the mixer still runs and logs what it mixed (SIMGOLF_MIXWAV records it).

## task-13: game bridge (native only)

All native-only (`#ifndef __EMSCRIPTEN__` or files the browser build does not
compile); the browser build is unchanged. docs/BRIDGE.md has the protocol.

| File | Change | Why |
|---|---|---|
| src/port/bridge.c (new) | localhost TCP bridge polled on the game thread; virtual clock; input, screenshots, memory, call, mods store, perf | Tools and chats drive the live game (simgolf MCP server). Off unless `--bridge`/`SIMGOLF_BRIDGE=1`. |
| src/port/runtime.c/.h | native `port_now_ms()` returns `bridge_clock_ms()` (real time unless paused, scaled or turbo); `icall_find()` (lookup without the abort) | Pause, step and fast-forward without touching game code; the bridge's `call`. |
| src/port/win32.c | native waits go through `bridge_wait()`; `bridge_tick()` at every yield | Turbo turns sleeps into clock jumps; commands run between frames. |
| src/port/gdi.c | `bridge_on_present()` after each present | Frame counting, `step`. |
| src/port/harness.c | `write_bmp` exported as `harness_write_bmp` | The bridge's screenshots. |
| src/port/host.h, host_win32.c | bridge declarations, `bridge_init`; `SIMGOLF_USER_DIR` and `host_user_dir()` | Per-instance saves folder. |
| src/port/fs_native.c | "Saved Games" and "Snapshots" resolve under `host_user_dir()` | Parallel games never share saves (same as before without the variable). |
| cmake/native.cmake | bridge.c, ws2_32.lib | |

## task-11: mod platform core (native only)

All native-only; the browser build does not compile these files and is
unchanged. docs/modding/CORE.md describes the platform.

| File | Change | Why |
|---|---|---|
| src/mods/ (new) | manifests, semver ranges, resolver, conflicts and merges, settings store, save records, relocation helper; `mods_runtime.c` is the game side | The mod platform. Plain C shared with the tools; only mods_runtime.c calls Windows. |
| src/mods/tool/modctl_core.c (new) | the helper behind tools/modctl | The CLI and the game resolve with the same code. |
| third_party/tomlc99 (new) | tomlc99 toml.c/toml.h, MIT, unchanged (README has the commit) | TOML for mod.toml, load_order.toml and save records. |
| cmake/mods.cmake (new), cmake/native.cmake | one `include(cmake/mods.cmake)`: the sources, `SIMGOLF_DEFAULT_MODS`/`SIMGOLF_DEFAULT_MODS_INI`, the modctl_core and mods_tests targets | Keeps the platform's build in its own file. |
| src/port/host_win32.c | `mods_init(argc, argv)` before the game folder becomes the current directory | Startup: golf.ini, mods.ini, the load order, the report in golf.log. |
| src/port/bridge.c | its key/value store replaced by the mods store (`bridge_mod*` wrap `mods_get*`); `--mods` read by mods_init; `mods_get` returns `origin`; new `mods_status` | One store for golf.ini, mods, mods.ini and live values. |
| src/port/fs_native.c | `fs_written` records the loaded mods next to a written save; `fs_resolve` (read) checks a save's record | Saves remember their mods; the .sve bytes are untouched. |

## task-25: modern UI font (native only)

In effect native only: `ui_font.h` stubs everything out under `__EMSCRIPTEN__`, so the browser
build draws text exactly as before. Setting `ui.font=original|plus_jakarta` (mods store). The
built-in mod `ui_font` sets plus_jakarta; `[ui] font=original` in data/mods.ini, the vanilla
profile or `--no-mods` give the original; the bridge's `mods_set` switches it live for text
drawn from then on.

| File | Change | Why |
|---|---|---|
| src/port/gdi.c | `Font` keeps its face and weight; `pj_prepare` picks a Plus Jakarta Sans cut, size and baseline shift per face on first use; `TextOutA` into a 16-bit DIB takes `text_out_pj` (anti-aliased coverage blended in linear light, gamma 1.8, into the 5-bit frame; tabular figures through the font's tnum glyphs); `GetTextExtentPoint32A` reports the width of what is drawn; the font table grows from 8 to 16 files. `GetTextMetricsA` still reports the original face's metrics, so the game's layout does not move. 8-bit DIBs and `original` mode run the old code untouched. | Text in a modern face, smooth edges, steady digits, same boxes. |
| src/port/ui_font.c/.h (new) | the setting, `font_map.txt` (face to weight, width fit, tabular on/off), the gamma blend | Keeps the switch and tuning out of gdi.c. |
| cmake/native.cmake | `ui_font.c`; target `ui_font_files` copies the five static cuts, OFL.txt and data/ui/font_map.txt to `fonts/plus-jakarta-sans/` next to the exe | The exe loads them like the other stand-in fonts. |

Sizing: each Plus Jakarta face is scaled so a sample sentence is `fit` (0.95, data/ui/font_map.txt)
of the original face's width, never below 9 px em, and its baseline moves so the middle of the
x-height/cap-height band matches the original's. No text records are made for Plus Jakarta text
(they feed the browser's HD layer only). `SIMGOLF_TEXTFIT=1` logs every string drawn wider than
the original face would have drawn it. Glyphs are rasterised into the 800x600 frame and then
scaled by the presenter; drawing them at the final pixel size needs the presenter (task-3 step B)
to take text runs.

## task-19: mod data layer

Native only except the prelude macros (the browser build gets the literal).
docs/modding/DATA.md describes the layer.

| File | Change | Why |
|---|---|---|
| src/mods/json.c/.h, data.c/.h, data_runtime.c, mdata.h, tool/modctl_data.c (new) | JSON with line numbers, the JSON Schema subset, tables and mod operations, the game side, `modctl_core data` | Moddable tables; data.c is shared by game, tool and tests. |
| src/port/port_main.c | `mdata_init(__argc, __argv)` after the static initializers, before WinMain (native) | Changed fields are in the game's tables before any game code reads them. |
| src/port/ghidra_prelude.h | `MDATA`, `MDATA_AT`, `mdata_thought_begin/end` (the literal / nothing in the browser build) | What the translator patches call. |
| tools/translate/translate.py | also reads `<config stem>.*.json` next to the config and appends their `patches` | task-19's patches live in golf.data.json, apart from golf.json. |
| tools/translate/golf.data.json (new) | patches in FUN_004289e0, FUN_0040f5c0, FUN_00436c00, FUN_00421bc0, FUN_00427380, FUN_00469b00 | Constants wrapped in MDATA; the thought routine's hooks. Needs a re-translate. |
| src/mods/mods_runtime.c, mods.h | `mods_report_add`; `mods_touch` counts an identical touch of one mod once | Data lines in the mods report; a touch declared in mod.toml and registered at run time is one touch. |
| src/port/bridge.c | `pause_at` {addr, value}: pause at the first present where the i32 at addr equals value | Frame-exact start for A/B runs (tests/mods/data_ab.mjs). |
| cmake/mods.cmake | json.c, data.c in the core; data_runtime.c in the game; modctl_data.c; `data_tests`; `SIMGOLF_DEFAULT_TABLES` | Build. |

## task-26/30: Frosted Mint UI kit and course HUD

The kit (`src/port/ui/`) and the HUD's render and game sides (`src/port/hud_frosted.c`,
`src/port/hud_game.c`) are native only. Shared with the browser build: the override file
`src/port/golf/hud_hooks.c` (the news strip tick, plus no-op hooks under `__EMSCRIPTEN__`) and the
translator patches below, which call those no-ops there, so the browser build behaves as before.
Setting `ui.hud=frosted_mint|original` (mods store, built-in mod `ui_hud`). docs/ui/KIT.md.

| File | Change | Why |
|---|---|---|
| src/port/ui/ (new) | tokens (TOML), layout, hit tests and routing, icons, SDL drawing, glyph atlas, components, the course HUD screen, PNG writer, gallery | The UI kit every new screen uses. |
| src/port/hud_frosted.c, hud_frosted.h, hud_game.c (new) | the HUD as a native UI layer (`hd_ui_add_layer`, `hd_ui_add_input`), game state snapshots, the game's tool art read back with alpha, clicks injected at the original controls through `FUN_004657e0` | The new HUD runs the original handlers. |
| src/port/golf/hud_hooks.c (new) | `// @override FUN_0040d6a0`: the news strip tick, its draw call skipped while the new HUD shows the message as a toast | Its timer logic must keep running. |
| tools/translate/golf.json | FUN_0040f5c0: `hud_save()` before the old banner, readouts, wheel and trays draw, `hud_restore()` after; the old wheel and trays get `hud_mx`/`hud_my` (mouse parked off screen); "Paused" gated by `hud_on()`; the y > 440 click gate and the chip strip gate ask `hud_old_click()` / `hud_strip_click()`; the world cursor's y thresholds go through `hud_ylim()` | Hide the old HUD's pixels without touching its logic; clicks where it was reach the course. |
| src/port/ghidra_prelude.h | declarations of the `hud_*` hooks | The generated code calls them. |
| src/port/gdi.c | a list of screen-sized 16-bit DIBs (golf's page buffers), `gdi_screen_dib`, `gdi_dib_at`, `gdi_backbuffer` | hud_game.c saves and restores the HUD areas in whichever page golf draws into. |
| src/port/win32.c | `hud_note_script_click` for button presses without a window | Scripted input (harness, bridge) still reaches the old controls. |
| src/port/hd_native.c | while a native UI layer is registered the picture is composed into a canvas texture (as with render=WxH) | The layer frosts the picture under its panels. |
| src/port/host_win32.c | `/ui/` resolves next to the exe like `/fonts/` | `ui/frosted_mint.toml`. |
| cmake/ui.cmake (new), cmake/native.cmake | kit and HUD sources in golf; `ui_tests`, `ui_gallery`; `ui/frosted_mint.toml` copied next to the exe | |




### task-26c: the jwe layout is the default

`mods/builtin/ui_hud/mod.toml` sets `ui.hud_density = jwe` (the maintainer's pick R5; the other densities stay switchable, `ui.hud=original` still restores the original HUD). `src/port/ui/ui_hud.c`: the R5 cluster measures its play / pause control instead of assuming a width, sits on the new `UI_PANEL_READABLE` glass (`ui_kit.c`: frosted over `glass.solid`, 7:1 text contrast over any picture), a small camera pill (zoom in / out, rotate left / right) sits in the bottom right corner, and news toasts sit just under the cluster. `src/port/hud_frosted.c` logs `hud: tray tucked|shown` for tests. Tests: `tests/native/hud_click_jwe.txt` and `hud_regular.ini` (regular stays click tested), `hud.sh`, `tests/ui/test_ui.c` (contrast of the readable glass).

## task-33: bridge input timing and fixes (native only)

| File | Change | Why |
|---|---|---|
| src/port/bridge.c | input acts wait on the game, not only the clock: one presented frame between mouse acts, and for keys and text until the game has taken the key (SDL queue and WM_KEY/CHAR queue drained, key mailbox 0x822b9c empty, key poll latch 0x4d3834 clear); a 4096-act queue that reports "full" instead of dropping; `screenshot` waits for fresh frames (`fresh`); `step` counts game-loop iterations on a screen that presents no frames (reply `unit`, `loops` in every state); `drag` takes `path` and `step_px` with a frame per move; `click` takes `path` (many clicks); new `mousedown`, `mouseup`, `trace` | Typed letters and Backspaces were dropped under load: the game keeps ONE key in a mailbox and its key poll clears the mailbox again after the next message pump. A shot right after a click showed the old frame. `step` never finished on reports. |
| src/port/win32.c | `win_key_msgs_pending()`: count of key messages still in the game's queue | The bridge's key acknowledgement. |
| src/port/bridge.c | `canvas_w`, `canvas_h`, `ui_dy`, `hud`, `map_w`, `map_h` in every state reply; new `grid` command (map size and where each per-tile array lives) | Tools place clicks for the old wheel on a taller canvas and read the tile arrays on any map size (task-33b, after task-3 B4 and task-8). |


## task-7: map size (grid pass, prototype for task-8)

docs/LIMITS.md has the findings. At map_scale 1 (the default) every change is
value-identical to the original: same arrays, same strides, same save layout.

| File | Change | Why |
|---|---|---|
| tools/translate/grid.py (new) | pass over build/gen/golf after translate.py: the 20 per-tile arrays move to GRID(i), 50/51 strides, bounds, loop starts and clear counts become MAP_W/MAP_H; writes golf_grid.c (array table) | The grid size becomes a start-up setting without hand-editing generated code. Rules and skips are in the file; tests/grid_pass_test.py covers them. |
| tools/translate/run.sh | runs grid.py on the golf module | Every regeneration gets the pass (it skips files already rewritten). |
| src/port/grid.h, src/port/golf/grid.c (new) | MAP_W, MAP_H, GRID(i); storage sized from map_scale with a spare row each side, image contents copied in | 1 = 50 x 50, 2 = 70, 4 = 100, 10 = 158 (max 160). |
| src/port/ghidra_prelude.h | includes port/grid.h | Generated code sees MAP_W and GRID. |
| src/port/port_main.c | grid_init() after the golf image loads | Arrays exist before any game code runs. |
| src/port/host.h, host_win32.c | `map_scale=` in golf.ini, host_map_scale() (SIMGOLF_MAP_SCALE overrides, read in grid.c) | The setting. |
| src/platform/terrain.h, terrain_dll.cpp | tiles and vertex arrays sized for TERRAIN_MAX (160); w, h and the vertex stride from the map size; visible ranges use w, h; the camera centres on w/2, h/2 instead of 25 | The renderer follows the map. |
| src/port/golf/camera_input.c | CAM_MAX = MAP_W - 1 | Drag and keys reach the far edge of a big map. |
| src/port/bridge.c | peek and poke pass addresses through grid_translate() | Tools that use the original address of a map array (symbols.json map_tiles 0x5722e8) reach the moved data. |

## task-8: bigger maps (map size in saves, scaled land, map screens)

docs/LIMITS.md section 1 has the rules. At map_scale 1 every change gives the
original's values (checked: the land generator run for all 16 property slots
with a pinned seed gives byte-identical tiles, flags, heights and objects with
and without the task-8 patches).

| File | Change | Why |
|---|---|---|
| src/port/golf/map_save.c (new) | override FUN_0040af70 (save field read/write): a 16-byte "SGMP" tag (version, w, h) before the first field when the map is not 50 x 50 | A save opens at the size it was made; original-size saves stay the original format. |
| src/port/golf/grid.c, src/port/grid.h | the size from data/mods.ini `[map] scale=` (beats golf.ini map_scale=; SIMGOLF_MAP_SCALE beats both); grid_resize, grid_resize_default, grid_save_tag; GEN_* macros (rules scaled by side or area, identity at 50); a zero buffer for FUN_00424120 | Loading resizes the map and the renderer; patches below use the macros. |
| src/platform/terrain_dll.cpp | terrain_resize | The renderer follows a loaded save's size. |
| src/port/golf/map_screens.c (new) | override FUN_00456b70 (tile to screen point on the routing map and buy land screens): the step shrinks with the map | Every size fills the same part of the screen. |
| tools/translate/golf.json | patches: FUN_004315e0 (new games return to map_scale), FUN_00470a60 (land generator scaled, docs/LIMITS.md 1.4), FUN_0040f5c0 (weeds over the whole map, as many picks per tick as original-size areas; corner stride; sandbox flatten row), FUN_00430360 (wildlife enters and leaves at the real edges), FUN_00406f20/FUN_00406f90 (raise/lower land: corner stride written as 0x22 + 0x11), FUN_00449540 (last row start 49 * 50), FUN_00424120 (stack array read past its size on big maps), FUN_00456be0 (tile diamonds shrink), FUN_004587a0 (3 x 3 buy-land tracts over the whole map) | Game rules tuned for 50 x 50. |
| tools/translate/grid.py | loop bounds a few bytes past an array's end move with the array (FUN_0042f7a0 heightfield, the generator's corner pass ran one row); KEEP_RE keeps non-map 48/49/50s on lines that also hold a tile index (sub-tile positions, the yardage loop, money / 50, golfer stats, pixel offsets) | Found by an audit of every rewrite in grid_report.txt. |

## task-31: golfers in the world (name tags, thought bubbles, golfer card)

Switch `ui.golfers=frosted_mint|original` (built-in mod `ui_golfers`). docs/ui/KIT.md
has the design and the split. With `original` every hook below is a no-op.

| File | Change | Why |
|---|---|---|
| port/tools/translate/golf.golfers.json (new) | patches (merged into golf.json's): FUN_00462be0 (golfer sprite and name: gw_seen, gw_name, mood bars), FUN_004038f0 (staff name), FUN_0040f5c0 (thought loop: gw_thought_begin/gw_face/gw_thought_end and every draw of the strip gated by gw_hide; the staff speech strip; the card call wrapped in gc_begin/gc_end), FUN_0045c560 (the card: its text and bar draws go through gc_text and gc_rect, its pointer through gc_mouse) | The original code keeps running; only its pixels are replaced. |
| src/port/ghidra_prelude.h | declarations of the gw_* and gc_* hooks | Generated code calls them. |
| src/port/golf/golfers_hooks.c (new) | gw_hide, and the browser build's no-op versions | Same split as hud_hooks.c. |
| src/port/golfers_game.c (new) | game thread: records tags, bubbles and the card into a GwNote, reads the art, saves and restores the card's pixels, injects the clicks | The native versions of the hooks. |
| src/port/golfers_frosted.c, .h (new) | render side: world and card layers, input, art textures | The UI layers. |
| src/port/ui/ui_golfers.c, .h (new) | the drawing, a pure function of a GwNote | Kit-level, gallery and tests use it. |
| src/port/hd_native.c, .h | HdUiFrame gets layered, slide_x/y, note, note_size; hd_ui_note_set (game thread) attaches a blob to the frame being presented | Native UI pinned to the course layer exactly, smooth scrolling included. |
| src/port/golf/hd_ui.c | calls gw_frame_begin (hd_ui_frame_camera) and gw_course_done (hd_ui_course_done) | The recording window of a course frame. |
| src/port/hud_game.c | capsule_visible stays true while the native card is up | The top bar no longer hides behind the original card. |
| src/port/bridge.c | commands hdshot, wmove, wclick | Tests drive the native UI like a player (docs/BRIDGE.md). |
| mods/builtin/ui_golfers, mods/load_order.toml, tests/mods/all_off.ini, data/mods.ini | the mod, in the default profile, switched off in the all-off test, documented | |
| port/cmake/ui.cmake, tools/check.sh | new sources; gallery page `golfers`; tests/native/golfers.sh stage | |

### task-60: quieter names and bubbles, new mood faces

No translator patch changes: the same hooks feed more drawing options. `ui.tags`, `ui.bubbles`,
`ui.tag_style`, `ui.tag_scale`, `ui.tag_opacity`, `ui.bubble_ms`, `ui.tags_key`, `ui.faces` (defaults in
mods/builtin/ui_golfers/mod.toml, documented in data/mods.ini). `ui.faces=original` or
`ui.golfers=original` give the 2002 faces.

| File | Change | Why |
|---|---|---|
| src/port/ui/ui_faces.c, .h (new) | the ten mood faces as vectors (SDF raster at the drawn size) | Original Frosted Mint art, art/manifests/ui-mood-faces.json. |
| src/port/ui/ui_kit.c, .h | ui_face (cached texture per mood and size) | Kit piece. |
| src/port/ui/ui_golfers.c, .h | GwOpts; plain outlined names, pill on hover or selection, fades, zoom shrink; brief bubbles, face icons for minor thoughts; card mood badge | The look the maintainer asked for. |
| src/port/golfers_frosted.c | reads the settings, tracks the pointer and the show-all key (N) from an input hook that returns 0 for the pointer | Hover and show-all. |
| src/port/golfers_game.c | SIMGOLF_FACES_DUMP writes golf's mood sprites once (tests, contact sheet) | Old-versus-new contact sheet. |
| src/port/ui/ui_gallery.c, tests/ui/test_ui.c, tests/native/golfers_look.mjs, tools/check.sh | pages names, faces, zoomout and contrast; face unit tests; the in-game look and coverage run; gallery pages checked | Evidence and guards. |

## task-40: waterfalls (water on a slope after a load)

golf pushes tile heights into the renderer (Terrain) in two places: the load/new-game push (FUN_00449540) and a sync of the tiles within 13 tiles of the camera (FUN_0044a5b0, when the course view finds its dirty flag clear, e.g. after a terrain edit). The load push skips water tiles (type 0x11), so water that sits above sea level (a stream running down a slope, with 3-level drops that draw as waterfalls) stayed at sea level in the renderer until a sync happened near the camera. Next to banks that were raised, the lowered water left a hole the black background showed through: black triangles at the waterfalls. Saves loaded with the camera far from a stream showed it; a game running while the course was built did not (every edit synced the tiles around the camera). The original game has the same load push; the fix is a port-level correction and `[fixes] water_push=0` in data/mods.ini restores the original behaviour.

| File | Change | Why |
|---|---|---|
| tools/translate/golf.json | patch FUN_00449540: the elevation push also covers water tiles (`iVar10 != 0x11 || fix_water_push()`) | Water on a slope is at its height after a load, wherever the camera is. |
| src/port/golf/overrides.c, src/port/ghidra_prelude.h | `fix_water_push()`: mods.ini `[fixes] water_push` (default 1; the browser build always 1) | The original behaviour stays reachable for A/B. |
| src/port/bridge.c, tools/mcp/simgolf (lib/instances.js, lib/tools.js) | bridge command `hdshot` and `screenshot` option `hd`: the picture the sharp presenter shows (native-resolution course), as the harness's hdshot | Tests and evidence at hd=1 through the bridge (tests/native/waterfall.sh). |
## task-41 (with task-43 option a): round HD golf ball, smooth flight

Native only in effect: `golf/ball_hooks.c` has the original calls for the browser build and the
hooks are no-ops there, so the browser build draws the ball as before. Setting `ui.ball=round|original`
(mods store, built-in mod `ui_ball`). docs/ui/BALL.md has the design, the tests and the limits.

| File | Change | Why |
|---|---|---|
| tools/translate/golf.ball.json (new) | patches: FUN_0040f5c0 (the 2 px line of a ball at rest becomes `hdball_rest`, `hdball_air` before the sprite list entry for the other balls), FUN_00463100 (each queued sprite goes through `hdball_item`, `hdball_resolve` after the loop) | Golf's own ball draws run as ever, between snapshots; with ui.ball=original they are the original calls. |
| src/port/golf/ball_hooks.c (new) | the four hooks; the original calls only under `__EMSCRIPTEN__` | |
| src/port/ghidra_prelude.h | declarations of the hooks | The generated code calls them. |
| src/port/hdball.c, hdball.h (new) | game thread: records each ball per frame, measures and takes golf's ball pixels out of the back buffer, keeps how much of them later sprites covered | The round ball replaces golf's; the cover keeps it behind trees and golfers. |
| src/port/hdball_render.c, hdball_render.h (new) | render thread: path through golf's last four positions, shaded ball and shadow textures at the exact screen size, ball log, tuning file | The round ball at the display rate. |
| src/port/hd_native.c | `hdball_publish` in `hd_present`, the record copied with the frame, `hdball_ingest` in `upload`, `hdball_draw` in `compose` between the course layer and golf's UI layer, `hd_native_balls_ok()` | The presenter owns the frame hand-over. |
| src/port/golf/hd_ui.c, hd_ui.h | `hd_ui_back_bits()` | Pixels of golf's back buffer for the snapshots. |
| src/port/bridge.c | command `hdshot {path}`: the presenter's picture as a BMP | Evidence and tests see the HD picture, not the 800x600 back buffer. |
| cmake/ball.cmake (new), cmake/native.cmake | the two sources, `ui/ball.txt` copied next to the exe | |

## task-32: Frosted Mint front end (start menu, difficulty, property, load, championship, themes)

Native only in effect: `golf/frontend_hooks.c` has no-op versions of the hooks for the browser build
(the text hook calls the original draw), so the browser build keeps the original screens. Switch
`ui.frontend=frosted_mint|original` (mods store, built-in mod `ui_frontend`); the environment variable
`SIMGOLF_FRONTEND=original` also turns it off (tools/ghost_run.ps1 sets it for scripted tests that click
the original title and menus by window pixels). docs/ui/KIT.md has the design and the limits.

| File | Change | Why |
|---|---|---|
| tools/translate/golf.frontend.json (new) | patches: the title loop in FUN_0040f5c0 (heartbeat, caller), FUN_0043a400 (difficulty), FUN_0046f550 (property screen: texts, pins, emblem per tag, hover), FUN_0043b610 (load game and championship courses: base picture, texts, rects, emblem, list variables, heartbeat), FUN_0043a8c0 (pick a pro), FUN_004725b0 (theme packs) | The original loops keep running and only report what they draw; a native layer draws the screens over their picture. |
| src/port/golf/frontend_hooks.c (new), src/port/ghidra_prelude.h | the hooks (`fe_frame`, `fe_base`, `fe_context`, `fe_text`, `fe_rect`, `fe_sprite3`, `fe_sprite_b`, `fe_list_vars`, `fe_theme_vars`, `fe_world_*`, `fe_suspend`); no-ops in the browser build | The generated code calls them. |
| src/port/frontend_game.c (new) | game thread: reads each screen into an FeSnap, reads the game's art (logo, globe, faces, emblems, portrait) from the back buffer, turns native clicks into scripted mouse clicks on the ORIGINAL controls (move, press, release, one step per frame) | The original handlers, checks and messages run. |
| src/port/frontend_frosted.c, frontend_frosted.h (new) | render thread: native UI layer that paints the background over the original picture, draws `ui_frontend_draw`, takes the pointer (real window events never reach the original screens; scripted ones do), logs the layout with SIMGOLF_FRONTEND_LOG=1 | |
| src/port/ui/ui_frontend.c, ui_frontend.h (new) | the six screens as pure functions of an FeSnap (kit components) and the background | |
| src/port/ui/ui_kit.c, ui_kit.h, ui_icons.c, ui_icons.h | `ui_vscroll_begin/end` (vertical scroll area), icons pin, trash, check | Long lists. |
| src/port/hd_native.c, hd_native.h | `hd_raw_mouse_x/y`: the window mouse position in canvas pixels before the clamp to the picture | The title and menus are 800x600 in the middle of a wider window; a full-screen native screen needs the whole window. |
| src/port/ui/ui_gallery.c | pages frontend-title, -difficulty, -world, -load, -pro, -themes and a click-through check | |
| mods/builtin/ui_frontend, mods/load_order.toml, tests/mods/all_off.ini, data/mods.ini | the mod, in the default profile, switched off in the all-off test, documented | |
| tests/native/frontend.sh, frontend.mjs, tools/check.sh | bridge runs: new game, themes, Continue (scroll), championship, ui.frontend=original | |
| tools/ghost_run.ps1, tools/art_run.mjs | `SIMGOLF_FRONTEND=original` for scripted runs | Their scripts click the original title by window pixels; the art pack test compares the original title picture. |
## task-44: Shift+T hides decoration trees too

Shift+T ("trees off/on") flips bit 0x400 of the game flags at 0x59e7b8, which only the terrain tree classes (13, 14, 15) test. Scenic trees (the Landscaping tray, tile class 16 with a variant), the cactus group, the scenic-tree decoration on cells and the dead swamp trees are drawn by the cell decoration switch in FUN_0040f5c0 and ignored it. The patches set `g_decor_tree_pass` while that switch draws a tree and clear it after the switch; the two sprite queue functions drop the sprite while the pass flag is set and the view is "trees off". Only the draw queue changes: cell flags, growth frames, sounds and the random generator run as before, so golfers, shots and saves are identical (tests/native/hide_trees.mjs compares them). Flower beds, hedges and brush are not trees and stay. `[view] hide_decor_trees=0` in data/mods.ini gives the original Shift+T.

| File | Change | Why |
|---|---|---|
| tools/translate/golf.json | FUN_0040f5c0: `g_decor_tree_pass = 1` in decoration cases 0 (class 10), 3 (scenic trees, cactus) and 5 (swamp trees), cleared after the switch; FUN_004628d0 and FUN_00462a30 return early when `g_decor_tree_pass && hide_decor_trees()` | Hide the sprites, keep the game's own state changes. |
| src/port/golf/overrides.c, src/port/ghidra_prelude.h | `g_decor_tree_pass`, `hide_decor_trees()` (flag first, then mods.ini `[view] hide_decor_trees`, default 1; the browser build always hides) | A/B against the original Shift+T. |
| tests/native/hide_trees.sh, .mjs, _check.py, .txt, .ini | Showcase Resort: pictures at hd=0 and hd=1, game state compared with the setting on and off | Evidence and the check.sh stage. |

## task-48: no overlapping duplicate lot (snack bar)

Report (task-18): "clicking a built snack bar's dock icon places an overlapping duplicate". Traced with a scripted hidden test (tests/native/snackbar_dup.mjs): the dock icon only picks the build tool in every layout (jwe, ui.hud=original, scripted clicks on the old tray); it never places anything. The duplicate comes from the NEXT click, on the course, ON a tile of a standing snack bar with the tool armed, and the original game does the same (ui.hud=original reproduced it). The placement validity check FUN_0040db90 accepts a footprint tile that is occupied by a building of the same kind (the upgrade relies on it: placing a lot whose level is above 0 first removes every old one of that kind in the main loop, FUN_0040e400). At level 0 nothing is removed, so a second lot lands on the first and its price is taken. The fix is a port-level correction; `[fixes] building_overlap=0` in data/mods.ini restores the original rule.

| File | Change | Why |
|---|---|---|
| tools/translate/golf.json | patch FUN_0040f5c0: the player's placement click and the hover ghost call `fix_place_check` instead of FUN_0040db90; patch FUN_0040db90: `... != param_4 or fix_overlap_blocks(param_4)` | A lot (tools 6 to 14) at level 0 may not overlap a building of its own kind; the ghost shows red there. Upgrades (level above 0) and the AI's own placements (they call FUN_0040db90 directly) are untouched. |
| src/port/golf/overrides.c, src/port/ghidra_prelude.h | `fix_place_check()`, `fix_overlap_blocks()`: mods.ini `[fixes] building_overlap` (default 1; the browser build always 1) | The original behaviour stays reachable for A/B. |
| tests/native/snackbar_dup.mjs, snackbar_dup.sh, tools/check.sh | the scripted test (jwe, ui.hud=original, fix off) as a check.sh stage | |

## task-27: Frosted Mint dialogs (popup menus, preferences, text entry, hire panel, hints, in-game Load game and world map)

Switch: `ui.dialogs = frosted_mint | original` (mods store; the built-in mod `ui_dialogs` sets it; `[ui] dialogs=original` in data/mods.ini, `ui_dialogs=0`, the vanilla profile, `--no-mods` or the environment variable `SIMGOLF_DIALOGS=original` give the original dialogs). The in-game Load game and world map follow `ui.frontend`. Every dialog is still the original's own modal loop: it keeps running and reports what it shows, a native layer draws it, and a native choice becomes a scripted click or key for the original dialog, so every handler, check and message runs and the game state is the same with the switch on or off (tests/native/dialogs.sh runs both).

| File | Change | Why |
|---|---|---|
| port/tools/translate/golf.dialogs.json (new) | patches: FUN_0046d6e0 (popup menu: begin, per frame before the present, every return, Esc in the check list), FUN_0045b2c0 (text entry: begin, per frame also in the wait loop, returns), FUN_00405b10 (save name: keep the picture before its strip is drawn), FUN_00459400 (hire panel), FUN_0040d320 + FUN_0045c0c0 (message panel with a speaker: keep the picture, per frame in the wait loop, end), FUN_004385d0 (golfer profile page: Esc) | The loops report and are hidden; golf's own handlers run. |
| src/port/golf/dialogs_hooks.c (new), src/port/ghidra_prelude.h | the hooks (`dlg_pre`, `dlg_begin`, `dlg_menu`, `dlg_entry`, `dlg_hire`, `dlg_hint_pre`, `dlg_wait`, `dlg_hint_end`, `dlg_profile_tick`, `dlg_done`); no-ops in the browser build | The generated code calls them. |
| src/port/dialogs_game.c (new) | game thread: reads each dialog into a DlgSnap (the text lines of the shared buffer 0x51a068, the job tables, the speaker's picture), keeps every page buffer of the picture from before the dialog and puts it back before each present (golf's own panel never shows), turns native choices into scripted clicks at the original control's place | |
| src/port/dialogs_frosted.c, dialogs_frosted.h (new) | render thread: native UI layer after the front end's, draws `ui_dialogs_draw`, takes the pointer (real window events never reach the original dialog), `SIMGOLF_DIALOGS_LOG=1` logs | |
| src/port/ui/ui_dialogs.c, ui_dialogs.h (new) | the dialogs as pure functions of a DlgSnap (kit components): menu, check list, entry, hire panel, hint | |
| src/port/frontend_frosted.c, frontend_game.c, ui/ui_frontend.c, ui_frontend.h | the front end screens also show over a running course (Load game, world map; `in_game`), keep their screen under a native dialog (delete a save, a pro's menu), `fe_suspend` no longer hides the screen when dialogs are native, Esc closes the in-game world map | Reuse of task-32's screens. |
| src/port/hud_game.c | `hud_clean_pages()` (the course back under the old HUD while a modal dialog opens inside the HUD's draw), the dialogs layer is registered early | The hire panel showed the old banner and dock discs. |
| src/port/ui/ui_gallery.c, tools/check.sh | pages dialogs-menu, -checks, -entry, -hire, -hint and a click-through check | |
| mods/builtin/ui_dialogs, mods/load_order.toml, tests/mods/all_off.ini, data/mods.ini | the mod, in the default profile, off in the all-off test, documented | |
| tests/native/dialogs.sh, dialogs_*.txt, dialogs_default.ini | hidden, muted runs: menus, preferences, entry strips, hire, hints, the native front end in a running game, 4K, `SIMGOLF_DIALOGS=original` | |

Fixed on the way: Esc did not close the preferences list, the world map in a running game or the golfer profile page (in both styles); the hire panel's text sat off its art and the old HUD showed through.

## task-28: Frosted Mint reports (one tabbed panel)

Switch: `ui.reports = frosted_mint | original` (mods store; the built-in mod `ui_reports` sets it; `[ui] reports=original` in data/mods.ini, `ui_reports=0`, the vanilla profile, `--no-mods` or `SIMGOLF_REPORTS=original` give the original screens). Every report is still golf's own modal loop: its draw calls go through recording hooks that then draw as before, so every check and handler runs and the game state is the same either way (tests/native/reports.sh compares with golf's clock paused).

| File | Change | Why |
|---|---|---|
| port/tools/translate/golf.reports.json (new) | main loop FUN_0040f5c0: `rp_tick` each frame, `rp_begin` / `rp_done` around the course report, SGA, comments, histograph, shortcuts, financial, roster, hole stats and the tournament check (FUN_0044fb30(2)); Information menu FUN_00432720: around best scores; in each report: text, colour, line, box and sprite calls to `rp_*`, `rp_frame` before each present and pump | The loops report what they draw and are hidden. |
| src/port/reports_game.c (new) | game thread: records, keeps the picture from before the report and puts it back each frame, reads golf's pictograms with alpha, turns native actions into Esc or golf's screen state | |
| src/port/reports_frosted.c, reports_frosted.h (new) | render thread: native UI layer, art textures, takes the pointer while a report is up | |
| src/port/ui/ui_reports.c (new) | the panel as a pure function of an RpSnap | |
| src/port/golf/reports_hooks.c (new), src/port/ghidra_prelude.h | the hooks for the browser build (originals) and their declarations | |
| cmake/ui.cmake, mods/builtin/ui_reports, mods/load_order.toml, tests/mods/all_off.ini, data/mods.ini, tools/check.sh | build, the mod, the switch, the check stage | |

## task-53: cart path help (path view, "No path" markers, "Show the building")

Switch: `ui.paths = frosted_mint | original` (mods store; the built-in mod `ui_paths` sets it; `[ui] paths=original` in data/mods.ini, `ui_paths=0`, the vanilla profile, `--no-mods`, or the Options row "Cart path help"). `ui.paths_key` (default y, none = off), `ui.paths_markers` (1/0), `ui.paths_opacity`. The rule itself is the original's and is not touched: golf's flood fill (FUN_0042f340) marks every path and building tile the clubhouse reaches with 0x40 and every joined building record with 0x40 at +7; unjoined buildings do not count as operational; Easy and Sandbox open every building. This only reads those flags.

| File | Change | Why |
|---|---|---|
| src/port/paths_game.c (new) | game thread, once per course frame (after the golfers' hook): reads the tile flags and building records, projects tile corners and building centres through golf's worldToScreen (FUN_0042fb90, terrain height included), fills a PvNote, `pv_building_of_text` (maps golf's monthly "WARNING: Your <building> will not be operational" to an unjoined building, several of one kind take turns), `pv_show_building` | Never writes game state except the camera tile on "Show the building" (the golfer jump does the same) |
| src/port/paths_frosted.c, paths_frosted.h (new) | render thread: world layer (under the HUD and golfers: veil, mint network tiles, amber unreached path tiles, "No path" markers) and top layer (over the HUD: the button, legend, hover card) plus the Y key | |
| src/port/ui/ui_paths.c, ui_paths.h (new) | the drawing as a pure function of a PvNote | |
| src/port/hd_native.c, hd_native.h | a second note channel (`hd_ui_note2_set`, `HdUiFrame.note2`) so the golfers' note and the paths' note both ride the frame | One blob per frame was taken |
| src/port/golf/hd_ui.c, golfers_hooks.c | `pv_frame_begin` (before the golfers' frame begin: its layer goes under their tags) and `pv_course_done`; browser build no-ops | |
| src/port/hud_game.c, hud_frosted.h, ui/ui_hud.c | `HUD_SHOW_BUILDING`: the warning message gets a "Show the building" button (camera to the building, path view on) | task-62 notification |
| src/port/golf/keymap.c, options_frosted.c | the Y key as a "This mod" action (own setting `ui.paths_key`), the Options row | |
| cmake/ui.cmake, mods/builtin/ui_paths, mods/load_order.toml, tests/mods/all_off.ini, data/mods.ini, tools/check.sh | build, the mod, the switch, the check stages | |
| tests/native/paths.mjs, paths.sh, paths_ab.mjs | the game test (place a building, marker, path tile, marker gone, warning, Show) and the 2400 frame state A/B | |


## task-73: Frosted Mint golfer profile page and land purchase map

Switch: the same as the dialogs and reports (`ui.dialogs` and `ui.reports` both frosted_mint); `[ui] dialogs=original` restores the original screens.

| File | Change | Why |
|---|---|---|
| port/tools/translate/golf.pages.json (new) | FUN_004385d0 (profile page loop): `rp_begin(30)`, text calls through `rp_tc`, `rp_frame` / `rp_done`, the face picker and bio editor hooks; FUN_0045b0d0 text through `rp_tcp`; FUN_004587a0 (land map): `rp_begin(32)`, text through `rp_tc` / `rp_t7c30` / `rp_t8430` / `rp_t8080`, `rp_frame`, `rp_esc`, every return through `rp_end` | the two screens as native pages |
| port/tools/translate/golf.pages.json: FUN_0047a4c0, FUN_00486110 | the widget create's call `FUN_0047abe0(this, param_6)` takes the parent (`rp_warg`) while the profile's bio text widget is created (`rp_wfix`) | the decompile passed the flags instead of the parent; the bio editor crashed. Fixed for this widget only: the front end's list widgets break when it is fixed for all |
| src/port/reports_game.c, reports_frosted.c, ghidra_prelude.h | page kinds 30 (profile) and 32 (tracts), page picture capture (portrait, map), the record functions for golf's string calls, Esc on the map | |
| src/port/ui/ui_pages.c, ui_tracts.inc (new) | the panels as pure functions of an RpSnap | |
| src/port/bridge.c | `call` with `"async":1` runs a modal screen after the bridge returns to the game loop, then clears golf's click and key result flags | tests open the land map, which only the year end opens |
| src/port/ui/ui_gallery.c, tools/check.sh, tests/native/reports.mjs | gallery pages `pages-profile`, `pages-tracts`; profile (trait, bio typing, Cancel, Esc) and land map (Esc, no thanks, buy) in the reports test | |

## task-78: realistic water (display.water=hd)

| File | Change | Why |
|---|---|---|
| src/platform/terrain_gl.h, terrain_gl.cpp | The recorder also gets the triangle's world positions (before the modelview); `Context::modelview()` | The water pass needs world coordinates per water pixel (depth, waves, ripples) and the view and light directions. |
| src/platform/terrain_hd.cpp | Water batches (texture file name Water*) get flag 8; world positions per vertex; `terrain_hd_world`, `terrain_hd_view` | Which recorded triangles are water, where they are. |
| src/platform/terrain_dll.cpp | `terrain_water_tiles` (water code and centre height per tile), `terrain_tile_water` | The map's water for depth, flow, waterfalls and ball splashes. |
| src/port/hd_terrain_native.c | The scene carries world positions, view, the map's water tiles; water textures go to the water pass; `hdt_scene` | Hand-over to the render thread. |
| src/port/hd_native.c | `water_draw` after the HD terrain copy (compose, zoom glide), `water_after_frame` (clips), `water_poll` in `hd_present` | Water over the ground, under golf's sprites. |
| src/port/water_native.c, water_native.h (new) | Direct3D 11 water pass through SDL2's renderer (own device context state) | The water itself. |
| src/port/golf/water_hooks.c (new), tools/translate/golf.water.json, src/port/ghidra_prelude.h | FUN_0040f5c0 looks its water sprites up through `water_sprite` and queues sprites through `water_queue`; the sparkle branch checks `water_hide_golf_ripples()` | While the pass runs golf does not draw its ripple, wave, waterfall, spray, rock and coral sprites or sparkles (drawing only; with display.water=original and in the browser build every call is the original). |
| src/port/options_frosted.c | Graphics: "Water: Realistic / Original" | The switch on the Options page. |
| cmake/native.cmake | water_native.c; ui/water.hlsl and ui/water.txt next to golf.exe | Shaders and numbers. |

## task-90: trees at the farthest zoom

golf draws the terrain trees (the cell classes whose decoration type is 13: tree tiles, palms, pines, the forests of a new site) in the second cell pass of FUN_0040f5c0 only when the zoom 0x4c2844 is above 1, so the whole-course view showed bare forest ground and empty beaches. A patch adds `far_trees()` to that test. Its sprite draw queue held 1024 sprites (ten arrays of 0x400 at 0x82415c..0x8361f4, count 0x838200, cap `0x3fe < count` in FUN_00462a30); a big course (map scale 10) at zoom 1 queues thousands, so the queue moves to port storage `g_sprq` (32768 per array) and its sorted insert starts from the last inserted sprite when the new one sorts after it (the same list in far fewer steps). Growth frames, cell flags and the random generator are untouched (the tree pass only queues sprites at zoom 1 as it already did at zoom 2 and 4); tests/native/far_trees.mjs compares golf's state with the setting on and off. One difference remains by design: a tree still growing finishes its growth animation flag while seen at zoom 1 instead of waiting for zoom 2. `[ui] far_trees=0` in data/mods.ini gives the original (no trees at zoom 1, 1024 sprites).

| File | Change | Why |
|---|---|---|
| tools/translate/golf.trees.json (new) | FUN_0040f5c0: `far_trees() ||` before the zoom test of the terrain tree pass; FUN_004627d0 calls `sprq_begin()`; FUN_004627d0, 00462800, 004628d0, 00462a30, 00462be0, 00463100 use `SPRQ_*` instead of the queue arrays; FUN_00462a30 caps at `g_sprq_cap` and inserts from `g_sprq_hint` | Trees at zoom 1, room for them, speed. |
| src/port/golf/overrides.c, src/port/ghidra_prelude.h | `far_trees()`, `sprq_begin()` (reads `[ui] far_trees` once per frame), `g_sprq`, `SPRQ_*` | The switch and the storage. |
| src/port/golf/ball_hooks.c | reads the queue entries from `g_sprq` | The round ball's sprite match. |
| tests/native/far_trees.sh, .mjs, tools/check.sh | Showcase Resort at zoom 1 and 2, on and off: pictures and game state; `--evidence`: 1080p and 4K pictures, Huge course frame rate | Evidence and the check.sh stage. |


## Placement alignment and large-map staff coordinates

Screen-space lines and curves already receive projected camera coordinates. Their 2D drawing pass temporarily clears the terrain camera offset and restores it before world geometry resumes. This removes the repeated offset in pathway and landmark outlines. `fixes.placement_lines=0` restores the earlier drawing behavior.

Staff tile bytes at coordinates 128 through 159 are interpreted as unsigned positions. The shared decoder retains wrapped negative targets outside the supported map range, including the 255 unassigned marker. It is used by worker simulation, hire positioning, the selected staff map marker and weed coverage. Record layout and serialized bytes remain unchanged. `fixes.staff_tiles=0` restores the original signed reads.

Accepted news posts are captured for HUD history immediately, before another post can replace the original single news slot. Original acceptance, display, timer and RNG calls are preserved. The hook restores formatter text, decimal scratch and cached year. Frame polling still handles loaded state and does not duplicate captured posts; fixes.news_history=0 retains the earlier polling behavior.
