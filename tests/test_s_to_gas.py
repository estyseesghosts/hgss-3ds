"""Unit tests for tools/s_to_gas.py.

Synthetic inputs only (no game data).  Each test builds a tiny checkout
tree in a temp dir, runs the converter, and -- where a working toolchain
exists -- assembles the result and checks the ELF with nm/readelf.

Assembly checks need an ELF-capable GAS (host cc and/or
arm-none-eabi-gcc).  If neither is present (e.g. macOS without a
cross-toolchain) the assembly checks skip; the pure-text translation
tests always run.  Inside the dev container both compilers exist, so the
full matrix runs there.
"""
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS))
import s_to_gas as s2g  # noqa: E402


def have_tool(name):
    return shutil.which(name) is not None


def try_assemble(cc, flags, gas_text):
    """Assemble GAS text; return (ok, obj_path, tmpdir)."""
    tmp = tempfile.TemporaryDirectory()
    src = Path(tmp.name) / "t.s"
    obj = Path(tmp.name) / "t.o"
    src.write_text(gas_text, encoding="utf-8")
    proc = subprocess.run([cc] + list(flags) + ["-c", str(src), "-o",
                                                str(obj)],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        tmp.cleanup()
        return False, proc.stderr, None
    return True, obj, tmp


def nm(path):
    proc = subprocess.run(["nm", "--defined-only", "--print-size", str(path)],
                          capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr
    syms = {}
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) == 4:
            syms[parts[3]] = (parts[2], int(parts[1], 16))
        elif len(parts) == 3:
            syms[parts[2]] = (parts[1], None)
    return syms


def readelf_sections(path):
    proc = subprocess.run(["readelf", "-S", "-W", str(path)],
                          capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr
    secs = {}
    for line in proc.stdout.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+"
                     r"([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+"
                     r"([0-9a-f]+)\s*(.*)", line)
        if m:
            # Columns: Name Type Addr Off Size ES Flg Lk Inf Al.
            rest = m.group(7).split()
            if rest and re.fullmatch(r"[WAXMOSILGTExo]+", rest[0]):
                flags, align = rest[0], rest[-1]
            else:
                flags, align = "", rest[-1] if rest else "?"
            secs[m.group(1)] = {
                "type": m.group(2), "flags": flags,
                "size": int(m.group(5), 16),
                "align": align,
            }
    return secs


def section_bytes(path, name):
    proc = subprocess.run(["readelf", "-x", name, str(path)],
                          capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr
    hexbytes = []
    for line in proc.stdout.splitlines():
        m = re.match(r"\s*0x[0-9a-f]+\s+((?:[0-9a-f]{2,8}\s?)+)", line)
        if m:
            for group in m.group(1).split():
                hexbytes.extend(group[i:i + 2]
                                for i in range(0, len(group), 2))
    return bytes(int(b, 16) for b in hexbytes)


def compilers():
    """(name, cc, flags) combos worth assembling with, probed for ELF."""
    combos = []
    if have_tool("arm-none-eabi-gcc"):
        combos.append(("arm", "arm-none-eabi-gcc", ["-march=armv6k"]))
    for cc in ("gcc-10", "gcc", "cc"):
        if have_tool(cc):
            combos.append(("host", cc, []))
            break
    good = []
    for name, cc, flags in combos:
        ok, _, tmp = try_assemble(
            cc, flags, ".text\ns2g_probe:\n.4byte 0x12345678\n")
        if ok:
            tmp.cleanup()
            good.append((name, cc, flags))
    return good


class SToGasTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compilers = compilers()
        cls.have_binutils = have_tool("nm") and have_tool("readelf")
        cls.host_cc = None
        for name, cc, flags in cls.compilers:
            if name == "host":
                cls.host_cc = (cc, flags)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / "up"
        self.root.mkdir()

    def write(self, rel, text):
        p = self.root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8")
        return p

    def convert(self, src_rel, cflags=(), overlay_ids=None):
        gas, warnings = s2g.convert(str(self.root / src_rel),
                                    str(self.root),
                                    cc=self.host_cc[0] if self.host_cc
                                    else "cc",
                                    cflags=list(cflags),
                                    extra_inc_dirs=s2g.extract_user_includes(
                                        list(cflags)),
                                    overlay_ids=overlay_ids)
        return gas, warnings

    def assemble_all(self, gas):
        """Assemble with every available compiler; skip if none/binutils."""
        if not self.compilers:
            self.skipTest("no ELF-capable assembler available")
        if not self.have_binutils:
            self.skipTest("nm/readelf unavailable")
        objs = []
        for _name, cc, flags in self.compilers:
            ok, obj, tmp = try_assemble(cc, flags, gas)
            self.assertTrue(ok, f"{cc} failed: {obj}")
            self.addCleanup(tmp.cleanup)
            objs.append(obj)
        return objs

    # -- pure-text translation tests (always run) -------------------------

    def test_semicolon_comment_stripped(self):
        line = "\t.word 1 ; trailing comment"
        self.assertEqual(s2g.strip_semicolon(line), "\t.word 1 ")

    def test_semicolon_in_string_preserved(self):
        # dwc_lobby_chat_data.s:2427's big printable-ASCII string.
        line = ('\t.asciz "ABC:;<=>?@[]^_`{|}~" ; 0x02256E04')
        self.assertEqual(s2g.strip_semicolon(line),
                         '\t.asciz "ABC:;<=>?@[]^_`{|}~" ')

    def test_escaped_quote_stays_in_string(self):
        line = r'.asciz "a\";b" ; comment'
        self.assertEqual(s2g.strip_semicolon(line), r'.asciz "a\";b" ')

    def test_escaped_backslash_before_quote(self):
        # "a\\" ends the string; the ; after it is a comment.
        line = r'.asciz "a\\" ; comment'
        self.assertEqual(s2g.strip_semicolon(line), r'.asciz "a\\" ')

    def test_public_becomes_global(self):
        out = s2g.gas_translate_line("\t.public mySym")
        self.assertEqual(out, ["\t.global mySym"])

    def test_type_at_becomes_percent(self):
        out = s2g.gas_translate_line("\t.type myFn, @function")
        self.assertEqual(out, ["\t.type myFn, %function"])
        out = s2g.gas_translate_line("\t.type myObj, @object")
        self.assertEqual(out, ["\t.type myObj, %object"])

    def test_word_becomes_4byte(self):
        out = s2g.gas_translate_line("\t.word 1, 2, sym")
        self.assertEqual(out, ["\t.4byte 1, 2, sym"])

    def test_bare_rodata(self):
        out = s2g.gas_translate_line("\t.rodata")
        self.assertEqual(out, ['\t.section .rodata,"a",%progbits',
                               "\t.balign 4"])

    def test_mwas_section_bss(self):
        out = s2g.gas_translate_line("\t.section .bss.foo,4,8,3")
        self.assertEqual(out, ['\t.section .bss.foo,"aw",%nobits',
                               "\t.balign 4"])

    def test_mwas_section_data(self):
        out = s2g.gas_translate_line("\t.section .data.foo,4,1,3")
        self.assertEqual(out, ['\t.section .data.foo,"aw",%progbits',
                               "\t.balign 4"])

    def test_mwas_section_rodata(self):
        out = s2g.gas_translate_line("\t.section .rodata.foo,4,1,2")
        self.assertEqual(out, ['\t.section .rodata.foo,"a",%progbits',
                               "\t.balign 4"])

    def test_mwas_section_exceptix(self):
        out = s2g.gas_translate_line("\t.section .exceptix,4,1,2")
        self.assertEqual(out, ['\t.section .exceptix,"a",%progbits',
                               "\t.balign 4"])

    def test_extern_kept(self):
        out = s2g.gas_translate_line("\t.extern someFn")
        self.assertEqual(out, ["\t.extern someFn"])

    def test_include_search_order(self):
        self.write("asm/dup.inc", ".global from_asm\n")
        self.write("lib/asm/include/dup.inc", ".global from_lib\n")
        self.write("t.s", '\t.include "dup.inc"\n')
        gas, _ = self.convert("t.s")
        self.assertIn(".global from_asm", gas)
        self.assertNotIn("from_lib", gas)

    def test_pragma_once_inlined_once(self):
        self.write("asm/include/once.inc",
                   "#pragma once\n\t.byte 0x11\n")
        self.write("t.s", '\t.include "once.inc"\n\t.include "once.inc"\n'
                          "lbl:\n\t.byte 0x22\n")
        gas, _ = self.convert("t.s")
        self.assertEqual(gas.count("0x11"), 1)
        self.assertIn("0x22", gas)

    def test_missing_dot_include_is_error(self):
        self.write("t.s", '\t.include "nope.inc"\n')
        rc = s2g.main(["--root", str(self.root),
                       str(self.root / "t.s"), "-o", "-"])
        # main() prints the error and returns 1.
        self.assertEqual(rc, 1)

    def test_missing_c_include_warns_and_skips(self):
        self.write("t.s", '#include "msgdata/msg/msg_9999.h"\n'
                          "\t.text\nlbl:\n\t.byte 1\n")
        gas, warnings = self.convert("t.s")
        self.assertEqual(len(warnings), 1)
        self.assertIn("msg_9999.h", warnings[0])
        self.assertNotIn("#include", gas)
        # Still assembles: the skipped header contributed no defines here.
        self.assemble_all(gas)

    # -- preprocessor / header tests --------------------------------------

    def test_c_header_with_enum_and_defines(self):
        self.write("include/consts.h",
                   "#ifndef CONSTS_H\n#define CONSTS_H\n"
                   "#define MAGIC_NUM 0x2A\n"
                   "typedef enum { A_VAL = 1, B_VAL = 2 } MyEnum;\n"
                   "typedef unsigned int u32;\n"
                   "typedef struct { u32 x; u32 y; } MyStruct;\n"
                   "extern u32 someDecl;\n"
                   "static inline u32 helper(void) { return 1; }\n"
                   "#endif\n")
        self.write("t.s", '#include "consts.h"\n'
                          "\t.rodata\nlbl:\n\t.word MAGIC_NUM\n")
        gas, warnings = self.convert("t.s")
        self.assertEqual(warnings, [])
        self.assertNotIn("typedef", gas)
        self.assertNotIn("MyEnum", gas)
        self.assertNotIn("someDecl", gas)
        for obj in self.assemble_all(gas):
            data = section_bytes(obj, ".rodata")
            self.assertEqual(data[:4], bytes([0x2A, 0x00, 0x00, 0x00]))

    def test_fs_extern_overlay_expands(self):
        self.write("lib/include/nitro/fs/overlay.h",
                   "#ifndef OVERLAY_H\n#define OVERLAY_H\n"
                   "#ifdef SDK_ASM\n"
                   "#define FS_EXTERN_OVERLAY(name) "
                   ".public SDK_OVERLAY_##name##_ID\n"
                   "#else\n"
                   "typedef unsigned int u32;\n"
                   "typedef u32 FSOverlayID;\n"
                   "#endif\n#endif\n")
        self.write("global.inc",
                   '#include <nitro/fs/overlay.h>\n'
                   "FS_EXTERN_OVERLAY(OVY_7)\n")
        self.write("t.s", '\t.include "global.inc"\n'
                          "\t.text\nlbl:\n\t.byte 0\n")
        gas, warnings = self.convert("t.s")
        self.assertEqual(warnings, [])
        self.assertIn(".global SDK_OVERLAY_OVY_7_ID", gas)
        self.assertNotIn("FSOverlayID", gas)

    def _overlay_fixture(self):
        self.write("lib/include/nitro/fs/overlay.h",
                   "#ifndef OVERLAY_H\n#define OVERLAY_H\n"
                   "#ifdef SDK_ASM\n"
                   "#define FS_EXTERN_OVERLAY(name) "
                   ".public SDK_OVERLAY_##name##_ID\n"
                   "#else\ntypedef unsigned int u32;\n"
                   "typedef u32 FSOverlayID;\n#endif\n#endif\n")
        self.write("global.inc",
                   '#include <nitro/fs/overlay.h>\n'
                   "FS_EXTERN_OVERLAY(OVY_7)\n")
        self.write("t.s", '\t.include "global.inc"\n'
                          "\t.text\nlbl:\n\t.4byte SDK_OVERLAY_OVY_7_ID\n")
        return self.write("ids.ld",
                          "/* generated */\nSDK_OVERLAY_field_ID = 1;\n"
                          "SDK_OVERLAY_OVY_7_ID = 7;\n")

    def test_load_overlay_ids_reads_the_linker_script(self):
        path = self._overlay_fixture()
        self.assertEqual(s2g.load_overlay_ids(path),
                         {"field": 1, "OVY_7": 7})

    def test_overlay_id_reference_becomes_a_number(self):
        path = self._overlay_fixture()
        gas, warnings = self.convert("t.s", overlay_ids=s2g.load_overlay_ids(path))
        self.assertEqual(warnings, [])
        self.assertIn(".4byte 7", gas)
        self.assertNotIn("SDK_OVERLAY_OVY_7_ID", gas)
        for obj in self.assemble_all(gas):
            undefined = subprocess.run(["nm", "-u", str(obj)],
                                       capture_output=True, text=True).stdout
            self.assertNotIn("SDK_OVERLAY", undefined)
            relocs = subprocess.run(["readelf", "-rW", str(obj)],
                                    capture_output=True, text=True).stdout
            self.assertNotIn("SDK_OVERLAY", relocs)

    def test_unknown_overlay_id_is_left_to_the_linker(self):
        path = self._overlay_fixture()
        gas, _ = self.convert("t.s", overlay_ids={"field": 1})
        self.assertIn(".global SDK_OVERLAY_OVY_7_ID", gas)
        self.assertIn(".4byte SDK_OVERLAY_OVY_7_ID", gas)

    # -- end-to-end assembly tests -----------------------------------------

    def test_data_file_roundtrip(self):
        src = (
            '\t.public gTable\n'
            '\t.public gBuf\n'
            '\t.extern extFn\n'
            '\t.rodata\n'
            'gTable:\n'
            '\t.word extFn, 0x12345678 ; pointer table\n'
            '\t.short 0x1234\n'
            '\t.byte 0x56\n'
            '\t.balign 4, 0\n'
            '\t.asciz "a;b@c" // trailing comment\n'
            '\t.section .bss.tbl,4,8,3\n'
            'gBuf:\n'
            '\t.space 0x10\n'
            '\t.size gBuf,.-gBuf\n'
        )
        self.write("t.s", src)
        gas, warnings = self.convert("t.s")
        self.assertEqual(warnings, [])
        self.assertIn('.section .rodata,"a",%progbits', gas)
        self.assertIn('.section .bss.tbl,"aw",%nobits', gas)
        for obj in self.assemble_all(gas):
            syms = nm(obj)
            self.assertIn("gTable", syms)
            self.assertIn("gBuf", syms)
            self.assertEqual(syms["gBuf"], ("B", 0x10))
            secs = readelf_sections(obj)
            self.assertEqual(secs[".rodata"]["flags"], "A")
            self.assertEqual(secs[".bss.tbl"]["flags"], "WA")
            self.assertEqual(secs[".bss.tbl"]["type"], "NOBITS")
            self.assertEqual(secs[".bss.tbl"]["align"], "4")
            # .word is 4 bytes on every target: 2 words + short + byte
            # padded to 4 = 12, then the string.
            data = section_bytes(obj, ".rodata")
            self.assertEqual(len(data), 12 + 6)
            self.assertEqual(data[4:8], bytes([0x78, 0x56, 0x34, 0x12]))
            self.assertEqual(data[8:12], bytes([0x34, 0x12, 0x56, 0x00]))
            self.assertEqual(data[12:], b"a;b@c\x00")

    def test_type_function_visible(self):
        self.write("t.s", "\t.text\n\t.type myFn, @function\n"
                          "\t.global myFn\nmyFn:\n\t.4byte 0\n"
                          '\t.size myFn, .-myFn\n')
        gas, _ = self.convert("t.s")
        for obj in self.assemble_all(gas):
            proc = subprocess.run(["readelf", "-s", "-W", str(obj)],
                                  capture_output=True, text=True)
            self.assertIn("FUNC", proc.stdout)
            self.assertIn("myFn", proc.stdout)

    def test_exception_macro_assembles(self):
        self.write("asm/macros.inc",
                   "\t.macro exception what, size, data\n"
                   "\t.section .exceptix,4,1,2\n"
                   "\t.balign 4, 0\n"
                   "\t.type ?exc$\\what, @object\n"
                   "\t.global ?exc$\\what\n"
                   "\t.size ?exc$\\what, 12\n"
                   "?exc$\\what:\n"
                   "\t.word \\what\n"
                   "\t.short \\size\n"
                   "\t.word \\data\n"
                   "\t.previous\n"
                   "\t.endm\n")
        self.write("t.s", '\t.include "asm/macros.inc"\n'
                          '\t.extern doThing\n'
                          '\t.text\n'
                          '\texception doThing, 0x0019, 0x00100000\n'
                          '\texception doOther, 0x0015, 0x00100000\n')
        gas, _ = self.convert("t.s")
        self.assertNotIn("?exc$", gas)
        self.assertIn(".Lexc_", gas)
        for obj in self.assemble_all(gas):
            secs = readelf_sections(obj)
            self.assertIn(".exceptix", secs)
            # 10-byte first entry + 2 pad + 12-byte second entry.
            self.assertEqual(secs[".exceptix"]["size"], 22)
            self.assertEqual(secs[".exceptix"]["flags"], "A")

    def test_output_o_direct(self):
        if not self.compilers:
            self.skipTest("no ELF-capable assembler available")
        name, cc, flags = self.compilers[0]
        self.write("t.s", "\t.text\nlbl:\n\t.4byte 7\n")
        out = str(Path(self.tmp.name) / "t.o")
        rc = s2g.main(["--root", str(self.root), str(self.root / "t.s"),
                       "-o", out, "--cc", cc, "--cflags"] + flags)
        self.assertEqual(rc, 0)
        self.assertTrue(Path(out).is_file())
        if self.have_binutils:
            self.assertIn("lbl", nm(Path(out)))

    def test_gas_output_to_file(self):
        self.write("t.s", "\t.public x\n\t.rodata\nx:\n\t.byte 1\n")
        out = Path(self.tmp.name) / "t_gas.s"
        rc = s2g.main(["--root", str(self.root), str(self.root / "t.s"),
                       "-o", str(out)])
        self.assertEqual(rc, 0)
        text = out.read_text(encoding="utf-8")
        self.assertIn(".global x", text)
        self.assertIn('.section .rodata,"a",%progbits', text)


if __name__ == "__main__":
    unittest.main()
