"""Exercise the production FindFirst/Next sequence used by the load browser."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SaveListOrder(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows filesystem integration")
    def test_native_dates_and_selected_filename(self):
        from test_publication_allocations import function
        native = (ROOT / "port/src/port/fs_native.c").read_text()
        source = (ROOT / "port/src/port/win32.c").read_text()
        listing = source[source.index("// FindFirstFileA: the whole listing"):
                         source.index("// ------------------------------------------------------------------ WINMM")]
        harness = r'''
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ctype.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/fs.h"
static const char *root;
static const char *host_user_dir(void) { return root; }
static const char *host_game_dir(void) { return root; }
int fs_resolve(const char *p,char *out,int cap,int writing) {
  assert(!writing);int n=snprintf(out,cap,"%s/%s",root,p);return n>=0&&n<cap;
}
'''
        harness += "\n".join(function(native, signature) for signature in (
            "static const char *base_for(", "static int normalize(",
            "static int has_parent_part(", "int fs_list("))
        harness += r'''
#define FindFirstFileA game_FindFirstFileA
#define FindNextFileA game_FindNextFileA
#define FindClose game_FindClose
typedef uintptr_t nu;
#define P(x) ((void *)(uintptr_t)(x))
#define S(x) ((char *)(uintptr_t)(x))
#define U32(p, off) (*(uint32_t *)((char *)P(p) + (off)))
#define API_TRACE() ((void)0)
#define OBJ_FIND 1
static nu g_last_error;
static void *object;
static nu obj_new(int kind, void *p) { (void)kind; object=p; return 1; }
static void *obj_get(nu h,int kind) { (void)kind; return h==1?object:NULL; }
static void obj_free(nu h) { (void)h; object=NULL; }
static int mods_get_int(const char *key,int def) { (void)key; return def; }
'''
        harness += (ROOT / "port/src/port/save_ratings.h").read_text().replace("#pragma once", "")
        harness += listing + r'''
int main(int argc,char **argv) {
  assert(argc==2);root=argv[1];
  union { uint64_t align; char bytes[320]; } data;
  nu h=FindFirstFileA((nu)"Saved Games\\*.sve",(nu)data.bytes);
  assert(h!=0xffffffffu);
  // Same selected filename that the browser passes to preview/load/delete.
  assert(!strcmp(data.bytes+44,"Z-new.sve"));
  uint64_t modified=((uint64_t)U32(data.bytes,24)<<32)|U32(data.bytes,20);
  assert(modified==133444736000000000ULL);
  char selected[1024];snprintf(selected,sizeof selected,"%s/Saved Games/%s",root,data.bytes+44);
  FILE *f=fopen(selected,"rb");assert(f);assert(fgetc(f)=='Z');fclose(f);
  assert(FindNextFileA(h,(nu)data.bytes));assert(!strcmp(data.bytes+44,"A-old.sve"));
  assert(!FindNextFileA(h,(nu)data.bytes));FindClose(h);
  assert(!remove(selected));
  h=FindFirstFileA((nu)"Saved Games\\*.sve",(nu)data.bytes);
  assert(!strcmp(data.bytes+44,"A-old.sve"));assert(!FindNextFileA(h,(nu)data.bytes));FindClose(h);
}
'''
        cc = os.environ.get("CC") or shutil.which("clang")
        if not cc:
            self.skipTest("Set CC to an installed C compiler")
        with tempfile.TemporaryDirectory(prefix="simgolf-save-files-") as temp:
            folder = Path(temp) / "Saved Games"
            folder.mkdir()
            for name, when in (("Z-new.sve", 1700000000), ("A-old.sve", 1600000000)):
                file = folder / name
                file.write_text(name)
                os.utime(file, (when, when))
            src, exe = Path(temp) / "test.c", Path(temp) / "test.exe"
            src.write_text(harness)
            build = subprocess.run([cc, *shlex.split(os.environ.get("CFLAGS", "")),
                                    "-std=c11", "-I", str(ROOT / "port/src"),
                                    str(src), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe), temp], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_saved_game_sequence(self):
        source = (ROOT / "port/src/port/win32.c").read_text()
        listing = source[source.index("// FindFirstFileA: the whole listing"):
                         source.index("// ------------------------------------------------------------------ WINMM")]
        timed = "uint64_t modified" in listing
        # Accept the original callback too, so the baseline fails on behavior.
        callback = ", uint64_t" if timed else ""
        stamp = ", stamps[i]" if timed else ""
        harness = r'''
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uintptr_t nu;
#define FindFirstFileA game_FindFirstFileA
#define FindNextFileA game_FindNextFileA
#define FindClose game_FindClose
#define P(x) ((void *)(uintptr_t)(x))
#define S(x) ((char *)(uintptr_t)(x))
#define U32(p, off) (*(uint32_t *)((char *)P(p) + (off)))
#define API_TRACE() ((void)0)
#define OBJ_FIND 1
static nu g_last_error;
static void *object;
static int enabled = 1, reverse;
static nu obj_new(int kind, void *p) { (void)kind; object=p; return 1; }
static void *obj_get(nu h,int kind) { (void)kind; return h==1?object:NULL; }
static void obj_free(nu h) { (void)h; object=NULL; }
static int mods_get_int(const char *key,int def) {
  if(!strcmp(key,"ui.save_sort"))return def;
  assert(!strcmp(key,"fixes.save_order"));return enabled;
}
static int fs_resolve(const char *p,char *out,int cap,int writing) {
  (void)p;(void)out;(void)cap;assert(!writing);return 0;
}
static const char *names[]={"A-old.sve","Z-new.sve","b-tie.sve","B-tie.sve","unknown.sve"};
static const uint64_t stamps[]={10,30,20,20,0};
static int fs_list(const char *p,void (*fn)(void *,const char *,int,unsigned CALLBACK),void *ctx) {
  (void)p; (void)stamps;
  for(int k=0;k<5;k++){int i=reverse?4-k:k; fn(ctx,names[i],0,(unsigned)i STAMP);}
  return 5;
}
'''.replace("CALLBACK", callback).replace("STAMP", stamp)
        harness += (ROOT / "port/src/port/save_ratings.h").read_text().replace("#pragma once", "")
        cases = r'''
static void check(const char *pattern, const int *order) {
  union { uint64_t align; char bytes[320]; } data;
  nu h=FindFirstFileA((nu)pattern,(nu)data.bytes); assert(h!=0xffffffffu);
  for(int i=0;i<5;i++) {
    assert(!strcmp(data.bytes+44,names[order[i]]));
    assert(U32(data.bytes,32)==(unsigned)order[i]);
    assert((((uint64_t)U32(data.bytes,24)<<32)|U32(data.bytes,20))==stamps[order[i]]);
    if(i<4)assert(FindNextFileA(h,(nu)data.bytes));
  }
  assert(!FindNextFileA(h,(nu)data.bytes));
  assert(FindClose(h)); assert(!object);
}
int main(void) {
  const int recent[]={1,3,2,0,4}, original[]={0,1,2,3,4};
  check("Saved Games\\*.sve",recent);
  reverse=1;check("saved games/*.SVE",recent);reverse=0;
  check("Themes\\Championship\\*.cse",original);
  check("Themes\\*.sve",original);
  check("Saved Games Backup\\*.sve",original);
  enabled=0;check("Saved Games\\*.sve",original);
  puts("saved-game order, stable ties, metadata, other listings and off switch passed");
}
'''
        cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
        if not cc:
            self.skipTest("Set CC to an installed C compiler")
        with tempfile.TemporaryDirectory(prefix="simgolf-save-order-") as temp:
            src, exe = Path(temp) / "test.c", Path(temp) / "test.exe"
            src.write_text(harness + listing + cases)
            build = subprocess.run([cc, *shlex.split(os.environ.get("CFLAGS", "")),
                                    "-std=c11", "-O0", str(src), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
