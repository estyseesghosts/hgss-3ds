"""Tests for tools/frametime_report.py (synthetic log lines only)."""
import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from tools import frametime_report as report  # noqa: E402


SAMPLE = """\
[1] starting
FRAMETIME frame=600 logic_us=800 draw_us=9000 audio_us=0 total_us=16700
noise
[2] FRAMETIME frame=1200 logic_us=900 draw_us=9500 audio_us=100 total_us=16800
FRAMETIME frame=1800 logic_us=1000 draw_us=20000 audio_us=200 total_us=22000
"""


class ParseTests(unittest.TestCase):
    def test_parse_ignores_noise(self):
        samples = report.parse(SAMPLE)
        self.assertEqual(len(samples), 3)
        self.assertEqual(samples[1]["frame"], 1200)
        self.assertEqual(samples[2]["total"], 22000)

    def test_summary(self):
        result = report.summary(report.parse(SAMPLE))
        self.assertEqual(result["samples"], 3)
        self.assertAlmostEqual(result["logic"]["mean_us"], 900.0)
        self.assertEqual(result["total"]["max_us"], 22000)
        self.assertEqual(result["frames_over_16_7ms"], 2)
        self.assertFalse(result["meets_target"])

    def test_empty(self):
        result = report.summary([])
        self.assertEqual(result["samples"], 0)
        self.assertIn("No FRAMETIME", report.markdown(result))

    def test_markdown(self):
        table = report.markdown(report.summary(report.parse(SAMPLE)))
        self.assertIn("| draw (us) |", table)
        self.assertIn("3 frames", table)
        self.assertIn("misses", table)


class CliTests(unittest.TestCase):
    def test_reads_a_file_and_writes_json(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "log.txt"
            path.write_text(SAMPLE, encoding="utf-8")
            out = Path(temp) / "report.json"
            with redirect_stdout(io.StringIO()) as stdout:
                code = report.main([str(path), "--json", str(out)])
            self.assertEqual(code, 0)
            self.assertIn("FRAMETIME", path.read_text())
            self.assertIn("draw", stdout.getvalue())
            data = json.loads(out.read_text())
            self.assertEqual(data["samples"], 3)

    def test_no_samples_is_a_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "log.txt"
            path.write_text("nothing here\n", encoding="utf-8")
            with redirect_stdout(io.StringIO()):
                self.assertEqual(report.main([str(path)]), 1)


if __name__ == "__main__":
    unittest.main()
