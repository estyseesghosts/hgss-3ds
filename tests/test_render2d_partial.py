"""Half-A L4: engine-B band redraw is bit-exact vs a whole redraw.

Builds the actual renderer with the same isolated SDK typedef adaptation
as the other renderer fixtures (no production header is changed) and runs
synthetic field-menu/battle-menu/bag-like frames plus fades through both
the fast band path (with the diagnostic dual-path compare on) and whole
renders, asserting identical pixels every frame.
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


class PartialBTests(unittest.TestCase):
    def test_band_redraw_exact(self):
        out = REPO / "build" / "partialB-native"
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
                 "-fsigned-char", "-fwrapv", "-DSDK_ARM9", "-DSDK_TS", "-DSDK_4M", "-DSDK_CODE_ARM",
                 "-DSDK_FINALROM", "-DNNS_FINALROM", "-DSDK_PORT_3DS",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
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
        exe = out / "partialB-test"
        subprocess.run([os.environ.get("CC", "cc"), *flags,
                        str(SUPPORT / "fixtures/render2d_partialB.c"), "-lm", "-o", str(exe)],
                       check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
