"""Tests for tools/boot_smoke.py. Synthetic packs and a fake runner only."""
import io
import json
import os
import stat
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from builder.pack import read_pak  # noqa: E402
from tools import boot_smoke  # noqa: E402


class FakeRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.pc = self.root / "fake-pc"
        self.pak, self.manifest = boot_smoke.synthetic_pack(self.root / "pack")

    def write_runner(self, body: str):
        self.pc.write_text("#!/bin/sh\n" + body, encoding="utf-8")
        self.pc.chmod(self.pc.stat().st_mode | stat.S_IEXEC)

    def run_tool(self, *extra):
        out = io.StringIO()
        with redirect_stdout(out):
            code = boot_smoke.main(["--pc", str(self.pc), "--pak", str(self.pak),
                                    "--manifest", str(self.manifest), *extra])
        return code, json.loads(out.getvalue())

    def test_synthetic_pack_names_a_supported_rom(self):
        decoded = read_pak(self.pak)
        self.assertEqual(decoded["build_id"], boot_smoke.BUILD_ID)
        self.assertEqual(decoded["rom_sha1"], boot_smoke.HG_SHA1)
        self.assertEqual(sorted(decoded["files"]), boot_smoke.SYNTHETIC_IDS)
        manifest = json.loads(self.manifest.read_text())
        self.assertEqual(len(manifest["files"]), len(boot_smoke.SYNTHETIC_IDS))

    def test_missing_file_event_is_reported(self):
        # The tool puts --events last; the fake runner writes the event there.
        self.pc.write_text(
            "#!/bin/sh\n"
            "for a in \"$@\"; do last=$a; done\n"
            "printf '%s\\n' '{\"event\":\"missing_file\",\"frame\":7,\"path\":\"a/0/1/6\"}' > \"$last\"\n"
            "echo '[     0] [heartgold] starting the game' >&2\n"
            "echo '[     7] [nitro] missing: a/0/1/6' >&2\n"
            "exit 5\n", encoding="utf-8")
        self.pc.chmod(self.pc.stat().st_mode | stat.S_IEXEC)
        code, result = self.run_tool()
        self.assertEqual(code, 0)
        self.assertEqual(result["stage"], "missing_file")
        self.assertEqual(result["frame"], 7)
        self.assertEqual(result["path"], "a/0/1/6")
        self.assertEqual(result["exit"], 5)
        self.assertEqual(result["boot_stage"], "game_start")

    def test_crash_event_resolves_nothing_without_a_real_binary(self):
        self.pc.write_text(
            "#!/bin/sh\n"
            "for a in \"$@\"; do last=$a; done\n"
            "printf '%s\\n' '{\"event\":\"crash\",\"frame\":3,\"signal\":11,\"pc\":\"0x1234\",\"address\":\"(nil)\"}' > \"$last\"\n"
            "exit 139\n", encoding="utf-8")
        self.pc.chmod(self.pc.stat().st_mode | stat.S_IEXEC)
        code, result = self.run_tool()
        self.assertEqual(code, 0)
        self.assertEqual(result["stage"], "crash")
        self.assertEqual(result["signal"], 11)
        self.assertEqual(result["pc"], "0x1234")
        # The fake "binary" is a shell script; addr2line either misses or
        # says it is not an object file. The report must still be JSON.
        self.assertIn("symbol", result)

    def test_frame_limit_is_a_stage(self):
        self.pc.write_text(
            "#!/bin/sh\n"
            "for a in \"$@\"; do last=$a; done\n"
            "printf '%s\\n' '{\"event\":\"frame_limit\",\"frame\":600}' > \"$last\"\n"
            "exit 0\n", encoding="utf-8")
        self.pc.chmod(self.pc.stat().st_mode | stat.S_IEXEC)
        code, result = self.run_tool("--expect-stage", "frame_limit")
        self.assertEqual(code, 0)
        self.assertEqual(result["stage"], "frame_limit")
        self.assertEqual(result["frame"], 600)

    def test_expect_stage_mismatch_fails(self):
        self.pc.write_text(
            "#!/bin/sh\n"
            "for a in \"$@\"; do last=$a; done\n"
            "printf '%s\\n' '{\"event\":\"stall\",\"frame\":9,\"seconds\":5}' > \"$last\"\n"
            "exit 6\n", encoding="utf-8")
        self.pc.chmod(self.pc.stat().st_mode | stat.S_IEXEC)
        code, result = self.run_tool("--expect-stage", "frame_limit")
        self.assertEqual(code, 1)
        self.assertEqual(result["stage"], "stall")

    def test_no_event_fails(self):
        self.write_runner("exit 0\n")
        code, result = self.run_tool()
        self.assertEqual(code, 1)
        self.assertEqual(result["stage"], "no_event")

    def test_missing_binary_is_a_setup_error(self):
        out = io.StringIO()
        with redirect_stdout(out):
            code = boot_smoke.main(["--pc", str(self.root / "nope"),
                                    "--pak", str(self.pak), "--manifest", str(self.manifest)])
        self.assertEqual(code, 2)


class ParseTests(unittest.TestCase):
    def test_parse_events_ignores_broken_lines(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "events.jsonl"
            path.write_text('{"event":"crash","frame":1}\nnot json\n\n{"event":"stall"}\n',
                            encoding="utf-8")
            events = boot_smoke.parse_events(path)
        self.assertEqual([event["event"] for event in events], ["crash", "stall"])

    def test_last_stage_tracks_overlays(self):
        log = ("[0] [nitro] overlay OVY_0 loaded\n"
               "[0] [nitro] overlay intro_title loaded\n"
               "[0] [heartgold] starting the game\n")
        self.assertEqual(boot_smoke.last_stage(log), "game_start")
        self.assertEqual(boot_smoke.last_stage("[0] overlay OVY_74 loaded\n"), "overlay:OVY_74")


if __name__ == "__main__":
    unittest.main()
