# Local crash diagnostics

Windows releases include `Collect-Diagnostics.ps1` and this guide next to
`golf.exe`. The installer copies both from the release `app` folder. This is a
manual, local exporter, not an automatic reporting service. Nothing is uploaded.

## Players

1. Close the game. Avoid repeated restarts before collecting evidence.
2. Open Windows PowerShell in the install folder. Preview without creating a ZIP:

   `powershell -NoProfile -File .\Collect-Diagnostics.ps1 -PreviewOnly`

3. Run without `-PreviewOnly` to see the same file list and privacy warning.
   Type exactly `EXPORT` to create the ZIP on your Desktop. Anything else cancels.
   Use `-OutputDirectory 'C:\existing\folder'` if Desktop is not writable.
   If Windows blocks the script, follow your organization's policy; do not change
   machine execution policy just for diagnostics.
4. Extract and inspect the ZIP before attaching it to a bug report. Include your
   reproduction steps, what you expected, and what happened. Redaction is best
   effort: free-form log text may contain private information beyond known paths.

## Submit the report to GitHub

After export and temporary-file cleanup, the collector prints these steps and
this fixed page: https://github.com/finalpantasy/simgolf-mod/issues

1. Extract and inspect the ZIP. Issues and their attachments are public, so
   remove private information before sharing. Saves and companions are not
   redacted, and logs may contain private text beyond known paths.
2. Visit the issues page and sign in to GitHub, or create an account.
3. Search for the same problem. Add a comment to a matching issue if appropriate;
   otherwise select **New issue**.
4. Add a short title, exact steps to reproduce, what you expected, what happened,
   and the version shown in your installed `version.txt`.
5. Drag the reviewed ZIP into the issue description or comment. Wait until the
   upload completes and an attachment link appears before continuing.
6. Review everything, then submit the issue or comment yourself.

Type exactly `OPEN` at the collector's final prompt to open the issues page in
your browser. Press Enter or enter anything else to finish without opening it.
Preview and cancellation never launch a browser. If opening the browser fails,
the ZIP stays where it was saved; open the printed link manually. The link never
contains your local paths, log contents, or other diagnostic data. The collector
does not sign in, upload files, or submit an issue for you.

## Optional arguments


- `-LogPath 'D:\logs\custom.log'`: the exact log used by the game's `--log` option.
  A relative log path is resolved against `InstallDir`, matching the native game.
  Relative `InstallDir`, `SavePath`, and `OutputDirectory` arguments are resolved
  against your current PowerShell location, even after `Set-Location`.
  A rooted log path such as `\logs\golf.log` uses the install drive.
  Drive-relative paths such as `C:logs\golf.log` require the same drive as
  the install folder; otherwise use a full path such as `C:\logs\golf.log`.
- `-InstallDir 'D:\SimGolf'`: defaults to the folder containing the script.
- `-SavePath 'D:\game\my-course.sve'`: explicitly opt in to this individual save.
  From PowerShell you can supply multiple paths as an array. Saves are never
  discovered or selected automatically, and no folder is recursively collected.
  Save bytes are unchanged and may contain player/course names or other private
  game data. Exported names are `save-01.sve`, `save-02.sve`, etc.
- `-IncludeSaveCompanions`: an additional opt-in that requires `-SavePath`.
  Includes only existing files with exact suffixes `.camera-tiles`, `.course-lights`,
  `.path-styles`, and `.living-club.json` appended to each selected save's full
  filename, for example `my-course.sve.path-styles`. Each appears in the preview.
  They are renamed to match the exported save (`save-01.sve.path-styles`) and
  copied byte-for-byte, including any CRC-bound content. Even JSON companions
  are not redacted because edits can break reproduction. They may contain private
  game data. No arbitrary sidecars, `.tmp` files, or backups are collected.
  Companions are omitted if their selected save is skipped as oversized.

## Contents and privacy

The allowlist is `golf.log`, numbered prior sessions `golf.log.1` through `.5`,
`golf.log.crash.txt`, and `version.txt` when present; missing files are harmless.
Custom log basenames are normalized to these names in the ZIP. Text over 8 MiB
and each save or opted-in companion over 64 MiB are skipped with a warning; linked inputs are refused.
The collector never bundles configuration files, environment variables, complete
memory dumps, screenshots, game assets or executables. It records SHA-256 hashes
of `golf.exe`, `Sim Golf Remastered.exe`, and `SDL2.dll` when present.

Text copies replace known install and user-profile paths, `SIMGOLF_GAME`,
`SIMGOLF_USER_DIR`, the `game=` path from `golf.ini`, the log directory, game/save paths in host log headers, and explicitly selected
save directories with `<INSTALL>`, `<USER>`, `<GAME>`, `<LOGS>`, or `<SAVES>`. Slash styles and case
are handled. Header paths are discovered across all selected logs before any text
is exported, so a header in an older log also protects matching paths in newer
logs. Relative `SIMGOLF_GAME` paths are resolved from the install folder; relative
`SIMGOLF_USER_DIR` paths are resolved from the game folder, as in the native host.
Single-root paths such as `\private-saves` use the corresponding host drive.
Drive-relative paths are accepted only when their drive matches that base;
otherwise use a fully qualified path. If the game folder is unknown, relative
save-environment paths are not guessed; available host headers supply the base.
Raw `stack ...` lines are omitted. Originals are never edited.
Registers and exception parameters remain useful crash evidence and may contain
numeric memory values. This is not a guarantee that all private text is removed.

`manifest.json` fields (schema 1):

- `schema_version`: integer format version
- `collected_utc`: ISO-8601 collection timestamp
- `os_version`: OS version reported by the PowerShell runtime
- `collector_process_bits`: collector process width, not the game's architecture
- `saves_included`: count of selected `.sve` files actually exported
- `save_companions_included`: count of explicitly opted-in companion files exported
- `raw_stack_included`: false
- `files`: entries with normalized `name`, original `source_bytes`, and `binary`
- `binary_sha256`: entries with executable/library `name` and `sha256`

Temporary redacted copies are removed after export or failure. The ZIP stays in
the chosen output directory until you remove it. Cancellation creates neither.

## Crash evidence and retention

On launch, the native host rotates `golf.log` to `.1`, retaining five prior
sessions. A rotation failure keeps the current log and appends the next session
instead of truncating it. Concurrent game launches are not supported; close the
game before collecting. Very old sessions eventually age out of this bounded
history.

Crash records are append-only and survive clean launches. Each record retains
existing exception/parameter/register/stack evidence and adds
`crash_record_version 2`, compiler `build` date/time and UTC `utc`. If Windows can
resolve the faulting address to a loaded module, `module` includes its basename,
load `base`, and relative `offset`. Core exception/register/stack evidence is
flushed before this best-effort loader lookup, which can stall in a damaged
process. Unmapped/generated image addresses may have
no module line. Pair this report with `version.txt` and binary SHA-256 values to
identify a precise build; compiler time alone is not a unique build identifier.
The local crash file still contains raw stack words and grows until the player
archives/removes it, so share the reviewed export rather than the raw file.

## Verification

`python3 tests/diagnostics/test_contract.py` compiles and exercises the actual
rotation helper and crash writer with minimal Win32 mocks and checks packaging/export
contracts. Run the PowerShell integration test on Windows:

`powershell -NoProfile -File tests\diagnostics\test_collector.ps1`

A public Linux checkout cannot verify actual Windows exception handling or the
full game build (generated game sources, Windows SDK, and internal tests are
required). Test a real Windows release before shipping: crash then relaunch,
custom log path, read-only log destination, repeated sessions, and collector
preview/cancel/export with and without an explicitly selected save, default
no-browser behavior, explicit `OPEN`, and browser-launch failure retaining the ZIP.
