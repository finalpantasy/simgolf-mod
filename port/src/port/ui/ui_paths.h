// Frosted Mint cart path help (simgolf mod, task-53): the path view (every tile of the clubhouse's
// cart paths lightly tinted) and a small "No path" marker over every building the
// network does not reach. A pure function of one PvNote, the snapshot the game thread takes of a course
// frame (paths_game.c), drawn with the kit. Plain data and the kit: no game code.
//
// Positions in a PvNote are canvas pixels of the frame as the game drew it (golf's worldToScreen, which
// already holds the terrain height and the sub-tile camera offset), so they are pinned exactly like the
// golfers' name tags: the caller passes the course layer's slide (smooth scrolling) or the zoom glide.
// docs/ui/KIT.md.
#pragma once

#include <SDL.h>
#include <stdint.h>

#include "port/ui/ui_kit.h"

#define PV_MAGIC 0x31565050u   // "PPV1"
#define PV_QUADS 3200
#define PV_BUILDINGS 64

typedef struct PvQuad {
  int16_t p[8];   // the four corners of one tile, canvas px: x0, y0, x1, y1, x2, y2, x3, y3 (around the tile)
  uint8_t path;   // a real path tile; buildings also carry the network but do not need a ground tint
} PvQuad;

typedef struct PvBuilding {
  int16_t x, y;       // the ground point at the middle of the building, canvas px
  int16_t kind;       // golf's building kind (6..16)
  int16_t idx;        // the building's record number
  int16_t row, col;   // its first tile
  int16_t connected;  // the path network reaches it
  char name[24];
} PvBuilding;

typedef struct PvNote {
  uint32_t magic;
  uint32_t serial;
  int32_t n_net, n_off;   // tiles in the network first, then path tiles the network does not reach
  int32_t n_bld;          // every building that needs a path (connected or not), up to PV_BUILDINGS
  int32_t zoom;           // golf's zoom (1..8)
  int32_t placing;        // a path or a building is being placed: the view turns itself on
  int32_t not_needed;     // Easy or Sandbox: golf opens every building, markers stay away
  int32_t view;           // the path view is drawn this frame (the quads are filled)
  PvBuilding bld[PV_BUILDINGS];
  PvQuad q[PV_QUADS];
} PvNote;

// Where the note's canvas pixels are on the target: target = view + (canvas + slide) * px, or while a zoom
// glide runs target = view + (zoom_s * canvas + zoom) * px.
typedef struct PvMap {
  float view_x, view_y, px;
  float slide_x, slide_y;
  int zoom_on;
  float zoom_s, zoom_x, zoom_y;
} PvMap;

typedef struct PvOpts {
  int markers;   // draw the "No path" markers
  float opacity; // glass opacity of the tint (1 = the first design)
  int subtle;    // only actual path tiles, without a course-wide veil or building-footprint tint
  float tint;    // additional path tint opacity, independent of the building markers
} PvOpts;

// Joined paths in mint, unreached paths in amber. The legacy comparison also tints building footprints
// and dims view_rect; the default leaves buildings and the rest of the course unchanged.
void ui_paths_draw_view(UiCtx *c, const PvNote *n, const PvMap *m, UiRect view_rect, const PvOpts *o);
// The markers over unconnected buildings (under the HUD). The legend line of the view is drawn by _draw_legend.
void ui_paths_draw_markers(UiCtx *c, const PvNote *n, const PvMap *m, const PvOpts *o, int hover_idx);
// Index into n->bld of the unconnected building whose marker is under (px, py) on the target, -1 if none.
int ui_paths_marker_at(UiCtx *c, const PvNote *n, const PvMap *m, float px, float py);
// A hover card beside the marker of building i (over the HUD).
// key: the name of the toggle key for the last line ("" or NULL: none).
void ui_paths_draw_card(UiCtx *c, const PvNote *n, const PvMap *m, int i, const char *key);
// The one-line legend at the top while the view is on ("Cart paths joined to the clubhouse ...").
void ui_paths_draw_legend(UiCtx *c, const PvNote *n, int forced);
