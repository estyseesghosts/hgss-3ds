"""Synthetic tests for builder/verify_rom.py. Never uses a real ROM."""
import contextlib
import hashlib
import io
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from builder import verify_rom  # noqa: E402
from builder.verify_rom import RomError, SUPPORTED, main, sha1_of, verify  # noqa: E402
from tests.synthetic_rom import build_nds, default_tree, synthetic_nds  # noqa: E402

HG_SHA1 = "4fcded0e2713dc03929845de631d0932ea2b5a37"
SS_SHA1 = "f8dc38ea20c17541a43b58c5e6d18c1732c7e582"
UNKNOWN_SHA1 = "0" * 40


class ProductionConstantsTests(unittest.TestCase):
    """The real constants, before the synthetic-size patch in setUp."""

    def test_retail_size(self):
        self.assertEqual(verify_rom.EXPECTED_DEVICE_SIZE, 0x0A)
        self.assertEqual(verify_rom.ROM_SIZE, 128 << 20)


class VerifyRomTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)
        # The synthetic images are 128 KiB (device size 0). Patch the two
        # size constants so the header/size logic can be exercised without
        # writing a 128 MiB file; the real values are pinned above.
        size_patch = patch.multiple(verify_rom, ROM_SIZE=0x20000,
                                    EXPECTED_DEVICE_SIZE=0)
        size_patch.start()
        self.addCleanup(size_patch.stop)

    def write(self, data: bytes, name: str = "rom.nds") -> Path:
        path = self.dir / name
        path.write_bytes(data)
        return path

    def test_supported_table_is_exactly_the_two_us_dumps(self):
        self.assertEqual(set(SUPPORTED), {HG_SHA1, SS_SHA1})
        self.assertIn("HeartGold", SUPPORTED[HG_SHA1])
        self.assertIn("SoulSilver", SUPPORTED[SS_SHA1])

    def test_sha1_of_reads_in_chunks(self):
        path = self.write(b"abc")
        self.assertEqual(sha1_of(path), hashlib.sha1(b"abc").hexdigest())

    def test_accepts_both_supported_digests(self):
        for digest, game_code, name in (
                (HG_SHA1, b"IPKE", "HeartGold"),
                (SS_SHA1, b"IPGE", "SoulSilver")):
            with self.subTest(digest=digest):
                image, _ = build_nds(default_tree(), overlay_count=2,
                                     game_code=game_code)
                rom = self.write(image)
                with patch.object(verify_rom, "sha1_of", return_value=digest):
                    self.assertIn(name, verify(rom))

    def test_digest_argument_skips_hashing(self):
        image, _ = synthetic_nds()
        rom = self.write(image)
        with patch.object(verify_rom, "sha1_of") as hasher:
            self.assertIn("HeartGold", verify(rom, digest=HG_SHA1))
            hasher.assert_not_called()

    def test_rejects_unknown_hash(self):
        image, _ = synthetic_nds()
        rom = self.write(image)
        with patch.object(verify_rom, "sha1_of", return_value=UNKNOWN_SHA1):
            with self.assertRaisesRegex(RomError, "not a clean HG/SS US dump"):
                verify(rom)

    def test_rejects_trimmed_dump(self):
        image, _ = synthetic_nds()
        rom = self.write(image[:0x10000])   # header intact, data cut
        with self.assertRaisesRegex(RomError, "shorter than .*trimmed"):
            verify(rom)

    def test_rejects_wrong_game_code(self):
        image, _ = build_nds(default_tree(), overlay_count=2, game_code=b"ZZZZ")
        with self.assertRaisesRegex(RomError, "game code"):
            verify(self.write(image))

    def test_rejects_dsi_unit_code(self):
        image, _ = build_nds(default_tree(), overlay_count=2, unit_code=2)
        with self.assertRaisesRegex(RomError, "unit code"):
            verify(self.write(image))

    def test_rejects_smaller_declared_device(self):
        image, _ = build_nds(default_tree(), overlay_count=2, device_size=9)
        with self.assertRaisesRegex(RomError, "trimmed, overdumped or modified"):
            verify(self.write(image))

    def test_rejects_out_of_bounds_fnt_and_fat(self):
        image, _ = synthetic_nds()
        for name, field in (("FNT", 0x40), ("FAT", 0x48)):
            with self.subTest(table=name):
                broken = bytearray(image)
                struct.pack_into("<I", broken, field, len(image) + 0x100)
                with self.assertRaisesRegex(RomError, f"{name} table"):
                    verify(self.write(bytes(broken)))

    def test_rejects_header_that_names_the_other_version(self):
        # A supported digest whose header says SoulSilver: the header was
        # edited. (Two supported dumps never share a digest.)
        image, _ = build_nds(default_tree(), overlay_count=2, game_code=b"IPGE")
        rom = self.write(image)
        with patch.object(verify_rom, "sha1_of", return_value=HG_SHA1):
            with self.assertRaisesRegex(RomError, "header was modified"):
                verify(rom)

    def test_too_short_file(self):
        with self.assertRaisesRegex(RomError, "too short"):
            verify(self.write(b"\0" * 16))

    def test_cli_exit_codes(self):
        image, _ = synthetic_nds()
        rom = self.write(image)
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(main([str(rom)]), 1)
        self.assertIn("Unsupported ROM", out.getvalue())
        self.assertIn(HG_SHA1, out.getvalue())

        with patch.object(verify_rom, "sha1_of", return_value=HG_SHA1):
            with contextlib.redirect_stdout(io.StringIO()) as out:
                self.assertEqual(main([str(rom)]), 0)
        self.assertEqual(out.getvalue().strip(), "OK: Pokémon HeartGold (USA)")

        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(main([str(self.dir / "absent.nds")]), 2)
            self.assertEqual(main([]), 2)
            self.assertEqual(main(["a", "b"]), 2)


if __name__ == "__main__":
    unittest.main()
