// One visit per tile. Paths use an L; terrain and flowers use a rectangle.
#pragma once
#include <stdlib.h>

typedef struct { int x0, y0, x1, y1, line; } PlaceDragShape;
static inline int place_drag_count(PlaceDragShape s) {
  return s.line ? abs(s.x1-s.x0)+abs(s.y1-s.y0)+1
                : (abs(s.x1-s.x0)+1)*(abs(s.y1-s.y0)+1);
}
static inline int place_drag_tile(PlaceDragShape s, int i, int *x, int *y) {
  if (i < 0 || i >= place_drag_count(s)) return 0;
  int dx = s.x1 < s.x0 ? -1 : 1, dy = s.y1 < s.y0 ? -1 : 1;
  int nx = abs(s.x1-s.x0)+1;
  if (s.line) {
    *x = s.x0 + (i < nx ? i : nx-1)*dx;
    *y = s.y0 + (i < nx ? 0 : i-nx+1)*dy;
  } else {
    *x = s.x0 + (i%nx)*dx;
    *y = s.y0 + (i/nx)*dy;
  }
  return 1;
}
