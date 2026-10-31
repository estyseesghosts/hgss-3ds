"""Unit tests for tools/hgss_nitro.py. Synthetic trees only; never game data."""
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from tools import hgss_nitro  # noqa: E402


def make_fake_root(base):
    root = Path(base) / "upstream"
    (root / "lib" / "NitroSDK" / "src" / "os").mkdir(parents=True)
    (root / "lib" / "NitroSDK" / "src" / "os" / "os_thread.c").write_text("/* fake */\n")
    (root / "lib" / "NitroSDK" / "src" / "os" / "os_terminate_arm9.c").write_text("/* fake */\n")
    (root / "lib" / "NitroSDK" / "src" / "os" / "os_cache.c").write_text("/* fake */\n")
    (root / "lib" / "NitroSystem" / "src").mkdir(parents=True)
    (root / "lib" / "NitroSystem" / "src" / "g3d_gecom.c").write_text("/* fake */\n")
    (root / "lib" / "NitroSystem" / "src" / "gfd_vram_transfer_manager.c").write_text("/* fake */\n")
    (root / "lib" / "include").mkdir(parents=True)
    for n in ("nitro", "nitro.h", "nnsys", "nnsys.h", "cw", "cw.h", "MSL.h", "MSL_C",
              "ansi_params.h", "size_t.h"):
        (root / "lib" / "include" / n).mkdir(exist_ok=True)
    (root / "lib" / "include" / "nitro" / "fx").mkdir(parents=True)
    (root / "lib" / "include" / "nitro" / "fx" / "fx_const.h").write_text("/* fake */\n")
    (root / "sub" / "lib" / "src").mkdir(parents=True)
    (root / "sub" / "lib" / "src" / "sound_main.c").write_text("/* fake */\n")
    (root / "files" / "data").mkdir(parents=True)
    (root / "files" / "data" / "a.nsbmd").write_bytes(b"BMD0fake")
    (root / "files" / "data" / "b.NSBMD").write_bytes(b"BMD0fake")
    (root / "files" / "data" / "c.NCGR").write_bytes(b"RGCNfake")
    return root


class HgssNitroTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name) / "build"
        self.build.mkdir()
        self.root = make_fake_root(self.temp.name)
        self.patch_build = patch.object(hgss_nitro, "BUILD", self.build)
        self.patch_build.start()
        self.addCleanup(self.patch_build.stop)

    def test_terminate_excluded(self):
        keep, drop = hgss_nitro.sdk_sources(self.root)
        self.assertIn("os/os_thread.c", keep)
        self.assertIn("os/os_terminate_arm9.c", drop)
        self.assertIn("os/os_cache.c", drop)

    def test_arm7_sound_list(self):
        self.assertEqual(len(hgss_nitro.ARM7_SOUND_FILES), 9)
        self.assertIn("sound_main.c", hgss_nitro.ARM7_SOUND_FILES)
        self.assertIn("snd_arm7_data.c", hgss_nitro.ARM7_COMPAT_FILES)

    def test_shim_links(self):
        sdk, nns = hgss_nitro.make_shim(self.root)
        self.assertEqual(os.readlink(sdk / "libraries" / "hgss"), str(self.root / "lib" / "NitroSDK" / "src"))
        self.assertEqual(os.readlink(sdk / "libraries" / "hgss_compat"),
                         str(hgss_nitro.REPO / "hgss" / "compat" / "src"))
        self.assertEqual(os.readlink(sdk / "libraries" / "hgss_sub"),
                         str(self.root / "sub" / "lib" / "src"))
        self.assertEqual(os.readlink(nns / "libraries" / "hgss_nns"),
                         str(self.root / "lib" / "NitroSystem" / "src"))
        self.assertEqual(os.readlink(nns / "libraries" / "hgss_nns_compat"),
                         str(hgss_nitro.REPO / "hgss" / "compat" / "src"))
        self.assertEqual(os.readlink(nns / "libraries" / "g3d" / "src" / "gecom.c"),
                         str(self.root / "lib" / "NitroSystem" / "src" / "g3d_gecom.c"))
        self.assertEqual(os.readlink(nns / "libraries" / "gfd" / "src" / "VramTransferMan"
                                     / "gfd_VramTransferManager.c"),
                         str(self.root / "lib" / "NitroSystem" / "src" / "gfd_vram_transfer_manager.c"))
        self.assertTrue((sdk / "gen" / "nitro" / "fx" / "gen_fx_const.py").is_file())

    def test_nitro_overrides(self):
        ov = hgss_nitro.nitro_overrides(self.root, "host32", "/tmp/out")
        text = "\n".join(ov)
        self.assertIn("SDK_SRCS=", text)
        self.assertIn("hgss/os/os_thread.c", text)
        self.assertNotIn("os_terminate_arm9", text)
        for c in hgss_nitro.COMPAT_SRCS:
            self.assertIn("hgss_compat/" + c, text)
        for f in hgss_nitro.ARM7_SOUND_FILES:
            self.assertIn("hgss_sub/" + f, text)
        self.assertIn("NNS=", text)
        self.assertIn("NNS_INCLUDES=", text)
        self.assertIn("-Isub/include", text.replace(str(self.root) + "/", ""))

    def test_make_out_helper(self):
        # The vendored recipes run `./$(TEST_BIN)`, which breaks on an absolute OUT,
        # so OUT= is spelled relative to the subproject directory (same directory).
        for sub in ("pak", "nitro", "nitrosystem"):
            rel = hgss_nitro._make_out(sub, f"/tmp/out/{sub}")
            self.assertFalse(os.path.isabs(rel))
            resolved = os.path.normpath(os.path.join(
                str(hgss_nitro.REPO / "3ds_port" / sub), rel))
            self.assertEqual(resolved, f"/tmp/out/{sub}")
        self.assertIsNone(hgss_nitro._make_out("pak", None))

    def test_nitro_out_is_relative(self):
        ov = hgss_nitro.nitro_overrides(self.root, "host32", "/tmp/out")
        outs = [line[4:] for line in ov if line.startswith("OUT=")]
        self.assertEqual(len(outs), 1)
        self.assertFalse(os.path.isabs(outs[0]))
        resolved = os.path.normpath(os.path.join(
            str(hgss_nitro.REPO / "3ds_port" / "nitro"), outs[0]))
        self.assertEqual(resolved, "/tmp/out")
        gens = [line[4:] for line in ov if line.startswith("GEN=")]
        self.assertEqual(gens, ["/tmp/out/gen"])

    def test_nitrosystem_out_is_relative(self):
        with patch.object(hgss_nitro, "write_nns_all_headers"):
            ov = hgss_nitro.nitrosystem_overrides(self.root, "host32", "/tmp/out")
        outs = [line[4:] for line in ov if line.startswith("OUT=")]
        self.assertEqual(len(outs), 1)
        self.assertFalse(os.path.isabs(outs[0]))
        resolved = os.path.normpath(os.path.join(
            str(hgss_nitro.REPO / "3ds_port" / "nitrosystem"), outs[0]))
        self.assertEqual(resolved, "/tmp/out")

    def test_nitrosystem_overrides(self):
        with patch.object(hgss_nitro, "write_nns_all_headers") as w:
            ov = hgss_nitro.nitrosystem_overrides(self.root, "host32", "/tmp/out")
        text = "\n".join(ov)
        self.assertIn("NNS_SRCS=hgss_nns/g3d_gecom.c hgss_nns/gfd_vram_transfer_manager.c", text)
        for c in hgss_nitro.NNS_COMPAT_SRCS:
            self.assertIn("hgss_nns_compat/" + c, text)
        self.assertIn("LIBNITRO=/tmp/out/libnitro.a", text)
        w.assert_called_once()

    def test_nns_all_headers(self):
        _, nns = hgss_nitro.make_shim(self.root)
        hdr = self.root / "lib" / "include" / "nnsys" / "g3d.h"
        hdr.write_text("/* fake */\n")
        gen = Path(self.temp.name) / "gen"
        hgss_nitro.write_nns_all_headers(str(gen), self.root, nns)
        text = (gen / "nns_all_headers.c").read_text()
        self.assertIn("#include <nitro.h>", text)
        self.assertIn("g3d.h", text)

    def test_ensure_nns_res(self):
        with patch.object(hgss_nitro, "NNS_RES_PAD_MODELS", 3):
            res = hgss_nitro.ensure_nns_res(self.root)
        self.assertTrue((res / "data" / "a.nsbmd").is_file())
        self.assertTrue((res / "data" / "c.NCGR").is_file())
        # uppercase model mirrored under a lowercase name
        self.assertTrue((res / "extra" / "b.nsbmd").is_file())
        pads = sorted((res / "pad").glob("*.nsbmd"))
        self.assertEqual(len(pads), 3)
        # hard link (or identical copy fallback): same bytes as the seed
        self.assertEqual(pads[0].read_bytes(), b"BMD0fake")

    def test_list_command(self):
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = hgss_nitro.main(["list", "--root", str(self.root)])
        self.assertEqual(rc, 0)
        out = buf.getvalue()
        self.assertIn("arm7 sound: 11 files", out)
        self.assertIn("hgss_nns/", out)


if __name__ == "__main__":
    unittest.main()
