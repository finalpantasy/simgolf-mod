# Joined pot bunkers

Optional native PC mod, off by default. Enable `joined_pot_bunkers` in the
mod manager or set `[terrain] joined_pot_bunkers=1` in `data/mods.ini`.
Adjacent pot-bunker tiles share one sandy floor with a steep outside rim.
Single tiles retain their exact original art. Diagonal contact alone does
not join two bunkers. Inside corners and grassy holes remain visible.

Enable Click-and-drag placement as well (`[ui] drag_placement=1`) to paint
rectangular pot bunkers. Release to place; Escape or right-click cancels.
The existing drag preview, HUD exclusions, batch limit and affordability
cancellation apply. Turning either setting off cancels a pending pot drag.
Joining works without drag placement when placing individual tiles.

Every tile remains original terrain type9. The original game still controls
placement validity, pot-bunker prices, clearing, replacement, affordability,
financial history, golfer behavior and ball lies. There is no terrain geometry
change or new save field. Existing saves can display joined art when enabled
and original individual bunkers when disabled. Vanilla and browser behavior
remain unchanged. No new balance rule needs to be serialized.

Artwork is generated locally from the installed course texture when a
connected footprint is first drawn. An eight-neighbor mask handles exposed
edges, inside corners and interiors; it rotates with the original UV mapping.
Broad rounded shoulders and coherent four-tile contour undulations soften the
footprint. The original pot-bunker sand and painted rim are sampled at native
64-pixel density, without synthetic shading or randomized fine grain. The
grassy lip uses nearby ground material. A temporary
draw-only transition pass omits the original square grass collar beside joined
pots and restores the tile render records before returning. Other terrain
transitions and the original off-switch rendering stay intact.

Only the enclosing rim is drawn. Cached art is freed with the course textures
when the theme changes or the renderer closes. The original game pixels stay
on the player's computer. Missing or unusually small source textures fall
back safely to the original pot-bunker texture.

the maintainer approved the continuous sand and steep rim concept on October17,2026.
Final native appearance and play acceptance remain separate from that concept
approval. Provenance is in `art/manifests/joined-pot-bunkers.json`.

Checks:

- `python tests/native/joined_pot_units.py`: actual renderer and all256 masks,
  rotation covariance, concave corners/interiors, removal/repaint, original
  fallback, cache reset and stable HD texture references.
- `python tests/native/drag_input_units.py`: original gesture regressions and
  pot dragging gated by both optional settings.
- With a shared game lease, mute, and private game mirror,
  `SIMGOLF_JOINED_POT_TEST=1 node tests/native/drag_placement.mjs` compares
  ordinary clicks against dragging for cash, tiles and full financial history.
- `node tests/native/joined_pot_native.mjs` captures deterministic footprints
  in the actual native game across all four themes and rotations. These are
  rendering fixtures; gesture proof comes from the separate comparison.
  It also checks save/reload in fresh joined and vanilla processes.
- `bash tools/check.sh` is the mandatory whole-project runner. Its output and
  skips must accompany qualification; a PASS is not human play approval.

`python tests/native/joined_pot_pixels.py` checks sampled single-tile pixels
against original on/off captures and rejects stale source/binary bindings.

Private build, native captures, concept and receipts: `build/joined-pot/`.
No release, merge, push or installed-game replacement is authorized here.
