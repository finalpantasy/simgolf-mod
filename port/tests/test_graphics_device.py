"""Device replacement detection must work without SDL's reset notification."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class GraphicsDevice(unittest.TestCase):
    def test_device_identity_and_reference_ownership(self):
        ext = Path(os.environ.get('TOOLS_EXT', 'C:/simgolf-tools'))
        sdl = Path(os.environ.get('SIMGOLF_SDL2', str(ext / 'SDL2-2.32.10')))
        cc = os.environ.get('SIMGOLF_TEST_C_COMPILER') or shutil.which('clang-cl')
        if not cc or not (sdl / 'include/SDL.h').is_file():
            self.skipTest('Requires clang-cl and the supplied SDL SDK')
        with tempfile.TemporaryDirectory(prefix='simgolf-device-') as temp:
            out = Path(temp)
            exe = out / 'device.exe'
            build = subprocess.run([cc, '--target=i686-pc-windows-msvc', '/nologo', '/O2',
                '/I'+str(ROOT/'port/src'), '/I'+str(sdl/'include'),
                str(ROOT/'port/tests/graphics_device.c'), '/Fe'+str(exe),
                '/Fo'+str(out/'fixture.obj')], cwd=out, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout+build.stderr)
            run = subprocess.run([str(exe)], cwd=out, capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
            self.assertIn('13 checks, 0 failures', run.stdout)
