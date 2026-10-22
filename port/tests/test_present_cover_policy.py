"""Exercise the production presenter policy without game assets or a window."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CoverPolicy(unittest.TestCase):
    def test_options_hold_and_full_screen_precedence(self):
        cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
        if not cc:
            self.skipTest("Set CC to an installed C compiler")
        source = r'''
#include <stdio.h>
#include "port/hd_present_policy.h"
int main(void) {
  int failures = 0;
  /* All 2048 combinations: a cover must publish, regardless of Options,
     transition, pause, or native dialog state. Ordinary Options retention
     remains available only with a complete course and no competing page. */
  for (unsigned bits = 0; bits < 2048; ++bits) {
    int f=(bits>>0)&1, l=(bits>>1)&1, c=(bits>>2)&1;
    int t=(bits>>3)&1, o=(bits>>4)&1, p=(bits>>5)&1;
    int d=(bits>>6)&1, n=(bits>>7)&1, r=(bits>>8)&1;
    int cover=(bits>>9)&1, enabled=(bits>>10)&1;
    int base = !f && l && c && enabled && !r && !cover;
    int expected = base && ((!d && (t || o || p)) || (d && n));
    int got = hd_present_hold_course(f,l,c,t,o,p,d,n,r,cover,enabled);
    int macro = HD_PRESENT_SHOULD_HOLD_COURSE(f,l,c,t,o,d,r,cover,enabled);
    if (got != expected || macro != (base && !d && (t || o))) {
      fprintf(stderr, "policy mismatch bits=%u hold=%d expected=%d\n", bits, got, expected);
      ++failures;
    }
  }
  return failures != 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="simgolf-cover-") as temp:
            src = Path(temp) / "policy.c"
            exe = Path(temp) / "policy.exe"
            src.write_text(source, encoding="utf-8")
            result = subprocess.run([cc, *shlex.split(os.environ.get("CFLAGS", "")),
                "-std=c11", "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "port/src"),
                str(src), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
