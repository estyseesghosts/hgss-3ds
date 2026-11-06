"""Check the experimental texture-cache lookup against the original scan."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TextureCacheHintTests(unittest.TestCase):
    def test_palette_hash_experiment_matches_bytewise_reference(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "palette-hash"
            cmd = [os.environ.get("CC", "cc"), "-std=c99", "-O2",
                   "-I" + str(ROOT / "3ds_port/nitro/include"),
                   str(ROOT / "tests/fixtures/texture_hash_palette.c"), "-o", str(exe)]
            cmd += shlex.split(os.environ.get("HGSS_TEXTURE_TEST_CFLAGS", ""))
            subprocess.run(cmd, check=True)
            subprocess.run([str(exe)], check=True)

    def test_cache_hint_fallback_and_slot_reuse(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "texture-hint"
            cmd = [os.environ.get("CC", "cc"), "-std=c99", "-O2",
                   str(ROOT / "tests/fixtures/texture_cache_hint.c"), "-o", str(exe)]
            cmd += shlex.split(os.environ.get("HGSS_TEXTURE_TEST_CFLAGS", ""))
            subprocess.run(cmd, check=True)
            subprocess.run([str(exe)], check=True)
