// Run with port/tests/tract_geom.sh, or build without game assets or SDL:
//   clang -Iport/src port/tests/tract_geom_test.c -o tract_geom_test.exe
// Issue 110: the Frosted land-sale map must outline the same tiles that golf's
// FUN_004587a0 sells as tract k, and the Buy click must land on tract k's row.
#include <stdio.h>

#include "port/ui/ui_tract_geom.h"

static int failed;

static void expect(int ok, const char *what, int k, int side) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s (tract %d, %d x %d map)\n", what, k + 1, side, side);
    failed++;
  }
}

// FUN_004587a0 as translated (golf.json GEN_TRACT0 patches): tract k reads
// tile [(GEN_TRACT0(k % 3) + i) * MAP_W + GEN_TRACT0(k / 3) + j].
static int orig_start(int side, int band) { return 1 + band * (side - 2) / 3; }
static int orig_len(int side, int band) { return orig_start(side, band + 1) - orig_start(side, band); }

// FUN_004587a0's pointer test: list column from x, row from y, tract column * 3 + row.
static int orig_hit(int x, int y) {
  if (x < 0xf || x > 0x306 || y < 0x36 || y > 0x101) return -1;
  int k = (y - 0x36) / 0x44;
  if (x > 0x105) {
    if (x < 0x21a) {
      if (x < 0x110 || x > 0x20b) return -1;
      k += 3;
    } else {
      k += 6;
    }
  }
  return k;
}

int main(void) {
  static const int sides[] = {50, 70, 100, 158};
  for (unsigned s = 0; s < sizeof sides / sizeof sides[0]; s++) {
    int side = sides[s];
    static unsigned char owner[160][160];
    for (int a = 0; a < side; a++) for (int b = 0; b < side; b++) owner[a][b] = 0;
    for (int k = 0; k < 9; k++) {
      int x0, x1, y0, y1;
      expect(ui_tract_tiles(k, side, side, &x0, &x1, &y0, &y1), "tract has tiles", k, side);
      expect(x0 == orig_start(side, k % 3) && x1 - x0 == orig_len(side, k % 3), "first axis matches golf (k % 3)", k, side);
      expect(y0 == orig_start(side, k / 3) && y1 - y0 == orig_len(side, k / 3), "second axis matches golf (k / 3)", k, side);
      for (int a = x0; a < x1; a++) for (int b = y0; b < y1; b++) owner[a][b]++;
      int cx, cy;
      ui_tract_click(k, &cx, &cy);
      expect(orig_hit(cx, cy) == k, "Buy click selects the same tract in golf", k, side);
    }
    // Nine tracts tile the map inside its one-tile border exactly once.
    for (int a = 0; a < side; a++) for (int b = 0; b < side; b++) {
      int inside = a >= 1 && a < side - 1 && b >= 1 && b < side - 1;
      if (owner[a][b] != inside) { expect(0, "tracts cover the land once", -1, side); a = side; break; }
    }
  }
  // The 0.1.36 map drew tract 2 on tract 4's land: tract 2 is first-axis band 1, second-axis band 0.
  int x0, x1, y0, y1;
  ui_tract_tiles(1, 50, 50, &x0, &x1, &y0, &y1);
  expect(x0 == 17 && y0 == 1, "tract 2 is not drawn transposed", 1, 50);
  expect(!ui_tract_tiles(9, 50, 50, &x0, &x1, &y0, &y1) && !ui_tract_tiles(-1, 50, 50, &x0, &x1, &y0, &y1), "bad tract rejected", 9, 50);
  if (failed) return 1;
  printf("PASS: tract map geometry matches golf's tract numbering on 4 map sizes\n");
  return 0;
}
