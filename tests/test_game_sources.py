"""Unit tests for tools/game_sources.py.

Synthetic directory trees only (no game data), plus a read-only test
against build/upstream/main.lsf when that checkout exists.
"""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS))
import game_sources as gs  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent / "build" / "upstream"

COMMON_MK = """\
SRC_SUBDIR                := src
ASM_SUBDIR                := asm
LIB_SUBDIRS               := NitroSDK NitroSystem NitroDWC NitroWiFi libCPS libVCT MSL_C
NITROSDK_SRC_SUBDIRS      := os mi fx gx card cp ctrdg fs math pxi rtc snd mb
"""

MAKEFILE = "include config.mk\ninclude common.mk\n"

FILESYSTEM_MK = "NITROFS_FILES := files/data/a.dat\n"

LSF = """\
Static main
{
    Address 0x02000000
    Object src/main.o
    Object src/to91_msl_scalar_new.o
    Object lib/NitroSDK/src/os/os_thread.o
}

Autoload ITCM
{
    Address 0x01FF8000
    Object lib/NitroSDK/src/mi/mi_dma.o (.itcm)
}

Autoload DTCM
{
    Address 0x027E0000
    Object lib/NitroSDK/src/os/os_irqTable.o (.dtcm.bss)
}

Overlay field
{
    After main
    Object src/fieldmap.o
    Object asm/field_data.o
}

Overlay lobbytest
{
    After field
    Object src/lobby/ppw_lobby.o
    Object lib/dsprot/dsprot_main_encrypted.o
}
"""

FILES = {
    "src/main.c": "int main_(void) { return 0; }\n",
    "src/to91_msl_scalar_new.cpp": "void *f() { return 0; }\n",
    "src/fieldmap.c": "int g;\n",
    "src/lobby/ppw_lobby.cpp": "int h;\n",
    "src/unused.c": "int u;\n",
    "asm/field_data.s": "\t.text\n",
    "lib/asm/spl_data.s": "\t.text\n",
    "lib/NitroSDK/asm/fx_sincos.s": "\t.text\n",
    "lib/MSL_C/asm/msl_abort_exit_data.s": "\t.text\n",
    "lib/NitroDWC/asm/dwc_lib_rodata.s": "\t.text\n",
    "lib/NitroSDK/src/os/os_thread.c": "int a;\n",
    "lib/NitroSDK/src/mi/mi_dma.c": "int b;\n",
    "lib/NitroSDK/src/os/os_irqTable.c": "int c;\n",
    "lib/NitroSDK/src/pxi/PXI_fifo.c": "int d;\n",
    "lib/NitroSDK/src/mi/mi_memory.c": "int e;\n",
    "lib/MSL_C/src/msl_mem.c": "int f;\n",
    "lib/NitroDWC/src/dwc_main.c": "int g;\n",
    "lib/NitroSystem/src/g2d_load.c": "int h;\n",
    "lib/src/spl_manager.c": "int i;\n",
}


def make_root(parent: Path) -> Path:
    root = parent / "up"
    root.mkdir(parents=True, exist_ok=True)
    (root / "Makefile").write_text(MAKEFILE, encoding="utf-8")
    (root / "common.mk").write_text(COMMON_MK, encoding="utf-8")
    (root / "filesystem.mk").write_text(FILESYSTEM_MK, encoding="utf-8")
    (root / "main.lsf").write_text(LSF, encoding="utf-8")
    for rel, text in FILES.items():
        p = root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8")
    return root


class ListingTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = make_root(Path(self.tmp.name))
        self.vars = gs.check_root(self.root)

    def test_c_sources(self):
        self.assertEqual(gs.game_c_sources(self.root),
                         ["src/fieldmap.c", "src/main.c", "src/unused.c"])

    def test_cpp_sources(self):
        self.assertEqual(gs.game_cpp_sources(self.root),
                         ["src/lobby/ppw_lobby.cpp", "src/to91_msl_scalar_new.cpp"])

    def test_game_sources_sorted(self):
        self.assertEqual(gs.game_sources(self.root),
                         ["src/fieldmap.c", "src/lobby/ppw_lobby.cpp", "src/main.c",
                          "src/to91_msl_scalar_new.cpp", "src/unused.c"])

    def test_asm_sources(self):
        self.assertEqual(gs.asm_sources(self.root, self.vars),
                         ["asm/field_data.s", "lib/MSL_C/asm/msl_abort_exit_data.s",
                          "lib/NitroDWC/asm/dwc_lib_rodata.s", "lib/NitroSDK/asm/fx_sincos.s",
                          "lib/asm/spl_data.s"])

    def test_lib_sources(self):
        self.assertEqual(gs.lib_sources(self.root, self.vars),
                         ["lib/MSL_C/src/msl_mem.c", "lib/NitroDWC/src/dwc_main.c",
                          "lib/NitroSDK/src/mi/mi_dma.c", "lib/NitroSDK/src/mi/mi_memory.c",
                          "lib/NitroSDK/src/os/os_irqTable.c", "lib/NitroSDK/src/os/os_thread.c",
                          "lib/NitroSDK/src/pxi/PXI_fifo.c", "lib/NitroSystem/src/g2d_load.c",
                          "lib/src/spl_manager.c"])

    def test_port_lib_sources_filtered(self):
        # Port-replaced SDK files (hgss_nitro), stubbed DWC, newlib's MSL_C.
        self.assertEqual(gs.port_lib_sources(self.root, self.vars),
                         ["lib/NitroSDK/src/mi/mi_dma.c",
                          "lib/NitroSDK/src/os/os_irqTable.c",
                          "lib/NitroSDK/src/os/os_thread.c",
                          "lib/NitroSystem/src/g2d_load.c",
                          "lib/src/spl_manager.c"])

    def test_lib_dirs_come_from_common_mk(self):
        self.assertIn("lib/NitroSDK/src/os", gs.lib_src_dirs(self.root, self.vars))
        self.assertIn("lib/NitroDWC/src", gs.lib_src_dirs(self.root, self.vars))


class OverlayMapTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.blocks = gs.overlay_map(make_root(Path(self.tmp.name)))

    def test_block_order_and_sections(self):
        self.assertEqual([(b["kind"], b["name"]) for b in self.blocks],
                         [("Static", "main"), ("Autoload", "ITCM"), ("Autoload", "DTCM"),
                          ("Overlay", "field"), ("Overlay", "lobbytest")])
        itcm = self.blocks[1]
        self.assertEqual(itcm["address"], 0x01FF8000)
        self.assertEqual(itcm["objects"],
                         [{"object": "lib/NitroSDK/src/mi/mi_dma.o",
                           "source": "lib/NitroSDK/src/mi/mi_dma.c", "section": ".itcm"}])

    def test_cpp_and_generated_sources(self):
        lobby = self.blocks[4]
        self.assertEqual(lobby["after"], "field")
        self.assertEqual(lobby["objects"][0],
                         {"object": "src/lobby/ppw_lobby.o",
                          "source": "src/lobby/ppw_lobby.cpp", "section": None})
        # Build-generated dsprot objects have no checkout source.
        self.assertIsNone(lobby["objects"][1]["source"])


class RootCheckTests(unittest.TestCase):
    def test_missing_files_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit):
                gs.check_root(Path(tmp))

    def test_makefile_must_include_common_mk(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = make_root(Path(tmp))
            (root / "Makefile").write_text("all:\n", encoding="utf-8")
            with self.assertRaises(SystemExit):
                gs.check_root(root)


class CliTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = make_root(Path(self.tmp.name))

    def run_cli(self, *args: str) -> str:
        proc = subprocess.run([sys.executable, str(TOOLS / "game_sources.py"),
                               "--root", str(self.root), *args],
                              check=True, capture_output=True, text=True)
        return proc.stdout

    def test_default_lists_game_sources(self):
        self.assertEqual(self.run_cli().split(),
                         ["src/fieldmap.c", "src/lobby/ppw_lobby.cpp", "src/main.c",
                          "src/to91_msl_scalar_new.cpp", "src/unused.c"])

    def test_single_list_flag(self):
        self.assertEqual(self.run_cli("--asm-sources").split(),
                         ["asm/field_data.s", "lib/MSL_C/asm/msl_abort_exit_data.s",
                          "lib/NitroDWC/asm/dwc_lib_rodata.s", "lib/NitroSDK/asm/fx_sincos.s",
                          "lib/asm/spl_data.s"])

    def test_overlay_map_text(self):
        out = self.run_cli("--overlay-map")
        self.assertIn("Overlay lobbytest (After field)", out)
        self.assertIn("src/lobby/ppw_lobby.o <- src/lobby/ppw_lobby.cpp", out)
        self.assertIn("lib/dsprot/dsprot_main_encrypted.o <- (generated)", out)

    def test_json_counts(self):
        data = json.loads(self.run_cli("--json"))
        self.assertEqual(data["counts"],
                         {"c": 3, "cpp": 2, "game": 5, "asm": 5, "lib": 9,
                          "port_lib": 5, "overlays": 2})
        self.assertEqual(len(data["overlay_map"]), 5)

    def test_json_subset(self):
        data = json.loads(self.run_cli("--cpp-sources", "--json"))
        self.assertEqual(set(data), {"root", "counts", "cpp_sources"})


@unittest.skipUnless((ROOT / "main.lsf").is_file(), "needs build/upstream")
class RealRootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vars = gs.check_root(ROOT)
        cls.data = gs.all_data(ROOT)

    def test_129_overlays(self):
        self.assertEqual(self.data["counts"]["overlays"], 129)

    def test_named_overlays_in_order(self):
        names = [b["name"] for b in self.data["overlay_map"] if b["kind"] == "Overlay"]
        self.assertEqual(names[0], "OVY_0")
        self.assertEqual(names[1], "field")
        for want in ("intro_title", "pokegear", "voltorb_flip", "OVY_45", "ds_protect"):
            self.assertIn(want, names)

    def test_ovy45_cpp_objects(self):
        block = next(b for b in self.data["overlay_map"] if b["name"] == "OVY_45")
        cpp = [e for e in block["objects"] if (e["source"] or "").endswith(".cpp")]
        self.assertEqual(len(block["objects"]), 133)
        self.assertEqual(len(cpp), 53)
        self.assertTrue(all(s.startswith("src/lobby/") for s in
                            (e["source"] for e in cpp)))

    def test_every_src_and_asm_object_resolves(self):
        unresolved = [e["object"] for b in self.data["overlay_map"] for e in b["objects"]
                      if e["object"].startswith(("src/", "asm/")) and e["source"] is None]
        self.assertEqual(unresolved, [])

    def test_dsprot_objects_are_generated(self):
        block = next(b for b in self.data["overlay_map"] if b["name"] == "ds_protect")
        self.assertEqual(len(block["objects"]), 18)
        self.assertTrue(all(e["source"] is None for e in block["objects"]))

    def test_all_overlays_load_in_main(self):
        import game_overlays as go
        overlays = [b for b in self.data["overlay_map"] if b["kind"] == "Overlay"]
        self.assertTrue(all(go.region(overlays, b["name"]) == "MAIN" for b in overlays))

    def test_static_and_autoloads(self):
        kinds = [(b["kind"], b["name"]) for b in self.data["overlay_map"][:3]]
        self.assertEqual(kinds, [("Static", "main"), ("Autoload", "ITCM"),
                                 ("Autoload", "DTCM")])


if __name__ == "__main__":
    unittest.main()
