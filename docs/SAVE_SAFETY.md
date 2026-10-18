# Save-group recovery and dormant Living Club histories

This change applies to the native low-level save path with `fixes.safe_saves=1`.
The `.sve`, camera-tiles, course-lights, path-styles and Living Club formats are
unchanged. The existing size/CRC checks remain mandatory on load.

## Save protocol

1. Write and flush the new main save to `.sve.tmp`.
2. Have all companion writers checksum that temporary save and write companions
   beside it. A rejected lamp/history or failed writer stops here, leaving the
   entire previous generation untouched. Old staging companions are removed at
   the start of each save, so a failed attempt cannot supply a later generation.
3. Flush staged companions. If an existing known companion has no staged
   replacement, fail instead of silently leaving it bound to the old save.
4. Copy existing group files to `.sve.save-old-0` through `-4`, flush and verify
   their lengths and CRCs. Record original presence, lengths and CRCs in a
   flushed, checksummed, binary `.sve.save-journal` before changing destinations.
5. Replace the group files. On failure, restore the previous group from copies;
   originally absent files are removed. Delete the journal only after the full
   new group is installed (or the full old group restored).
6. Notify `fs_written` only for a completed group. Living Club errors now reach
   this checked path instead of being hidden in a void notification callback.

Before the native filesystem opens a `.sve` for reading or writing, a remaining
valid journal causes rollback. Recovery validates **all** required backups before
changing anything. Backups are copied during rollback, rather than consumed, so
recovery can be retried after a second failure. Invalid/incomplete journals or
corrupt/missing backups fail closed. Keep these files and obtain recovery help;
do not delete the journal to force a potentially mixed save to load.

Index mapping: 0 main save; 1 camera-tiles; 2 course-lights; 3 path-styles;
4 living-club.json. Backup files after a completed journal deletion are harmless
and may remain if best-effort cleanup fails.

## Disabled Living Club

The content needed to validate histories is initialized even when the feature is
disabled. Existing histories are loaded with the same complete validation while
narrative events and presentation remain disabled. Saving reserializes that
validated state against the new main-save CRC. Re-enabling therefore retains the
history. A disabled legacy save with no history does not acquire an empty one.
An invalid/unreadable history blocks a safe save; it is never silently rebound to
new bytes or overwritten with an empty history.

## Deliberate limits

- This is recoverable multi-file replacement, **not atomic multi-file rename**.
  Abrupt process termination during replacement is tested. Power-cut durability
  and filesystem/hardware write reordering are not guaranteed; there is no
  portable directory-flush contract here.
- One process may access a save group at a time. Concurrent game instances or
  external writers/readers of that group are unsupported. There is no OS lock.
- Browser IndexedDB saves and the explicit `safe_saves=0` direct-write mode do
  not gain this group transaction. The latter can still lose data on write errors.
- The separate advisory loaded-mod record written by `fs_written` remains outside
  this transaction and retains its existing error-reporting behavior.
- A partial journal created before destination changes can require manual
  inspection even though the old group is intact. Failing closed is intentional.
- Disk space is needed for the new group plus a full copy of the old group.
- Windows/x86 build and real-game save/load/play testing remain required. The
  public export lacks full-game build inputs; these tests do not emulate Windows
  MoveFileEx, antivirus locks, directory permissions or storage hardware.

## Reproducible portable tests

From the repository root, with Python 3 and a C compiler named `cc`:

```
python3 port/tests/save_group_test.py
python3 port/tests/living_dormant_test.py
python3 -m unittest discover -s port/tests -p 'test_*.py' -v
```

The first test compiles the actual CRT low-level I/O region and actual save-group
header, stubbing game-side writers and the filesystem boundary for injection.
It covers every sidecar failure, each destination replacement and file-flush
failure, failed rollback and retry, process death after each replacement,
corrupt backup/journal rejection, a skipped companion, and first-save rollback.
The second compiles the actual native Living Club adapter, persistence/JSON
logic and released content, with settings and game-memory APIs isolated. It
covers disabled save/re-enable, CRC mismatch rejection, invalid-history refusal,
legacy saves and disable-after-save without reloading.

These focused tests reproduced the two original failures before implementation.
All 21 existing portable translator tests also pass on the pinned public source.
