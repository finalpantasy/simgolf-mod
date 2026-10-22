"""Run actual production allocation functions with deterministic allocation faults.

The small source slices avoid game assets, fixed-address game memory and SDL.
CC may name an installed C compiler; no compiler is downloaded by this test.
"""
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


COMMON = r'''
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fail_at, calls;
static size_t allocated;
static void *test_malloc(size_t n) {
  if (++calls == fail_at) return NULL;
  allocated = n;
  return malloc(n);
}
static void *test_calloc(size_t n, size_t s) {
  if (++calls == fail_at) return NULL;
  allocated = n * s;
  return calloc(n, s);
}
static void *test_realloc(void *p, size_t n) {
  if (++calls == fail_at) return NULL;
  allocated = n;
  return realloc(p, n);
}
static void *test_memcpy(void *dst, const void *src, size_t n) {
  assert(dst && src);
  return memcpy(dst, src, n);
}
#define malloc test_malloc
#define calloc test_calloc
#define realloc test_realloc
#define memcpy test_memcpy
'''


class PublicationAllocations(unittest.TestCase):
    def run_c(self, name, source, case):
        cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
        if not cc:
            self.skipTest("Set CC to an installed C compiler")
        with tempfile.TemporaryDirectory(prefix="simgolf-alloc-") as temp:
            src = Path(temp) / (name + ".c")
            exe = Path(temp) / (name + ".exe")
            src.write_text(source, encoding="utf-8")
            build = subprocess.run([cc, *shlex.split(os.environ.get("CFLAGS", "")), "-std=c11", "-O0", "-Wall", "-Wextra",
                                    "-Wno-unused-function", "-Werror", str(src), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe), str(case)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def paths(self, case):
        src = (ROOT / "port/src/port/paths_game.c").read_text()
        declarations = src[src.index("typedef struct { int16_t x, y;"):src.index("static int corner(")]
        # Keep the actual entry-point allocation path, stopping before fixed game addresses.
        prefix = src[src.index("void pv_course_done(void)"):src.index("  const uint16_t *fl =", src.index("void pv_course_done(void)"))] + "}\n"
        self.run_c("paths", COMMON + declarations + r'''
static int g_map_w, g_map_h, log_on;
static void *g_grid[10];
static int pvf_enabled(void) { return 1; }
''' + prefix + r'''
static void resize(int w, int h) {
  g_map_w = w; g_map_h = h; g_grid[9] = &g_map_w;
  pv_course_done();
}
int main(int argc, char **argv) {
  (void)argc;
  resize(50, 50);
  Corner *old = g_corner;
  g_corner[0].stamp = 42;
  if (atoi(argv[1]) == 0) {
    resize(50, 100);
    assert(allocated == 51 * 101 * sizeof(Corner));
    g_corner[100 * g_corner_w + 50].stamp = 1;
    resize(100, 50);
    assert(allocated == 101 * 51 * sizeof(Corner));
    g_corner[50 * g_corner_w + 100].stamp = 2;
    resize(50, 25);
    assert(allocated == 51 * 26 * sizeof(Corner));
    int before = calls;
    resize(50, 25);
    assert(calls == before);
  } else {
    fail_at = calls + 1;
    resize(100, 50);
    assert(g_corner == old && g_corner_w == 51);
    assert(g_corner[0].stamp == 42);
    fail_at = 0;
    resize(100, 50);
    assert(g_corner_w == 101 && g_corner[0].stamp == 0);
  }
  free(g_corner);
  return 0;
}
''', case)

    def test_rectangular_transitions(self):
        self.paths(0)

    def test_corner_allocation_failure_retry(self):
        self.paths(1)

    def publication(self, case):
        src = (ROOT / "port/src/port/hd_art.c").read_text()
        types = src[src.index("enum { LC, LT };"):src.index("} List;") + len("} List;")]
        self.run_c("publication", COMMON + types + r'''
static List g_closed[2], g_pub[2];
static int g_closed_ok[2], g_pub_ok[2];
''' + function(src, "static void publish_one(") + r'''
int main(int argc, char **argv) {
  (void)argc;
  uint16_t owners[16] = {1};
  Entry entries[400] = {0};
  entries[0].file = 17;
  g_closed[0] = (List){entries, 1, 400, owners, 2, 2};
  g_closed_ok[0] = 1;
  publish_one(0, 1);
  assert(allocated > 0);
  assert(g_pub_ok[0] && g_pub[0].e[0].file == 17);
  uint16_t *old_owner = g_pub[0].owner;
  Entry *old_entries = g_pub[0].e;
  int old_cap = g_pub[0].cap;
  entries[0].file = 23;
  int which = atoi(argv[1]);
  if (which == 0 || which == 2) g_closed[0].w = g_closed[0].h = 4;
  if (which == 1 || which == 2) g_closed[0].n = 400;
  fail_at = calls + (which == 2 ? 2 : 1);
  publish_one(0, 1);
  assert(!g_pub_ok[0]);
  assert(g_pub[0].owner == old_owner && g_pub[0].e == old_entries);
  assert(g_pub[0].w == 2 && g_pub[0].h == 2 && g_pub[0].cap == old_cap);
  assert(g_pub[0].n == 1 && g_pub[0].e[0].file == 17 && g_pub[0].owner[0] == 1);
  fail_at = 0;
  publish_one(0, 1);
  assert(g_pub_ok[0] && g_pub[0].n == g_closed[0].n);
  assert(g_pub[0].e[0].file == 23 && g_pub[0].owner[0] == 1);
  publish_one(0, 0);
  assert(!g_pub_ok[0]);
  free(g_pub[0].owner); free(g_pub[0].e);
  return 0;
}
''', case)

    def test_owner_allocation_failure_retry(self):
        self.publication(0)

    def test_entry_allocation_failure_retry(self):
        self.publication(1)

    def test_second_reservation_failure_is_transactional(self):
        self.publication(2)


if __name__ == "__main__":
    unittest.main()
