"""Real SDL glyph pixels survive font, allocation, upload and drawing failures."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TextResourceRecovery(unittest.TestCase):
    def test_pixel_identical_recovery(self):
        ext = Path(os.environ.get('TOOLS_EXT', 'C:/simgolf-tools'))
        sdl = Path(os.environ.get('SIMGOLF_SDL2', str(ext / 'SDL2-2.32.10')))
        cc = os.environ.get('SIMGOLF_TEST_C_COMPILER') or shutil.which('clang-cl')
        if not cc or not (sdl / 'lib/x86/SDL2.lib').is_file():
            self.skipTest('Requires clang-cl and the supplied x86 SDL SDK')
        with tempfile.TemporaryDirectory(prefix='simgolf-text-') as temp:
            out = Path(temp)
            exe = out / 'text.exe'
            build = subprocess.run([cc, '--target=i686-pc-windows-msvc', '/nologo', '/O2',
                '/D_CRT_SECURE_NO_WARNINGS', '/I'+str(ROOT/'port/src'),
                '/I'+str(ROOT/'port/third_party/stb'), '/I'+str(sdl/'include'),
                str(ROOT/'port/tests/text_resources.c'), '/Fe'+str(exe),
                '/Fo'+str(out/'fixture.obj'), '/link', str(sdl/'lib/x86/SDL2.lib')],
                cwd=out, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout+build.stderr)
            env = dict(os.environ, PATH=str(sdl/'lib/x86')+os.pathsep+os.environ['PATH'])
            run = subprocess.run([str(exe), str(ROOT/'data/fonts/plus-jakarta-sans')],
                env=env, cwd=out, capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
            self.assertIn('33 checks, 0 failures', run.stdout)
