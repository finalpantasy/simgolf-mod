"""Exercise the real common property-purchase decision, without launching golf.

Set PORT_EXPORTS to the authorized cached Ghidra export directory (containing
port/functions.jsonl), or SIMGOLF_FUNCTIONS_JSONL to that file. Decompiled code
is read at test time and written only to the checkout's ignored build/tests.
The menu's affordability/debit blocks and its complete short caller are taken
from that cache, with current translator patches applied in production order.
Drawing/course generation are mocked; production course_switch.c and the
existing arena/save mocks from test_course_switch.c handle snapshot decisions.
By default, checked test-only memory instrumentation maps original addresses
to synthetic byte arrays. This is a component test, not native-address/gameplay
acceptance. SIMGOLF_PURCHASE_FIXED_ARENA=1 retains the fixed-address diagnostic.
SIMGOLF_PURCHASE_TEST_VARIANT=released omits only golf.course_purchase.json and
is expected to fail the two save-failure scenarios; the default is fixed.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TRANSLATE = ROOT / "port/tools/translate"
sys.path.insert(0, str(TRANSLATE))
from required_patches import validate_required_patches


def replace_exact(source, old, new, count):
    actual = source.count(old)
    if actual != count:
        raise AssertionError(f"test-memory instrumentation drift: expected {count}, got {actual}: {old!r}")
    return source.replace(old, new)


def patched_functions(export, released=False):
    wanted = {"FUN_0046f550", "FUN_00407d30"}
    functions = {}
    with export.open(encoding="utf-8") as stream:
        for line in stream:
            entry = json.loads(line)
            if entry.get("name") in wanted:
                functions[entry["name"]] = entry
    if set(functions) != wanted:
        raise AssertionError("cached export lacks the purchase menu/caller")
    patches, required = {}, {}
    paths = [TRANSLATE / "golf.json", *sorted(TRANSLATE.glob("golf.*.json"))]
    if released:
        if sum(path.name == "golf.course_purchase.json" for path in paths) != 1:
            raise AssertionError("released comparison requires exactly one current purchase-guard topic")
        paths = [path for path in paths if path.name != "golf.course_purchase.json"]
    for path in paths:
        config = json.loads(path.read_text(encoding="utf-8"))
        for key, target in (("patches", patches), ("required_patch_counts", required)):
            for name, entries in config.get(key, {}).items():
                if name in wanted:
                    target.setdefault(name, []).extend(entries)
    validate_required_patches(list(functions.values()), required, patches)
    result = {}
    for name, entry in functions.items():
        body = entry["c"]
        for old, new in patches.get(name, []):
            # Match translate.py: optional patches may be no-ops, while the
            # required anchors above must match their declared exact counts.
            body = body.replace(old, new)
        result[name] = body
    return result


def native_source(functions, fixed_arena=False):
    menu = functions["FUN_0046f550"]
    cost = "uVar6 = FUN_0046f1d0(iVar5);"
    if menu.count(cost) != 2:
        raise AssertionError("expected one affordability check and one purchase debit")
    start = menu.index(cost)
    end = menu.index("FUN_0045c0c0(0);", start) + len("FUN_0045c0c0(0);")
    decision = menu[start:end]
    start = menu.index(cost, end)
    end = menu.index("\n", menu.index("DAT_00543cfc =", start))
    debit = menu[start:end]
    fixture = (ROOT / "port/tests/test_course_switch.c").read_text(encoding="utf-8")
    if fixture.count("int main(void) {") != 1:
        raise AssertionError("course-switch arena fixture changed")
    mocks = fixture.split("int main(void) {")[0]
    source = mocks + r'''
typedef unsigned int uint;
typedef uint32_t undefined4;
typedef uint16_t undefined2;
typedef uint8_t undefined1;
typedef uint8_t byte;
_Static_assert(sizeof(void *) == 4, "use a Windows x86 compiler for this fixture");
#define DAT_00571fd4 I32(0x571fd4)
#define DAT_00543cfc I32(0x543cfc)
#define DAT_0059e7b8 I32(0x59e7b8)
#define DAT_00834170 I32(0x834170)
#define DAT_00571ffb U8(0x571ffb)
#define DAT_00571ff8 U8(0x571ff8)
#define DAT_0059bf90 I32(0x59bf90)
#define DAT_005a6d3c I32(0x5a6d3c)
#define DAT_005a34e0 I32(0x5a34e0)
#define DAT_0058421e U8(0x58421e)
#define DAT_00822d68 I32(0x822d68)
#define DAT_00519a60 U8(0x519a60)
#define PTR_DAT_004c1570 0
static char s_We_re_off_to_004e3eac[] = "test loading text";
static int clear_calls, last_choice = -1;
static uint FUN_0046f1d0(int site) { assert(site == 1); return 100; }
static void FUN_004762d0(int a,int b,int c,int d,int e) {(void)a;(void)b;(void)c;(void)d;(void)e;}
static void FUN_0040cef0(int a,int b,int c,int d,int e) {(void)a;(void)b;(void)c;(void)d;(void)e;}
static void FUN_0040d320(int a,int b,unsigned c,int d) {(void)a;(void)b;(void)c;(void)d;}
static void FUN_00480c80(void *p) {(void)p;}
static void FUN_0045c0c0(int a) {(void)a;}

static int purchase_once(void) {
  int iVar5 = 1;
  uint uVar6;
  int bVar17 = I32(0x5a6364) == 0x11;
  char *pcVar3;
  for (;;) {
''' + decision + r'''
    return -1; /* Unaffordable: no accepted selection in this input cycle. */
LAB_0046f733:
    return -1; /* Menu redraw: no accepted selection in this input cycle. */
  }
''' + debit + r'''
  /* fe_world_end forwards a successful selection here after the debit. */
  course_switch_before_purchase(iVar5);
  return iVar5;
}
static uint thunk_FUN_0046f550(void) { last_choice = purchase_once(); return (uint)last_choice; }
static uint FUN_004a0890(void) { return 0; }
static void FUN_00442180(int a) {(void)a; clear_calls++; U8(0x5722e8) = 0;}
static void FUN_004315e0(void) {}
static void clubhouse_start_begin(void) {}
static void clubhouse_start_end(void) {}
static void FUN_00470a60(uint site) { I32(0x59bf90) = (int)site; U8(0x5722e8) = 22; }
static void FUN_0042f7a0(void) {}
static void FUN_00449400(int theme) {(void)theme;}
static void FUN_00449520(void) {}
''' + functions["FUN_00407d30"] + r'''

static int case_failed;
static void require_case(int ok, const char *scenario, const char *message) {
  if (!ok) { fprintf(stderr, "FAIL %s: %s\n", scenario, message); case_failed = 1; }
}
static void reset_case(void) {
  for (int i = 0; i < 16; i++) { free(saves[i]); saves[i] = NULL; }
  memset(save_names, 0, sizeof save_names);
  memset((void *)(uintptr_t)MID, 0, MID_BYTES);
  memset((void *)(uintptr_t)HIGH, 0, HIGH_BYTES);
  memset((void *)(uintptr_t)ROSTER, 0, ROSTER_BYTES);
  clear_calls = 0;
  last_choice = -1;
  last_save_write = -1;
  fail_next_save = 0;
  case_failed = 0;
  course_switch_refresh(); /* Zero live-game state also resets the snapshot cache. */
}
static int run_case(const char *scenario) {
  reset_case();
  int retry = !strcmp(scenario, "fail-then-retry");
  int blocked = !strcmp(scenario, "failed-save") || retry;
  int off = !strcmp(scenario, "off");
  int fresh = !strcmp(scenario, "new-career");
  int unaffordable = !strcmp(scenario, "unaffordable");
  int special = !strcmp(scenario, "special-budget");
  require_case(blocked || off || fresh || unaffordable || special || !strcmp(scenario,"success"),
               scenario, "unknown scenario");
  multi_course = !off;
  fail_next_save = blocked || off || fresh || unaffordable;
  I32(0x822c78) = 12345;
  DAT_00834170 = fresh ? 0 : 100;
  DAT_0059bf90 = 0;
  DAT_00571fd4 = unaffordable || special ? 10 : 500;
  DAT_005a6d3c = 5;
  I32(0x5a6364) = special ? 0x11 : 13;
  U8(0x571ff4) = 0;
  U8(0x571ff4 + STRIDE) = 1;
  U8(0x5722e8) = 11;
  DAT_00822d68 = 1;
  FUN_00407d30(0);
  printf("%s: choice=%d cash=%d clear=%d terrain=%u snapshot=%d pending_mouse=%d\n",
         scenario, last_choice, DAT_00571fd4, clear_calls, U8(0x5722e8), saves[0]!=NULL, DAT_00822d68);
  fflush(stdout);
  if (blocked || unaffordable) {
    require_case(last_choice == -1, scenario, "purchase accepted without a safe recovery snapshot");
    require_case(!clear_calls && U8(0x5722e8) == 11 && DAT_0059bf90 == 0,
                 scenario, "old course changed on rejected purchase");
    require_case(DAT_00571fd4 == (unaffordable ? 10 : 500), scenario, "cash deducted on rejected purchase");
    require_case(!saves[0], scenario, "rejected purchase unexpectedly wrote a snapshot");
    if (blocked) require_case(DAT_00822d68 == 0, scenario, "failed purchase leaves a pending click");
  } else {
    require_case(last_choice == 1 && clear_calls == 1 && DAT_0059bf90 == 1 && U8(0x5722e8) == 22,
                 scenario, "allowed original purchase did not generate the new course");
    require_case(DAT_00571fd4 == (special ? -90 : 400), scenario, "original price deduction changed");
    require_case((saves[0] != NULL) == (!off && !fresh), scenario, "snapshot disagrees with opt-in/live-course state");
    if (saves[0]) require_case(saves[0]->mid[0x5722e8-MID] == 11, scenario, "snapshot lost the old layout");
  }
  if (retry && !case_failed) {
    fail_next_save = 0;
    DAT_00822d68 = 1; /* A new real click after the write failure is removed. */
    FUN_00407d30(0);
    printf("retry: choice=%d cash=%d clear=%d terrain=%u snapshot=%d\n",
           last_choice, DAT_00571fd4, clear_calls, U8(0x5722e8), saves[0]!=NULL);
    require_case(last_choice == 1 && clear_calls == 1 && DAT_0059bf90 == 1 && U8(0x5722e8) == 22,
                 scenario, "retry did not perform exactly one course replacement");
    require_case(DAT_00571fd4 == 400, scenario, "retry did not charge exactly one purchase price");
    require_case(saves[0] != NULL, scenario, "retry did not save a recovery snapshot");
    if (saves[0]) require_case(saves[0]->mid[0x5722e8-MID] == 11, scenario, "retry snapshot lost the old layout");
  }
  printf("%s %s\n", case_failed ? "FAIL" : "PASS", scenario);
  return case_failed;
}
int main(void) {
#ifdef FIXTURE_COMPONENT_MEMORY
  puts("memory mode: synthetic component arrays (not native-address acceptance)");
#else
  printf("fixture process heap: %p\n", GetProcessHeap());
  const uintptr_t base[3] = {MID, HIGH, ROSTER};
  const size_t bytes[3] = {MID_BYTES, HIGH_BYTES, ROSTER_BYTES};
  for (int i = 0; i < 3; i++) {
    void *p = VirtualAlloc((void *)base[i], bytes[i], MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (p != (void *)base[i]) {
      DWORD error = GetLastError();
      MEMORY_BASIC_INFORMATION info;
      memset(&info, 0, sizeof info);
      VirtualQuery((void *)base[i], &info, sizeof info);
      fprintf(stderr, "fixed test arena unavailable at %p (error %lu); allocation=%p region=%p size=%zu state=%lu type=%lu\n",
              (void *)base[i], error, info.AllocationBase, info.BaseAddress, info.RegionSize, info.State, info.Type);
      return 2;
    }
  }
#endif
  const char *scenarios[] = {"failed-save", "success", "off", "new-career",
                             "unaffordable", "special-budget", "fail-then-retry"};
  int failed = 0;
  for (unsigned i = 0; i < sizeof scenarios / sizeof scenarios[0]; i++) failed += run_case(scenarios[i]);
  printf("purchase guard: %d passed, %d failed, 0 skipped\n", 7-failed, failed);
  reset_case();
#ifndef FIXTURE_COMPONENT_MEMORY
  for (int i = 0; i < 3; i++) assert(VirtualFree((void *)base[i], 0, MEM_RELEASE));
#endif
  return failed ? 1 : 0;
}
'''
    if fixed_arena:
        return source
    source = replace_exact(source, "#define I32(a) (*(int32_t *)(uintptr_t)(a))",
                           "void *fixture_ptr(uintptr_t address);\n#define I32(a) (*(int32_t *)fixture_ptr((uintptr_t)(a)))", 1)
    source = replace_exact(source, "#define U8(a) (*(uint8_t *)(uintptr_t)(a))", r'''
#define U8(a) (*(uint8_t *)fixture_ptr((uintptr_t)(a)))
static _Alignas(16) uint8_t arena_mid[MID_BYTES], arena_high[HIGH_BYTES], arena_roster[ROSTER_BYTES];
void *fixture_ptr(uintptr_t address) {
  if (address >= MID && address - MID < MID_BYTES) return arena_mid + address - MID;
  if (address >= HIGH && address - HIGH < HIGH_BYTES) return arena_high + address - HIGH;
  if (address >= ROSTER && address - ROSTER < ROSTER_BYTES) return arena_roster + address - ROSTER;
  fprintf(stderr, "unmapped fixture address: %08lx\n", (unsigned long)address);
  exit(3);
}
''', 1)
    for base in ("MID", "HIGH", "ROSTER"):
        # Existing save/load mocks and reset_case each access one whole arena.
        source = replace_exact(source, "(void *)(uintptr_t)" + base, "fixture_ptr(" + base + ")", 3)
    return "#define FIXTURE_COMPONENT_MEMORY 1\n" + source


class CoursePurchaseGuard(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name != "nt":
            raise unittest.SkipTest("Windows x86 arena test")
        default = Path(os.environ.get("PORT_EXPORTS", ROOT / "build/ghidra_work")) / "port/functions.jsonl"
        export = Path(os.environ.get("SIMGOLF_FUNCTIONS_JSONL", default))
        if not export.is_file():
            raise unittest.SkipTest("set SIMGOLF_FUNCTIONS_JSONL to the authorized cached export")
        cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cl")
        if not cc:
            raise unittest.SkipTest("set CC to clang or use the x86 MSVC developer environment")
        variant = os.environ.get("SIMGOLF_PURCHASE_TEST_VARIANT", "fixed")
        if variant not in ("released", "fixed"):
            raise AssertionError("SIMGOLF_PURCHASE_TEST_VARIANT must be released or fixed")
        fixed_arena = os.environ.get("SIMGOLF_PURCHASE_FIXED_ARENA") == "1"
        source = native_source(patched_functions(export, variant == "released"), fixed_arena)
        print(f"purchase fixture variant={variant}, fixed_arena={int(fixed_arena)}")
        out = (ROOT / "build/tests").resolve()
        if not out.is_relative_to(ROOT.resolve()):
            raise AssertionError("test output escapes the owned checkout")
        out.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="course-purchase-", dir=out)
        cls.addClassCleanup(cls.temp.cleanup)
        file = Path(cls.temp.name) / "fixture.c"
        cls.exe = Path(cls.temp.name) / "fixture.exe"
        file.write_text(source, encoding="utf-8")
        production = ROOT / "port/src/port/golf/course_switch.c"
        if not fixed_arena:
            helper = replace_exact(production.read_text(encoding="utf-8"),
                                   "#define VALUE(a) (*(volatile int32_t *)(uintptr_t)(a))",
                                   "void *fixture_ptr(uintptr_t address);\n#define VALUE(a) (*(volatile int32_t *)fixture_ptr((uintptr_t)(a)))", 1)
            production = Path(cls.temp.name) / "course_switch_fixture.c"
            production.write_text(helper, encoding="utf-8")
            print("checked memory replacements: I32=1 U8=1 MID/HIGH/ROSTER=3 each production VALUE=1")
        if Path(cc).name.lower() in ("cl", "cl.exe"):
            command = [cc, "/nologo", "/TC", "/std:c11", "/Od", "/D_CRT_SECURE_NO_WARNINGS",
                       "/I" + str(ROOT / "port/tests"), "/I" + str(ROOT / "port/src"),
                       str(file), str(production),
                       "/Fo" + cls.temp.name + "\\", "/Fe" + str(cls.exe),
                       "/link", "/BASE:0x60000000", "/DYNAMICBASE"]
        else:
            command = [cc, "-target", "i686-pc-windows-msvc", "-std=c11", "-O0",
                       "-I", str(ROOT / "port/tests"), "-I", str(ROOT / "port/src"),
                       str(file), str(production),
                       "-o", str(cls.exe), "-Wl,/BASE:0x60000000,/DYNAMICBASE"]
        built = subprocess.run(command, capture_output=True, text=True, timeout=90)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)

    def test_common_purchase_failure_and_controls(self):
        # Every scenario resets all arena/save state in one child process.
        # Fixed-address mode is an explicit diagnostic, never retried or skipped.
        run = subprocess.run([str(self.exe)], capture_output=True, text=True,
                             timeout=10, env=dict(os.environ, SIMGOLF_MUTE="1"))
        print(run.stdout.strip())
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
