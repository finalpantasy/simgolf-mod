#pragma once
// Land-sale tract geometry (issue 110), shared by the Frosted tract page and
// port/tests/tract_geom_test.c. FUN_004587a0 numbers tract k (0..8) by the
// first tile axis band k % 3 and the second axis band k / 3: it reads tile
// (first, second) at first * MAP_W + second and draws it through
// FUN_00456b70(first, second), the same axes as UiCourseMap's x and y.
// Each band starts at 1 + band * (side - 2) / 3, as GEN_TRACT0 in port/grid.h.

static inline int ui_tract_band_start(int side, int band) { return 1 + band * (side - 2) / 3; }

// Tract k's tiles: x in [*x0, *x1), y in [*y0, *y1). Returns 0 for a bad tract or map.
static inline int ui_tract_tiles(int k, int width, int height, int *x0, int *x1, int *y0, int *y1) {
  if (k < 0 || k > 8 || width < 5 || height < 5) return 0;
  *x0 = ui_tract_band_start(width, k % 3);
  *x1 = ui_tract_band_start(width, k % 3 + 1);
  *y0 = ui_tract_band_start(height, k / 3);
  *y1 = ui_tract_band_start(height, k / 3 + 1);
  return 1;
}

// Where the Frosted Buy button clicks for tract k in golf's own tract list:
// column k / 3 and row k % 3, inside FUN_004587a0's hit boxes.
static inline void ui_tract_click(int k, int *x, int *y) {
  static const int col_x[3] = {140, 400, 656}, row_y[3] = {88, 156, 224};
  *x = col_x[(k / 3) % 3];
  *y = row_y[(k % 3 + 3) % 3];
}
