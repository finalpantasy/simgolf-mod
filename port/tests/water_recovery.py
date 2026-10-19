#!/usr/bin/env python3
"""Compile the real water upload queue against fault-injected C dependencies.

Run from any directory with Python 3 and a C11 compiler (CC defaults to cc).
No Windows SDK, SDL, original game files, or downloaded dependencies are used.
"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def main():
    tests = Path(__file__).resolve().parent
    source = (tests.parent / "src/port/water_native.c").read_text()
    types = source[source.index("typedef struct { uint32_t serial; ID3D11Texture2D"):
                   source.index("typedef struct { int first, count; uint32_t serial;")]
    fields = source[source.index("  WTex *tex;"):source.index("  float *vtx;")]
    state = re.search(r"  int state;[^\n]*", source).group()
    globals = "\n".join(re.search(pattern, source).group() for pattern in [
        r"static volatile LONG g_mode[^;]*;", r"static volatile LONG g_works[^;]*;",
        r"static char g_clip_path[^;]*;", r"static volatile LONG g_clip_req[^;]*;",
        r"static unsigned g_seen_serial[^;]*;",
    ])
    poll = source[source.index("void water_poll(void)"):source.index("int water_on(void)")]
    marker = "// Pending water texture uploads"
    start = source.index(marker) if marker in source else re.search(
        r"(?:void|int) water_take_texture\(", source).start()
    code = source[start:source.index("// ------------------------------------------------------------------ the map:", start)]
    reset = source[source.index("void water_device_reset(void)"):]
    # The reset body is compiled unchanged. Resource hooks exercise every
    # released member without requiring SDL, a driver, or a physical GPU loss.
    reset_fields = """
      ID3D11Texture2D *vs_water,*vs_quad,*vs_imp,*ps_water,*ps_sim,*ps_imp,*ps_fall;
      ID3D11Texture2D *il,*cb,*vb,*s_wrap,*s_clamp,*s_point,*b_water,*b_add,*rs,*noise_srv,*info_srv,*info;
      ID3D11Texture2D *sim_srv[2],*sim_rtv[2],*sim[2],*qd[4],*q0[4],*q1[4],*st,*ctx1,*ctx,*dev;
      int vb_cap,scene_serial,map_serial,sim_w,sim_h,qn,stat_n;
      double gpu_sum,gpu_max,cpu_sum,cpu_max;
    """
    with tempfile.TemporaryDirectory(prefix="simgolf-water-recovery-") as directory:
        directory = Path(directory)
        (directory / "water_upload.inc").write_text(
            globals + "\n" + poll + types + "\nstatic struct {\n" + state + "\n" + fields + reset_fields + "} W;\n" + code + reset)
        executable = directory / "water_recovery"
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
            *shlex.split(os.environ.get("CFLAGS", "")),
            "-I", str(directory), str(tests / "water_recovery_test.c"), "-o", str(executable),
        ]
        subprocess.run(command, check=True)
        cases = subprocess.check_output([str(executable), "--list"], text=True).splitlines()
        failures = 0
        for case in cases:
            result = subprocess.run([str(executable), case], check=False)
            if result.returncode < 0:
                print(f"FAIL {case}: terminated by signal {-result.returncode}", flush=True)
            failures += result.returncode != 0
        print(f"water recovery: {len(cases) - failures}/{len(cases)} cases passed", flush=True)
        return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
