# Large-course performance baseline (2026-10-25)

This is a baseline for [issue #85](https://github.com/finalpantasy/simgolf-mod/issues/85),
not a measured improvement from the source change in this branch.

## Reproduction

- Installed v0.1.30 (`25ab425`), Windows, Direct3D 11.
- A fresh copy of the same 158 x 158 course autosave was loaded for each run
  through `--headless --script`. All runs started at zoom 4.
  `SIMGOLF_USER_DIR` pointed at a disposable folder so the player's saved games
  were not changed. `--mods` and `SIMGOLF_OPTIONS_FILE` selected disposable
  copies of the settings files.
- The same course and zoom-4 camera were used for the weather comparisons at
  both 800 x 600 and 2560 x 1440. The player's earlier 2560 x 1440 log also
  reported about 11 game frames/s and 100% game-code time on this map.

| Setting after load | Original game frames/s (`perf:`) | Game-code time | Presenter frames/s (`hd:`) |
| --- | ---: | ---: | ---: |
| 800 x 600, speed 3, weather off | about 11 | 100% | about 180 |
| 800 x 600, speed 3, storm/rain/clouds on | about 11 | 100% | about 175 |
| 800 x 600, speed 1, weather off | about 10.8 | 43% | about 180 |
| 2560 x 1440, speed 3, weather off | about 10.9 | 100% | about 164 |
| 2560 x 1440, speed 3, storm/rain/clouds on | about 10.9 | 100% | about 120 |
| 2560 x 1440, speed 3, storm/rain/clouds on, water reflections off | about 10.9 | 100% | about 128 |

The speed-3 loop is CPU saturated even with weather off. Weather adds render
work, especially at native monitor resolution, but the original game loop
remained at roughly the same rate in these controlled runs. Speed 1 leaves
game-code headroom. Other hardware or course views may behave differently.

With `SIMGOLF_RENDER_PROFILE=1` and the 2560 x 1440 test, the mean render
compose time after course load rose from 2.65 to 5.69 ms per frame. The
measured reflection stage rose from 0 to 2.20 ms, cast shadows from 0.01 to
0.91 ms, and art drawing from 0.13 to 0.95 ms. This profile identifies
separate weather rendering costs; it does not account for the speed-3 game
loop saturation. Setting `environment.reflections=0` is an optional visual
tradeoff that recovered about 8 presenter frames/s in this run. It did not
raise the original game-loop rate.

## Candidate source path

`FUN_0043d6f0`, the sprite-cell lookup, calls `fix_re_bounds_on` for visible
sprites. Before this branch, each call to `fix_re_bounds_on` searched the
settings table and parsed `fixes.re_bounds` again. The change caches the
boolean until `mods_serial()` changes, following the existing
`bounds_fixes.c` pattern. Live setting changes still invalidate the cache.

The cost reduction has **not** been measured in a rebuilt executable: the
generated Ghidra code and native x86 build dependencies are not in this
checkout. Keep the baseline separate from any eventual before/after claim.

## Farthest-zoom tree bottleneck

On the same 158 x 158 course, two zoom-out wheel events selected zoom 1. With
`ui.far_trees=1`, the game's headless `perf:` yield rate fell from roughly
630/s at zoom 4 to roughly 20–40/s at zoom 1 while game-code time stayed at
100%. With `ui.far_trees=0` at zoom 1, the yield rate returned to about
640–650/s. This setting changes the tree sprite pass and queue size, so the
A/B result isolates that path, though it does not distinguish tree production
from sorted queue insertion. It is a strong lead for [issue #85](https://github.com/finalpantasy/simgolf-mod/issues/85).
The HD presenter averaged 26.2 frames/s with distant trees on and 121.5
frames/s with them off; HD art drawing averaged 7.55 versus 3.50 ms/frame.
That indicates rendering work is also material at zoom 1. The queue change
does not address all of that render cost.

`FUN_00462a30` maintains the sprite queue as a sorted linked list. Its current
hint starts at the last inserted sprite only when that sprite sorts before the
new one. For nonmonotonic keys, it walks from the head. This branch adds 64
bounded insertion hints and begins the walk from the largest known key below
the new key. The same strict comparison retains the original order for equal
keys. `performance.sprite_queue_fingers=0` restores the single-hint behavior.

The standalone C test in `port/tests/sprq_fingers.c` compares the exact queue
index order across ascending, descending, random, cyclic and duplicate-heavy
keys. All 8,000-entry cases match. It counted 10,469,422 linked-list walks in
the random case with the prior single hint versus 491,578 with the bounded
hints. This is a synthetic traversal count, **not a measured in-game speedup**.
The rebuilt native game still needs a far-tree visual and frame-time check.
