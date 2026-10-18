#!/usr/bin/env python3
"""Compile actual ui_text.c with real bundled fonts and fault-injected SDL/malloc.

Requires a C11 compiler, not SDL or game assets. Set CFLAGS for sanitizers.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    tests = Path(__file__).resolve().parent
    port = tests.parent
    with tempfile.TemporaryDirectory(prefix="simgolf-ui-text-recovery-") as directory:
        executable = Path(directory) / "ui_text_recovery"
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-g",
            *shlex.split(os.environ.get("CFLAGS", "")),
            "-I", str(tests / "ui_text_stubs"), "-I", str(port / "src"),
            "-I", str(port / "third_party/stb"), str(tests / "ui_text_recovery_test.c"),
            "-lm", "-o", str(executable),
        ]
        subprocess.run(command, check=True)
        cases = subprocess.check_output([str(executable), "--list"], text=True).splitlines()
        fonts = port.parent / "data/fonts/plus-jakarta-sans"
        failures = sum(subprocess.run([str(executable), case, str(fonts)], check=False).returncode != 0
                       for case in cases)
        print(f"UI text recovery: {len(cases) - failures}/{len(cases)} cases passed", flush=True)
        return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
