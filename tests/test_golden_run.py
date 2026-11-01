"""Tests for tools/golden/run.py. Synthetic PNGs and scenes only."""
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from tools.golden import run  # noqa: E402


def image(width: int, height: int, colour=(0, 0, 0)) -> list[bytearray]:
    rows = []
    for _ in range(height):
        row = bytearray()
        for _ in range(width):
            row += bytes(colour)
        rows.append(row)
    return rows


class PngTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_rgb_round_trip(self):
        rows = image(5, 3, (10, 20, 30))
        rows[1][3:6] = bytes((255, 0, 1))
        path = self.root / "a.png"
        run.write_png(path, 5, 3, rows)
        width, height, decoded = run.read_png(path)
        self.assertEqual((width, height), (5, 3))
        self.assertEqual(decoded, rows)

    def test_rgba_and_gray_decode(self):
        # RGBA: one pixel with alpha; the alpha is dropped.
        import struct, zlib
        def png(colour_type, channels):
            raw = bytearray()
            for _ in range(2):
                raw.append(0)
                raw += bytes([7] * (2 * channels))
            def chunk(kind, body):
                return (struct.pack(">I", len(body)) + kind + body
                        + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))
            header = struct.pack(">IIBBBBB", 2, 2, 8, colour_type, 0, 0, 0)
            return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                    + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))
        rgba = self.root / "rgba.png"
        rgba.write_bytes(png(6, 4))
        self.assertEqual(run.read_png(rgba)[2], image(2, 2, (7, 7, 7)))
        gray = self.root / "gray.png"
        gray.write_bytes(png(0, 1))
        self.assertEqual(run.read_png(gray)[2], image(2, 2, (7, 7, 7)))

    def test_rejects_interlaced(self):
        import struct, zlib
        def chunk(kind, body):
            return (struct.pack(">I", len(body)) + kind + body
                    + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))
        header = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 1)
        path = self.root / "i.png"
        path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                         + chunk(b"IDAT", zlib.compress(b"\0\0\0\0")) + chunk(b"IEND", b""))
        with self.assertRaisesRegex(ValueError, "unsupported"):
            run.read_png(path)


class SceneTests(unittest.TestCase):
    def test_parse_and_specs(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "scene.txt"
            path.write_text("# comment\nevery: 5\nend: 900\n0 -\n6550 START\n"
                            "8050 touch 128 176\n8100 A+B\n", encoding="utf-8")
            scene = run.parse_scene(path)
        self.assertEqual(scene.name, "scene")
        self.assertEqual(scene.every, 5)
        self.assertEqual(scene.end, 900)
        self.assertEqual(scene.press_spec(), "6550:START,8100:A+B")
        self.assertEqual(scene.touch_spec(), "8050:128:176")

    def test_requires_end(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "scene.txt"
            path.write_text("every: 5\n0 -\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "end"):
                run.parse_scene(path)


class DiffTests(unittest.TestCase):
    def test_exact_and_tolerance(self):
        a = image(3, 2, (0, 0, 0))
        b = image(3, 2, (0, 0, 0))
        self.assertEqual(run.pixel_diff(a, b, 0), (0, 0))
        b[0][0] = 9
        self.assertEqual(run.pixel_diff(a, b, 0), (1, 9))
        self.assertEqual(run.pixel_diff(a, b, 9), (0, 9))

    def test_tolerance_region(self):
        self.assertEqual(run.allowed_tolerance(100, 0, 6600, 8), 8)
        self.assertEqual(run.allowed_tolerance(7000, 0, 6600, 8), 0)

    def test_align_finds_offset(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            run_dir, ref_dir = root / "run", root / "ref"
            run_dir.mkdir(); ref_dir.mkdir()
            for frame in (10, 12, 14):
                run.write_png(run_dir / f"frame_{frame:07d}.png", 2, 2, image(2, 2, (frame, 0, 0)))
                run.write_png(ref_dir / f"{frame + 3}.png", 2, 2, image(2, 2, (frame, 0, 0)))
            _, _, run_frames, _ = run.load_frames(run_dir)
            _, _, ref_frames, _ = run.load_frames(ref_dir)
            offset, first = run.align(run_frames, ref_frames, scene_3d_until=0,
                                      tolerance=0, tolerance_3d=0, window=10)
        self.assertEqual(offset, 3)
        self.assertEqual(first, 10)


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.scene = self.root / "scene.txt"
        self.scene.write_text("every: 1\nend: 3\n0 -\n", encoding="utf-8")
        self.run_dir = self.root / "run"
        self.ref_dir = self.root / "ref"
        self.run_dir.mkdir(); self.ref_dir.mkdir()
        self.out = self.root / "out"

    def frames(self, colour, frames=(1, 2, 3)):
        for frame in frames:
            run.write_png(self.run_dir / f"frame_{frame:07d}.png", 3, 2, image(3, 2, colour))

    def refs(self, colour, frames=(1, 2, 3)):
        for frame in frames:
            run.write_png(self.ref_dir / f"{frame}.png", 3, 2, image(3, 2, colour))

    def invoke(self, *extra):
        out = []
        with redirect_stdout(type("Sink", (), {"write": lambda self, s: out.append(s),
                                               "flush": lambda self: None})()):
            code = run.main(["--scene", "synthetic", "--scene-file", str(self.scene),
                             "--no-run", "--run", str(self.run_dir),
                             "--out", str(self.out), *extra])
        return code, "".join(out)

    def test_exact_match_passes_and_writes_report(self):
        self.frames((1, 2, 3))
        self.refs((1, 2, 3))
        code, _ = self.invoke("--reference", str(self.ref_dir))
        self.assertEqual(code, 0)
        report = json.loads((self.out / "report.json").read_text())
        self.assertTrue(report["pass"])
        self.assertEqual(report["summary"]["failed"], 0)
        self.assertEqual(report["summary"]["compared"], 3)
        self.assertTrue((self.out / "report.html").is_file())

    def test_difference_fails_and_writes_heat_map(self):
        self.frames((0, 0, 0))
        self.refs((0, 0, 0))
        run.write_png(self.run_dir / "frame_0000002.png", 3, 2, image(3, 2, (255, 0, 0)))
        code, _ = self.invoke("--reference", str(self.ref_dir))
        self.assertEqual(code, 1)
        report = json.loads((self.out / "report.json").read_text())
        failing = [f for f in report["frames"] if f["status"] == "fail"]
        self.assertEqual(len(failing), 1)
        self.assertEqual(failing[0]["frame"], 2)
        self.assertTrue((self.out / failing[0]["heat_map"]).is_file())

    def test_3d_tolerance_is_applied_before_the_cutoff(self):
        self.frames((0, 0, 0))
        self.refs((0, 0, 0))
        run.write_png(self.run_dir / "frame_0000002.png", 3, 2, image(3, 2, (5, 0, 0)))
        code, _ = self.invoke("--reference", str(self.ref_dir), "--3d-tolerance", "8",
                              "--3d-until", "2")
        self.assertEqual(code, 0)

    def test_no_reference_captures_a_baseline(self):
        self.frames((9, 9, 9))
        code, _ = self.invoke()
        self.assertEqual(code, 0)
        report = json.loads((self.out / "report.json").read_text())
        self.assertTrue(report["pass"])
        self.assertEqual(report["summary"]["reference_missing"], 3)
        self.assertIn("pending", report["note"])


if __name__ == "__main__":
    unittest.main()
