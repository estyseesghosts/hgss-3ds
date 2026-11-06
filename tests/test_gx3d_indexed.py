import os
import shlex
import subprocess
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


class IndexedGeometryTests(unittest.TestCase):
    def test_per_batch_reuse(self):
        out = REPO / "build" / "gx3d-indexed-native"
        flags = ["-std=gnu99", "-O2", "-ffunction-sections", "-fdata-sections",
                 "-I" + str(REPO / "3ds_port/nitro/include"),
                 "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"]
        flags += shlex.split(os.environ.get("HGSS_GX3D_TEST_CFLAGS", ""))
        subprocess.run([os.environ.get("CC", "cc"), *flags,
                        str(REPO / "tests/fixtures/gx3d_indexed.c"),
                        str(REPO / "3ds_port/nitro/src/port_gx3d_vertex.c"),
                        "-lm", "-o", str(out)], check=True)
        subprocess.run([str(out)], check=True)


if __name__ == "__main__":
    unittest.main()
