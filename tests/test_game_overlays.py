"""Unit tests for tools/game_overlays.py.

Synthetic linker specs and synthetic ELF objects only (no game data),
plus a read-only test against build/upstream/main.lsf when present.
"""
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS))
import game_overlays as go  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent / "build" / "upstream"

LSF = """\
Static main
{
\tAddress 0x02000000
\tObject src/main.o
}

Autoload ITCM
{
\tAddress 0x01FF8000
\tObject lib/NitroSDK/src/mi/mi_dma.o (.itcm)
}

Overlay field
{
\tAfter main
\tObject src/fieldmap.o
\tObject asm/field_data.o
}

Overlay lobbytest
{
\tAfter field
\tObject src/lobby/ppw_lobby.o
\tObject lib/NitroDWC/asm/dwc_lobby_data.o
\tObject lib/dsprot/dsprot_main_encrypted.o
}

Overlay fast
{
\tAfter ITCM
}

Overlay placed
{
\tAddress 0x027E0100
}
"""


def _elf_toolchain(probe: str, source: str) -> str | None:
    """A compiler whose output is little-endian ELF, or None."""
    cc = shutil.which(probe)
    if cc is None:
        return None
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / ("t.cpp" if probe.endswith("++") else "t.c")
        out = Path(tmp) / "t.o"
        src.write_text(source, encoding="utf-8")
        try:
            subprocess.run([cc, "-c", str(src), "-o", str(out)],
                           check=True, capture_output=True)
        except (subprocess.CalledProcessError, OSError):
            return None
        if out.is_file() and out.read_bytes()[:4] == b"\x7fELF":
            return cc
    return None


ELF_CC = _elf_toolchain("cc", "int x;\n")
ELF_CXX = _elf_toolchain("c++", "struct S { S(); }; S s;\nS::S() {}\n")

OBJCOPY = next((c for c in ("objcopy", "arm-none-eabi-objcopy", "llvm-objcopy")
                if shutil.which(c)), None)


def make_elf(sections: list[tuple[str, int]], bits: int = 64) -> bytes:
    """A minimal relocatable little-endian ELF with the named sections."""
    names = [".shstrtab"] + [n for n, _ in sections]
    shstrtab = b"\x00" + b"\x00".join(n.encode("ascii") for n in names) + b"\x00"
    offsets = {}
    pos = 1
    for name in names:
        offsets[name] = pos
        pos += len(name.encode("ascii")) + 1
    nsec = len(sections) + 2  # null + shstrtab + payload
    # Machines the host objcopy recognises (x86-64 / i386): the 32-bit path
    # exercises the ELF32 parser, not ARM relocs (real ARM objects are
    # renamed with arm-none-eabi-objcopy).
    if bits == 64:
        ehdr = struct.pack("<16sHHIQQQIHHHHHH", b"\x7fELF\x02\x01\x01" + b"\x00" * 9,
                           1, 0x3E, 1, 0, 0, 64, 0, 64, 0, 0, 64, nsec, 1)
        shoff = 64
        fmt, size = "<IIQQQQIIQQ", 64
    else:
        ehdr = struct.pack("<16sHHIIIIIHHHHHH", b"\x7fELF\x01\x01\x01" + b"\x00" * 9,
                           1, 3, 1, 0, 0, 52, 0, 52, 0, 0, 40, nsec, 1)
        shoff = 52
        fmt, size = "<IIIIIIIIII", 40
    headers = [b"\x00" * size,
               struct.pack(fmt, offsets[".shstrtab"], 3, 0, 0, shoff + nsec * size,
                           len(shstrtab), 0, 0, 1, 0)]
    pos = shoff + nsec * size + len(shstrtab)
    body = b""
    for name, size_ in sections:
        nobits = name.startswith(".bss") or name.startswith("ovl_bss")
        headers.append(struct.pack(fmt, offsets[name], 8 if nobits else 1,
                                   0 if nobits else 3, 0, pos, size_, 0, 0, 1, 0))
        if not nobits:
            body += b"\x00" * size_
            pos += size_
    return ehdr + b"".join(headers) + shstrtab + body


class ParseTests(unittest.TestCase):
    def setUp(self):
        self.spec = go.parse_lsf(LSF)

    def test_static_autoloads_overlays(self):
        self.assertEqual(self.spec["static"]["name"], "main")
        self.assertEqual(self.spec["static"]["address"], 0x02000000)
        self.assertEqual([a["name"] for a in self.spec["autoloads"]], ["ITCM"])
        self.assertEqual([o["name"] for o in self.spec["overlays"]],
                         ["field", "lobbytest", "fast", "placed"])

    def test_objects_with_sections(self):
        field = self.spec["overlays"][0]
        self.assertEqual(field["after"], "main")
        self.assertEqual(field["objects"],
                         [{"path": "src/fieldmap.o", "section": None},
                          {"path": "asm/field_data.o", "section": None}])
        self.assertEqual(self.spec["autoloads"][0]["objects"],
                         [{"path": "lib/NitroSDK/src/mi/mi_dma.o", "section": ".itcm"}])
        self.assertEqual(self.spec["overlays"][3]["address"], 0x027E0100)

    def test_overlay_names_in_order(self):
        self.assertEqual(go.overlay_names(LSF), ["field", "lobbytest", "fast", "placed"])

    def test_rejects_bad_specs(self):
        with self.assertRaises(ValueError):
            go.parse_lsf("Static main\n{\n}\n")
        with self.assertRaises(ValueError):
            go.parse_lsf("Overlay a\n{\n}\nOverlay a\n{\n}\n")
        with self.assertRaises(ValueError):
            go.parse_lsf("Static a\n{\n}\nStatic b\n{\n}\nOverlay c\n{\n\tAfter a\n}\n")
        with self.assertRaises(ValueError):
            go.parse_lsf("Overlay a\n{\n\tAfter main\n\tObject x.o (bogus\n}\n")


class RegionTests(unittest.TestCase):
    def test_after_chains_and_addresses(self):
        overlays = go.parse_lsf(LSF)["overlays"]
        self.assertEqual(go.region(overlays, "field"), "MAIN")
        self.assertEqual(go.region(overlays, "lobbytest"), "MAIN")
        self.assertEqual(go.region(overlays, "fast"), "ITCM")
        self.assertEqual(go.region(overlays, "placed"), "DTCM")

    def test_cycles_and_unknown_are_errors(self):
        overlays = go.parse_lsf("Overlay a\n{\n\tAfter b\n}\nOverlay b\n{\n\tAfter a\n}\n")["overlays"]
        with self.assertRaises(ValueError):
            go.region(overlays, "a")
        with self.assertRaises(ValueError):
            go.region(overlays, "nope")


class ObjectSourceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.up = Path(self.tmp.name) / "up"
        for rel in ("src/fieldmap.c", "src/lobby/ppw_lobby.cpp", "src/to91_msl_scalar_new.cpp",
                    "asm/field_data.s", "lib/NitroDWC/asm/dwc_lobby_data.s",
                    "lib/NitroSDK/src/mi/mi_dma.c"):
            p = self.up / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text("x\n", encoding="utf-8")

    def test_c_cpp_asm_resolution(self):
        self.assertEqual(go.object_source(self.up, "src/fieldmap.o"), "src/fieldmap.c")
        self.assertEqual(go.object_source(self.up, "src/lobby/ppw_lobby.o"),
                         "src/lobby/ppw_lobby.cpp")
        self.assertEqual(go.object_source(self.up, "src/to91_msl_scalar_new.o"),
                         "src/to91_msl_scalar_new.cpp")
        self.assertEqual(go.object_source(self.up, "asm/field_data.o"), "asm/field_data.s")
        self.assertEqual(go.object_source(self.up, "lib/NitroDWC/asm/dwc_lobby_data.o"),
                         "lib/NitroDWC/asm/dwc_lobby_data.s")

    def test_generated_objects_have_no_source(self):
        self.assertIsNone(go.object_source(self.up, "lib/dsprot/dsprot_main_encrypted.o"))
        self.assertIsNone(go.object_source(self.up, "src/missing.o"))

    def test_non_object_raises(self):
        with self.assertRaises(ValueError):
            go.object_source(self.up, "src/fieldmap.c")


class ObjectMapTests(unittest.TestCase):
    def test_objects_are_checkout_paths_under_objdir(self):
        objects = go.overlay_objects(Path("up"), go.parse_lsf(LSF)["overlays"], Path("obj"))
        self.assertEqual(objects["field"],
                         [Path("obj/src/fieldmap.o"), Path("obj/asm/field_data.o")])
        self.assertEqual(objects["lobbytest"][2],
                         Path("obj/lib/dsprot/dsprot_main_encrypted.o"))
        self.assertEqual(objects["fast"], [])
        self.assertEqual(objects["placed"], [])

    def test_non_object_and_library_raise(self):
        bad = go.parse_lsf(LSF)["overlays"]
        bad[0]["objects"].append({"path": "src/nope.c", "section": None})
        with self.assertRaises(ValueError):
            go.overlay_objects(Path("up"), bad, Path("obj"))
        bad2 = go.parse_lsf(LSF)["overlays"]
        bad2[0]["libraries"].append("libfoo.a")
        with self.assertRaises(ValueError):
            go.overlay_objects(Path("up"), bad2, Path("obj"))


class SectionKindTests(unittest.TestCase):
    def test_data_kinds(self):
        self.assertEqual(go.data_kind(".data"), "data")
        self.assertEqual(go.data_kind(".data.sCount"), "data")
        self.assertEqual(go.data_kind(".bss.sBuffer"), "bss")
        self.assertIsNone(go.data_kind(".data.rel.ro.table"))
        self.assertIsNone(go.data_kind(".rodata.str1.1"))
        self.assertIsNone(go.data_kind(".text.Func"))

    def test_sinit_kinds(self):
        # NitroStaticInit (.sinit) and GCC C++ constructors (.init_array/.ctors).
        for section in (".sinit", ".sinit.1", ".init_array", ".init_array.0",
                        ".ctors", ".ctors.123"):
            self.assertEqual(go.sinit_kind(section), "sinit", section)
        for section in (".text", ".rodata", ".data", ".bss", ".init", ".fini_array"):
            self.assertIsNone(go.sinit_kind(section), section)


class ElfTests(unittest.TestCase):
    def test_sections_round_trip_64_and_32(self):
        payload = [(".text", 16), (".data", 8), (".bss", 32),
                   (".sinit", 8), (".init_array", 16)]
        for bits in (64, 32):
            with self.subTest(bits=bits):
                blob = make_elf(payload, bits=bits)
                with tempfile.TemporaryDirectory() as tmp:
                    path = Path(tmp) / "t.o"
                    path.write_bytes(blob)
                    parsed = go.sections(path)
                    self.assertEqual([n for n, _ in parsed],
                                     ["", ".shstrtab", ".text", ".data", ".bss",
                                      ".sinit", ".init_array"])
                    self.assertEqual(dict(parsed)[".bss"], 32)
                    self.assertEqual(dict(parsed)[".init_array"], 16)
                    self.assertIn(".init_array", go.section_names(path))

    def test_non_elf_raises(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "t.o"
            path.write_text("not an object\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                go.sections(path)


@unittest.skipUnless(OBJCOPY, "needs objcopy")
class RenameTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def obj(self, sections: list[tuple[str, int]], bits: int = 64) -> Path:
        path = self.dir / "x.o"
        path.write_bytes(make_elf(sections, bits=bits))
        return path

    def test_data_and_bss_renamed_once(self):
        path = self.obj([(".text", 16), (".data", 8), (".data.rel.ro.x", 8),
                         (".bss", 4), (".rodata", 8)])
        self.assertEqual(go.rename_sections(path, "field", OBJCOPY), {"data", "bss"})
        names = go.section_names(path)
        self.assertIn("ovl_data_field", names)
        self.assertIn("ovl_bss_field", names)
        self.assertFalse([n for n in names if go.data_kind(n)])
        self.assertIn(".rodata", names)  # constants stay
        self.assertIn(".data.rel.ro.x", names)  # relative relocations stay
        before = path.read_bytes()
        self.assertEqual(go.rename_sections(path, "field", OBJCOPY), {"data", "bss"})
        self.assertEqual(path.read_bytes(), before)

    def test_sinit_and_init_array_merge(self):
        # .sinit (NitroStaticInit) and .init_array (C++ constructors) land in
        # one ovl_sinit table, run by FS_StartOverlay on every overlay load.
        path = self.obj([(".text", 4), (".sinit", 8), (".init_array", 16)])
        self.assertEqual(go.rename_sections(path, "watch", OBJCOPY), {"sinit"})
        names = go.section_names(path)
        self.assertNotIn(".sinit", names)
        self.assertNotIn(".init_array", names)
        self.assertEqual(names.count("ovl_sinit_watch"), 2)
        self.assertEqual(go.rename_sections(path, "watch", OBJCOPY), {"sinit"})

    def test_ctors_and_empty_sections(self):
        path = self.obj([(".ctors", 8), (".data", 0), (".bss", 0)])
        self.assertEqual(go.rename_sections(path, "old", OBJCOPY), {"sinit"})
        self.assertIn("ovl_sinit_old", go.section_names(path))

    def test_object_without_data(self):
        path = self.obj([(".text", 16)])
        self.assertEqual(go.rename_sections(path, "code", OBJCOPY), set())

    def test_32bit_objects(self):
        path = self.obj([(".data", 4), (".init_array", 8)], bits=32)
        self.assertEqual(go.rename_sections(path, "w", OBJCOPY), {"data", "sinit"})


@unittest.skipUnless(ELF_CC and OBJCOPY, "needs an ELF cc and objcopy")
class CompilerRenameTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def compile(self, source: str, *flags: str, lang: str = "cc") -> Path:
        tool = ELF_CXX if lang == "c++" else ELF_CC
        src = self.dir / ("x.cpp" if lang == "c++" else "x.c")
        out = self.dir / "x.o"
        src.write_text(source, encoding="utf-8")
        subprocess.run([tool, "-c", "-O2", *flags, str(src), "-o", str(out)], check=True)
        return out

    def test_data_and_bss_renamed_once(self):
        obj = self.compile("int a = 1; int z; static int s = 2;\n"
                           "const int k = 3; int *f(void) { static int t; return &t + s; }\n",
                           "-fdata-sections")
        kinds = go.rename_sections(obj, "field", OBJCOPY)
        self.assertEqual(kinds, {"data", "bss"})
        names = go.section_names(obj)
        self.assertIn("ovl_data_field", names)
        self.assertIn("ovl_bss_field", names)
        self.assertTrue([n for n in names if n.startswith(".rodata")])

    def test_static_initializers_renamed(self):
        obj = self.compile("static void NitroStaticInit(void);\n"
                           "__attribute__((section(\".sinit\"), used))\n"
                           "static void (*NitroStaticInit_[])(void) = { NitroStaticInit };\n"
                           "int x = 0; static void NitroStaticInit(void) { x = 1; }\n")
        self.assertIn("sinit", go.rename_sections(obj, "watch", OBJCOPY))
        self.assertIn("ovl_sinit_watch", go.section_names(obj))

    @unittest.skipUnless(ELF_CXX, "needs an ELF c++")
    def test_cpp_constructors_land_in_sinit(self):
        obj = self.compile("struct S { S(); }; S s;\nint g = 0; S::S() { g = 1; }\n",
                           lang="c++")
        before = go.section_names(obj)
        self.assertTrue(".init_array" in before or ".ctors" in before)
        self.assertIn("sinit", go.rename_sections(obj, "lobby", OBJCOPY))
        after = go.section_names(obj)
        self.assertNotIn(".init_array", after)
        self.assertNotIn(".ctors", after)
        self.assertIn("ovl_sinit_lobby", after)


class TableTests(unittest.TestCase):
    def test_every_overlay_gets_both_sections(self):
        text = go.render_table(go.parse_lsf(LSF)["overlays"])
        self.assertIn("extern u8 __start_ovl_data_field[], __stop_ovl_data_field[];", text)
        self.assertIn("extern u8 __start_ovl_bss_fast[], __stop_ovl_bss_fast[];", text)
        self.assertIn('{ "lobbytest", NITRO_PORT_OVERLAY_MAIN, __start_ovl_data_lobbytest, '
                      '__stop_ovl_data_lobbytest, __start_ovl_bss_lobbytest, '
                      '__stop_ovl_bss_lobbytest, '
                      '__start_ovl_sinit_lobbytest, __stop_ovl_sinit_lobbytest }', text)
        self.assertIn("extern void (*__start_ovl_sinit_fast[])(void), "
                      "(*__stop_ovl_sinit_fast[])(void);", text)
        self.assertIn("NITRO_PORT_OVERLAY_ITCM", text)
        self.assertIn('".section ovl_bss_placed,\\"aw\\",%nobits', text)
        self.assertIn("/*   3 */", text)
        self.assertIn("const NitroPortOverlay gHeartgoldOverlays[] = {", text)
        self.assertIn("const u32 gHeartgoldOverlayCount =", text)

    @unittest.skipUnless(ELF_CC, "needs an ELF cc")
    def test_table_compiles_and_pads_sections(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "nitro_port.h").write_text(
                "typedef unsigned char u8; typedef unsigned int u32;\n"
                "enum { NITRO_PORT_OVERLAY_MAIN, NITRO_PORT_OVERLAY_ITCM, NITRO_PORT_OVERLAY_DTCM };\n"
                "typedef struct { const char *name; u32 region; u8 *a, *b, *c, *d;\n"
                "                 void (**e)(void), (**f)(void); } NitroPortOverlay;\n",
                encoding="utf-8")
            (d / "t.c").write_text(go.render_table(go.parse_lsf(LSF)["overlays"]),
                                   encoding="utf-8")
            subprocess.run(["cc" if ELF_CC == "cc" else ELF_CC, "-c", "-Werror", "-I", str(d),
                            str(d / "t.c"), "-o", str(d / "t.o")], check=True)
            sections = dict(go.sections(d / "t.o"))
            self.assertEqual(sections["ovl_data_field"], 4)
            self.assertEqual(sections["ovl_bss_fast"], 4)
            # A null pointer ends each table of static initializers.
            self.assertEqual(sections["ovl_sinit_placed"], struct.calcsize("P"))


class IdTests(unittest.TestCase):
    def test_numbers_follow_linker_spec_order(self):
        names = go.overlay_names(LSF)
        self.assertEqual(names, ["field", "lobbytest", "fast", "placed"])
        text = go.render_ids(names)
        self.assertIn("SDK_OVERLAY_field_ID = 0;", text)
        self.assertIn("SDK_OVERLAY_lobbytest_ID = 1;", text)
        self.assertIn("SDK_OVERLAY_placed_ID = 3;", text)

    def test_header_has_the_same_numbers(self):
        header = go.render_ids_header(go.overlay_names(LSF))
        self.assertIn("SDK_PORT_OVERLAY_ID_field = 0,", header)
        self.assertIn("SDK_PORT_OVERLAY_ID_placed = 3,", header)
        self.assertTrue(header.startswith("/* Generated"))
        self.assertIn("#endif", header)

    def test_rejects_empty_or_duplicate(self):
        with self.assertRaises(ValueError):
            go.overlay_names("Static main\n{\n}\n")
        with self.assertRaises(ValueError):
            go.overlay_names("Overlay a\n{\n}\nOverlay a\n{\n}\n")


class MainTests(unittest.TestCase):
    def test_end_to_end_with_empty_objdir(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "main.lsf").write_text(LSF, encoding="utf-8")
            (d / "up" / "src").mkdir(parents=True)
            argv = ["--lsf", str(d / "main.lsf"), "--upstream", str(d / "up"),
                    "--objdir", str(d / "obj"), "--output", str(d / "table.c"),
                    "--ids-output", str(d / "ids.ld"), "--header", str(d / "ids.h")]
            import argparse  # noqa: F401  (main parses sys.argv)
            saved = sys.argv
            sys.argv = ["game_overlays.py", *argv]
            try:
                go.main()
            finally:
                sys.argv = saved
            self.assertIn("gHeartgoldOverlays", (d / "table.c").read_text(encoding="utf-8"))
            self.assertIn("SDK_OVERLAY_field_ID = 0;", (d / "ids.ld").read_text(encoding="utf-8"))
            self.assertIn("SDK_PORT_OVERLAY_ID_field = 0,",
                          (d / "ids.h").read_text(encoding="utf-8"))


@unittest.skipUnless((ROOT / "main.lsf").is_file(), "needs build/upstream")
class RealLsfTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = go.parse_lsf((ROOT / "main.lsf").read_text(encoding="utf-8"))

    def test_129_overlays_in_order(self):
        overlays = self.spec["overlays"]
        self.assertEqual(len(overlays), 129)
        names = [o["name"] for o in overlays]
        self.assertEqual(names[0], "OVY_0")
        self.assertEqual(names[1], "field")
        for want in ("intro_title", "field", "pokegear", "voltorb_flip", "OVY_45"):
            self.assertIn(want, names)

    def test_ovy45_mixes_c_cpp_and_data(self):
        overlays = self.spec["overlays"]
        by_name = {o["name"]: o for o in overlays}
        objects = by_name["OVY_45"]["objects"]
        self.assertEqual(len(objects), 133)
        kinds = {"c": 0, "cpp": 0, "s": 0, "missing": 0}
        for entry in objects:
            source = go.object_source(ROOT, entry["path"])
            if source is None:
                kinds["missing"] += 1
            else:
                kinds[Path(source).suffix[1:]] += 1
        self.assertEqual(kinds, {"c": 77, "cpp": 53, "s": 3, "missing": 0})

    def test_all_overlays_resolve_regions(self):
        overlays = self.spec["overlays"]
        regions = {go.region(overlays, o["name"]) for o in overlays}
        self.assertEqual(regions, {"MAIN"})

    def test_every_object_maps_to_objdir(self):
        objects = go.overlay_objects(ROOT, self.spec["overlays"], Path("obj"))
        self.assertEqual(sum(len(v) for v in objects.values()), 913)
        self.assertEqual(objects["OVY_45"][0], Path("obj/src/wifi_plaza/wifi_plaza.o"))


if __name__ == "__main__":
    unittest.main()
