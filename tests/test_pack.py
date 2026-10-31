"""Synthetic pack-format tests. No ROMs or game assets are used."""
import contextlib
import hashlib
import io
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from builder.extract_rom import extract  # noqa: E402
from builder.pack import (DEFAULT_BUILD_ID, ENTRY, HEADER,  # noqa: E402
                          HEARTGOLD_PAK_BUILD_ID, PLATINUM_PAK_BUILD_ID,
                          main, pack, read_pak, write_pak)
from tests.synthetic_rom import synthetic_nds  # noqa: E402

SHA1 = "0123456789abcdef0123456789abcdef01234567"
# A supported (clean HG US) digest, for the CLI tests: the CLI checks the
# ROM SHA-1 against builder/verify_rom.SUPPORTED. The data is synthetic.
HG_SHA1 = "4fcded0e2713dc03929845de631d0932ea2b5a37"


class PakTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "data_pack"
        self.root.mkdir()
        (self.root / "sub").mkdir()
        (self.root / "first.bin").write_bytes(b"abc")
        (self.root / "sub" / "second.bin").write_bytes(b"\x00\x01\xff\x02\x03")
        self.out = Path(self.temp.name) / "heartgold3ds.pak"
        self.files = [
            {"id": 99, "path": "sub/second.bin", "size": 5},
            {"id": 2, "path": "first.bin", "size": 3},
        ]
        self.write_manifest(self.files)

    def write_manifest(self, files, **extra):
        (self.root / "manifest.json").write_text(
            json.dumps({"files": files, **extra}), encoding="utf-8")

    def test_build_ids(self):
        # The builder's default is the ID the game program checks at boot
        # (HEARTGOLD_PAK_BUILD_ID); Platinum's stays as the adapted-from
        # alias the vendored fixtures name.
        self.assertEqual(HEARTGOLD_PAK_BUILD_ID, "heartgold-nitrofs-1")
        self.assertEqual(PLATINUM_PAK_BUILD_ID, "platinum-nitrofs-1")
        self.assertEqual(DEFAULT_BUILD_ID, HEARTGOLD_PAK_BUILD_ID)

    def test_round_trip_sorted_ids_and_padding(self):
        pack(self.root, self.out, build_id="release-2026.10", rom_sha1=SHA1)
        decoded = read_pak(self.out)
        self.assertEqual(decoded["version"], 1)
        self.assertEqual(decoded["build_id"], "release-2026.10")
        self.assertEqual(decoded["rom_sha1"], SHA1)
        self.assertEqual(decoded["files"], {2: b"abc", 99: b"\x00\x01\xff\x02\x03"})
        raw = self.out.read_bytes()
        self.assertEqual(struct.unpack_from("<I", raw, HEADER.size)[0], 2)
        self.assertEqual(struct.unpack_from("<I", raw, HEADER.size + ENTRY.size)[0], 99)
        self.assertEqual(len(raw) % 4, 0)
        self.assertEqual(struct.unpack_from("<I", raw, HEADER.size + 4)[0] % 4, 0)

    def test_write_pak_matches_pack(self):
        pack(self.root, self.out, build_id="v1", rom_sha1=SHA1)
        buffer = io.BytesIO()
        write_pak(buffer, [(99, b"\x00\x01\xff\x02\x03"), (2, b"abc")],
                  build_id="v1", rom_sha1=SHA1)
        self.assertEqual(buffer.getvalue(), self.out.read_bytes())

    def test_manifest_rom_sha1(self):
        self.write_manifest(self.files, rom_sha1=SHA1)
        pack(self.root, self.out, build_id="v1")
        self.assertEqual(read_pak(self.out)["rom_sha1"], SHA1)
        with self.assertRaisesRegex(ValueError, "disagrees"):
            pack(self.root, self.out, build_id="v1", rom_sha1="f" * 40)

    def test_cli_uses_manifest_sha1(self):
        self.write_manifest(self.files, rom_sha1=HG_SHA1)
        with patch.object(sys, "argv", ["pack.py", str(self.root), "-o", str(self.out),
                                       "--build-id", "dev-001"]):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(main(), 0)
        decoded = read_pak(self.out)
        self.assertEqual(decoded["build_id"], "dev-001")
        self.assertEqual(decoded["rom_sha1"], HG_SHA1)

    def test_cli_default_build_id_is_the_games(self):
        self.write_manifest(self.files, rom_sha1=HG_SHA1)
        with patch.object(sys, "argv", ["pack.py", str(self.root), "-o", str(self.out)]):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(main(), 0)
        self.assertEqual(read_pak(self.out)["build_id"], HEARTGOLD_PAK_BUILD_ID)
        self.assertEqual(read_pak(self.out)["build_id"], "heartgold-nitrofs-1")

    def test_wrong_build_id_is_visible_in_the_pack(self):
        # The C reader (tests/sdk/pak_boot) refuses this pack for the game;
        # here check the builder wrote the ID it was told, not the default.
        pack(self.root, self.out, build_id="platinum-nitrofs-1", rom_sha1=HG_SHA1)
        self.assertEqual(read_pak(self.out)["build_id"], PLATINUM_PAK_BUILD_ID)
        self.assertNotEqual(read_pak(self.out)["build_id"], HEARTGOLD_PAK_BUILD_ID)

    def test_cli_rejects_an_unsupported_rom(self):
        self.write_manifest(self.files, rom_sha1=SHA1)
        with patch.object(sys, "argv", ["pack.py", str(self.root), "-o", str(self.out)]):
            with contextlib.redirect_stderr(io.StringIO()) as errors:
                with self.assertRaises(SystemExit) as result:
                    main()
        self.assertEqual(result.exception.code, 1)
        self.assertIn("Unsupported ROM", errors.getvalue())
        self.assertFalse(self.out.exists())

    def test_cli_rejects_an_unsupported_rom_argument(self):
        with patch.object(sys, "argv", ["pack.py", str(self.root), "-o", str(self.out),
                                       "--rom-sha1", "f" * 40]):
            with contextlib.redirect_stderr(io.StringIO()) as errors:
                with self.assertRaises(SystemExit) as result:
                    main()
        self.assertEqual(result.exception.code, 1)
        self.assertIn("Unsupported ROM", errors.getvalue())

    def test_cli_rom_sha1_argument(self):
        with patch.object(sys, "argv", ["pack.py", str(self.root), "-o", str(self.out),
                                       "--build-id", "dev-001",
                                       "--rom-sha1", HG_SHA1]):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(main(), 0)
        self.assertEqual(read_pak(self.out)["rom_sha1"], HG_SHA1)

    def test_synthetic_extract_then_pack(self):
        with tempfile.TemporaryDirectory() as tmp:
            rom_bytes, _ = synthetic_nds()
            rom = Path(tmp) / "synthetic.nds"
            rom.write_bytes(rom_bytes)
            extracted = Path(tmp) / "extracted"
            extract(rom, extracted, verify=False)
            packed = Path(tmp) / "synthetic.pak"
            # The synthetic digest isn't in verify_rom.SUPPORTED, so the CLI
            # would refuse it; pack() directly (the CLI is covered above).
            pack(extracted, packed, build_id=DEFAULT_BUILD_ID)
            decoded = read_pak(packed)
            self.assertEqual(decoded["build_id"], "heartgold-nitrofs-1")
            self.assertEqual(decoded["rom_sha1"],
                             hashlib.sha1(rom_bytes).hexdigest())
            self.assertEqual(decoded["files"], {
                0: b"ovl0", 1: b"ovl1", 2: b"data:/a.bin", 3: b"data:d/sub/b.bin"})

    def test_cli_rejects_conflicting_sha1(self):
        self.write_manifest(self.files, rom_sha1=SHA1)
        with patch.object(sys, "argv", ["pack.py", str(self.root), "-o", str(self.out),
                                       "--build-id", "dev-001",
                                       "--rom-sha1", "f" * 40]):
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as result:
                    main()
        self.assertEqual(result.exception.code, 1)
        self.assertFalse(self.out.exists())

    def test_supported_check_covers_manifest_and_argument(self):
        other = "f" * 40
        with self.assertRaisesRegex(ValueError, "Unsupported ROM"):
            pack(self.root, self.out, build_id="v1", rom_sha1=SHA1, supported={other})
        self.write_manifest(self.files, rom_sha1=SHA1)
        with self.assertRaisesRegex(ValueError, "Unsupported ROM"):
            pack(self.root, self.out, build_id="v1", supported={other})
        self.assertFalse(self.out.exists())
        pack(self.root, self.out, build_id="v1", supported={SHA1})
        self.assertEqual(read_pak(self.out)["rom_sha1"], SHA1)

    def test_missing_rom_sha1_is_not_guessed(self):
        with self.assertRaisesRegex(ValueError, "--rom"):
            pack(self.root, self.out, build_id="v1")

    def test_mismatched_manifest_size(self):
        self.files[0]["size"] = 6
        self.write_manifest(self.files)
        with self.assertRaisesRegex(ValueError, "Size mismatch"):
            pack(self.root, self.out, build_id="v1", rom_sha1=SHA1)
        self.assertFalse(self.out.exists())

    def test_truncated_pak(self):
        pack(self.root, self.out, build_id="v1", rom_sha1=SHA1)
        self.out.write_bytes(self.out.read_bytes()[:-2])
        with self.assertRaises(ValueError):
            read_pak(self.out)

    def test_truncated_index(self):
        pack(self.root, self.out, build_id="v1", rom_sha1=SHA1)
        self.out.write_bytes(self.out.read_bytes()[:HEADER.size + 1])
        with self.assertRaisesRegex(ValueError, "Truncated pak index"):
            read_pak(self.out)

    def test_out_of_range_offset(self):
        pack(self.root, self.out, build_id="v1", rom_sha1=SHA1)
        raw = bytearray(self.out.read_bytes())
        struct.pack_into("<I", raw, HEADER.size + 4, 0xfffffff0)
        self.out.write_bytes(raw)
        with self.assertRaises(ValueError):
            read_pak(self.out)

    def test_out_of_range_size(self):
        pack(self.root, self.out, build_id="v1", rom_sha1=SHA1)
        raw = bytearray(self.out.read_bytes())
        struct.pack_into("<I", raw, HEADER.size + 8, 0xffffffff)
        self.out.write_bytes(raw)
        with self.assertRaises(ValueError):
            read_pak(self.out)

    def test_duplicate_id_and_path(self):
        self.write_manifest(self.files + [{"id": 2, "path": "sub/second.bin", "size": 5}])
        with self.assertRaisesRegex(ValueError, "Duplicate NitroFS"):
            pack(self.root, self.out, build_id="v1", rom_sha1=SHA1)
        self.write_manifest(self.files + [{"id": 3, "path": "first.bin", "size": 3}])
        with self.assertRaisesRegex(ValueError, "Duplicate or invalid manifest path"):
            pack(self.root, self.out, build_id="v1", rom_sha1=SHA1)

    def test_path_traversal_and_absolute_paths(self):
        for bad in ("../outside.bin", "/tmp/other.bin", "sub/../../outside.bin",
                    "sub\\second.bin", "sub/./second.bin", "C:/windows/file"):
            with self.subTest(path=bad):
                self.write_manifest([{"id": 1, "path": bad, "size": 3}])
                with self.assertRaises(ValueError):
                    pack(self.root, self.out, build_id="v1", rom_sha1=SHA1)

    def test_escaping_symlink(self):
        outside = Path(self.temp.name) / "outside.bin"
        outside.write_bytes(b"abc")
        (self.root / "link.bin").symlink_to(outside)
        self.write_manifest([{"id": 1, "path": "link.bin", "size": 3}])
        with self.assertRaisesRegex(ValueError, "escapes"):
            pack(self.root, self.out, build_id="v1", rom_sha1=SHA1)

    def test_invalid_index_order(self):
        pack(self.root, self.out, build_id="v1", rom_sha1=SHA1)
        raw = bytearray(self.out.read_bytes())
        struct.pack_into("<I", raw, HEADER.size + ENTRY.size, 2)
        self.out.write_bytes(raw)
        with self.assertRaisesRegex(ValueError, "sorted"):
            read_pak(self.out)

    def test_bad_magic_and_version(self):
        pack(self.root, self.out, build_id="v1", rom_sha1=SHA1)
        original = self.out.read_bytes()
        for at, value in ((0, b"BADMAGIC"), (8, struct.pack("<I", 2))):
            with self.subTest(at=at):
                raw = bytearray(original)
                raw[at:at + len(value)] = value
                self.out.write_bytes(raw)
                with self.assertRaises(ValueError):
                    read_pak(self.out)

    def test_output_cannot_overwrite_input(self):
        with self.assertRaisesRegex(ValueError, "overwrite"):
            pack(self.root, self.root / "first.bin", build_id="v1", rom_sha1=SHA1)


if __name__ == "__main__":
    unittest.main()
