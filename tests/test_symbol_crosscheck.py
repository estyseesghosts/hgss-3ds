"""Unit tests for tools/symbol_crosscheck.py.

Synthetic ELF symbol tables (a tiny ELF32 writer and fake pyelftools
objects) and tiny allowlist files. No ROM, no game content, no real build.
"""
import contextlib
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS))
import symbol_crosscheck as sc  # noqa: E402

try:
    import elftools  # noqa: F401
except ImportError:  # pragma: no cover - the image and the container have it
    elftools = None

HAVE_ELFTOOLS = elftools is not None


# --------------------------------------------------------------- fake ELF

class FakeSymbol:
    def __init__(self, name, bind="STB_GLOBAL", kind="STT_FUNC",
                 shndx="1"):
        self.name = name
        self.entry = {"st_info": {"bind": bind, "type": kind},
                      "st_shndx": shndx}


class FakeSection:
    def __init__(self, sh_type, symbols):
        self.header = {"sh_type": sh_type}
        self._symbols = symbols

    def iter_symbols(self):
        return iter(self._symbols)


class FakeElf:
    def __init__(self, sections):
        self._sections = sections

    def iter_sections(self):
        return iter(self._sections)


# ------------------------------------------------------- synthetic ELF32

def make_elf(symbols: list[tuple[str, int, int, int]]) -> bytes:
    """A minimal little-endian ELF32 relocatable with a .symtab.

    `symbols` items are (name, bind, type, st_shndx). Section 1 is .shstrtab,
    2 .strtab, 3 .text and 4 .symtab, so shndx 3 means "defined in .text".
    """
    shstr = b"\x00"
    shstr_offsets = {}
    for name in (".shstrtab", ".strtab", ".text", ".symtab"):
        shstr_offsets[name] = len(shstr)
        shstr += name.encode("ascii") + b"\x00"

    strtab = b"\x00"
    name_offsets = {}
    for name, _, _, _ in symbols:
        if name not in name_offsets:
            name_offsets[name] = len(strtab)
            strtab += name.encode("ascii") + b"\x00"

    text = b"\x00" * 4
    symtab = struct.pack("<IIIBBH", 0, 0, 0, 0, 0, 0)
    for name, bind, kind, shndx in symbols:
        info = (bind << 4) | kind
        symtab += struct.pack("<IIIBBH", name_offsets[name], 0, 0, info, 0, shndx)

    sections = [b"", shstr, strtab, text, symtab]
    offsets = []
    pos = 52
    for data in sections:
        offsets.append(pos)
        pos += len(data)

    e_shoff = pos
    shdrs = b"\x00" * 40  # null section
    shdrs += struct.pack("<IIIIIIIIII", shstr_offsets[".shstrtab"], 3, 0, 0,
                         offsets[1], len(shstr), 0, 0, 1, 0)
    shdrs += struct.pack("<IIIIIIIIII", shstr_offsets[".strtab"], 3, 0, 0,
                         offsets[2], len(strtab), 0, 0, 1, 0)
    shdrs += struct.pack("<IIIIIIIIII", shstr_offsets[".text"], 1, 6, 0,
                         offsets[3], len(text), 0, 0, 4, 0)
    shdrs += struct.pack("<IIIIIIIIII", shstr_offsets[".symtab"], 2, 0, 0,
                         offsets[4], len(symtab), 2, 1, 4, 16)

    header = struct.pack("<16sHHIIIIIHHHHHH",
                         b"\x7fELF\x01\x01\x01" + b"\x00" * 9,
                         1, 3, 1, 0, 0, e_shoff, 0, 52, 0, 0, 40, 5, 1)
    return header + b"".join(sections[1:]) + shdrs


def write_elf(directory: Path, name: str, symbols) -> Path:
    path = directory / name
    path.write_bytes(make_elf(symbols))
    return path


# ------------------------------------------------------------- symbol side

class SymbolExtractionTests(unittest.TestCase):
    def test_only_global_defined_c_symbols(self):
        elf = FakeElf([
            FakeSection("SHT_SYMTAB", [
                FakeSymbol("Game_Func", "STB_GLOBAL", "STT_FUNC"),
                FakeSymbol("Game_Data", "STB_GLOBAL", "STT_OBJECT"),
                FakeSymbol("_020D9960", "STB_GLOBAL", "STT_NOTYPE"),
                FakeSymbol("local_helper", "STB_LOCAL", "STT_FUNC"),
                FakeSymbol("Undefined_Symbol", "STB_GLOBAL", "STT_FUNC",
                           "SHN_UNDEF"),
                FakeSymbol("SDK_OVERLAY.OVY_0.ID", "STB_GLOBAL", "STT_NOTYPE"),
                FakeSymbol("?exc$_ZdlPv", "STB_GLOBAL", "STT_FUNC"),
                FakeSymbol("section_name", "STB_GLOBAL", "STT_SECTION"),
                FakeSymbol("_Z10NextActionP14ActionIterator", "STB_GLOBAL",
                           "STT_FUNC"),
            ]),
            FakeSection("SHT_PROGBITS", []),
        ])
        got = sc.symbols_from_elf(elf)
        self.assertEqual(sorted(got), [
            "Game_Data", "Game_Func", "_020D9960",
            "_Z10NextActionP14ActionIterator"])

    def test_a_different_symtab_section_is_ignored(self):
        elf = FakeElf([FakeSection("SHT_DYNSYM", [FakeSymbol("Not_Here")])])
        self.assertEqual(sc.symbols_from_elf(elf), {})


# ---------------------------------------------------------------- allowlist

class AllowlistTests(unittest.TestCase):
    def test_parse_rows_and_comments(self):
        entries = sc.parse_allowlist(
            "# header\n\n"
            "WM_*    # wireless manager stubbed\n"
            "Exact_Symbol # replaced by the port\n")
        self.assertEqual([e.pattern for e in entries], ["WM_*", "Exact_Symbol"])
        self.assertEqual(entries[0].reason, "wireless manager stubbed")

    def test_a_row_needs_a_reason(self):
        with self.assertRaises(ValueError):
            sc.parse_allowlist("WM_*\n")
        with self.assertRaises(ValueError):
            sc.parse_allowlist("WM_*  #\n")

    def test_first_matching_row_wins(self):
        entries = [sc.AllowEntry("Foo_*", "first"),
                   sc.AllowEntry("Foo_Bar", "second"),
                   sc.AllowEntry("Exact", "third")]
        self.assertEqual(sc.find_allow_entry("Foo_Bar", entries).reason, "first")
        self.assertEqual(sc.find_allow_entry("Exact", entries).reason, "third")
        self.assertIsNone(sc.find_allow_entry("Bar", entries))

    def test_the_repository_allowlist_parses(self):
        entries = sc.load_allowlist(sc.DEFAULT_ALLOWLIST)
        self.assertTrue(entries)
        self.assertTrue(all(entry.reason for entry in entries))


# --------------------------------------------------------------- comparing

def sym(name: str) -> sc.Symbol:
    return sc.Symbol(name, "STT_FUNC", "1")


class CompareTests(unittest.TestCase):
    def test_match(self):
        reference = {"A": sym("A"), "B": sym("B")}
        pc = {"A": sym("A"), "B": sym("B"), "Only_PC": sym("Only_PC")}
        result = sc.compare_symbols(reference, pc, [])
        self.assertTrue(result.ok)
        self.assertEqual(result.missing, [])
        self.assertEqual(result.matched, ["A", "B"])
        self.assertEqual(result.allowlisted, [])

    def test_missing(self):
        reference = {"A": sym("A"), "B": sym("B")}
        pc = {"A": sym("A")}
        result = sc.compare_symbols(reference, pc, [])
        self.assertFalse(result.ok)
        self.assertEqual(result.missing, ["B"])

    def test_allowlisted(self):
        reference = {"A": sym("A"), "WM_Start": sym("WM_Start")}
        pc = {"A": sym("A")}
        allow = [sc.AllowEntry("WM_*", "wireless manager stubbed")]
        result = sc.compare_symbols(reference, pc, allow)
        self.assertTrue(result.ok)
        self.assertEqual(result.missing, [])
        self.assertEqual(result.allowlisted[0][0], "WM_Start")
        self.assertEqual(result.allowlisted[0][1].reason,
                         "wireless manager stubbed")

    def test_missing_and_allowlisted_together(self):
        reference = {"Missing_Source": sym("Missing_Source"),
                     "WM_Start": sym("WM_Start")}
        result = sc.compare_symbols(
            reference, {}, [sc.AllowEntry("WM_*", "stub")])
        self.assertEqual(result.missing, ["Missing_Source"])
        self.assertEqual(result.allowlisted[0][0], "WM_Start")


# --------------------------------------------------------------- end to end

@unittest.skipUnless(HAVE_ELFTOOLS, "pyelftools is not installed")
class SyntheticElfTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.allow = self.dir / "allowlist.txt"
        self.allow.write_text("# empty\n", encoding="utf-8")

    def run_tool(self, pc, reference, allow=None):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = sc.main(["--pc", str(pc), "--reference", str(reference),
                            "--allowlist", str(allow or self.allow)])
        return code, out.getvalue(), err.getvalue()

    def test_parses_a_real_elf(self):
        path = write_elf(self.dir, "one.elf", [
            ("Game_Func", 1, 2, 3),
            ("Undefined", 1, 2, 0),
            ("local", 0, 2, 3),
        ])
        symbols = sc.load_elf_symbols(path)
        self.assertEqual(sorted(symbols), ["Game_Func"])
        self.assertEqual(symbols["Game_Func"].kind, "STT_FUNC")

    def test_match_exits_zero(self):
        ref = write_elf(self.dir, "ref.elf", [("A", 1, 2, 3), ("B", 1, 2, 3)])
        pc = write_elf(self.dir, "pc.elf", [("A", 1, 2, 3), ("B", 1, 2, 3)])
        code, out, _ = self.run_tool(pc, ref)
        self.assertEqual(code, 0)
        self.assertIn("missing:    0", out)

    def test_missing_exits_one_and_prints_the_set(self):
        ref = write_elf(self.dir, "ref.elf", [("A", 1, 2, 3), ("B", 1, 2, 3)])
        pc = write_elf(self.dir, "pc.elf", [("A", 1, 2, 3)])
        code, out, err = self.run_tool(pc, ref)
        self.assertEqual(code, 1)
        self.assertIn("missing:    1", out)
        self.assertIn("  B", err)

    def test_allowlisted_exits_zero_and_names_the_allowlist(self):
        ref = write_elf(self.dir, "ref.elf", [("A", 1, 2, 3),
                                             ("WM_Start", 1, 2, 3)])
        pc = write_elf(self.dir, "pc.elf", [("A", 1, 2, 3)])
        self.allow.write_text("WM_*  # wireless manager stubbed\n", encoding="utf-8")
        code, out, _ = self.run_tool(pc, ref)
        self.assertEqual(code, 0)
        self.assertIn(str(self.allow), out)
        self.assertIn("allowlisted: 1", out)

    def test_missing_pc_reports_the_allowlist(self):
        ref = write_elf(self.dir, "ref.elf", [("A", 1, 2, 3)])
        code, out, err = self.run_tool(self.dir / "no-such-pc", ref)
        self.assertEqual(code, 2)
        self.assertIn(str(self.allow), out)
        self.assertIn("PC ELF not found", err)


if __name__ == "__main__":
    unittest.main()
