"""Synthetic NitroFS tests for the extractor. Never uses a real ROM."""
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

from builder import extract_rom, verify_rom  # noqa: E402
from builder.extract_rom import (NitroFSError, extract, main,  # noqa: E402
                                parse_nitrofs, parse_overlay_table, summarize)
from tests.synthetic_rom import build_nds, default_tree, synthetic_nds  # noqa: E402

HG_SHA1 = "4fcded0e2713dc03929845de631d0932ea2b5a37"


class NitroFSTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)
        self.image, self.meta = synthetic_nds()

    def write(self, data: bytes | None = None, name: str = "rom.nds") -> Path:
        path = self.dir / name
        path.write_bytes(self.image if data is None else data)
        return path

    # ------------------------------------------------------------- parsing
    def test_two_level_tree_ids_paths_and_sizes(self):
        entries = parse_nitrofs(self.image)
        self.assertEqual(
            [(e["id"], e["path"], e["size"], e["overlay"]) for e in entries],
            [(0, "overlay/overlay_0000.bin", 4, True),
             (1, "overlay/overlay_0001.bin", 4, True),
             (2, "a.bin", len(b"data:/a.bin"), False),
             (3, "d/sub/b.bin", len(b"data:d/sub/b.bin"), False)])
        counts = summarize(entries)
        self.assertEqual(counts["files"], 4)
        self.assertEqual(counts["overlays"], 2)
        self.assertEqual(counts["unnamed"], 0)
        self.assertEqual(counts["bytes"], sum(e["size"] for e in entries))

    def test_overlay_table_is_read_from_the_header(self):
        overlays = parse_overlay_table(self.image)
        self.assertEqual([o["id"] for o in overlays], [0, 1])
        self.assertEqual([o["file_id"] for o in overlays], [0, 1])

    def test_no_overlay_table(self):
        tree = {"": {"files": [("a.bin", 0)], "dirs": []}}
        image, _ = build_nds(tree, overlay_count=0)
        self.assertEqual(parse_overlay_table(image), [])
        self.assertEqual([e["id"] for e in parse_nitrofs(image)], [0])

    def test_named_file_that_is_also_an_overlay_keeps_its_fnt_path(self):
        broken = bytearray(self.image)
        # Overlay 0 points at named file ID 2 instead of unnamed ID 0.
        struct.pack_into("<I", broken, self.meta["overlay_offset"] + 24, 2)
        start, end = self.meta["extents"][2]
        struct.pack_into("<I", broken, self.meta["overlay_offset"] + 8, end - start)
        entries = {e["id"]: e for e in parse_nitrofs(bytes(broken))}
        self.assertEqual(entries[2]["path"], "a.bin")
        self.assertTrue(entries[2]["overlay"])
        self.assertEqual(entries[0]["path"], "unnamed/file_0000.bin")
        self.assertFalse(entries[0]["overlay"])

    def test_unsafe_names(self):
        for bad in ("a/b", "a\\b", "..", ".", "a:b", "a\x01b"):
            with self.subTest(name=bad):
                tree = {"": {"files": [(bad, 0)], "dirs": []}}
                image, _ = build_nds(tree, overlay_count=0)
                with self.assertRaisesRegex(NitroFSError, "unsafe|control|name"):
                    parse_nitrofs(image)

    def test_cyclic_directory(self):
        broken = bytearray(self.image)
        offset = self.meta["dir_refs"][("d", "sub")]
        struct.pack_into("<H", broken, offset, 0xF000)   # "sub" -> root
        with self.assertRaisesRegex(NitroFSError, "cyclic"):
            parse_nitrofs(bytes(broken))

    def test_duplicate_file_id(self):
        # "d" starts its file list at ID 2, where the root's a.bin already is.
        broken = bytearray(self.image)
        struct.pack_into("<H", broken,
                         self.meta["dir_entry_offsets"]["d/sub"] + 4, 2)
        with self.assertRaisesRegex(NitroFSError, "duplicate file ID"):
            parse_nitrofs(bytes(broken))

    def test_invalid_fnt_directory_reference(self):
        broken = bytearray(self.image)
        struct.pack_into("<H", broken, self.meta["dir_refs"][("d", "sub")], 0xFFFF)
        with self.assertRaisesRegex(NitroFSError, "outside its"):
            parse_nitrofs(bytes(broken))

    def test_bad_fat_bounds(self):
        broken = bytearray(self.image)
        struct.pack_into("<I", broken, 0x48, 0x100)     # inside the header
        with self.assertRaisesRegex(NitroFSError, "inside the cartridge header"):
            parse_nitrofs(bytes(broken))

        broken = bytearray(self.image)
        struct.pack_into("<I", broken, 0x4C, 7)         # not a multiple of 8
        with self.assertRaisesRegex(NitroFSError, "multiple of 8"):
            parse_nitrofs(bytes(broken))

    def test_fat_entry_before_header(self):
        broken = bytearray(self.image)
        struct.pack_into("<II", broken, self.meta["fat_offset"], 0x100, 0x104)
        with self.assertRaisesRegex(NitroFSError, "inside the cartridge header"):
            parse_nitrofs(bytes(broken))

    def test_fat_entry_past_eof(self):
        broken = bytearray(self.image)
        start = self.meta["extents"][0][0]
        struct.pack_into("<II", broken, self.meta["fat_offset"],
                         start, len(broken) + 0x100)
        with self.assertRaisesRegex(NitroFSError, "past the end"):
            parse_nitrofs(bytes(broken))

    def test_bad_overlay_table_bounds(self):
        broken = bytearray(self.image)
        struct.pack_into("<I", broken, 0x54, 33)        # not a multiple of 32
        with self.assertRaisesRegex(NitroFSError, "multiple of 32"):
            parse_nitrofs(bytes(broken))

        broken = bytearray(self.image)
        struct.pack_into("<I", broken, 0x50, len(broken) + 0x100)
        with self.assertRaisesRegex(NitroFSError, "outside the image"):
            parse_nitrofs(bytes(broken))

    def test_manifest_path_collision(self):
        tree = {"": {"files": [("manifest.json", 0)], "dirs": []}}
        image, _ = build_nds(tree, overlay_count=0)
        with self.assertRaisesRegex(NitroFSError, "collide with the manifest"):
            parse_nitrofs(image)

    def test_manifest_in_a_subdirectory_is_allowed(self):
        tree = {"": {"files": [("a.bin", 0)], "dirs": ["sub"]},
                "sub": {"files": [("manifest.json", 1)], "dirs": []}}
        image, _ = build_nds(tree, overlay_count=0)
        paths = [e["path"] for e in parse_nitrofs(image)]
        self.assertIn("sub/manifest.json", paths)

    def test_unnamed_entry_that_would_collide_with_an_fnt_path(self):
        tree = {"": {"files": [("a.bin", 2)], "dirs": ["unnamed"]},
                "unnamed": {"files": [("file_0001.bin", 5)], "dirs": []}}
        image, _ = build_nds(tree, overlay_count=0)
        with self.assertRaisesRegex(NitroFSError, "cannot name unnamed FAT entry"):
            parse_nitrofs(image)

    # ----------------------------------------------------------- extracting
    def test_extract_writes_files_and_manifest(self):
        rom = self.write()
        output = self.dir / "data_pack"
        entries = extract(rom, output, verify=False)
        self.assertEqual((output / "a.bin").read_bytes(), b"data:/a.bin")
        self.assertEqual((output / "d/sub/b.bin").read_bytes(),
                         b"data:d/sub/b.bin")
        self.assertEqual((output / "overlay/overlay_0000.bin").read_bytes(), b"ovl0")
        manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["rom_sha1"], hashlib.sha1(self.image).hexdigest())
        self.assertEqual(manifest["files"], [
            {"id": e["id"], "path": e["path"], "size": e["size"]}
            for e in entries])

    def test_extract_requires_verification(self):
        rom = self.write()
        with self.assertRaises(verify_rom.RomError):
            extract(rom, self.dir / "out")

    def test_extract_accepts_a_supported_digest(self):
        rom = self.write()
        with patch.object(extract_rom.verify_rom, "verify",
                          return_value="Pokémon HeartGold (USA)"):
            entries = extract(rom, self.dir / "out")
        self.assertEqual(len(entries), 4)

    def test_output_cannot_overwrite_the_rom(self):
        rom = self.write()
        with self.assertRaisesRegex(NitroFSError, "overwrite the ROM"):
            extract(rom, rom, verify=False)
        with self.assertRaisesRegex(NitroFSError, "overwrite the ROM"):
            extract(rom, self.dir, verify=False)

    def test_cli(self):
        rom = self.write()
        with patch.object(extract_rom.verify_rom, "verify",
                          return_value="Pokémon HeartGold (USA)"):
            with contextlib.redirect_stdout(io.StringIO()) as out:
                self.assertEqual(main([str(rom), "-o", str(self.dir / "out")]), 0)
        text = out.getvalue()
        self.assertIn("Extracted 4 files (2 overlays, 0 unnamed)", text)
        self.assertNotIn("a.bin", text)     # counts, never names

        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as result:
                main([str(rom), "-o", str(self.dir / "out2")])
        self.assertEqual(result.exception.code, 1)

    def test_rom_size_limit_reports_truncation(self):
        cut = self.meta["extents"][3][0] + 1   # file ID 3 loses all but a byte
        rom = self.write(self.image[:cut])
        with self.assertRaisesRegex(NitroFSError, "past the end"):
            parse_nitrofs(rom.read_bytes())


if __name__ == "__main__":
    unittest.main()
