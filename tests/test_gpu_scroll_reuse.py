"""Native synthetic tests of the actual renderer, not the host32 game ABI.

HGSS spells u32/s32 as long (32 bits on DS/3DS, 64 on macOS). The isolated
test include directory changes ONLY those typedef spellings to int, retaining
the SDK's remaining header and u64 alignment. No production headers are edited.
"""
import os
import shlex
from pathlib import Path
import subprocess
import unittest

REPO = Path(os.environ.get("HGSS_RENDER_TEST_ROOT", Path(__file__).resolve().parents[1]))
SUPPORT = Path(__file__).resolve().parent
UPSTREAM = Path(os.environ.get("HGSS_RENDER_TEST_UPSTREAM",
                              REPO / "build" / "upstream"))


class GpuScrollReuseTests(unittest.TestCase):
    def test_actual_renderer(self):
        out = REPO / "build" / "r1-native"
        inc = out / "include" / "nitro"
        inc.mkdir(parents=True, exist_ok=True)
        types = (UPSTREAM / "lib/include/nitro/types.h").read_text()
        self.assertIn("typedef unsigned long u32;", types)
        self.assertIn("typedef signed long s32;", types)
        (inc / "types.h").write_text(types.replace(
            "typedef unsigned long u32;", "typedef unsigned int u32;").replace(
            "typedef signed long s32;", "typedef signed int s32;"))
        # Filtered SDK shim: never let lib/include's MSL libc shadow the host.
        for name in ("nitro.h", "nnsys", "nnsys.h", "cw", "cw.h"):
            link = inc.parent / name
            if not link.exists():
                link.symlink_to(UPSTREAM / "lib/include" / name)
        for header in (UPSTREAM / "lib/include/nitro").iterdir():
            link = inc / header.name
            if header.name != "types.h" and not link.exists():
                link.symlink_to(header)
        flags = ["-std=gnu99", "-O1", "-fno-strict-aliasing", "-fno-short-enums",
                 "-fsigned-char", "-fwrapv", "-DSDK_ARM9", "-DSDK_TS", "-DSDK_4M",
                 "-DSDK_CODE_ARM", "-DSDK_FINALROM", "-DNNS_FINALROM", "-DSDK_PORT_3DS",
                 "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
                 "-Wno-unknown-pragmas", "-Wno-deprecated-non-prototype",
                 "-I" + str(inc.parent), "-I" + str(REPO / "3ds_port/nitro/include"),
                 "-I" + str(REPO / "hgss/compat/include"),
                 "-I" + str(UPSTREAM / "include"),
                 "-I" + str(UPSTREAM / "include/library")]
        flags += ["-I" + str(UPSTREAM / "lib/NitroSDK" / d)
                  for d in ("include", "include-mw", "include-pokediamond")]
        flags += ["-include", str(REPO / "hgss/compat/include/hgss_port_math.h"),
                  "-include", str(UPSTREAM / "lib/include/assert.h"),
                  '-DRENDER_SOURCE="' + str(REPO / "3ds_port/nitro/src/port_render2d.c") + '"']
        flags += shlex.split(os.environ.get("HGSS_RENDER_TEST_CFLAGS", ""))
        exe = out / "gpu-scroll-test"
        subprocess.run([os.environ.get("CC", "cc"), *flags,
                        str(SUPPORT / "fixtures/gpu_scroll_reuse.c"), "-lm", "-o", str(exe)],
                       check=True)
        args = [str(exe)]
        if os.environ.get("HGSS_RENDER_CAPTURE_DIR"):
            args.append(os.environ["HGSS_RENDER_CAPTURE_DIR"])
        subprocess.run(args, check=True)


if __name__ == "__main__":
    unittest.main()
