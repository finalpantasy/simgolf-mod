# Limits: map size, golfers, holes (task-7)

What stops the land being bigger, the crowd larger and the course longer in
golf.exe v1.02, where each limit lives, what depends on it, what it costs to
raise, and the 2x prototype result. Addresses are golf.exe image addresses;
every global sits at a fixed address with no slack after it, so nothing can
grow in place (see [[ARCHITECTURE]], [[PORT_CHANGES]]).

Sources: the translated code in build/gen/golf (function `FUN_xxxxxxxx`, file
lines `gNN:line` = golf_NN.c), the save list in FUN_0040afa0, upstream notes
(simgolf-native docs, simgolfRE names and state regions). EXACT = read in code,
DERIVED = inferred.

## 1. Map grid (50 x 50 tiles)

### 1.1 The limit

| what | value | where |
|---|---|---|
| tiles | 50 x 50 = 2500, row stride 50 (`y * 0x32 + x`) | every per-tile array below |
| corner heights | 51 x 51 = 2601, stride 51 (`* 0x33`) | 0x5a4998 |
| world units | 1024 per tile (`>> 10`, `* 0x400 + 0x200` = tile centre), 25 yards per tile | golfer, ball, object positions are 32-bit ints: no width limit below 2 million tiles |
| camera | tile x, y at 0x4c2ba0 / 0x4c2ba4, clamped `FUN_00467130(v, 0, 0x31)` | FUN_0040f5c0 (5 sites), camera input |
| Terrain (renderer) | `w, h` at Terrain+0x14/+0x18, tiles inline at +0x3a4, 0x248 bytes each | port/src/platform/terrain_dll.cpp (our port of Terrain.dll); golf reads `w` for the stride, so golf's side follows it |

### 1.2 Every per-tile array

21 arrays: 12 in the save, 9 rebuilt at load. "fns" = translated functions
that address the array directly (EXACT, scanned); 65 functions in all.

| address | bytes/tile | size at 50 | saved | fns | role |
|---|---|---|---|---|---|
| 0x5722e8 | 1 | 2500 | yes | 45 | tile type (index into the 0x30-byte type table 0x578372) |
| 0x56988c | 1 | 2500 | yes | 14 | placed-object id per tile (0xff none; ids index 0x58bcb8, 256 max) |
| 0x53bbac | 1 | 2500 | yes | 6 | per-tile byte (terrain detail) |
| 0x5a6378 | 1 | 2500 | yes | 5 | per-tile byte (growth/condition, walked with 0x56c7e4, 0x53ea24) |
| 0x56c7e4 | 1 | 2500 | yes | 5 | per-tile byte (same walk) |
| 0x53ea24 | 1 | 2500 | yes | 7 | per-tile byte (same walk) |
| 0x542414 | 1 | 2500 | yes | 4 | tile types before purchase (buying land restores from it) |
| 0x5830b8 | 1 | 2500 | yes | 6 | queued terrain work: target type |
| 0x59c090 | 1 | 2500 | yes | 3 | queued terrain work: signed cost (units of $100) |
| 0x53caf0 | 2 | 5000 | yes | 35 | tile flags (u16; low 5 bits = hole number, 0x20, 0x40 visited, 0x100, 0x800, 0x1000) |
| 0x5a4998 | 1 | 2601 (51 x 51) | yes | 10 | corner heights |
| 0x543018 | 1 | 2500 | no | 6 | height field work array (FUN_0042f7a0 rebuildHeightfield) |
| 0x5619a0 | 1 | 2500 | no | 7 | per-tile direction bits (FUN_00449330) |
| 0x578804 | 1 | 2500 | no | 8 | per-tile counter (compared with per-hole 0x53f3e8) |
| 0x55eb40 | 2 | 5000 | no | 2 | per-tile short |
| 0x55fec8 | 2 | 5000 | no | 1 | per-tile short |
| 0x51b770 | 8 | 20000 | no | 2 | 8 bytes per tile (`(y*50+x)*8`) |
| 0x822da0 | 1 | 2500 | no | 3 | shade map A (FUN_004615f0 spreadShade) |
| 0x82376c | 1 | 2500 | no | 3 | shade map B |
| 0x58d370 | 1 | 2500 | no | 1 | path search distances (FUN_0042e7e0 pathStep) |
| 0x838c1c | 1 | 361 (19 x 19) | yes | 7 | height noise; fixed size, does not scale with the map |

Total: 29 bytes per tile plus the corner lattice (73 KB at 50 x 50).

### 1.3 Every dependent loop and constant

In the 66 functions that touch those arrays (EXACT scan, prototype report):

| form | count | example |
|---|---|---|
| row stride `* 0x32`, `/ 0x32`, `% 0x32` | about 300 | `(&DAT_005722e8)[y * 0x32 + x]` |
| corner stride `* 0x33` | 25 | `0x5a4998 + x + y * 0x33` |
| bounds `< 0x32`, `0x31 <`, `< 0x30`, `== 0x31` | about 110 | `(-1 < x) && (x < 0x32)` |
| row steps `+ 0x32`, `+ -0x32`, `+ 0x33` | about 90 | pointer walks a row at a time |
| loop starts `n = 0x31; do {...} n--` | 20 | syncTiles, rebuildTerrainGrid, land generator |
| clear counts (rep stosd) 0x271, 0x4e2, 0x28a, 5000 dwords | 25 | new game, load, generator |
| save sizes 0x9c4, 5000, 0xa29 | 26 | FUN_0040afa0 save, FUN_0040bbf0 load |
| neighbour symbols (row -1, column -1 by address) | 8 | `DAT_005722b6` = 0x5722e8 - 50, `DAT_0053ca8a` |

Outside those functions: the terrain glue FUN_00449470 (syncTiles, `0x31`
loop starts) and FUN_0044a5b0 (rebuildTileData, `< 0x32`), and the camera clamps.

The same numbers mean other things in the same functions, kept as they are:
sprite-sheet pixels in FUN_00442180 (loadInterfaceArt), 50-byte records
(dialogue 0x543d10, story names 0x53a454, 0x81b490), the home-site price
`/ 0x32`, a sprite frame `+ 0x32`, event id 50, `rand % 48`, hole length
`< 0x33`, golfer stat `< 0x32`, a distance `< 0x32`, the home value 0x9c4 and
the theme value at 0x4c2fa0. A loop ending at 0x53caf0 is the end of the
sprite array before it, not the grid.

### 1.4 Game rules tuned for 50 x 50, and how task-8 scales them

Land generator FUN_00470a60 and new game FUN_004315e0 (upstream
PUBLISHER_EXE_NOTES "The land generator"). the maintainer (2026-10-09): generated
features scale with the land AREA, so a 10x map gets about 10x as many and its
far corners look like the original land. s = side / 50, A = area / 2500
(GEN_LIN, GEN_AREA, GEN_TIMES in port/src/port/grid.h; golf.json patches).
Every rule gives the original value at 50 x 50.

| rule | 50 x 50 value | task-8 rule |
|---|---|---|
| owned land | border of m tiles, smallest k with `acres*10 >= 4*(25-k)^2` | `acres*10*A >= 4*(side/2-k)^2`: the same share of the map is owned |
| island ring | `abs(row - 25)` clamped 8..50, plus m - 12 + rand(3) | `abs(row - side/2)` clamped 8s..side, plus m - 12s + rand(3) |
| clubhouse | random x, y in 15..31 | 15s + rand(17s): the middle third |
| tree streaks | walks from a random tile of 0..49, total 1250 tiles, limit 5000 + 2500 per walk, stop at 45000 | start anywhere, total 1250A tiles, the limit cycles back to 5000 instead of stopping (same walk lengths, more walks); pines on the far half (row > side/2 - 1) |
| 24 patches (water, rocks, palms) | 4 x 6 grid of 12.5-tile cells; patch 0 is wetlands | (4s) x (6s) grid of the same cells; every 24th is wetlands |
| creek | one, starting at 16 + rand(16) on the far edge | round(s) creeks, at side * (3i+1) / (3n) + rand(16) |
| coast | 2..12 tiles of water per row | unchanged: the sea stays a strip along one side (water per area drops from about 230 to 150 tiles at 10x) |
| wildlife spots | (4 - difficulty) x 9 on tiles 2..47 | x A, on tiles 2..side-3 |
| Monterey / Ireland spots | 16 | 16A |
| San Diego dolphins | 8 - difficulty, rows 10..39 | x A, rows 10s..40s |
| landmarks (Hawaii, Oahu, Northeast, Wales, Spain, Jamaica) | 8 - difficulty attempts on 5..44 | x A on 5..side-6 |
| Nova Scotia lighthouses | 2, at row 25 -/+ rand(10) | 2 per original-size area, pairs spread along the shore |
| obstacles | difficulty, on 5..43 | difficulty x A on 5..side-7 |
| weeds (main loop) | one random tile of 0..49 per pick | a tile of the whole map, GEN_TIMES picks (same weed rate per tile) |
| wildlife walking across (FUN_00430360) | enters at 0..49 or tile 50, leaves past 51 | the real edges |
| buy land | 3 x 3 tracts of 16 x 16 (`(n % 3) * 0x30 / 3 + 1`), FUN_004587a0 | 3 x 3 tracts covering 1..side-2; the price follows the tile count (a 10x tract costs about 10x) |
| routing map, buy land drawing | 12 x 6 px diamonds at `(x+y)*6`, `(y-x)*3` (FUN_00456b70) | the step and the diamonds shrink with the map (map_screens.c) |

Measured (all 16 slots, pinned seed, sandbox acreage): trees per original-size
area 378 (50), 370 (100), 368 (158).

### 1.5 Save format impact

The .sve is a flat dump of fixed-size globals (FUN_0040afa0 writes, FUN_0040bbf0
reads the same list) with no magic, version or lengths. The map part is 11
blocks: 9 x 2500 + 5000 + 2601 = 30,101 bytes at 50 x 50, 12 bytes per tile.

| map | side | saved map bytes | save size (original 294,733 B) |
|---|---|---|---|
| original | 50 | 30,101 | 294,733 |
| 2x area | 70 | 58,941 | +28.2 KB |
| 4x area | 100 | 120,201 | +88 KB |
| 10x area | 158 | 299,885 | +264 KB |

With the grid pass a save made at any size has the same layout with bigger
blocks. task-8 (port/src/port/golf/map_save.c): a map other than 50 x 50
writes a 16-byte tag ("SGMP", version 1, width, height) before the first
field; reading resizes the map to the tag's size (no tag = 50 x 50), so every
save opens at the size it was made and original saves stay byte for byte the
original format. map_scale only picks the size of new games.

### 1.6 Memory per step

| map | side | game arrays | Terrain tiles (0x248 B each) | vertex arrays | total |
|---|---|---|---|---|---|
| original | 50 | 73 KB | 1.4 MB | 0.5 MB | about 2 MB |
| 2x | 70 | 143 KB | 2.8 MB | 1.0 MB | about 4 MB |
| 4x | 100 | 293 KB | 5.6 MB | 2.1 MB | about 8 MB |
| 10x | 158 | 732 KB | 13.9 MB | 5.1 MB | about 20 MB |

The prototype reserves Terrain tiles and vertex arrays for 160 x 160 always (20 MB),
which is small for a 2 GB process. Frame cost is about the same: the renderer only
visits the tiles near the camera.

### 1.7 Risk

- High: a missed 50 in a grid function is a silent wrong tile, not a crash. The
  pass is value-identical at map_scale 1 (MAP_W = 50), so the original game
  cannot regress from it; bugs only show on bigger maps.
- High: hand-written overrides of a grid function must use MAP_W themselves (none today).
- Medium: object ids are bytes in 0x56988c (256 objects), the terrain edit queue
  is 256 entries and drops extra edits silently; bigger maps reach both sooner.
- Medium: FUN_0040acd0 (distance) is 8x too large when both axes are over 16
  tiles (section 2.4); on a big map more distances are that long.
- Low: the 19 x 19 noise grid wraps (finer relief repeats on big maps).
- Overlap with task-3 (widescreen): the camera clamp and the Terrain visible
  range now follow MAP_W; scroll bounds work for task-3 should use MAP_W too.

### 1.8 How the size becomes a setting (prototype, kept for task-8)

port/tools/translate/grid.py runs after translate.py (port/tools/translate/run.sh):
it moves the 20 scalable arrays to storage allocated at start-up (GRID(i),
port/src/port/golf/grid.c), re-encodes neighbour symbols as (row, column) with
the new stride, and turns the constants in 1.3 into MAP_W / MAP_H. golf.ini
`map_scale=` (land area as a multiple of the original; SIMGOLF_MAP_SCALE for
tests) picks the side: 1 = 50, 2 = 70, 4 = 100, 10 = 158 (max 160, Terrain's
TERRAIN_MAX). The Terrain port takes its size from it and centres its camera on
the map's real middle. tests/grid_pass_test.py covers the rewrite rules.

### 1.9 Prototype result (2x per side = 100 x 100, 4x land)

New game (Sandbox, San Diego) on the hidden desktop, muted, map_scale 4:
reaches the course view, a golfer arrives and plays for a minute, the camera
scrolls to all four edges, no crash. Same run at map_scale 1 looks like the
original. Evidence: evidence/task7-sheet.png and evidence/task7-scale*.png (local, not in git), evidence/task-7-prototype.txt.

Frame time (hd log, 800 x 600): original 119.7 fps, frame 8.35 ms avg, p99 10.2 ms;
100 x 100: 114.5 fps, 8.73 ms avg, p99 29 ms; game tick 10.8 vs 10.0 per second.

Broke on the way (fixed in the pass, listed so task-8 knows the traps):
1. A sprite loop ending at the grid's start address was moved with the grid: crash at load.
2. Sprite-sheet pixel offsets (+50) in loadInterfaceArt were treated as rows.
3. syncTiles and rebuildTerrainGrid start their loops at a literal 49: only a 50 x 50 corner reached the renderer (black void, cliff walls).
4. The renderer centred its camera on tile 25: everything drew off screen.

task-8 fixed what was still wrong at 100 x 100: the land generator and the
other rules in 1.4 scale with the map, saves carry their size (1.5) and open
at it, the routing map and buy land screens draw any size. Also found by an
audit of every grid.py rewrite: two loop ends just past an array (the
heightfield rebuild FUN_0042f7a0 and the generator's water-edge corner pass
ran one row on the relocated arrays, at every size), the raise/lower land
stride written as 0x22 + 0x11, a last-row start 0x992, and ten false rewrites
of 48/49/50 that are not the map (kept by KEEP_RE in grid.py).

## 2. Golfer pool

### 2.1 Slot id space (EXACT unless noted)

| ids | meaning | evidence |
|---|---|---|
| 0..0x97 (152) | live golfers; every loop and every `% 0x98` ring uses this | see 2.2 |
| 0x98, 0x99, 0x9a | scratch records inside the saved array: 0x98 = story/pair snapshot copy A (0x582cb8), 0x99 = copy B and the player's pro (0x582db8; FUN_00424120(0x99), FUN_004099f0(0x99)), 0x9a = hole-build trial golfer (0x582eb8; g01:17617, FUN_00424120(0x9a)) | g01:4428,4444,25679-25700,17617-17668 |
| 0x9b | present in the array (156 slots), no use found | save size 0x9c00 = 156 x 0x100 |
| >= 0x9c | not a golfer record. 0x9c+n (n < 100) = celebrity n, drawn with sprite tag `(0x9c+n) \| 0x100` | g00:473 (FUN_004017d0); FUN_00462020 `param_1 < 0x9c` g03:27789,27821; celebrity table note in upstream DECODE_BODIES.md (DERIVED) |
| 0xff | "none" sentinel in a byte-wide sprite tag: `(tag & 0xff) != 0xff && < 0x98` | FUN_00462be0 g03:28515 |

Hard consequence: golfer ids must stay below 255 unless the sprite tag (queue arrays 0x82a15c, 0x830164, 0x831164) is widened, and ids 0x98..0x9b and 0x9c.. are already taken by scratch and celebrity codes.

### 2.2 Every loop or ring bound over golfer slots (all = 152)

Loop-end constants are `field_addr + 0x9800` (0x98 x 0x100), e.g. 0x582cd9 = 0x5794d9 + 0x9800.

| function | sites | role |
|---|---|---|
| FUN_00421bc0 | g01:29404, 29553 (`% 0x98`), 29496 (`< 0x582d6e`) | claim a slot, type-uniqueness scan |
| FUN_0040f190 | g01:13606 (`% 0x98`), 13616 (`< 0x582cd9`) | start practice/match/tournament pair, reset waiting golfers |
| FUN_0040f5c0 (main loop) | 15484-15500 (`% 0x98` scan, spawn gate), 15539 (`<0x582cd0`), 16149 (0x98 dword clear of 0x567b34), 18772 and 21711 (`< 0x98`), 21639/21645 (`% 0x98`), 19792, 24361, 27050 (`<0x582cc0`), 25380 (`<0x582cd1`), 27281 (`<0x582cb8`) | spawn, draw, camera, per-frame passes |
| FUN_004289e0 (per-golfer AI) | 36916, 38461 (`<0x582cd9`), 38749 (`<0x582d76`), 37591 (`% 0x98`), 38855 (`0x97 <`) | main update plus neighbour/give-way ring scans (O(N^2)) |
| FUN_00402a40 | g00:2017 (`<0x582cbc`) | staff walkers look for nearest golfer |
| FUN_00430360 | g02:1215 (`<0x582d9c`) | wildlife near golfers |
| FUN_004315e0 (new game) | g02:1851 (0x2700 dwords = 156 slots cleared), 1899 (0x98 dwords of 0x567b34 set -1), 1934 (`<0x582d5a`, partner = -1) | init |
| FUN_00435760, 00453330, 00456be0, 0045a090, 0045de80, 0046c970, 0046d040 | g02:7300-7522; g03:9773; g03:15537; g03:17439-18340; g03:22517-22544; g04:13709; g04:14092 | employee panel, hole stats, routing map, tournament results, VIP announce, loadGolfPro, initGolferState |
| FUN_00459850 (clubhouse list) | g03:16922, 17213; **stack array `uint[152]`** g03:16849 | waiting-golfer list |
| FUN_0045a090 (tournament results) | **stack array `int[152]`** g03:17411 | sort by score |
| FUN_00422530, 00467a00 | `0x97 < param_1` g01:29862, g04:3508 | "pseudo golfer" test (id >= 0x98) |

Pairing is `slot ^ 1` everywhere (partner field +0xa2 defaults to self). Keep the pool even.

### 2.3 Per-slot data (everything indexed by golfer slot)

| data | address | size | per slot | saved | note |
|---|---|---|---|---|---|
| golfer record | 0x5794b8 | 156 x 0x100 = 0x9c00 | 256 B | yes (FUN_0040afa0) | EXACT. Record is full: last field +0xf0 skills[12], only +0xfc..+0xff free (DERIVED from upstream golfer.rs) |
| idle/stuck timer | 0x56962c | 152 ints (0x260) | 4 B | no | EXACT index `local_6c*4` g01:38114-38185 (FUN_004289e0); size DERIVED (next global is 0x56988c) |
| label x | 0x567b34 | 152 ints (0x260) | 4 B | no | EXACT clear loop 0x98 dwords (g02:1899) |
| label y | 0x567d94 | 152 ints (0x260) | 4 B | no | EXACT index `slot*4` (FUN_0040f5c0 draw pass); size DERIVED |
| story pairs / shot traces | 0x59fc60 | **16** x 0x388 = 0x3880 | n/a | yes | EXACT loop bound `< 0x5a34e0` (FUN_004099f0 g01:7001). Each holds 2 embedded golfer copies (+0x188, +0x288), 3 x 32 int path arrays, counter capped at 31 (FUN_00409950). Brief said 10, code says 16 |
| class counters | 0x59dea0 | 4 x 8 ints | indexed `slot & 3` | yes | balances skill classes (FUN_00421bc0) |
| ring cursor / last created | 0x59ae7c / 0x5a5a24 | 4 B each | n/a | yes | both mod 0x98 |
| arrival (tee order) counter | 0x56a51c | int | n/a | yes | stored into golfer +0xbe as **short**: FUN_0045de80 g03:22271-22273 `(short)DAT_0056a51c`, compared as `DAT_0056a51c - (short)field` in FUN_004289e0 g01:36895-37131. Wraps wrongly after 32767 counts (16383 pairs teed off) |
| shadow/hit list | 0x5a47e8 (x), 0x5a4868 (y) | counted by 0x58bcb0, no bound check | n/a | no | EXACT g01:18754-18756. DERIVED spacing: x list has room for 32 entries before 0x5a4868, y list 76 before 0x5a4998. More than 32 golfers drawn in one pass corrupt the y list, more than 76 reach the corner-height grid |
| balls in flight | 0x5736b0 | 256 x 36 B | n/a | yes | bound `< 0x575ab4`; each golfer scans all 256 (FUN_00409a90) |
| thought/event queue | 0x5689e8 | 100 x 8 B | keyed by golfer type | yes | FUN_00426670 g01:32138 |

Nothing else found indexed by slot. Total fixed cost per extra slot: 256 + 12 = 268 B RAM, 256 B save.

### 2.4 Golfer world position

| field | offset | width | evidence |
|---|---|---|---|
| x, y | +0x00, +0x04 (0x5794b8, 0x5794bc) | **32-bit int**, 1/1024 tile (tile centre = `t*0x400+0x200`) | EXACT: stores `(&DAT_005794b8)[slot*0x40] = tile*0x400+0x200` g01:17649-17701, 26339; reads `>> 10` g01:11318 |
| ball x, y, z | +0xd4, +0xd8, +0xdc | int | EXACT (`DAT_0057958c/90/94`, undefined4 arrays) |
| shot origin | +0xc4, +0xc8 | int | EXACT (`DAT_0057957c/80`) |
| path trace x,y,z | story-pair arrays | int | EXACT g01:6894-6955 |
| staff x,y | 0x585850 +0, +4 | int | EXACT g00:1610-1612 |

No short truncation of golfer coordinates found (grep of `(short)`/`(ushort)` casts on these fields: none). 100 tiles = 102,400 units fits an int with 20 bits to spare. Real risks are elsewhere:
- FUN_0040acd0 (distance): components above 0x4000 (16 tiles) are divided by 8; result scale is `1 x 8` per big axis, so **64 when both axes are big** (matched C in simgolfRE re/match/golf_util.cpp). Overflow-safe, but distances over 16 tiles on both axes come out 8x too large. DERIVED impact on any far-away scan.
- Tile index math `x*0x32+y` (50 wide) and `>>10` are map-size issues, not position width.
- Per-hole shorts in the hole record (+0x15c drive total etc., g01:38494-38520 `*(short*)(0x575c0c+...) +=`) overflow sooner with more golfers (DERIVED).

## 3. How many golfers arrive

### 3.1 Spawn gate, every tick (87 ms) in FUN_0040f5c0 g01:15470-15530 (EXACT)

```
scan slots from last-created backward (152 slots):
  Q  = #(state == -1 waiting) + #(state == 1 and strokes == 0)   // state = +0x21, strokes = +0x22
  waitingPair = oldest slot s with state(s) == -1 and state(s^1) == -1
  teeFree = hole1.par != 0 and no golfer on hole 1 with strokes == 0
want = ((clock & 0x7f) == 0 || Q < 2) && Q < 8        // 7 is the most that can queue
if want && !(flags 0x59e7b8 & 0x200000 tournament) && hole1.par != 0:
    slot = FUN_00421bc0(1)          // one golfer per call, one call per tick
tee off: if !tournament && teeFree && waitingPair != -1 (and flag 0x100000 clear, else nobody on hole 1 at all):
    state(s) = state(s^1) = 1 ; FUN_0045de80 (tee order, VIP announce)
```
Matches upstream GAMELOGIC.md ("fewer than two waiting, or every 128 ticks, up to eight"). A month is 1024 ticks (DERIVED, sve.md), so the 128-tick refill is 8 per month on top of the "<2 waiting" refill.

### 3.2 Which person: FUN_00421bc0 (g01:29347-29560, EXACT)

1. Slot = `0x59ae7c % 0x98`, cursor++ . The slot is **zeroed without checking it is free**. A pool shorter than the lifetime of a slow golfer risks overwriting a live golfer.
2. Person (profile) = `rand(0x4b) + 1`, tried up to 999 times, accepted only if all hold:
   - no other waiting golfer (state -1) has the same profile or the same `profile % 0x13`;
   - no other golfer on the course (state > 0) has the same profile;
   - profile not banned (`0x5849e0 + p*0x2c + 0x29 != 0xff`);
   - for the main spawn (param 1): membership level `byte +2 != 0` (invited). Customize/replay calls pass 0 and skip this.
3. Failure: "Your membership is declining." (0x4c6cac), cursor restored, returns -1.
4. Class balancing via 0x59dea0; start mood, skill class from the profile (no read of rating, price, cash).

No term for course rating, green fee, cash or population exists in the arrival path. The brief's "rating - 10 squared ... cash/200" formula (upstream PUBLISHER_EXE_NOTES.md line 214) belongs to the pro/tournament draw (g01:15555 `rand(100)` over 0x58dd70), not to ordinary arrivals.

### 3.3 What actually limits concurrent golfers

| limit | value | basis |
|---|---|---|
| distinct invited, unbanned profiles | at most 75 (profiles 1..75) | EXACT rule above; invited count starts at 12 |
| waiting + first-tee queue | 7 | EXACT |
| slots | 152 (bound only if profiles were raised) | EXACT |
| tee throughput | a pair leaves only when no golfer on hole 1 has strokes == 0, i.e. one pair per "time to first swing" | EXACT |

Invites (the real "population" lever): new game invites 12 (3 per skill class; FUN_004315e0 g02:1938-1972, sets `+2 = 1`). Each round-end upgrade (FUN_00406670 g01:2086-2091) sets one more random uninvited profile to level 1 (up to 1000 tries). Points rule: `(1 << level) <= points`, level < 5 (simgolfRE membership.md, DERIVED). A golfer whose mood stays below 0 quits and sets `+0x29 = 0xff` (profile banned for the save) so the pool only shrinks on bad courses (same note). Observed in a real autosave: 19 golfers active (port/docs/formats/sve.md).

## 4. Other caps

| table | address | cap | enforcement | save |
|---|---|---|---|---|
| profiles (people) | 0x4d6088 (0x230 each), 0x5849e0 (0x2c each), 0x543d10 (0x4e2 each) | 76 saved + 4 more in 0x4e06c8 when flag 0x40000000; arrays are 84 deep | `rand(0x4b)+1` picks 1..75; specials 0x4c.. set by hand (g01:13648) | yes, 3 blocks |
| staff/walkers ("roster" 0x585850) | 0x585850 | **64** x 0x4c (not golfers) | update loop `< 0x40` g00:2151; FUN_00402970 spawn scans for a free flag with **no bound** g00:1594-1602 (65th corrupts 0x586b50) | yes 0x1300 |
| placed objects | 0x58bcb8 | **256** x 16 | `0xff < index return 0` FUN_0040e000 g01:11919. Object id is also stored in the byte grid 0x56988c (0xff = none) | yes 0x1000 |
| terrain edit queue | 0x586b50/0x586fa8/0x5a8834 | 256 each | `< 0x100` FUN_00409cb0 g01:7155, extra edits silently dropped | no |
| celebrities | 0x55d738 | 100 x 0x25 | `rand(100)` g01:20377 | no |
| pro golfers | 0x58dd50 | 100 x 0x38 | `rand(100)`; 96 shipped | conditional block |
| story pairs | 0x59fc60 | 16 | bound `<0x5a34e0` | yes |
| balls in flight | 0x5736b0 | 256 | FUN_00409a90 | yes |
| event queue | 0x5689e8 | 100 | FUN_00426670 | yes |
| hole name strings | 0x4c2e38 / 0x4c2e88 / 0x4c2ed8 | 20 pointers each | FUN_00407280 `[param_1]` | rodata |
| tournament field | slots 0..2H-1, two per hole | 36 on 18 holes | upstream DECODE_TOURNAMENTS.md (EXACT there, not re-read) | in golfer array |

## 5. Hole cap (18)

### 5.1 Where holes are created and refused (EXACT)

| place | rule |
|---|---|
| `0x5685f0` | next hole number = first record with par 0 (recount loops g01:13429-13436 unbounded, g03:15538-15548 bounded `< 0x578148` = records 1..18, default 0x13) |
| tool deselect | `if (0x12 < hole && tool == 0) tool = -1` FUN_0040f5c0 g01:18864 (tee tool dies at hole 19) |
| tee/green apply | refused when `0x12 < hole` g01:23277, 27483; green stored only `if hole < 0x13` g01:27860; cursor previews `< 0x13` g01:23171, 23360, 17609 |
| toolbar | FUN_00433190 g02:4601,4604 hides tee/green buttons at hole >= 19 |
| tile flag | hole number lives in the **low 5 bits** of u16 grid 0x53caf0 (`& 0xffe0 \| n`, `& 0x1f`) g03:15507, g01:27852: hard cap 31 |
| size tier | FUN_0044faf0 (course-size bucket): holes 0-5 -> 0, 6-9 -> 1, 10-17 -> 2, exactly 18 -> 3, **more than 18 -> -1** g03:4989-5003. Callers: hire g01:8413, g01:5579, 11461, 20328, g02:5609, g03:6413 |
| milestones | holes 9 and 18 trigger events (FUN_0040e720 g01:13402-13423, FUN_0044fb30 g03:8880-8886) |
| golfer state | `+0x21` = hole 1..18, 19 = walking home, 0 free, -1 waiting. Record 19 must stay empty: it ends the recount scan |
| UI | leader board height `holes*0x16+0x10` g03:17534; hole list pages every 18 rows g03:5203; stack arrays `int[20]` FUN_00433190 g02:4229, FUN_00456be0 g03:14113; hole renumber loop `< 0x14` |

### 5.2 Arrays sized by hole count (index range 0..18 or 0..19)

| array | address | size | hole dimension | saved |
|---|---|---|---|---|
| hole records | 0x575ab0 | 20 x 0x208 = 0x28a0 | 20 (0 unused, 1..18, 19 sentinel) | yes |
| yardage markers | 0x59ae80 | 19 x 24 = 0x1c8 | 19 (`+ hole*6` ints) g01:17783 | yes |
| per-class hole stats | 0x56ae70 | 32 x 0xb8 (each 8 + 19 + 19 ints) | 19 | yes 0x1700 |
| stroke histogram | inside hole record +0x28, `hole*0x104` shorts | per record | n/a | via records |
| per-profile hole flags + last card | 0x5849e0 + p*0x2c: +3..+0x15 flags, +0x16..+0x28 card | 19 B + 19 B per profile x 84 | 19 | yes 0xe70 |
| golfer record: card +0x23, mood +0x37, story +0x4a, end mood +0x5c | 19 B each x 4 per slot | 19 | yes (inside 0x9c00). +0x23 EXACT (`(&DAT_005794db)[slot*0x100+hole]` g01:33968); others DERIVED (upstream golfer.rs) |
| hole name tables | 3 x 20 pointers | 20 | no |
| membership roster counter | stack `int[19]` FUN_00454c50 g03:11546 | 19 | no |

Save impact: any change to these sizes changes offsets of everything after it. The `.sve` has no magic, version or lengths (sve.md), so old saves cannot load into a changed layout and a changed save cannot load in the original. Needs our own container (magic, version, per-block sizes) when mods are on.

## 6. Cost and risk of raising each limit

| limit | step | RAM / save per step | risk |
|---|---|---|---|
| golfer slots 152 | +100 slots | +26.8 KB RAM, +25.6 KB save (+8.7% of 294,733 B) | **High.** Ids capped below 255 by the byte sprite tag; must move scratch 0x98-0x9b and celebrity 0x9c+ codes; ~59 functions, ~3300 reference lines, 23 raw loop-end constants and 10+ `% 0x98` sites (one `POOL` macro via @override or translator patch); stack arrays `[152]` in 2 functions; shadow list overflow past 32; wrap-around slot reuse needs a free-slot check in FUN_00421bc0; tee-order short counter |
| concurrent golfers (profiles) | +75 profiles (75 to 150) | 1,854 B each: 0x230 + 0x2c + 0x4e2 = +139 KB RAM and save (+47%) | **High.** `rand(0x4b)`, `% 0x13` rule, 84-deep tables, art/portraits per profile. Cheaper: drop the "same profile" uniqueness and let profiles repeat (needs name/portrait de-dup, member stats are per profile) |
| waiting gate (7) | change constants 2 and 8 in g01:15515-15521 | none | Low, data-file tunable |
| holes 18 -> 36 | +18 holes | records +9.4 KB, markers +0.4 KB, stats table +4.6 KB, profile arrays +3.0 KB, golfer per-hole arrays +10.9 KB (record has 4 spare bytes, so side tables or stride 0x180 = +20 KB) = about **28 KB** | **High.** Hole size tier FUN_0044faf0 returns -1 above 18 (override to 3); tile flag 5 bits; name tables; state 19 sentinel moves; 9/18 milestones; UI paging; tournament field 72 golfers (fine for 152 slots, needs pros 99 <= 100 ok) |
| staff 64 | x2 | +4.9 KB | Medium, add bound in FUN_00402970; table is followed by the edit queue |
| objects 256 | x2 | +4 KB (+0x56988c byte grid to u16 = +2.5 KB at 50x50, +20 KB at 100x100) | Medium, byte object ids |
| story pairs 16 | x2 | +14.5 KB RAM and save | Low, pure bound |
| edit queue 256 | x4 | +9 KB, no save | Low, but silent drops hurt big maps |
| pros / celebrities 100 | x2 | +5.6 KB / +3.7 KB | Low, `rand(100)` constants |

Ranking for the mod plan (do now vs later):
1. Do now: waiting gate, story pairs, edit queue, pros/celebrities (data-driven constants, no layout change).
2. Do with the golfer-pool card: `POOL` macro, free-slot check, shadow list bound, short counter fix, slot id tag widening, our own save container.
3. Later: profile count, holes beyond 18 (largest blast radius).

## 7. Corrections to the brief and open items

- 0x585850 is the staff/walker table (64 x 0x4c), not a golfer roster. The golfer "roster" is the profile set (0x4d6088 / 0x5849e0 / 0x543d10).
- Story pairs: 16, not 10 (0x3880 / 0x388).
- Scratch slots are 0x98, 0x99 and 0x9a; 0x9b unused; 0x9c is the first celebrity code.
- Not verified in code: the exact tick that advances 0x834170 (taken from sve.md and GAMELOGIC), tournament slot assignment (upstream doc), tee-order semantics beyond ordering, meaning of flag 0x100000.
- Not checked: the sprite-queue arrays 0x82a15c / 0x830164 / 0x831164 sizes (needed before widening the tag), and whether `0x5a5a04` (32 B) is slot-indexed.
