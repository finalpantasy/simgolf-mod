# Large-course performance baseline (2026-10-25)

This is a baseline for [issue #85](https://github.com/finalpantasy/simgolf-mod/issues/85),
not a measured improvement from the source change in this branch.

## Reproduction

- Installed v0.1.30 (`25ab425`), Windows, Direct3D 11.
- A copy of a 158 x 158 course autosave was loaded through `--headless --script`.
  `SIMGOLF_USER_DIR` pointed at a disposable folder so the player's saved games
  were not changed. `--mods` and `SIMGOLF_OPTIONS_FILE` selected disposable
  copies of the settings files.
- The same course and camera were used for the weather comparisons. The
  automated presentation was 800 x 600; the player's earlier 2560 x 1440 log
  also reported about 11 game frames/s and 100% game-code time on this map.

| Setting after load | Original game frames/s (`perf:`) | Game-code time | Presenter frames/s (`hd:`) |
| --- | ---: | ---: | ---: |
| Speed 3, weather off | about 11 | 100% | about 180 |
| Speed 3, storm/rain/clouds on | about 11 | 100% | about 174-175 |
| Speed 1, weather off | about 10.8 | 43% | about 180 |

The speed-3 loop is CPU saturated even with weather off. Weather adds render
work, but the original game loop remained at roughly the same rate in this
controlled run. Speed 1 leaves game-code headroom. A larger or differently
positioned view and other hardware may behave differently.

## Source hotspot

`FUN_0043d6f0`, the sprite-cell lookup, calls `fix_re_bounds_on` for visible
sprites. Before this branch, each call to `fix_re_bounds_on` searched the
settings table and parsed `fixes.re_bounds` again. The change caches the
boolean until `mods_serial()` changes, following the existing
`bounds_fixes.c` pattern. Live setting changes still invalidate the cache.

The cost reduction has **not** been measured in a rebuilt executable: the
generated Ghidra code and native x86 build dependencies are not in this
checkout. Keep the baseline separate from any eventual before/after claim.
