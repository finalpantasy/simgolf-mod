"""Source-bound CPU art publication and capture-retry fault regressions."""
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    match = re.search(r"(?:static )?(?:int|void) " + name + r"\([^;]*?\)\s*\{", source)
    start = match.start()
    end, depth = source.index("{", start) + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


COMMON = r'''
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static int allocations, fail_at;
static void *allocate(size_t n) { return ++allocations==fail_at ? NULL : malloc(n); }
#define malloc allocate
static void lock(void) {}
static void unlock(void) {}
static double host_now_ms(void) { return 123; }
enum { FE_ART_MAX=16, RP_ART=16, DLG_FACE_TILES=12, DLG_ART_PORTRAIT=0, DLG_ART_FACE_FIRST=1, DLG_ART_MAX=13 };
static struct { uint32_t *px; int w,h; uint32_t serial; } g_art[16];
static uint32_t g_art_pub;
typedef struct { int label; } DlgSnap;
typedef struct { uint32_t *px; int w,h; } DlgPixels;
static DlgSnap g_snap;
static double g_snap_t;
static int g_ended;
'''


class UiArtPublication(unittest.TestCase):
    def run_c(self, source, args=()):
        cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
        if not cc:
            self.skipTest("Set CC to an installed C compiler")
        with tempfile.TemporaryDirectory(prefix="simgolf-ui-art-") as temp:
            src, exe = Path(temp) / "art.c", Path(temp) / "art.exe"
            src.write_text(source)
            build = subprocess.run([cc, *shlex.split(os.environ.get("CFLAGS", "")), "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-variable", "-Werror", "-I" + str(ROOT / "port/src"), str(src), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def publisher(self, module, name):
        src = (ROOT / ("port/src/port/" + module + ".c")).read_text()
        helper = '#include "port/ui/art_publication.h"\n' if '"port/ui/art_publication.h"' in src else ""
        functions = function(src, "set_art") + "\n" if module == "dialogs_frosted" else ""
        functions += function(src, name)
        self.run_c(COMMON + helper + functions + r'''
int main(void) {
  uint32_t pixels[4]={1,2,3,4};
  PUBLISH(0,pixels,2,2);
  uint32_t *old=g_art[0].px, serial=g_art[0].serial;
  pixels[0]=42;
  fail_at=allocations+1;
  PUBLISH(0,pixels,2,2);
  CHECK(g_art[0].px==old && g_art[0].px[0]==1 && g_art[0].serial==serial);
  fail_at=0;
  PUBLISH(0,pixels,2,2);
  CHECK(g_art[0].px[0]==42 && g_art[0].serial>serial);
  serial=g_art[0].serial;
  PUBLISH(0,pixels,-1,2);
  CHECK(g_art[0].px[0]==42 && g_art[0].serial==serial);
  PUBLISH(0,NULL,0,0);
  CHECK(!g_art[0].px && !g_art[0].w && !g_art[0].h && g_art[0].serial>serial);
  return 0;
}
'''.replace("PUBLISH", name))

    def test_frontend_transaction(self):
        self.publisher("frontend_frosted", "fef_publish_art")

    def test_report_transaction(self):
        self.publisher("reports_frosted", "rpf_publish_art")

    def test_dialog_transaction(self):
        self.publisher("dialogs_frosted", "dlgf_publish_art")

    def test_face_batch_preserves_labels_and_pixels_on_each_failure(self):
        src = (ROOT / "port/src/port/dialogs_frosted.c").read_text()
        helper = '#include "port/ui/art_publication.h"\n' if '"port/ui/art_publication.h"' in src else ""
        functions = function(src, "set_art") + "\n" + function(src, "dlgf_publish_faces")
        self.run_c(COMMON + helper + functions + r'''
int main(void) {
  uint32_t pixel=1;
  DlgPixels tiles[DLG_FACE_TILES], preview={&pixel,1,1};
  for(int i=0;i<DLG_FACE_TILES;i++)tiles[i]=preview;
  DlgSnap snap={1};
  dlgf_publish_faces(&snap,tiles,&preview);
  CHECK(g_snap.label==1 && g_snap_t==123 && !g_ended);
  uint32_t serial=g_art_pub;
  pixel=2;snap.label=2;
  for(int failure=1;failure<=DLG_FACE_TILES+1;failure++) {
    fail_at=allocations+failure;
    dlgf_publish_faces(&snap,tiles,&preview);
    CHECK(g_snap.label==1 && g_art_pub==serial);
    for(int i=0;i<DLG_ART_MAX;i++)CHECK(g_art[i].px[0]==1);
  }
  fail_at=0;
  dlgf_publish_faces(&snap,tiles,&preview);
  CHECK(g_snap.label==2);
  for(int i=0;i<DLG_ART_MAX;i++){CHECK(g_art[i].px[0]==2);free(g_art[i].px);}
  return 0;
}
''')

    def test_frontend_capture_and_publication_retry(self):
        src = (ROOT / "port/src/port/frontend_game.c").read_text()
        self.run_c(r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
enum { FE_ART_COURSE=0 };
static int g_on=1,g_rec_all=1,g_art_done[1]={1},g_emb_p1,captures,fail_publish;
static void *g_emb_this;
static void draw_emblem3(int x,int y,void *u){(void)x;(void)y;(void)u;}
static uint32_t *capture(int a,int b,void (*draw)(int,int,void*),void *u,int *w,int *h) {
  (void)a;(void)b;(void)draw;(void)u;captures++;*w=*h=1;
  return captures==1?NULL:malloc(4);
}
static int fef_publish_art(int key,const uint32_t *p,int w,int h){(void)key;(void)p;(void)w;(void)h;return !fail_publish;}
static int FUN_00473f60(void *t,int p,int target,uint32_t x,uint32_t y,int p5){(void)t;(void)p;(void)target;(void)x;(void)y;(void)p5;return 7;}
''' + function(src, "fe_sprite3") + r'''
int main(void) {
  void *next=(void *)(uintptr_t)123;
  CHECK(fe_sprite3(next,2,0,0,0,0)==7 && !g_art_done[0]);
  fail_publish=1;fe_sprite3(next,2,0,0,0,0);CHECK(!g_art_done[0]);
  fail_publish=0;fe_sprite3(next,2,0,0,0,0);CHECK(g_art_done[0] && captures==3);
  fe_sprite3(next,2,0,0,0,0);CHECK(captures==3);
  return 0;
}
''')

    def test_report_capture_and_publication_retry(self):
        src = (ROOT / "port/src/port/reports_game.c").read_text()
        self.run_c(r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static int allocations, fail_at, fail_publish;
static void *allocate(size_t n) { return ++allocations==fail_at ? NULL : malloc(n); }
#define malloc allocate
enum { PAGE_WIDGET=0, RP_IT_CHIP=1 };
static struct { void *spr; int done,page,plain; uint16_t col; } g_art[1];
static int g_nart=1, g_log;
static struct { int n; struct { int type,art,big; uint16_t col; } it[1]; } g_snap;
static uint16_t surface[16*16];
static int sprite_size(void *s,int *w,int *h,int *x,int *y){(void)s;*w=*h=4;*x=*y=0;return 1;}
static uint16_t *gdi_screen_dib(int page,int *w,int *h,int *pitch,int *is565){(void)page;*w=*h=*pitch=16;*is565=1;return surface;}
static int FUN_00473e60(void *s,int t,int x,int y,int c){(void)s;(void)t;(void)x;(void)y;(void)c;return 0;}
static int rpf_publish_art(int key,const uint32_t *p,int w,int h){(void)key;(void)p;(void)w;(void)h;return !fail_publish;}
''' + function(src, "capture_art") + r'''
int main(void) {
  fail_at=1;capture_art();CHECK(!g_art[0].done);
  fail_at=allocations+2;capture_art();CHECK(!g_art[0].done);
  fail_at=0;fail_publish=1;capture_art();CHECK(!g_art[0].done);
  fail_publish=0;capture_art();CHECK(g_art[0].done);
  int before=allocations;capture_art();CHECK(allocations==before);
  return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
