"""Windows x86 crash breadcrumbs at a readable stack-region boundary."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.name == "nt", "Windows crash recorder")
class CrashStackBoundary(unittest.TestCase):
    def test_readable_prefix_and_inaccessible_stack(self):
        cc = os.environ.get("CC") or shutil.which("clang")
        if not cc:
            self.skipTest("Set CC to an installed clang compiler")
        source = r'''
#define _CRT_SECURE_NO_WARNINGS
#include "port/crash_record.h"
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  SYSTEM_INFO si; GetSystemInfo(&si);
  unsigned char *p = VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
  if (!p || !VirtualAlloc(p, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE)) return 3;
  EXCEPTION_RECORD e = {0}; CONTEXT c = {0};
  EXCEPTION_POINTERS ep = {&e, &c};
  e.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
  e.ExceptionAddress = (void *)main;
  for (int i = 0; i < 4; ++i) ((DWORD *)(p + si.dwPageSize - 16))[i] = 0x12345000 + i;
  char name[MAX_PATH];
  for (int test = 0; test < 3; ++test) {
    c.Esp = (DWORD)(uintptr_t)(p + si.dwPageSize - (test == 0 ? 16 : test == 1 ? 512 : 0));
    snprintf(name, sizeof name, "%s/case%d", argv[1], test);
    if (!host_write_crash_record(name, &ep)) return 4;
  }
  VirtualFree(p, 0, MEM_RELEASE);
  return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="simgolf-stack-") as temp:
            src = Path(temp) / "stack.c"
            exe = Path(temp) / "stack.exe"
            src.write_text(source, encoding="utf-8")
            result = subprocess.run([cc, "-target", "i686-pc-windows-msvc", "-std=c11",
                "-I" + str(ROOT / "port/src"), str(src), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe), temp], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            partial = (Path(temp) / "case0.crash.txt").read_text()
            full = (Path(temp) / "case1.crash.txt").read_text()
            inaccessible = (Path(temp) / "case2.crash.txt").read_text()
            self.assertIn("12345000 12345001 12345002 12345003", partial)
            self.assertEqual(partial.count("stack "), 1)
            self.assertEqual(full.count("stack "), 16)
            self.assertNotIn("stack ", inaccessible)
            for text in (partial, full, inaccessible):
                self.assertIn("exception c0000005", text)
                self.assertIn("[fault]", text)
                self.assertNotIn(temp.lower(), text.lower())


if __name__ == "__main__":
    unittest.main()
