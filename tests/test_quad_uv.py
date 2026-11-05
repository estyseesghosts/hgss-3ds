"""Exercise the compositor's front-BG PICA coordinate conversion."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class QuadUvTests(unittest.TestCase):
    def test_bg_and_obj_coordinates(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "quad-uv"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O2",
                            "-I", str(ROOT / "3ds_port/game/app"),
                            str(ROOT / "tests/fixtures/quad_uv.c"), "-lm", "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
