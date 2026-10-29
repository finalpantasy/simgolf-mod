"""Check the shipped icon bytes and optional real Windows executable/window use.

Portable discovery validates the ICO. For native acceptance, set
SIMGOLF_ICON_EXE to the rebuilt golf.exe and SIMGOLF_ICON_RUNTIME=1. The latter
also needs CC (clang), SDL2_ROOT and RC (Windows resource compiler), or their
usual installed defaults. No game process, installation or player data is used.

Resource-only baseline example:
  python test_native_icon.py --exe path/to/old/golf.exe --resource-only
Full native example:
  python test_native_icon.py --exe build/native/golf.exe --runtime
"""
import argparse
import ctypes
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SIZES = (16, 20, 24, 32, 48, 64, 128, 256)
ICON_ID = 101


def unpack(data, offset, fmt):
    length = struct.calcsize(fmt)
    if offset < 0 or offset + length > len(data):
        raise AssertionError("truncated icon/PE structure")
    return struct.unpack_from(fmt, data, offset)


def image_size(payload):
    if payload.startswith(b"\x89PNG\r\n\x1a\n"):
        assert payload[12:16] == b"IHDR", "PNG icon is missing IHDR"
        width, height, depth, colour = unpack(payload, 16, ">IIBB")
        assert (depth, colour) == (8, 6), "icon PNG must be 32-bit RGBA"
        return width, height
    header, width, doubled_height, planes, bits, compression = unpack(
        payload, 0, "<IiiHHI")
    assert header >= 40 and planes == 1 and bits == 32 and compression == 0, \
        "icon DIB must be uncompressed 32-bit BGRA"
    assert width > 0 and doubled_height > 0 and doubled_height % 2 == 0
    height = doubled_height // 2
    pixels_end = header + width * height * 4
    mask_bytes = ((width + 31) // 32) * 4 * height
    assert len(payload) >= pixels_end + mask_bytes, "truncated icon DIB pixels/mask"
    assert any(payload[header + 3:pixels_end:4]), "icon DIB is completely transparent"
    return width, height


def ico_images(path):
    data = Path(path).read_bytes()
    reserved, kind, count = unpack(data, 0, "<HHH")
    assert (reserved, kind, count) == (0, 1, len(SIZES)), "ICO must contain eight frames"
    images = {}
    for i in range(count):
        w, h, colours, zero, planes, bits, length, offset = unpack(
            data, 6 + i * 16, "<BBBBHHII")
        w, h = w or 256, h or 256
        assert w == h and w not in images, "ICO has duplicate/non-square frames"
        assert colours == zero == 0 and planes == 1 and bits == 32
        assert offset >= 6 + count * 16 and offset + length <= len(data)
        payload = data[offset:offset + length]
        assert image_size(payload) == (w, h), "ICO directory disagrees with image size"
        images[w] = payload
    assert tuple(sorted(images)) == SIZES, f"ICO sizes differ: {sorted(images)}"
    return images


def pe_resources(path):
    """Read resource bytes without loading or executing the game executable."""
    data = Path(path).read_bytes()
    assert data[:2] == b"MZ", "not a PE executable"
    pe, = unpack(data, 0x3c, "<I")
    assert data[pe:pe + 4] == b"PE\0\0", "bad PE signature"
    machine, sections = unpack(data, pe + 4, "<HH")
    assert machine == 0x14c, "native game/test executable must be x86"
    opt_size, = unpack(data, pe + 20, "<H")
    opt = pe + 24
    magic, = unpack(data, opt, "<H")
    assert magic == 0x10b, "native game/test executable must be PE32"
    resource_rva, resource_size = unpack(data, opt + 96 + 2 * 8, "<II")
    assert resource_rva and resource_size, "executable has no resource directory"
    section_rows = []
    for i in range(sections):
        virtual_size, rva, raw_size, raw_offset = unpack(
            data, opt + opt_size + i * 40 + 8, "<IIII")
        section_rows.append((rva, virtual_size, raw_offset, raw_size))

    def file_offset(rva, size):
        for start, virtual_size, raw, raw_size in section_rows:
            delta = rva - start
            if 0 <= delta and delta + size <= raw_size:
                assert raw + delta + size <= len(data), "resource exceeds file"
                return raw + delta
        raise AssertionError("resource RVA is outside section data")

    base = file_offset(resource_rva, resource_size)
    found = {}

    def walk(relative, keys):
        assert relative + 16 <= resource_size, "resource directory outside section"
        named, numbered = unpack(data, base + relative + 12, "<HH")
        assert named + numbered <= 4096, "unreasonable resource entry count"
        assert relative + 16 + (named + numbered) * 8 <= resource_size
        for i in range(named + numbered):
            name, target = unpack(data, base + relative + 16 + i * 8, "<II")
            # Only numeric resource types/IDs/languages are relevant here.
            if name & 0x80000000:
                continue
            next_keys = keys + (name,)
            if target & 0x80000000:
                assert len(next_keys) < 3, "unexpected resource directory depth"
                walk(target & 0x7fffffff, next_keys)
            else:
                assert len(next_keys) == 3, "resource leaf lacks type/id/language"
                assert target + 16 <= resource_size, "resource descriptor outside section"
                rva, length = unpack(data, base + target, "<II")
                offset = file_offset(rva, length)
                found[next_keys] = data[offset:offset + length]
    walk(0, ())
    return found


def check_executable(path, expected=None):
    resources = pe_resources(path)
    groups = [(key, payload) for key, payload in resources.items()
              if key[:2] == (14, ICON_ID)]
    assert groups, "missing RT_GROUP_ICON 101 (Windows shows the generic executable icon)"
    for (_, _, language), group in groups:
        reserved, kind, count = unpack(group, 0, "<HHH")
        assert (reserved, kind, count) == (0, 1, len(SIZES)), "icon group must have eight frames"
        sizes = []
        for i in range(count):
            w, h, colours, zero, planes, bits, length, icon_id = unpack(
                group, 6 + i * 14, "<BBBBHHIH")
            w, h = w or 256, h or 256
            assert w == h and colours == zero == 0 and planes == 1 and bits == 32
            payload = resources.get((3, icon_id, language))
            assert payload is not None, f"group references missing RT_ICON {icon_id}"
            assert len(payload) == length, "group has wrong icon payload length"
            assert image_size(payload) == (w, h), "resource directory/image size mismatch"
            if expected is not None:
                assert payload == expected[w], f"embedded {w}px icon differs from source ICO"
            sizes.append(w)
        assert tuple(sorted(sizes)) == SIZES, f"PE icon sizes differ: {sizes}"
    return len(groups)


def check_shell_extraction(path):
    from ctypes import wintypes
    shell, user = ctypes.WinDLL("shell32"), ctypes.WinDLL("user32")
    extract = shell.ExtractIconExW
    extract.argtypes = [wintypes.LPCWSTR, ctypes.c_int,
                        ctypes.POINTER(wintypes.HICON), ctypes.POINTER(wintypes.HICON),
                        wintypes.UINT]
    extract.restype = wintypes.UINT
    user.DestroyIcon.argtypes = [wintypes.HICON]
    big, small = wintypes.HICON(), wintypes.HICON()
    try:
        # This returns extracted HICONs: one large plus one small is two.
        # The separate (-1, NULL, NULL) query returns the resource-group count.
        assert extract(str(Path(path).resolve()), 0, ctypes.byref(big),
                       ctypes.byref(small), 1) == 2, "Explorer did not extract both icon handles"
        assert big.value and small.value, "Explorer could not extract both icon sizes"
    finally:
        if big.value:
            user.DestroyIcon(big)
        if small.value:
            user.DestroyIcon(small)


class NativeIcon(unittest.TestCase):
    def test_source_icon_sizes(self):
        ico_images(ROOT / "data/ui/app_icon.ico")

    def test_executable_resources_and_extraction(self):
        if not os.environ.get("SIMGOLF_ICON_EXE"):
            self.skipTest("set SIMGOLF_ICON_EXE to verify a built executable")
        expected = None if os.environ.get("SIMGOLF_ICON_RESOURCE_ONLY") else \
            ico_images(ROOT / "data/ui/app_icon.ico")
        check_executable(os.environ["SIMGOLF_ICON_EXE"], expected)
        if os.name == "nt":
            check_shell_extraction(os.environ["SIMGOLF_ICON_EXE"])

    def test_hidden_sdl_window_icons(self):
        if os.name != "nt" or not os.environ.get("SIMGOLF_ICON_RUNTIME"):
            self.skipTest("set SIMGOLF_ICON_RUNTIME=1 for hidden real SDL window checks")
        cc = os.environ.get("CC") or shutil.which("clang")
        rc = os.environ.get("RC") or shutil.which("rc")
        sdl = Path(os.environ.get("SDL2_ROOT", os.path.join(
            os.environ.get("TOOLS_EXT", "C:/simgolf-tools"), "SDL2-2.32.10")))
        self.assertTrue(cc, "CC must identify clang for the requested native test")
        self.assertTrue(rc, "RC must identify the Windows resource compiler")
        self.assertTrue((sdl / "include/SDL.h").is_file(), "SDL2_ROOT lacks SDL.h")
        # Keep compiler outputs and automatic cleanup inside this checkout.
        # Windows managed sessions may expose a non-writable process TEMP.
        test_root = (ROOT / "build/tests").resolve()
        self.assertTrue(test_root.is_relative_to(ROOT.resolve()), "test output escapes checkout")
        test_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="native-icon-", dir=test_root) as temp:
            out = Path(temp)
            resource, exe = out / "app_icon.res", out / "native_icon_test.exe"
            commands = [
                [rc, "/nologo", "/fo", str(resource), "/I", str(ROOT / "port/src/port"),
                 "/I", str(ROOT / "data/ui"), str(ROOT / "port/src/port/app_icon.rc")],
                [cc, "-target", "i686-pc-windows-msvc", "-std=c11", "-O1",
                 "-I", str(ROOT / "port/src"), "-I", str(sdl / "include"),
                 str(Path(__file__).with_name("native_icon_test.c")), str(resource),
                 str(sdl / "lib/x86/SDL2.lib"), "-luser32", "-lgdi32", "-o", str(exe)]
            ]
            for command in commands:
                result = subprocess.run(command, capture_output=True, text=True, timeout=90,
                                        cwd=ROOT / "data/ui")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            shutil.copyfile(sdl / "lib/x86/SDL2.dll", out / "SDL2.dll")
            check_executable(exe, ico_images(ROOT / "data/ui/app_icon.ico"))
            env = dict(os.environ, SIMGOLF_MUTE="1", SDL_AUDIODRIVER="dummy")
            env.pop("SDL_WINDOWS_INTRESOURCE_ICON", None)
            env.pop("SDL_WINDOWS_INTRESOURCE_ICON_SMALL", None)
            # Production uses SDL's resource enumeration with no icon hints.
            result = subprocess.run([str(exe)], capture_output=True, text=True,
                                    timeout=20, env=env, cwd=out)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--exe")
    parser.add_argument("--resource-only", action="store_true")
    parser.add_argument("--runtime", action="store_true")
    options, remainder = parser.parse_known_args()
    if options.exe:
        os.environ["SIMGOLF_ICON_EXE"] = options.exe
    if options.resource_only:
        if not options.exe:
            parser.error("--resource-only requires --exe")
        os.environ["SIMGOLF_ICON_RESOURCE_ONLY"] = "1"
        suite = unittest.TestSuite([NativeIcon("test_executable_resources_and_extraction")])
    else:
        if options.runtime:
            os.environ["SIMGOLF_ICON_RUNTIME"] = "1"
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(NativeIcon)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(not result.wasSuccessful())
