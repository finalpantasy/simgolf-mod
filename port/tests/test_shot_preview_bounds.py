"""Bound preview reads without changing valid padded reads or golfer state.

The native fixture reads the existing translated cache at PORT_GEN (default
build/gen). No generated game source or original game data is stored here.
"""
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "port/tools/translate"
sys.path.insert(0, str(TOOLS))
from required_patches import validate_required_patches


def compiler(name):
    configured = os.environ.get("SIMGOLF_TEST_C_COMPILER" if name == "clang-cl" else "CC")
    if configured:
        return configured
    for root in [os.environ.get("SIMGOLF_LLVM"),
                 str(Path(os.environ["TOOLS_EXT"]) / "llvm") if "TOOLS_EXT" in os.environ else None]:
        if root:
            candidate = Path(root) / "bin" / (name + (".exe" if os.name == "nt" else ""))
            if candidate.is_file():
                return str(candidate)
    return shutil.which(name) or (shutil.which("gcc") if name == "clang" else None)


class PreviewPatchAnchors(unittest.TestCase):
    def test_every_read_and_classifier_anchor_is_required(self):
        cfg = json.loads((TOOLS / "golf.preview_bounds.json").read_text())
        name = "FUN_004226a0"
        corner = "FUN_0040c170"
        self.assertEqual(set(cfg["patches"]), {name, corner})
        replacements = cfg["patches"][name]
        counts = dict(cfg["required_patch_counts"][name])
        self.assertEqual(len(replacements), 9)
        self.assertEqual(sorted(counts.values()), [1, 1, 1, 1, 1, 2, 2, 2, 5])
        self.assertEqual(counts["FUN_0042fa30("], 2)
        self.assertEqual(counts["FUN_0040c450("], 5)
        self.assertEqual({old for old, _ in replacements}, set(counts))
        self.assertEqual(len(cfg["patches"][corner]), 1)
        self.assertEqual([n for _, n in cfg["required_patch_counts"][corner]], [1])
        bodies = {}
        for fn, patches in cfg["patches"].items():
            required = dict(cfg["required_patch_counts"][fn])
            self.assertEqual({old for old, _ in patches}, set(required))
            bodies[fn] = "\n".join(old for old, _ in patches for _ in range(required[old]))

        def validate(sources):
            validate_required_patches([{"name": fn, "c": body} for fn, body in sources.items()],
                                      cfg["required_patch_counts"], cfg["patches"])

        validate(bodies)
        for fn, patches in cfg["patches"].items():
            for old, _ in patches:
                with self.subTest(function=fn, missing=old), self.assertRaises(ValueError):
                    validate({**bodies, fn: bodies[fn].replace(old, "", 1)})
                with self.subTest(function=fn, duplicate=old), self.assertRaises(ValueError):
                    validate({**bodies, fn: bodies[fn] + "\n" + old})
            with self.subTest(missing_function=fn), self.assertRaises(ValueError):
                validate({key: value for key, value in bodies.items() if key != fn})


class PreviewGridStorage(unittest.TestCase):
    def test_real_helper_preserves_padding_aliases_and_low_bytes(self):
        cc = compiler("clang")
        if not cc:
            self.skipTest("C compiler unavailable; set CC")
        source = r'''
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include "port/grid.h"
int g_map_w, g_map_h;
uintptr_t g_grid[20];
static void check_map(int w, int h, int elem) {
  g_map_w=w; g_map_h=h;
  int pad=w+2, cells=w*h;
  unsigned char *allocation=malloc((size_t)(cells+2*pad)*elem);
  assert(allocation);
  unsigned char *base=allocation+pad*elem;
  for(int i=-pad;i<cells+pad;i++) {
    base[i*elem]=(unsigned char)(i*17+131);
    if(elem==2)base[i*elem+1]=0xee;
  }
  // Every physically valid element, including both padding endpoints.
  for(int i=-pad;i<cells+pad;i++) {
    assert(grid_tile_in_storage(0,i));
    assert(grid_tile_byte(base,0,i,elem)==base[i*elem]);
  }
  assert(!grid_tile_in_storage(0,-pad-1));
  assert(!grid_tile_in_storage(0,cells+pad));
  // A null base proves rejected reads return before touching memory.
  assert(grid_tile_byte(NULL,0,-pad-1,elem)==0);
  assert(grid_tile_byte(NULL,0,cells+pad,elem)==0);
  assert(grid_tile_byte(NULL,INT_MIN,INT_MIN,elem)==0);
  assert(grid_tile_byte(NULL,INT_MAX,INT_MAX,elem)==0);
  assert(grid_tile_byte(base,1,-1,elem)==base[(w-1)*elem]);
  assert(grid_tile_byte(base,0,w,elem)==base[w*elem]);
  assert(grid_tile_byte(base,-1,w-1,elem)==base[-elem]);
  assert(grid_tile_byte(base,h,0,elem)==base[cells*elem]);
  base[0]=0x80;
  assert((int)(char)grid_tile_byte(base,0,0,elem)==(int)(char)0x80);
  assert(grid_tile_byte(base,0,0,elem)==0x80);
  free(allocation);
}
int main(void) {
  for(int elem=1;elem<=2;elem++) {
    check_map(50,50,elem); check_map(158,158,elem);
    check_map(100,50,elem); check_map(50,100,elem);
  }
  return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="simgolf-preview-grid-") as tmp:
            src, exe = Path(tmp) / "grid.c", Path(tmp) / "grid.exe"
            src.write_text(source)
            built = subprocess.run([cc, *shlex.split(os.environ.get("CFLAGS", "")),
                                    "-std=c11", "-O0", "-I" + str(ROOT / "port/src"),
                                    str(src), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


NATIVE_PREFIX = r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "port/grid.h"
#include "port/golf/shot_preview.h"
typedef unsigned char byte, undefined, undefined1;
typedef unsigned short ushort, undefined2;
typedef unsigned int uint, undefined4;
typedef unsigned long long ulonglong;
typedef long long longlong;
int g_map_w=50,g_map_h=50;
uintptr_t g_grid[20];
static unsigned char *image;
static int row, col, mode, classifier_calls, sample_calls, motion_calls;
static void trace(unsigned address) {
  if(address==0x0040bf60)classifier_calls++;
  if(address==0x0042fa30 || address==0x0040c450)sample_calls++;
}
#define PORT_TRACE(x) trace(x)
static LONG CALLBACK fault(EXCEPTION_POINTERS *p) {
  if(p->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
    fprintf(stderr,"FAIL guarded access at %p\n",(void *)p->ExceptionRecord->ExceptionInformation[1]);
    ExitProcess(90);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
uint FUN_0040c170(int r,int c);
static int port_course_fast(void){return 0;}
static uint port_course_height(uint r,uint c){(void)r;(void)c;return 3;}
static int FUN_0042dba0(int x,int y){(void)x;(void)y;return 0;}
static void FUN_0042f4b0(uint r,uint c,uint *low,uint *high) {
  (void)r;(void)c;(void)low;(void)high;
  // Synthetic ground has no special bridge/building height attributes.
  // Do not silently replace an unexpected special-terrain call with zero.
  fputs("FAIL unexpected special-height path\n",stderr);ExitProcess(91);
}
static int FUN_00467270(uint a,int b){
  (void)a;(void)b;motion_calls++;return mode==5?-1024:mode>=2?1024:0;
}
static int FUN_004672b0(int a,int b){(void)a;(void)b;return 0;}
static void FUN_00409950(int a){(void)a;}
static int FUN_00467130(int a,int b,int c){return a<b?b:a>c?c:a;}
static int FUN_004070b0(uint a,uint b,int c,int d){(void)a;(void)b;(void)c;(void)d;return 0;}
static int FUN_0040acd0(uint a,uint b){(void)a;(void)b;return 0;}
static uint FUN_0045c1e0(void *a,uint b){(void)a;(void)b;return 0;}
static void FUN_00424120(uint who,int unused,int a,int b,int c) {
  (void)unused;(void)a;(void)b;(void)c;
  unsigned char *rec=(unsigned char *)(uintptr_t)(0x5794b8+who*256);
  memset(rec,0,256);
  *(int *)(rec+0xd4)=row*1024+(mode==1?0:512);
  *(int *)(rec+0xd8)=col*1024+512;
  *(int *)(rec+0xdc)=mode==4?2:mode>=2?0:-1;
  *(int *)(rec+0xe4)=64;
  *(int *)(rec+0xe8)=mode>=2?0:-64;
}
'''

NATIVE_SUFFIX = r'''
int main(int argc,char **argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  AddVectoredExceptionHandler(1,fault);
  if(argc!=7 && argc!=8)return 3;
  mode=atoi(argv[1]);g_map_w=atoi(argv[2]);g_map_h=atoi(argv[3]);
  row=atoi(argv[4]);col=atoi(argv[5]);int high_guard=atoi(argv[6]);
  image=VirtualAlloc(NULL,5242880,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
  if(!image)return 3;
  SYSTEM_INFO info;GetSystemInfo(&info);
  for(int i=0;i<20;i++) {
    int elem=i==9?2:i==16?8:1;
    int width=MAP_W+(i==10),height=MAP_H+(i==10);
    SIZE_T bytes=(SIZE_T)width*height*elem,pad=(width+2)*elem;
    SIZE_T total=bytes+2*pad,page=info.dwPageSize;
    SIZE_T pages=(total+page-1)/page*page,guard=65536;
    unsigned char *p=VirtualAlloc(NULL,pages+2*guard,MEM_RESERVE,PAGE_NOACCESS);
    if(!p || !VirtualAlloc(p+guard,pages,MEM_COMMIT,PAGE_READWRITE))return 3;
    // Put the exact production-sized span against the selected guard edge.
    unsigned char *allocation=p+guard+(high_guard?pages-total:0);
    g_grid[i]=(uintptr_t)(allocation+pad);
    if(i==10)memset(allocation,3,total); // Flat synthetic corner lattice.
  }
  *(int *)0x834170=argc==8?atoi(argv[7]):1;
  *(int *)0x4c2fa0=1; // Valid procedural-height scale for clock-zero controls.
  if(mode==6 || mode==7) {
    // Exercise the compiled production wrappers against real height/slope
    // callees, including non-flat in-storage results and all directions.
    static const int dx[8]={1,1,0,-1,-1,-1,0,1};
    static const int dy[8]={0,1,1,1,0,-1,-1,-1};
    memcpy((void *)0x4c2878,dx,sizeof dx);memcpy((void *)0x4c2898,dy,sizeof dy);
    for(int r=0;r<=MAP_H;r++)for(int c=0;c<=MAP_W;c++)
      ((unsigned char *)GRID(10))[r*(MAP_W+1)+c]=(unsigned char)(3+(r+c)%3);
    int x=(mode==7?row:10)*1024+800,y=(mode==7?col:21)*1024+200;
    int height=FUN_0042fa30(x,y);
    if((mode==6 && !height) || shot_preview_height(x,y)!=height)return 4;
    int nonzero_slope=0;
    for(unsigned d=0;d<8;d++) {
      int slope=FUN_0040c450(x,y,d);nonzero_slope|=slope!=0;
      if(shot_preview_slope(x,y,d)!=slope)return 4;
    }
    if(mode==6 && !nonzero_slope)return 4;
    if(mode==7){puts("PASS edge wrappers match original height and eight slopes");return 0;}
    sample_calls=0;
    x=row*1024+512;y=col*1024+512;
    if(shot_preview_height(x,y)!=0)return 4;
    for(unsigned d=0;d<8;d++)if(shot_preview_slope(x,y,d)!=0)return 4;
    if(sample_calls){fputs("FAIL wrapper called unsafe sampler\n",stderr);return 4;}
    puts("PASS production wrappers preserve valid values and reject unsafe calls");
    return 0;
  }
  unsigned char saved[256];memset(saved,0x3a,sizeof saved);
  memcpy((void *)0x5795b8,saved,sizeof saved);
  unsigned result=FUN_004226a0(1,0,0,0);
  if(result || memcmp((void *)0x5795b8,saved,sizeof saved)) {
    fputs("FAIL result or player record changed\n",stderr);return 2;
  }
  int steps=mode==4?2:1;
  int movement=mode==5?-1024:mode>=2?1024:0;
  int expected_x=row*1024+(mode==1?0:512)+steps*movement;
  int expected_y=col*1024+512;
  if(motion_calls!=steps || *(int *)0x56a78c!=expected_x || *(int *)0x56a790!=expected_y) {
    fputs("FAIL valid trajectory outcome changed\n",stderr);return 2;
  }
  // This case formerly faulted in the classifier before any flags read.
  if(MAP_W==100 && MAP_H==50 && row==75 && classifier_calls) {
    fputs("FAIL unsafe rectangular classifier call\n",stderr);return 2;
  }
  printf("PASS preview result=0 restored=1 mode=%d map=%dx%d row=%d col=%d\n",
         mode,MAP_W,MAP_H,row,col);
  return 0;
}
'''


class NativePreviewBounds(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name != "nt":
            raise unittest.SkipTest("actual preview fixture requires Windows/x86")
        cc = compiler("clang-cl")
        if not cc:
            raise unittest.SkipTest("Windows clang-cl unavailable; set SIMGOLF_TEST_C_COMPILER")
        gen = Path(os.environ.get("PORT_GEN", ROOT / "build/gen"))
        source_path = gen / "golf/golf_01.c"
        if not source_path.is_file():
            raise unittest.SkipTest("translated cache unavailable; set PORT_GEN or translate build/gen")
        text = source_path.read_text()
        height_path = gen / "golf/golf_02.c"
        if not height_path.is_file():
            raise unittest.SkipTest("translated height cache unavailable; translate build/gen")

        def section(source, name):
            start = source.index("/* " + name + " @")
            end = source.index("/* FUN_", start + 5)
            return source[start:end]

        preview = section(text, "FUN_004226a0")
        callees = "".join(section(text, name) for name in
                          ["FUN_0040bf60", "FUN_0040c170", "FUN_0040bfe0",
                           "FUN_0040c2f0", "FUN_0040c3a0", "FUN_0040c450"])
        callees += section(height_path.read_text(), "FUN_0042fa30")
        cls.work = tempfile.TemporaryDirectory(prefix="simgolf-preview-native-")
        cls.addClassCleanup(cls.work.cleanup)
        tmp = Path(cls.work.name)

        def remap(match):
            address = int(match[0], 16)
            if 0x4c0000 <= address <= 0x840000:
                return "(uintptr_t)(image+%du)" % (address - 0x400000)
            return match[0]

        # Redirect original image addresses into synthetic memory. The actual
        # translated preview body, relocated grid accesses and header stay real.
        source = re.sub(r"0x[0-9a-f]+", remap, NATIVE_PREFIX + callees + preview + NATIVE_SUFFIX)
        src, cls.exe = tmp / "preview.c", tmp / "preview.exe"
        src.write_text(source)
        # Compile the real wrapper source as a separate translation unit. This
        # minimal generated-prototype substitute declares only its two callees.
        (tmp / "golf_protos.h").write_text(
            "int FUN_0042fa30(int,int);\nint FUN_0040c450(int,int,unsigned);\n")
        built = subprocess.run([cc, "/nologo", "/MT", "/Od",
                                "/clang:--target=i686-pc-windows-msvc",
                                "/I" + str(tmp), "/I" + str(ROOT / "port/src"), str(src),
                                str(ROOT / "port/src/port/golf/shot_preview.c"),
                                "/Fe:" + str(cls.exe), "/Fo:" + str(tmp) + os.sep,
                                "/link", "/BASE:0x60000000", "/DYNAMICBASE:NO"],
                               cwd=tmp, capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)

    def run_case(self, mode, width, height, row, col=21, high=0, clock=1):
        result = subprocess.run([str(self.exe), *map(str, (mode, width, height, row, col, high, clock))],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("result=0 restored=1", result.stdout)

    def test_negative_and_positive_boundaries(self):
        for width, height in [(50, 50), (158, 158), (50, 100), (100, 50)]:
            for mode in range(3):
                for row, high in [(-3, 0), (height + 3, 1)]:
                    with self.subTest(mode=mode, width=width, height=height, row=row):
                        self.run_case(mode, width, height, row, high=high)

    def test_wide_rectangle_classifier_precheck(self):
        for mode in range(3):
            with self.subTest(mode=mode):
                self.run_case(mode, 100, 50, 75, high=1)

    def test_interior_outcomes_and_restoration(self):
        for width, height, row in [(50, 50, 10), (158, 158, 10), (100, 50, 10), (50, 100, 75)]:
            for mode in range(3):
                with self.subTest(mode=mode, width=width, height=height, row=row):
                    self.run_case(mode, width, height, row)

    def test_movement_crosses_storage_before_ground_and_air_sampling(self):
        for width, height in [(50, 50), (158, 158), (50, 100), (100, 50)]:
            for mode, row, high in [(3, height, 1), (4, height, 1), (5, -1, 0)]:
                with self.subTest(mode=mode, width=width, height=height, row=row):
                    self.run_case(mode, width, height, row, high=high)

    def test_production_wrappers_with_real_sampling_callees(self):
        for width, height in [(50, 50), (158, 158), (50, 100), (100, 50)]:
            with self.subTest(width=width, height=height):
                result = subprocess.run([str(self.exe), *map(str, (6, width, height, -3, 21, 0))],
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("production wrappers preserve valid values", result.stdout)

    def test_rectangular_trailing_padding_corner_neighbors(self):
        for mode in range(3):
            with self.subTest(mode=mode):
                self.run_case(mode, 100, 50, 51, col=1, high=1)

    def test_in_map_edges_and_corners(self):
        for width, height in [(50, 50), (158, 158), (50, 100), (100, 50)]:
            points = [(0, 0), (0, width-1), (height-1, 0), (height-1, width-1),
                      (0, width//2), (height-1, width//2),
                      (height//2, 0), (height//2, width-1)]
            for row, col in points:
                for mode in range(3):
                    with self.subTest(width=width, height=height, row=row, col=col, mode=mode):
                        self.run_case(mode, width, height, row, col=col, high=int(row>=height//2))
                # Non-flat in-map edge sampling must stay byte-for-byte equal
                # to the original callees, including all eight slope directions.
                with self.subTest(width=width, height=height, row=row, col=col, wrappers=True):
                    result = subprocess.run([str(self.exe), *map(str, (7, width, height, row, col, 1))],
                                            capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("edge wrappers match original", result.stdout)

    def test_clock_zero_corner_neighbor_bounds(self):
        points = [(100, 50, 49, 99), (100, 50, 49, 50),
                  (50, 50, 49, 49), (158, 158, 157, 157), (50, 100, 99, 49)]
        for width, height, row, col in points:
            for mode in range(3):
                with self.subTest(width=width, height=height, row=row, col=col, mode=mode):
                    self.run_case(mode, width, height, row, col=col, high=1, clock=0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
