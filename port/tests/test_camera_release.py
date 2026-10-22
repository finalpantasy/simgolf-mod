"""Exercise production mouse handling and native event-pump ordering without game memory."""
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
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class CameraRelease(unittest.TestCase):
    def test_modal_and_consumed_releases(self):
        camera = (ROOT / "port/src/port/golf/camera_input.c").read_text()
        pump = (ROOT / "port/src/port/win32.c").read_text()
        reports = (ROOT / "port/src/port/reports_game.c").read_text()
        state = camera[camera.index("static struct {", camera.index("// -----", camera.index("static void log_camera"))):camera.index("// task-56: while a zoom")]
        # Actual mouse branches; other event types are irrelevant to this fixture.
        event = function(camera, "int camera_input_event(")
        event = "int camera_input_event(SDL_Event *e) { double now=100; int active=1; switch(e->type) {\n" + event[event.index("    case SDL_MOUSEMOTION:"):event.index("    case SDL_MOUSEWHEEL:")] + "default: return 0; }}\n"
        route = function(pump, "static void pump_sdl(void) {")
        route = route[:route.index("    switch (e.type)")] + "  }\n}\n"
        begin = function(reports, "void rp_begin(int kind) {")
        begin = begin[:begin.index("  if (g_log")] + "  (void)kind;\n}\n"
        source = r'''
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n", __LINE__, #c); exit(1); } } while(0)
typedef unsigned char Uint8;
typedef unsigned Uint32;
enum { SDL_MOUSEMOTION=1, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP,
       SDL_BUTTON_LEFT=1, SDL_BUTTON_MIDDLE=2, SDL_BUTTON_RIGHT=3, RP_PHOTO=7, RP_LEADERBOARD=8 };
#define WIN_SCREEN_W 800
#define WIN_SCREEN_H 600
#define INJECTED 0xffffffffu
typedef struct { unsigned type; struct { unsigned which,windowID; int x,y; } motion;
                 struct { unsigned which,windowID; int x,y; Uint8 button; } button; } SDL_Event;
static struct { int edge_scroll,drag_middle,drag_right; } cfg={1,1,1};
static int g_ptr_x, g_ptr_y, moved, pushed, consume;
static SDL_Event queued;
static int queued_valid;
static int hd_native_poll_event(SDL_Event *e) { if (!queued_valid)return 0; *e=queued; queued_valid=0; return 1; }
static int dbg_overlay_event(SDL_Event *e) { (void)e; return 0; }
int place_drag_event(SDL_Event *e,int after) { (void)e; (void)after; return 0; }
static int hd_ui_event(SDL_Event *e) { return consume && e->type==SDL_MOUSEBUTTONUP; }
static int keymap_event(SDL_Event *e) { (void)e; return 0; }
static void drag_to(int x,int y) { (void)x; (void)y; moved++; }
static void zoom_remap(int *x,int *y) { (void)x; (void)y; }
static void note_ignored(const char *s,double now) { (void)s; (void)now; }
static double click_slop(unsigned id) { (void)id; return 6; }
static void push_button(Uint8 b,int down,int x,int y) { (void)b;(void)down;(void)x;(void)y;pushed++; }
''' + state + r'''
static void drag_anchor(int x,int y) { g_drag.x0=x; g_drag.y0=y; }
''' + event + route + begin + r'''
static void send(unsigned type, unsigned button, unsigned window, unsigned which) {
  queued=(SDL_Event){0}; queued.type=type;
  queued.button.button=(Uint8)button; queued.button.windowID=window; queued.button.which=which;
  queued.motion.windowID=window; queued.motion.which=which; queued.motion.x=30; queued.motion.y=30;
  queued_valid=1; pump_sdl();
}
int main(void) {
  send(SDL_MOUSEBUTTONDOWN,SDL_BUTTON_MIDDLE,1,0);
  CHECK(g_drag.button==SDL_BUTTON_MIDDLE);
  consume=1; send(SDL_MOUSEBUTTONUP,SDL_BUTTON_MIDDLE,1,0);
  CHECK(g_drag.button==0);
  send(SDL_MOUSEMOTION,0,1,0); CHECK(moved==0 && g_ptr_x==30 && g_ptr_y==30);
  consume=0; send(SDL_MOUSEBUTTONDOWN,SDL_BUTTON_RIGHT,1,0);
  rp_begin(RP_PHOTO); rp_begin(RP_PHOTO);
  CHECK(!g_drag.button && !g_drag.panning && pushed==0);
  send(SDL_MOUSEBUTTONUP,SDL_BUTTON_RIGHT,1,0); CHECK(pushed==0);
  send(SDL_MOUSEBUTTONDOWN,SDL_BUTTON_MIDDLE,1,0);
  rp_begin(RP_LEADERBOARD); CHECK(g_drag.button==SDL_BUTTON_MIDDLE);
  consume=1; send(SDL_MOUSEBUTTONUP,SDL_BUTTON_RIGHT,1,0); CHECK(g_drag.button==SDL_BUTTON_MIDDLE);
  send(SDL_MOUSEBUTTONUP,SDL_BUTTON_MIDDLE,0,INJECTED); CHECK(g_drag.button==SDL_BUTTON_MIDDLE);
  send(SDL_MOUSEBUTTONUP,SDL_BUTTON_MIDDLE,0,0); CHECK(g_drag.button==SDL_BUTTON_MIDDLE);
  send(SDL_MOUSEBUTTONUP,SDL_BUTTON_MIDDLE,1,0); CHECK(g_drag.button==0);
  consume=0; send(SDL_MOUSEBUTTONDOWN,SDL_BUTTON_RIGHT,1,0);
  send(SDL_MOUSEBUTTONUP,SDL_BUTTON_RIGHT,1,0);
  CHECK(pushed==1 && g_click_up_at==160 && g_click_x==0 && g_click_y==0);
  rp_begin(RP_PHOTO); CHECK(g_click_up_at==160); // pending injected release must survive
  puts("camera release: modal, consumed, injected, unrelated and leaderboard cases passed");
  return 0;
}
'''
        cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
        if not cc:
            self.skipTest("Set CC to an installed C compiler")
        with tempfile.TemporaryDirectory(prefix="simgolf-camera-") as temp:
            src, exe = Path(temp) / "camera.c", Path(temp) / "camera.exe"
            src.write_text(source)
            compiled = subprocess.run([cc, *shlex.split(os.environ.get("CFLAGS", "")), "-std=c11", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
