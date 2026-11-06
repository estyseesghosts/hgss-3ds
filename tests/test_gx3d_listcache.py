"""Half-A lane G wave 2: G3 list-cache bounds and G5 matrix/state dedupe.

Builds tests/fixtures/gx3d_listcache.c twice (with and without
HGSS_GX3D_LISTCACHE / HGSS_GX3D_STATEDEDUP) and requires bit-identical
scene checksums over 12,000 synthetic scenes including map transitions,
in-place rewrites and heap reuse. The flagged binary additionally asserts
its own cache behavior (hits, skips, census, auto-disable) on stderr.
"""
import os
import shlex
import subprocess
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SCENES = 12000


def build_fixture(out, extra_defs):
    inc = REPO / "build" / "clip-matrix-native" / "include"
    up = REPO / "build" / "upstream"
    flags = ["-std=gnu99", "-O2", "-fno-strict-aliasing", "-fno-short-enums",
             "-fsigned-char", "-fwrapv", "-ffunction-sections", "-fdata-sections",
             "-DSDK_ARM9", "-DSDK_TS", "-DSDK_4M", "-DSDK_CODE_ARM",
             "-DSDK_FINALROM", "-DNNS_FINALROM", "-DSDK_PORT_3DS",
             "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
             "-Wno-unknown-pragmas", "-Wno-deprecated-non-prototype"]
    flags += extra_defs
    flags += ["-I" + str(inc), "-I" + str(REPO / "3ds_port/nitro/include"),
              "-I" + str(REPO / "3ds_port/nitro/src"),
              "-I" + str(REPO / "hgss/compat/include"),
              "-I" + str(up / "include"), "-I" + str(up / "include/library"),
              "-I" + str(up / "lib/NitroSDK/include"),
              "-I" + str(up / "lib/NitroSDK/include-mw"),
              "-I" + str(up / "lib/NitroSDK/include-pokediamond")]
    flags += ["-include", str(REPO / "hgss/compat/include/hgss_port_math.h"),
              "-include", str(up / "lib/include/assert.h"),
              '-DGX3D_SOURCE="' + str(REPO / "3ds_port/nitro/src/port_gx3d.c") + '"',
              "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"]
    flags += shlex.split(os.environ.get("HGSS_GX3D_TEST_CFLAGS", ""))
    exe = out / "gx3d-listcache-test"
    subprocess.run([os.environ.get("CC", "cc"), *flags,
                    str(REPO / "tests/fixtures/gx3d_listcache.c"), "-lm",
                    "-o", str(exe)], check=True)
    return exe


class ListCacheTests(unittest.TestCase):
    def test_cached_matches_uncached(self):
        out = REPO / "build" / "gx3d-listcache-native"
        out.mkdir(parents=True, exist_ok=True)
        ref = build_fixture(out / "ref", [])
        exp = build_fixture(out / "exp",
                            ["-DHGSS_GX3D_LISTCACHE=1", "-DHGSS_GX3D_STATEDEDUP=1"])
        ref_run = subprocess.run([str(ref)], check=True, capture_output=True,
                                 text=True, timeout=1200)
        exp_run = subprocess.run([str(exp)], check=False, capture_output=True,
                                 text=True, timeout=1200)
        self.assertEqual(exp_run.returncode, 0,
                         "flagged binary failed:\n" + exp_run.stderr[-4000:])
        ref_lines = ref_run.stdout.splitlines()
        exp_lines = exp_run.stdout.splitlines()
        self.assertEqual(len(ref_lines), len(exp_lines),
                         "scene count differs: %d vs %d" % (len(ref_lines), len(exp_lines)))
        self.assertGreaterEqual(len(ref_lines), SCENES)
        bad = [(a, b) for a, b in zip(ref_lines, exp_lines) if a != b]
        self.assertEqual(bad, [], "first mismatches: %r" % (bad[:5],))
        err = exp_run.stderr
        self.assertIn("listcache:", err)
        self.assertIn("matrixdedup:", err)
        self.assertNotIn("FAIL", err)
        self.assertNotIn("failures", err)


if __name__ == "__main__":
    unittest.main()
