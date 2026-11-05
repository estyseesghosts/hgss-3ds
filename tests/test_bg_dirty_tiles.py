"""Compare the real full-tile BG updater with full texture conversion."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BgDirtyTilesTests(unittest.TestCase):
    def test_tile_delta_matches_full_conversion(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "bg-dirty-tiles"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O2",
                            "-I", str(ROOT / "3ds_port/nitro/include"),
                            str(ROOT / "tests/fixtures/bg_dirty_tiles.c"),
                            str(ROOT / "3ds_port/nitro/src/port_upscale.c"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
