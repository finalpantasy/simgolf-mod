"""Windows/x86 regression for completed updater backups under a frozen clock.

Set SIMGOLF_TEST_C_COMPILER to an installed clang-cl.exe, then run this file.
Only synthetic files and the updater are used; no payload is executed.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMPILER = os.environ.get("SIMGOLF_TEST_C_COMPILER")
HARNESS = r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static VOID WINAPI fixture_time(LPSYSTEMTIME st) {
  SYSTEMTIME fixed = {2026, 10, 0, 18, 12, 0, 0, 0};
  *st = fixed;
}
#define GetLocalTime fixture_time
#include "SOURCE"
'''


@unittest.skipUnless(os.name == "nt" and COMPILER,
                     "requires Windows and SIMGOLF_TEST_C_COMPILER=clang-cl.exe")
class BackupPruning(unittest.TestCase):
    def test_same_second_suffix_reuse_keeps_only_current_completed_backup(self):
        with tempfile.TemporaryDirectory(prefix="sg-pruning-") as temp:
            root = Path(temp)
            source = root / "pruning.c"
            executable = root / "pruning.exe"
            source.write_text(HARNESS.replace("SOURCE", (ROOT / "port/src/updater/sgupdate.c").as_posix()))
            compiled = subprocess.run([
                COMPILER, "--target=i686-pc-windows-msvc", "/nologo", "/O2",
                "/D_CRT_SECURE_NO_WARNINGS", "/Fe:" + str(executable),
                "/Fo:" + str(root / "pruning.obj"), str(source), "/link",
                "/MANIFEST:EMBED", "/MANIFESTUAC:level='asInvoker' uiAccess='false'",
            ], capture_output=True, text=True, cwd=root)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            app = root / "app"
            stage = app / "updates/stage-fixture/app"
            stage.mkdir(parents=True)
            # A failed older attempt must survive every later successful update.
            recovery = app / "updates/backup-20261018-120000-099"
            recovery.mkdir()
            (recovery / "golf.exe").write_bytes(b"unmarked recovery sentinel")
            (app / "golf.exe").write_bytes(b"original program")
            (app / "Sim Golf Remastered.exe").write_bytes(b"original alias")
            previous = b"original program"
            for generation in range(3):
                payload = ("synthetic program " + str(generation)).encode("ascii")
                stage.mkdir(parents=True, exist_ok=True)
                (stage / "golf.exe").write_bytes(payload)
                result = subprocess.run([
                    str(executable), "--apply", "--app", str(app), "--stage", str(stage),
                ], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stderr)
                completed = [p for p in (app / "updates").glob("backup-*")
                             if (p / ".update-complete").is_file()]
                self.assertEqual(len(completed), 1, "obsolete completed backup survived suffix reuse")
                self.assertEqual((completed[0] / "golf.exe").read_bytes(), previous)
                self.assertEqual((app / "golf.exe").read_bytes(), payload)
                self.assertEqual((app / "Sim Golf Remastered.exe").read_bytes(), payload)
                self.assertEqual((recovery / "golf.exe").read_bytes(), b"unmarked recovery sentinel")
                previous = payload


if __name__ == "__main__":
    unittest.main(verbosity=2)
