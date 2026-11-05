"""Native fixed-width harness for the actual geometry translator's clip math."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import unittest

REPO = Path(__file__).resolve().parents[1]


class ClipMatrixTests(unittest.TestCase):
    def test_facing_is_computed_only_by_stage(self):
        source = (REPO / "3ds_port/nitro/src/port_gx3d.c").read_text()
        self.assertEqual(source.count("sPolygonFront=polygon_front("), 1)
        stage = source[source.index("static void stage_triangle("):source.index("static void begin_polygon(")]
        self.assertIn("sPolygonFront=polygon_front(a,b,c);", stage)

    def test_actual_geometry(self):
        # Use the same isolated SDK typedef adaptation as the renderer fixture;
        # no generated SDK shim or production header is changed.
        out = REPO / "build" / "clip-matrix-native"
        inc = out / "include" / "nitro"
        inc.mkdir(parents=True, exist_ok=True)
        up = REPO / "build" / "upstream"
        types = (up / "lib/include/nitro/types.h").read_text()
        (inc / "types.h").write_text(types.replace(
            "typedef unsigned long u32;", "typedef unsigned int u32;").replace(
            "typedef signed long s32;", "typedef signed int s32;"))
        for name in ("nitro.h", "nnsys", "nnsys.h", "cw", "cw.h"):
            link = inc.parent / name
            if not link.exists(): link.symlink_to(up / "lib/include" / name)
        for h in (up / "lib/include/nitro").iterdir():
            link = inc / h.name
            if h.name != "types.h" and not link.exists(): link.symlink_to(h)
        flags = ["-std=gnu99", "-O2", "-fno-strict-aliasing", "-fno-short-enums",
                 "-fsigned-char", "-fwrapv", "-ffunction-sections", "-fdata-sections",
                 "-DSDK_ARM9", "-DSDK_TS", "-DSDK_4M", "-DSDK_CODE_ARM",
                 "-DSDK_FINALROM", "-DNNS_FINALROM", "-DSDK_PORT_3DS",
                 "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
                 "-Wno-unknown-pragmas", "-Wno-deprecated-non-prototype"]
        for d in (inc.parent, REPO / "3ds_port/nitro/include", REPO / "3ds_port/nitro/src", REPO / "hgss/compat/include",
                  up / "include", up / "include/library", up / "lib/NitroSDK/include",
                  up / "lib/NitroSDK/include-mw", up / "lib/NitroSDK/include-pokediamond"):
            flags += ["-I" + str(d)]
        flags += ["-include", str(REPO / "hgss/compat/include/hgss_port_math.h"),
                  "-include", str(up / "lib/include/assert.h"),
                  '-DGX3D_SOURCE="' + os.environ.get("HGSS_GX3D_SOURCE", str(REPO / "3ds_port/nitro/src/port_gx3d.c")) + '"',
                  "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"]
        flags += shlex.split(os.environ.get("HGSS_GX3D_TEST_CFLAGS", ""))
        exe = out / "clip-matrix-test"
        subprocess.run([os.environ.get("CC", "cc"), *flags,
                        str(REPO / "tests/fixtures/gx3d_clip_matrix.c"), "-lm", "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__": unittest.main()
