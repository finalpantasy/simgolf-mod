# Updater safety regression checks

Run from the repository root with Python 3 and GCC:

    python3 -m unittest discover -s tests -p 'test_updater_safety.py' -v

The Python suite extracts the actual changed C functions and compiles them with
`-std=c11 -Wall -Wextra -Werror` against isolated test doubles. Format-truncation,
unused fixture helpers and partial Win32 structure initializer warnings are
suppressed. Test executables and filesystem fixtures live in temporary folders.

Coverage:
- FindFirstFile access-denied/path errors fail copying; empty directories pass
  only if the destination directory was successfully created.
- FindNextFile errors after a copied entry fail; normal end-of-directory passes.
- UI shipped files update while optional terrain and player files survive.
- Failures during UI preservation and release overlay roll back the whole tree
  and both program names.
- Player symlinks cause a safe rollback rather than following/copying the link.
- Consecutive successful updates and completed-backup pruning preserve terrain.
- The diagnostics exporter and guide are included in upgrades and rollback.
- Extraction timeout/wait failure requests termination and waits for confirmed
  exit before cleanup, including when the termination request fails.
- Outer prepare wait does not return on a transient wait error.

These tests do not validate Windows APIs, real tar extraction, BCrypt hashing,
process scheduling, ACLs, junction behavior, SDL UI, or the full game build.
The public repository's Source development guide documents absent internal test
sources/build helpers. Run its Windows updater tests and real extraction/rollback
scenarios in the maintainer's complete build before shipping.

## Intentional tradeoffs

The release lacks a per-file ownership/deletion manifest for UI. Consequently
no per-file ownership/deletion manifest is validated. Files absent from an
update are retained, including obsolete formerly shipped UI
files. Files included in the release replace matching paths. Existing UI needs
space for both the backup and copied installation. Reparse points abort safely;
this change does not implement portable link copying.

The outer preparation deadline is removed so it cannot orphan the extractor.
Tar retains its five-minute deadline; termination is requested and its exit is
confirmed. If checksum/filesystem operations hang, termination fails, or a valid
process handle repeatedly fails to signal, the update remains busy rather than
allowing an unsafe retry. This is deliberately fail-closed, not bounded
cancellation. A Windows-tested cancellable job-object design is future work.
Concurrent manual updater invocations and externally killing prepare are not
newly synchronized by this change.
