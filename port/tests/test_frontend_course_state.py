"""A stale terrain match must not turn a front-end page into a course."""
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FrontendCourseState(unittest.TestCase):
    def test_hooks_track_original_pages_and_unlayered_course_return(self):
        from test_publication_allocations import function
        frontend = (ROOT / "port/src/port/frontend_game.c").read_text()
        for signature, guard in (("void fe_base(", "if (!fef_enabled())"),
                                 ("void fe_loading(", "if (!fef_loading_enabled())"),
                                 ("void fe_frame(", "g_on = fef_enabled()")):
            body = function(frontend, signature)
            self.assertLess(body.index("g_frontend_page = 1;"), body.index(guard))
        self.assertIn("g_frontend_page = 0;", function(frontend, "void fe_course_done("))
        course = function((ROOT / "port/src/port/golf/hd_ui.c").read_text(), "void hd_ui_course_done(")
        self.assertLess(course.index("fe_course_done();"), course.index("if (!hd_ui_course_hook)"))
        native = (ROOT / "port/src/port/hd_native.c").read_text()
        hold = native[native.index("if (hd_present_hold_course("):]
        self.assertIn("frontend_page || fef_cover_screen()", hold[:hold.index("{" )])

    def test_stale_terrain_does_not_classify_frontend_as_course(self):
        source = (ROOT / "port/src/port/hd_native.c").read_text()
        expression = re.search(r"  int course = (.*?);", source[source.index("int hd_present("):]).group(1)
        gate = re.search(r"  int frontend_page = (.*?);", source)
        harness = r'''
#include <assert.h>
#include <stdint.h>
static int captures;
static const uint16_t *g_snap;
static int g_frame_serial;
static int page_active, fix_enabled;
static int fe_game_page_active(void) { return page_active; }
static int mods_get_int(const char *k,int d) { (void)k;(void)d;return fix_enabled; }
static int terrain_hd_seen(void) { return 1; }
static int hdt_capture(const uint16_t *s,uint8_t *m,int frame) {
  (void)s;(void)m;(void)frame;captures++;return 1; // wide-canvas stale pixels
}
static int select_course(int page,int layered,int fix) {
  page_active=page;fix_enabled=fix;
  int frontend_page = GATE;
  const uint16_t *screen=0;uint8_t *mask=0;
  int course = EXPRESSION;
  return course;
}
int main(void) {
  assert(select_course(0,1,1)); // completed course frame
  assert(select_course(0,0,1)); // modal course report still uses its terrain
  captures=0;
  assert(!select_course(1,0,1)); // original and modern title/load/world pages
  assert(captures==0); // stale scene must not be republished
  assert(select_course(0,1,1)); // returning from front end resumes the course
  assert(select_course(1,0,0)); // explicit original-behavior switch
}
'''.replace("EXPRESSION", expression).replace("GATE", gate.group(1) if gate else "0")
        cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
        if not cc:
            self.skipTest("Set CC to an installed C compiler")
        with tempfile.TemporaryDirectory(prefix="simgolf-front-page-") as temp:
            src, exe = Path(temp) / "test.c", Path(temp) / "test.exe"
            src.write_text(harness)
            build = subprocess.run([cc, *shlex.split(os.environ.get("CFLAGS", "")),
                                    "-std=c11", str(src), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
