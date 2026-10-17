"""Compile and exercise the production display parser and canvas sizing code.

Run: python port/tests/test_hd_canvas_size.py [path/to/clang]
No game data, window, graphics device, or large buffer allocations are needed.
"""
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    source = (ROOT / "port/src/port/hd_native.c").read_text(encoding="utf-8")
    cfg_start = source.index("typedef struct {\n  int hd, borderless")
    cfg_end = source.index("} Cfg;", cfg_start) + len("} Cfg;")
    sizing_start = source.index("  int cw = g_cfg.canvas_w, ch = g_cfg.canvas_h;")
    sizing_end = source.index("  hd_frame_h = ch & ~1;", sizing_start) + len("  hd_frame_h = ch & ~1;")
    fixture = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef _WIN32
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif
#define MAX_PATH 260
static const char *const k_filters[] = {"sharp", "integer", "nearest", "smooth"};
static void hd_art_set_key(const char *k, const char *v) { (void)k; (void)v; }
''' + source[cfg_start:cfg_end] + '''
static Cfg g_cfg;
static int hd_frame_w, hd_frame_h;
''' + "\n".join(function(source, signature) for signature in (
        "static int parse_size(", "static void set_key(",
        "static void trim(", "static void set_pair(")) + '''
static void size_canvas(int out_w, int out_h) {
''' + source[sizing_start:sizing_end] + r'''
}
static int checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
  fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)
int main(void) {
  static const struct { const char *value; int w, h; } cases[] = {
    {"800x600", 800, 600}, {"1920x1080", 1920, 1080},
    {"3840x2160", 3840, 2160}, {"4096x4096", 4096, 4096},
    {"4096x600", 4096, 600}, {"800x4096", 800, 4096},
    {"4095x4095", 4095, 4095}, {"160x120", 160, 120},
    {"4097x600", 0, 0}, {"800x4097", 0, 0},
    {"8192x8192", 0, 0}, {"16384x16384", 0, 0},
    {"16385x600", 0, 0}, {"800x16385", 0, 0},
    {"auto", 0, 0}, {"bad", 0, 0}, {"800x", 0, 0},
    {"0x600", 0, 0}, {"-800x600", 0, 0}, {"800x-600", 0, 0}
  };
  set_key("window", "16384x8192");
  set_key("render", "8192x16384");
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
    set_key("canvas", "1920x1080");
    set_key("canvas", cases[i].value);
    CHECK(g_cfg.canvas_w == cases[i].w && g_cfg.canvas_h == cases[i].h);
    CHECK(g_cfg.win_w == 16384 && g_cfg.win_h == 8192);
    CHECK(g_cfg.render_w == 8192 && g_cfg.render_h == 16384);
    size_canvas(1920, 1080);
    if (cases[i].w) {
      CHECK(hd_frame_w == (cases[i].w & ~1) && hd_frame_h == (cases[i].h & ~1));
    } else {
      CHECK(hd_frame_w == 1280 && hd_frame_h == 720);
    }
  }
  char pair[] = " CaNvAs = 8192x8192 \r\n";
  set_pair(pair);
  CHECK(!g_cfg.canvas_w && !g_cfg.canvas_h);
  size_canvas(7680, 4320);
  CHECK(hd_frame_w >= 800 && hd_frame_w <= 4096);
  CHECK(hd_frame_h >= 600 && hd_frame_h <= 4096);
  set_key("canvas", "800x600");
  size_canvas(7680, 4320);
  CHECK(hd_frame_w == 800 && hd_frame_h == 600);
  set_key("hd", "0");
  set_key("canvas", "8192x8192");
  CHECK(!g_cfg.hd);
  printf("HD canvas size: %d checks passed\n", checks);
  return 0;
}
'''
    out = ROOT / "build/hd-canvas-size"
    out.mkdir(parents=True, exist_ok=True)
    c_file = out / "fixture.c"
    exe = out / "fixture.exe"
    c_file.write_text(fixture, encoding="utf-8")
    compiler = (sys.argv[1] if len(sys.argv) > 1 else
                shutil.which("clang") or "F:/dev/tools_ext/llvm/bin/clang.exe")
    subprocess.run([compiler, "-std=c11", "-D_CRT_SECURE_NO_WARNINGS", "-Wall", "-Wextra",
                    "-Werror", str(c_file), "-o", str(exe)], check=True, timeout=60)
    subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == "__main__":
    main()
