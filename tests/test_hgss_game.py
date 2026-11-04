"""Unit tests for tools/hgss_game.py.

Synthetic source lists, flags and directories only: no game data, no
compiler run, no bootstrapped checkout required.
"""
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from tools import hgss_game as hg  # noqa: E402
from tools import stage_3ds as st  # noqa: E402

REPO = Path(__file__).resolve().parent.parent

# A .3dsx with no segments and no relocations: what a fake 3dsxtool writes so
# the packaging step's validation has something sound to accept.
MINIMAL_3DSX = (struct.pack("<IHHIIIIII", 0x58534433, 0x20, 8, 0, 0, 0, 0, 0, 0)
                + bytes(24))


def fake_3dsx_tool_script(payload: bytes) -> str:
    """A stand-in 3dsxtool that writes `payload` to argv[2]."""
    return ("#!/usr/bin/env python3\n"
            "import pathlib, sys\n"
            f"pathlib.Path(sys.argv[2]).write_bytes({payload!r})\n")


def one_word_3dsx(code_word: int) -> bytes:
    """A .3dsx whose code segment is one word, marked as an absolute reloc."""
    header = struct.pack("<IHHIIIIII", 0x58534433, 0x20, 8, 0, 0, 4, 0, 0, 0)
    heads = struct.pack("<II", 1, 0) + struct.pack("<II", 0, 0) + struct.pack("<II", 0, 0)
    return header + heads + struct.pack("<I", code_word) + struct.pack("<HH", 0, 1)

DATA = {
    "c_sources": ["src/main.c", "src/battle/foo.c"],
    "cpp_sources": ["src/lobby/ppw_lobby.cpp"],
    "asm_sources": ["asm/foo_data.s", "lib/asm/spl_data.s"],
    "port_lib_sources": [
        "lib/src/nitrocrypto_bn.c",
        "lib/syscall/src/syscall.c",
        "lib/NitroSDK/src/os/os_thread.c",
        "lib/NitroSystem/src/g3d_gecom.c",
        "lib/MSL_C/src/msl_mem.c",
        "lib/NitroDWC/src/dwc_main.c",
        "lib/dsprot/src/dsprot_main.c",
    ],
    "overlay_map": [
        {"kind": "Static", "name": "main", "objects": [
            {"object": "src/main.o"}, {"object": "src/battle/foo.o"},
            {"object": "lib/NitroSDK/src/os/os_thread.o"},
            {"object": "lib/NitroDWC/src/dwc_main.o"},
            {"object": "lib/MSL_C/asm/MSL_Common_errno.o"},
        ]},
        {"kind": "Overlay", "name": "field", "objects": [
            {"object": "src/field/unlinked.o"},
            {"object": "lib/NitroDWC/asm/dwc_gamespy_bss.o"},
        ]},
    ],
}


class FlagTests(unittest.TestCase):
    REQUIRED = ["-std=gnu99", "-fgnu89-inline", "-fno-strict-aliasing",
                "-fno-short-enums", "-fsigned-char", "-fwrapv",
                "-fno-aggressive-loop-optimizations",
                "-fno-delete-null-pointer-checks", "-DSDK_PORT_3DS",
                "-Wall", "-Wuninitialized", "-Wmaybe-uninitialized"]

    def test_host32_flags(self):
        flags = hg.port_cflags("host32")
        for want in self.REQUIRED:
            self.assertIn(want, flags)
        self.assertIn("-m32", flags)
        # The DS build keeps asserts; several GF_ASSERT operands have side
        # effects (the Oak's-speech transfer tasks, for one).
        self.assertIn("-DPM_KEEP_ASSERTS", flags)
        self.assertIn("-malign-double", flags)
        self.assertNotIn("-march=armv6k", flags)
        self.assertNotIn("-fno-exceptions", flags)

    def test_3ds_flags(self):
        flags = hg.port_cflags("3ds")
        for want in ("-march=armv6k", "-mtune=mpcore", "-mfpu=vfp",
                     "-mfloat-abi=hard"):
            self.assertIn(want, flags)
        self.assertNotIn("-m32", flags)

    def test_cxx_flags(self):
        flags = hg.port_cflags("host32", cxx=True)
        self.assertIn("-std=gnu++98", flags)
        self.assertNotIn("-std=gnu99", flags)
        self.assertIn("-fno-exceptions", flags)
        self.assertIn("-fno-rtti", flags)

    def test_unknown_target(self):
        with self.assertRaises(ValueError):
            hg.port_cflags("arm7")


class IncludeTests(unittest.TestCase):
    def test_game_includes(self):
        root = Path("/checkout")
        flags = hg.hgss_includes(root, Path("/shim"), Path("/out/gen"))
        self.assertIn("-I/checkout/include", flags)
        self.assertIn("-I/checkout/include/library", flags)
        self.assertIn("-I/checkout/files", flags)
        self.assertIn("-I/checkout/files/msgdata", flags)
        self.assertIn("-idirafter/checkout/lib/include", flags)
        self.assertIn(f"-I{REPO / 'hgss' / 'compat' / 'include'}", flags)
        self.assertIn(f"-I{REPO / '3ds_port' / 'nitro' / 'include'}", flags)
        self.assertIn(f"-I{REPO / '3ds_port' / 'pak' / 'include'}", flags)
        self.assertIn("-I/out/gen", flags)

    def test_lib_include_is_after_system_headers(self):
        # HGSS's lib/include has Metrowerks stdarg.h/stddef.h/string.h; only
        # -idirafter lets the system headers win while dwc/... still resolves.
        flags = hg.hgss_includes(Path("/c"), Path("/s"), Path("/g"))
        self.assertNotIn("-I/c/lib/include", flags)
        self.assertIn("-idirafter/c/lib/include", flags)


class SourceTests(unittest.TestCase):
    def test_selects_game_and_only_unsupplied_libs(self):
        got = hg.select_sources(DATA)
        self.assertEqual(got["c"], ["lib/src/nitrocrypto_bn.c",
                                    "lib/syscall/src/syscall.c",
                                    "src/battle/foo.c", "src/main.c"])
        self.assertEqual(got["cpp"], ["src/lobby/ppw_lobby.cpp"])
        self.assertEqual(got["asm"], ["asm/foo_data.s", "lib/asm/spl_data.s"])

    def test_object_path_keeps_the_object_name(self):
        out = Path("/out")
        self.assertEqual(hg.object_path(out, "src/field/map.c"),
                         out / "obj" / "src" / "field" / "map.o")
        self.assertEqual(hg.object_path(out, "asm/foo_data.s"),
                         out / "obj" / "asm" / "foo_data.o")
        self.assertEqual(hg.object_path(out, "src/lobby/a.cpp"),
                         out / "obj" / "src" / "lobby" / "a.o")


class LinkSelectionTests(unittest.TestCase):
    def test_lsf_object_set(self):
        self.assertEqual(hg.lsf_object_set(DATA), {
            "src/main.o", "src/battle/foo.o", "src/field/unlinked.o",
            "lib/NitroSDK/src/os/os_thread.o",
            "lib/NitroDWC/src/dwc_main.o",
            "lib/MSL_C/asm/MSL_Common_errno.o",
            "lib/NitroDWC/asm/dwc_gamespy_bss.o"})

    def test_link_objects_keeps_lsf_objects_but_drops_replaced_libraries(self):
        out = Path("/out")
        candidates = [out / "obj" / rel for rel in (
            "src/main.o", "src/battle/foo.o", "src/not_in_lsf.o",
            "src/field/unlinked.o",
            "lib/NitroSDK/src/os/os_thread.o",
            "lib/NitroDWC/src/dwc_main.o",
            "lib/NitroDWC/asm/dwc_gamespy_bss.o",
            "lib/MSL_C/asm/MSL_Common_errno.o")]
        got = [p.relative_to(out / "obj").as_posix()
               for p in hg.link_objects(candidates, DATA, out)]
        self.assertEqual(got, ["src/main.o", "src/battle/foo.o",
                               "src/field/unlinked.o",
                               "lib/NitroSDK/src/os/os_thread.o"])

    def test_link_objects_keeps_the_nitrodwc_data_but_not_its_code(self):
        # NitroDWC asm objects are pure data the game reads, except the
        # libc-state blobs that reference a non-TLS errno; DWC functions
        # (lib/NitroDWC/src) stay stubbed and unlinked.
        data = {"overlay_map": [{"objects": [
            {"object": "lib/NitroDWC/asm/dwc_lobby_chat_data.o"},
            {"object": "lib/NitroDWC/asm/wifi_setup_ui_system_bss.o"},
            {"object": "lib/NitroDWC/asm/dwc_gamespy_bss.o"},
            {"object": "lib/NitroDWC/src/dwc_main.o"}]}]}
        out = Path("/out")
        rels = ("lib/NitroDWC/asm/dwc_lobby_chat_data.o",
                "lib/NitroDWC/asm/wifi_setup_ui_system_bss.o",
                "lib/NitroDWC/asm/dwc_gamespy_bss.o",
                "lib/NitroDWC/src/dwc_main.o")
        got = [p.relative_to(out / "obj").as_posix()
               for p in hg.link_objects([out / "obj" / r for r in rels], data, out)]
        self.assertEqual(got, list(rels[:2]))


class AppSourceTests(unittest.TestCase):
    def test_host_program_uses_the_pc_main(self):
        names = [p.name for p in hg.app_sources("host32")]
        self.assertEqual(names[0], "main_pc.c")
        self.assertNotIn("main_3ds.c", names)
        self.assertIn("heartgold.c", names)

    def test_3ds_program_uses_the_libctru_main(self):
        sources = hg.app_sources("3ds")
        names = [p.name for p in sources]
        self.assertEqual(names[0], "main_3ds.c")
        self.assertNotIn("main_pc.c", names)
        self.assertIn("heartgold.c", names)

    def test_program_carries_the_runtime_replacements(self):
        # These are program objects, not archive members: the MSL helpers the
        # host libc lacks, the lobby's small C++ runtime, and MB stand-ins.
        for target in ("host32", "3ds"):
            names = [p.name for p in hg.app_sources(target)]
            self.assertIn("msl_runtime.c", names)
            self.assertIn("cpp_runtime.c", names)
            self.assertIn("mb_stubs.c", names)

    def test_msl_runtime_lives_in_compat_but_is_an_app_object(self):
        paths = hg.app_sources("host32")
        msl = [p for p in paths if p.name == "msl_runtime.c"][0]
        self.assertEqual(msl, REPO / "hgss" / "compat" / "src" / "msl_runtime.c")

    def test_unknown_target(self):
        with self.assertRaises(ValueError):
            hg.app_sources("arm7")

    def test_host_program_has_the_trace_wrappers(self):
        names = [p.name for p in hg.app_sources("host32")]
        self.assertIn("pc_trace.c", names)
        self.assertNotIn("pc_trace.c", [p.name for p in hg.app_sources("3ds")])

    def test_3ds_program_has_the_script_parser(self):
        names = [p.name for p in hg.app_sources("3ds")]
        self.assertIn("3ds_input.c", names)
        self.assertNotIn("3ds_input.c", [p.name for p in hg.app_sources("host32")])

    def test_3ds_program_has_the_pica_backend(self):
        """The 3D layer needs a GPU worker; the backend is program-only.

        port_gx3d_pica.c draws the DS's polygons; port_compose_pica.c puts
        the 2D layers and 3D together at the 3DS screen's resolution.
        port_pica_abi.c is linked with them: it exposes libctru's struct
        layout to the port (the port is built with -fno-short-enums, libctru
        is not).
        """
        names = [p.name for p in hg.app_sources("3ds")]
        self.assertIn("port_gx3d_pica.c", names)
        self.assertIn("port_compose_pica.c", names)
        self.assertIn("port_pica_abi.c", names)
        self.assertNotIn("pica_classic_stubs.c", names)
        self.assertNotIn("pica_scene.c", names)
        for name in ("port_gx3d_pica.c", "port_compose_pica.c", "port_pica_abi.c"):
            self.assertNotIn(name, [p.name for p in hg.app_sources("host32")])

    def test_shader_sources_are_in_dvle_order(self):
        sources = hg.gx3d_shader_sources()
        self.assertEqual([p.name for p in sources], list(hg.GX3D_SHADERS))
        for source in sources:
            self.assertTrue(source.is_file(), source)


class SanitizerFlagTests(unittest.TestCase):
    def tearDown(self):
        hg.SANITIZE = False

    def test_sanitize_instruments_host_only(self):
        hg.SANITIZE = True
        flags = hg.port_cflags("host32")
        self.assertIn("-fsanitize=address,undefined", flags)
        self.assertIn("-fno-omit-frame-pointer", flags)
        # The 3DS target must not see host sanitizer flags.
        self.assertNotIn("-fsanitize=address,undefined", hg.port_cflags("3ds"))

    def test_sanitize_off_by_default(self):
        hg.SANITIZE = False
        self.assertNotIn("-fsanitize=address,undefined", hg.port_cflags("host32"))


class ObjectAliasTests(unittest.TestCase):
    """The missing decomp build stage: object_aliases.py after compiling."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)

    def test_alias_map_path_is_the_source_sibling(self):
        # common.mk looks for <source without extension>.alias-map.json.
        self.assertEqual(hg.alias_map_path(Path("src/lobby/ppw_lobby.cpp")),
                         Path("src/lobby/ppw_lobby.alias-map.json"))
        self.assertEqual(
            hg.alias_map_path(Path("src/application/pokewalker/pokewalker_ir_requests.c")),
            Path("src/application/pokewalker/pokewalker_ir_requests.alias-map.json"))
        self.assertEqual(hg.alias_map_path(Path("asm/foo.s")),
                         Path("asm/foo.alias-map.json"))

    def test_object_alias_command_uses_the_checkout_tool(self):
        cmd = hg.object_alias_command(
            Path("/checkout"), Path("/out/obj/src/foo.o"),
            Path("/checkout/src/foo.alias-map.json"))
        self.assertEqual(cmd[0], sys.executable)
        self.assertEqual(cmd[1], "/checkout/tools/object_aliases.py")
        self.assertEqual(cmd[2], "/out/obj/src/foo.o")
        self.assertEqual(cmd[3], "/checkout/src/foo.alias-map.json")

    def test_alias_environment_puts_the_target_tools_on_path(self):
        env = hg.alias_tool_environment(self.base, {"objcopy": "objcopy",
                                                   "readelf": "readelf"})
        directory = hg.alias_tool_dir(self.base)
        self.assertEqual(directory, self.base / "gen" / "alias-tools")
        self.assertTrue(env["PATH"].startswith(str(directory) + os.pathsep))
        # object_aliases.py looks these exact names up; they are this
        # target's tools, since arm-none-eabi-objcopy rejects an x86 ELF.
        for name, tool in (("arm-none-eabi-objcopy", "objcopy"),
                           ("arm-none-eabi-readelf", "readelf")):
            link = directory / name
            self.assertTrue(link.is_symlink())
            self.assertEqual(link.resolve(), Path(shutil.which(tool)).resolve())

    def test_missing_map_is_not_a_failure_and_runs_nothing(self):
        src = self.base / "src" / "plain.c"
        src.parent.mkdir(parents=True)
        src.write_text("int x;\n")
        ok, text = hg.apply_object_aliases(Path("/checkout"), src,
                                           self.base / "plain.o", env={})
        self.assertTrue(ok)
        self.assertEqual(text, "")

    def test_prepared_map_drops_runtime_aliases_and_fixes_func_sizes(self):
        # GCC's function sizes differ from MWCC's pinned sizes; the alias
        # offsets are what matter, so the FUNC owner's size is replaced with
        # the compiled size. Entries cpp_runtime.c already defines are
        # dropped. OBJECT (BSS) sizes are never adjusted.
        src = self.base / "owner.c"
        out = self.base / "out"
        obj = out / "obj" / "owner.o"
        obj.parent.mkdir(parents=True)
        src.write_text("void owner_fn(void) { }\n")
        cc = os.environ.get("HGSS_HOST_CC", "gcc-10")
        subprocess.run([cc, "-m32", "-c", str(src), "-o", str(obj)],
                       check=True, capture_output=True)
        alias_map = self.base / "owner.alias-map.json"
        alias_map.write_text(json.dumps([
            {"section": ".text",
             "owner": {"name": "owner_fn", "size": "999", "type": "FUNC"},
             "aliases": [{"name": "alias_fn", "offset": 0}]},
            {"section": ".text",
             "owner": {"name": "owner_fn", "size": "999", "type": "FUNC"},
             "aliases": [{"name": "_ZN5BytesD1Ev", "offset": 0}]},
        ]))
        prepared = hg.prepared_alias_map(obj, alias_map, out, "nm", "readelf")
        self.assertIsNotNone(prepared)
        self.assertNotEqual(prepared, alias_map)
        configs = json.loads(Path(prepared).read_text())
        self.assertEqual([c["owner"]["name"] for c in configs], ["owner_fn"])
        self.assertEqual([a["name"] for a in configs[0]["aliases"]], ["alias_fn"])
        actual = hg.defined_symbol_size("nm", obj, "owner_fn")
        self.assertEqual(configs[0]["owner"]["size"], actual)

    def test_prepared_map_translates_to_data_section_layout(self):
        # The harness compiles with -fdata-sections, so the owner sits in
        # .bss.<name> at offset 0. The map's .bss-relative offsets must be
        # shifted by the same delta, keeping the aliases at the same distance
        # inside the owner (the five gPokewalkerIr* labels).
        src = self.base / "data.c"
        out = self.base / "out"
        obj = out / "obj" / "data.o"
        obj.parent.mkdir(parents=True)
        src.write_text("unsigned char owner[0x20];\n")
        cc = os.environ.get("HGSS_HOST_CC", "gcc-10")
        subprocess.run([cc, "-m32", "-fdata-sections", "-c", str(src), "-o", str(obj)],
                       check=True, capture_output=True)
        alias_map = self.base / "data.alias-map.json"
        alias_map.write_text(json.dumps(
            {"section": ".bss",
             "owner": {"name": "owner", "offset": "0x10", "size": "0x10",
                       "type": "OBJECT"},
             "aliases": [{"name": "inside_a", "offset": "0x14"},
                         {"name": "inside_b", "offset": "0x18"}]}))
        prepared = hg.prepared_alias_map(obj, alias_map, out, "nm", "readelf")
        config = json.loads(Path(prepared).read_text())[0]
        self.assertEqual(config["section"], ".bss.owner")
        self.assertEqual(config["owner"]["offset"], 0)
        self.assertEqual([a["offset"] for a in config["aliases"]], [0x04, 0x08])

    def test_prepared_map_with_only_runtime_aliases_is_skipped(self):
        src = self.base / "only.c"
        obj = self.base / "obj" / "only.o"
        obj.parent.mkdir(parents=True)
        src.write_text("void owner_fn(void) { }\n")
        cc = os.environ.get("HGSS_HOST_CC", "gcc-10")
        subprocess.run([cc, "-m32", "-c", str(src), "-o", str(obj)],
                       check=True, capture_output=True)
        alias_map = self.base / "only.alias-map.json"
        alias_map.write_text(json.dumps(
            {"section": ".text",
             "owner": {"name": "owner_fn", "size": "1", "type": "FUNC"},
             "aliases": [{"name": "_ZN5DequeD1Ev", "offset": 0}]}))
        out = self.base / "out"
        (out / "obj").mkdir(parents=True)
        self.assertIsNone(hg.prepared_alias_map(obj, alias_map, out, "nm", "readelf"))


class CommandTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)

    def test_compile_command(self):
        cmd = hg.compile_command("gcc-10", ["-m32", "-DX"], Path("src/a.c"),
                                 Path("out/a.o"), "c")
        self.assertEqual(cmd, ["gcc-10", "-m32", "-DX", "-x", "c", "-c",
                               "src/a.c", "-o", "out/a.o"])

    def test_asm_command(self):
        cmd = hg.asm_command(Path("up"), Path("up/asm/a.s"), Path("o/a.o"),
                             "gcc-10", ["-m32", "-Iup/include"])
        self.assertIn(str(REPO / "tools" / "s_to_gas.py"), cmd)
        self.assertIn("--root", cmd)
        self.assertIn("--cc", cmd)
        self.assertIn("--cflags", cmd)
        self.assertIn("-Iup/include", cmd)

    def test_asm_command_resolves_overlay_ids_before_cflags(self):
        cmd = hg.asm_command(Path("/up"), Path("/up/asm/x.s"),
                             Path("/out/x.o"), "cc", ["-m32"],
                             Path("/out/gen/overlay_ids.ld"))
        self.assertEqual(cmd[cmd.index("--overlay-ids") + 1],
                         "/out/gen/overlay_ids.ld")
        # s_to_gas' --cflags is an argparse REMAINDER, so the option has to
        # come before it or it would be passed to the assembler.
        self.assertLess(cmd.index("--overlay-ids"), cmd.index("--cflags"))

    def test_asm_flags_have_no_c_standard(self):
        # include_flags() calls make_shim(), which creates symlinks under
        # hgss_nitro.BUILD; keep them in a temp tree so a test run never
        # re-points the real build/hgss-sdk at this fake checkout.
        with tempfile.TemporaryDirectory() as tmp:
            build = hg.nitro.BUILD
            hg.nitro.BUILD = Path(tmp)
            try:
                flags = hg.asm_flags("host32", Path("/up"), Path("/out"))
            finally:
                hg.nitro.BUILD = build
        self.assertNotIn("-std=gnu99", flags)
        self.assertIn("-DSDK_PORT_3DS", flags)
        self.assertIn("-m32", flags)

    def test_prepare_writes_overlay_ids_into_out(self):
        cmd = hg.prepare_command(Path("/up"), Path("/out"))
        self.assertIn("--root", cmd)
        self.assertIn("/up", cmd)
        self.assertIn("/out/gen/overlay_ids.ld", cmd)
        self.assertIn("/out/gen/overlay_ids.h", cmd)

    def test_overlay_command(self):
        cmd = hg.overlay_command(Path("/up"), Path("/out"), "objcopy")
        self.assertIn("--objdir", cmd)
        self.assertIn("/out/obj", cmd)
        self.assertIn("--objcopy", cmd)
        self.assertIn("objcopy", cmd)
        self.assertIn("/out/gen/overlay_table.c", cmd)
        self.assertIn("/out/gen/overlay_ids.ld", cmd)

    def test_wifi_stub_command(self):
        root = self.base / "up"
        (root / "lib/NitroDWC/include").mkdir(parents=True)
        (root / "lib/include").mkdir(parents=True)
        objects = [self.base / "a.o", self.base / "b.o"]
        cmd = hg.wifi_stub_command("nm", root, objects, self.base / "w.c")
        self.assertIn("--nm", cmd)
        self.assertIn("--headers", cmd)
        self.assertIn("--sources", cmd)
        # everything after -- is the object list
        self.assertEqual(cmd[cmd.index("--") + 1:], [str(o) for o in objects])
        self.assertIn(str(root / "lib/NitroDWC/include"), cmd)
        self.assertNotIn(str(root / "lib/NitroWiFi/include"), cmd)  # absent


class NnsVolumeTableTests(unittest.TestCase):
    """NNS's _0210D750 must be the real volume->decibel table.

    The decomp emits it as an empty alias section; its DS link order makes it
    share SNDi_DecibelTable's data, but the port's default order left it on an
    unrelated pointer table. NNS then read pointers as decibels: a large
    positive fader pinned every channel at 0 dB and the release stop
    (volume <= -723) never fired, so a note sustained until the next key-on.
    """

    def port_values(self):
        text = hg.NNS_VOLUME_PORT_SOURCE.read_text()
        body = text[text.index("_0210D750[0x100] = {"):]
        body = body[body.index("{") + 1:body.index("};")]
        return [int(v) for v in re.findall(r"-?\d+", body)]

    def sdk_values(self):
        sdk = hg.REPO / "build" / "upstream" / "lib" / "NitroSDK" / "src" / "snd" / "SND_util.c"
        if not sdk.is_file():
            self.skipTest("run tools/bootstrap.py first")
        text = sdk.read_text()
        body = text[text.index("SNDi_DecibelTable[0x80] = {"):]
        body = body[body.index("{") + 1:body.index("};")]
        return [int(v) for v in re.findall(r"-?\d+", body)]

    def test_has_the_256_entry_table(self):
        values = self.port_values()
        self.assertEqual(len(values), 256)
        self.assertEqual(values[0], -32768)     # 0 = silence
        self.assertEqual(values[127], 0)        # 127 = 0 dB
        self.assertTrue(all(a <= b for a, b in zip(values, values[1:])))

    def test_size_and_field_fader_entry_regression(self):
        # Field code parks the fader at 128 (GF_SndHandleMoveVolume(..., 128, 15)),
        # so snd_player.c reads entry 128 on every field BGM channel. The port
        # table once stopped at 127; the read past it hit unrelated rodata.
        self.assertEqual(hg.NNS_VOLUME_TABLE_SIZE, 0x200)
        self.assertEqual(hg.NNS_VOLUME_FADER_INDEX, 128)
        values = self.port_values()
        self.assertEqual(len(values) * 2, hg.NNS_VOLUME_TABLE_SIZE)
        self.assertEqual(values[hg.NNS_VOLUME_FADER_INDEX], 0)
        self.assertEqual(values[128:132], [0, 59, 118, 178])  # DS .rodata.unified_01

    def test_matches_the_nitrosdk_table(self):
        self.assertEqual(self.port_values()[:128], self.sdk_values())

    def tail_values(self):
        """Entries 128..255 from the DS `.rodata.unified_01` bytes in nitro_sdk_data.s.

        That section follows the empty `.rodata.alias_d750` section that
        _0210D750 names; its first 256 bytes, read little-endian s16, are the tail.
        """
        asm = hg.REPO / "build" / "upstream" / hg.NNS_VOLUME_SDK_SOURCE
        if not asm.is_file():
            self.skipTest("run tools/bootstrap.py first")
        data = bytearray()
        in_section = False
        for line in asm.read_text().splitlines():
            directive = line.strip()
            if directive.startswith(".section"):
                if in_section:
                    break
                in_section = directive.startswith(".section .rodata.unified_01")
                continue
            if in_section and directive.startswith(".byte"):
                data += bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{1,2})", directive))
                if len(data) >= 256:
                    break
        self.assertGreaterEqual(len(data), 256, "unified_01 section shorter than 256 bytes")
        return list(struct.unpack("<128h", bytes(data[:256])))

    def test_matches_the_nitrosdk_tail(self):
        # Index 128 is the field fader's entry (0 dB); 128..255 come from the same
        # DS bytes, so the port must carry all 128 values, not just index 128.
        self.assertEqual(self.port_values()[128:], self.tail_values())

    def test_port_programs_link_the_table(self):
        for target in ("host32", "3ds"):
            self.assertIn(hg.NNS_VOLUME_PORT_SOURCE, hg.app_sources(target))
        self.assertEqual(hg.NNS_VOLUME_TABLE_SIZE, 0x200)

    def link_table(self, values):
        """Link a program that defines _0210D750 with `values`; (nm, readelf, path)."""
        if not sys.platform.startswith("linux"):
            self.skipTest("needs GNU nm and readelf on a linux host")
        cc, nm, readelf = (shutil.which(t) for t in ("cc", "nm", "readelf"))
        if not (cc and nm and readelf):
            self.skipTest("needs cc, nm and readelf")
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        src = Path(tmp.name) / "table.c"
        src.write_text("const short _0210D750[%d] = {%s};\n"
                       "int main(void) { return 0; }\n"
                       % (len(values), ", ".join(str(v) for v in values)))
        binary = Path(tmp.name) / "table"
        subprocess.run([cc, "-o", str(binary), str(src)], check=True)
        return nm, readelf, binary

    def test_checker_accepts_the_full_table(self):
        nm, readelf, binary = self.link_table(self.port_values())
        ok, text = hg.check_nns_volume_table(nm, readelf, binary)
        self.assertTrue(ok, text)

    def test_checker_rejects_the_old_128_entry_table(self):
        nm, readelf, binary = self.link_table(self.port_values()[:128])
        ok, text = hg.check_nns_volume_table(nm, readelf, binary)
        self.assertFalse(ok)
        self.assertIn("is 256 bytes, want 512", text)

    def test_checker_rejects_a_nonzero_field_fader_entry(self):
        values = self.port_values()
        values[hg.NNS_VOLUME_FADER_INDEX] = 59
        nm, readelf, binary = self.link_table(values)
        ok, text = hg.check_nns_volume_table(nm, readelf, binary)
        self.assertFalse(ok)
        self.assertIn("[128] is 59, want 0", text)

    def test_sdk_alias_section_is_the_one_dropped(self):
        self.assertEqual(hg.NNS_VOLUME_ALIAS_SECTION, ".rodata.alias_d750")
        self.assertEqual(hg.NNS_VOLUME_SDK_SOURCE, "lib/asm/nitro_sdk_data.s")


class LinkTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.out = Path(self.tmp.name) / "out"
        self.out.mkdir()

    def make_link(self, target):
        rsp = self.out / "link.rsp"
        return hg.link_command(
            "gcc-10", target,
            [self.out / "app" / "main_pc.o", self.out / "app" / "heartgold.o"],
            [self.out / "obj" / "src" / "main.o", self.out / "obj" / "asm" / "a.o"],
            [self.out / "app" / "wm_stubs.o"],
            self.out / "gen" / "overlay_table.o",
            self.out / "gen" / "overlay_ids.ld",
            [self.out / "sdk" / "libnitrosystem.a", self.out / "sdk" / "libnitro.a"],
            self.out / "pokeheartgold-pc", rsp), rsp

    def test_host_link(self):
        cmd, rsp = self.make_link("host32")
        self.assertEqual(cmd[0], "gcc-10")
        self.assertIn("-m32", cmd)
        self.assertIn("-no-pie", cmd)
        self.assertIn("-pthread", cmd)
        self.assertIn("-lm", cmd)
        self.assertIn("-Wl,--gc-sections", cmd)
        self.assertIn("-Wl,--wrap=ErrorHandling_AssertFail", cmd)
        self.assertIn("-Wl,--start-group", cmd)
        self.assertIn("-Wl,--end-group", cmd)
        self.assertIn(str(self.out / "gen" / "overlay_ids.ld"), cmd)
        # The response file holds app, then game, then stub objects.
        lines = rsp.read_text().split()
        self.assertEqual(lines[:4], [
            str(self.out / "app" / "main_pc.o"),
            str(self.out / "app" / "heartgold.o"),
            str(self.out / "obj" / "src" / "main.o"),
            str(self.out / "obj" / "asm" / "a.o")])
        self.assertEqual(lines[-1], str(self.out / "app" / "wm_stubs.o"))
        # The overlay table is an input after the response file, not in it.
        self.assertLess(cmd.index(f"@{rsp}"), cmd.index(str(self.out / "gen" / "overlay_table.o")))

    def test_3ds_link_uses_the_libctru_program(self):
        cmd, _ = self.make_link("3ds")
        self.assertIn("-march=armv6k", cmd)
        self.assertNotIn("-m32", cmd)
        self.assertIn("-specs=3dsx.specs", cmd)
        self.assertNotIn("-nostartfiles", cmd)
        self.assertIn("-lcitro3d", cmd)
        self.assertIn("-lctru", cmd)
        self.assertIn("-Wl,--undefined=Heartgold_Run", cmd)
        self.assertNotIn("-pthread", cmd)
        self.assertTrue(any(arg.endswith("libctru/lib") for arg in cmd))


class OverlayCheckTests(unittest.TestCase):
    """The --check-overlays code path: the flag, the double load and the
    summary. The binary itself cannot link until the game-symbol queue is
    empty, so these check the wiring in the program's sources."""

    APP = REPO / "hgss" / "game" / "app"

    def test_pc_has_the_flag_before_the_runner(self):
        text = (self.APP / "main_pc.c").read_text()
        self.assertIn('"--check-overlays"', text)
        self.assertIn("Heartgold_CheckOverlays()", text)
        self.assertLess(text.index("--check-overlays"),
                        text.index("Heartgold_Run(&settings)"))

    def test_check_loads_each_overlay_twice_and_reports_the_table(self):
        text = (self.APP / "heartgold.c").read_text()
        self.assertIn("for (pass = 0; pass < 2; pass++)", text)
        self.assertIn("FS_LoadOverlay(", text)
        self.assertIn("FS_UnloadOverlay(", text)
        self.assertIn("NitroPort_IsOverlayLoaded", text)
        self.assertIn("overlays (%d with static initializers), %d failed", text)

    def test_3ds_shell_shows_the_pak_error_on_screen(self):
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("consoleClear();", text)
        self.assertIn('printf("\\n " HEARTGOLD_APP_NAME', text)
        self.assertIn("Press START to exit", text)
        # The error is logged when the open fails, and printed on the console
        # after consoleClear(); the screen text must be the latter.
        screen = text.index("Can't start: %s.", text.index("consoleClear();"))
        self.assertLess(screen, text.index("Press START to exit"))

    def test_only_the_game_thread_polls_apt(self):
        """aptMainLoop is per frame, on one thread (libctru's contract).

        The main thread polling it while the game was still loading overlays
        returned false and stopped the program at frame 0 on hardware.
        """
        def code(text):
            return "\n".join(line for line in text.splitlines()
                             if not line.strip().startswith(("*", "/*", "//")))

        text = (self.APP / "main_3ds.c").read_text()
        wait = text[text.index("static void WaitForGame"):]
        wait = code(wait[:wait.index("\n}")])
        self.assertNotIn("aptMainLoop", wait)
        # The thread's own flag decides it ended, not threadJoin's result.
        self.assertIn("sGameThreadAlive", wait)
        frame = text[text.index("void NitroPort_OnFrame"):]
        frame = code(frame[:frame.index("\n}")])
        self.assertIn("aptMainLoop", frame)
        self.assertIn("threadExit(0)", frame)

    def test_shell_hooks_are_strong_references(self):
        """Port hooks must not be weak.

        A weak undefined reference does not make the linker extract the
        definition from libnitro.a: it binds to NULL, the call is skipped, and
        the 3DS shell drew nothing for 39,000 frames that way.
        """
        text = (self.APP / "main_3ds.c").read_text()
        for hook in ("NitroPort_CardFlush", "NitroPort_SetButtons",
                     "NitroPort_SetTouch", "NitroPort_EngineAOnTop",
                     "NitroPort_SoundRender", "NitroPort_SoundPumpReplies",
                     "NitroPort_HBlankLinesOnce"):
            declarations = [line for line in text.splitlines()
                            if line.startswith(("void ", "int ")) and hook + "(" in line]
            self.assertTrue(declarations, f"{hook} is not declared")
            for line in declarations:
                self.assertNotIn("weak", line, hook)

    def test_shell_enables_the_new_3ds_speedup(self):
        """Without it the app runs at 268 MHz with the L2 off.

        The port's software 2D compositor is memory-bound and measured
        65-97 ms a frame that way on hardware.
        """
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("osSetSpeedupEnable(true)", text)
        self.assertIn("PTMSYSM_ConfigureNew3DSCPU", text)
        self.assertLess(text.index("osSetSpeedupEnable"), text.index("gfxInitDefault()"))

    def test_shell_feeds_the_sound_mixer_to_ndsp(self):
        """The DS mixer has to reach NDSP, or the game is silent.

        NitroPort_SoundRender mixes 32768 Hz stereo; the shell renders it into
        NDSP wave buffers on its own thread and pumps the ARM7's alarm replies
        on the game thread.
        """
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("NitroPort_SoundRender(", text)
        self.assertIn("ndspChnWaveBufAdd(", text)
        self.assertIn("NitroPort_SoundPumpReplies()", text)
        # The DSP reads the blocks with its own DMA, which does not see the
        # ARM11's write-back L1: libctru never flushes, so the shell must, or
        # the DSP plays stale FCRAM -- the music, only scratchy.
        self.assertIn("DSP_FlushDataCache(", text)
        self.assertLess(text.index("NitroPort_SoundRender(sAudioData"),
                        text.index("DSP_FlushDataCache("))
        self.assertLess(text.index("DSP_FlushDataCache("),
                        text.index("ndspChnWaveBufAdd(0, &sAudioWave[slot])"))
        # Announce the DSP's own rate: 32768 asks for a 2-tap linear resample
        # at ratio 1.0012, which amplitude-modulates the high band at 39.5 Hz
        # (up to -8 dB at 12 kHz) -- audible roughness.
        self.assertIn("ndspChnSetRate(0, (float)NDSP_SAMPLE_RATE)", text)
        # Above the game: a 20-30 ms composite must not starve the mixer.
        self.assertIn("AUDIO_THREAD_PRIORITY 0x2e", text)

    def test_shell_presents_through_the_remaster_only(self):
        """There is no classic 1:1 path: the compositor is the display.

        The game thread hands its display state to a drawer, the drawer
        publishes the 2D layers, and the V-blank thread presents the composed
        frames. The old software blit and gfxSwapBuffers path is gone, so a
        256x192 image can never reach the LCD by itself.
        """
        text = (self.APP / "main_3ds.c").read_text()
        # The drawer/compositor/V-blank pipeline.
        self.assertIn("NitroPort_Render2DSplit(", text)
        self.assertIn("NitroPort_Render2DSplitIfChanged(", text)
        self.assertIn("NitroPort_ComposePublish(", text)
        self.assertIn("NitroPort_Compose2DCurrent(", text)
        self.assertIn("NitroPort_Compose2DDone(", text)
        self.assertIn("NitroPort_ComposeVBlank()", text)
        self.assertIn("NitroPort_ComposeFrame(", text)
        # The classic display: a converted 256x192 image copied to a
        # framebuffer and swapped. None of it may remain.
        self.assertNotIn("static void Blit(", text)
        self.assertNotIn("gfxSwapBuffers()", text)
        self.assertNotIn("NitroPort_Render2D(0,", text)
        # The image counter is logged so a hardware run can prove the GPU
        # published images.
        self.assertIn("NitroPort_Gx3dImageCount()", text)

    def test_shell_runs_the_remaster_and_geometry_threads(self):
        """The display is Platinum's: display, drawer and V-blank threads.

        The display thread initialises citro3d and the compositor, draws each
        finished scene with NitroPort_ComposeFrame, and the V-blank thread
        presents it. The geometry thread keeps scene building off the game's
        thread, and the game flushes once a frame before handing the state to
        a drawer.
        """
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("NitroPort_PicaInit()", text)
        self.assertIn("NitroPort_ComposeInit(&sCompose)", text)
        self.assertIn("NitroPort_ComposeFrame(scene, seen,", text)
        # A 2D-only screen (a menu, the intro before its first 3D) has no
        # scene; the compositor must still draw its changed layers.
        self.assertIn("scene == NULL || (r & bit)", text)
        self.assertIn("NitroPort_Gx3dNewerScene(seen)", text)
        self.assertIn("NitroPort_Gx3dNewerScenes(seen)", text)
        self.assertIn("NitroPort_Gx3dFlush()", text)
        self.assertIn("NitroPort_Gx3dThreadMain(&sGeoRunning)", text)
        self.assertIn("NitroPort_Render2DTakeState(", text)
        self.assertIn("NitroPort_ComposeVBlank()", text)
        # The camera-plane workaround is gone: the port drops the exact
        # (0,0,0,0) zero-area triangles itself, so the scene is passed on
        # untouched.
        self.assertNotIn("Hgss_PicaDropBehindCamera", text)
        self.assertNotIn("NitroPort_PicaRender(scene, &dropped)", text)
        # The first scene is built with the wide view: the startup
        # SetWideView is before the geometry thread, not only per frame.
        self.assertLess(text.index("NitroPort_Gx3dSetWideView(320);"),
                        text.index("GpuStartGeometry(priority)"))
        # Engine A's drawer gets the New 3DS's free core. The display thread's
        # core is chosen by the caller: core 0 on a New 3DS, core 1 on an Old
        # 3DS (whose share is 80%, not the 30% the first plan assumed).
        self.assertIn("StartDrawer(ENGINE_A, priority + 1, 2)", text)
        self.assertIn("threadCreate(GpuMain, NULL, 128 * 1024, priority, core, false)",
                      text)
        self.assertIn("GpuStart(displayPriority, displayCore)", text)
        # A strong override: the port's default is a weak no-op.
        self.assertIn("void NitroPort_Gx3dScenePublished(void)", text)
        self.assertNotIn("weak", text[text.index("void NitroPort_Gx3dScenePublished"):
                                    text.index("void NitroPort_Gx3dScenePublished") + 200])
        # Flush happens before the drawers that read the scene.
        frame = text[text.index("void NitroPort_OnFrame"):]
        frame = frame[:frame.index("\n}")]
        self.assertLess(frame.index("NitroPort_Gx3dFlush()"),
                        frame.index("DrawFrame()"))

    def test_3d_camera_plane_triangle_is_dropped(self):
        """The exact (0,0,0,0) vertex is a zero-area camera-plane triangle.

        Both edges from it to a rim vertex project to that vertex, so the DS
        stores no polygon. The port drops it before staging; giving it a
        positive w or its screen (0,0) fanned Ho-oh's wing out of the corner.
        The old HGSS-side drop workaround (every w <= 0 triangle) also removed
        real wing geometry and stays gone.
        """
        source = (REPO / "3ds_port" / "nitro" / "src" / "port_gx3d.c").read_text()
        self.assertIn("static int camera_plane(", source)
        self.assertIn("v->clip[0]==0 && v->clip[1]==0 && v->clip[2]==0 && "
                      "v->clip[3]==0", source)
        self.assertIn("if(camera_plane(a) || camera_plane(b) || camera_plane(c))",
                      source)
        self.assertNotIn("out.clip[3]=1;", source)
        # The workaround's file and call are gone from the program.
        self.assertFalse((self.APP / "pica_scene.c").exists())
        self.assertNotIn("Hgss_PicaDropBehindCamera",
                         (self.APP / "main_3ds.c").read_text())

    def test_shell_refuses_to_fall_back_to_2d(self):
        """The remaster is mandatory: no failed display may run the game 1:1.

        A failed citro3d or compose init stops the display thread with
        ``sDisplayFailed`` set, and main shows ``DisplayErrorScreen`` and
        never starts the game thread. The only remaining fallback is the
        geometry thread going back to the game's own thread, which changes
        where scenes are built, not how they are displayed.
        """
        text = (self.APP / "main_3ds.c").read_text()
        gpu = text[text.index("static void GpuMain"):]
        gpu = gpu[:gpu.index("\nstatic bool GpuStart")]
        self.assertIn("if (!NitroPort_PicaInit())", gpu)
        self.assertIn("if (!NitroPort_ComposeInit(&sCompose))", gpu)
        self.assertIn("sGpuRunning = false", gpu)
        self.assertNotIn("sDisplayFailed", gpu)
        start = text[text.index("static bool GpuStart"):]
        start = start[:start.index("\nstatic void GpuStop")]
        self.assertIn("if (!sGpuRunning)", start)
        self.assertIn("return false", start)
        # A failed display is reported, not papered over with the old blit.
        self.assertIn("static void DisplayErrorScreen(void)", text)
        self.assertIn("if (displayReady) {", text)
        self.assertLess(text.index("displayReady = GpuStart("),
                        text.index("threadCreate(GameThread"))
        self.assertIn("DisplayErrorScreen();", text)
        # Geometry failure returns the engine to the game's thread.
        geo = text[text.index("static bool GpuStartGeometry"):]
        geo = geo[:geo.index("\nstatic void GpuStopGeometry")]
        self.assertIn("NitroPort_Gx3dStopThread()", geo)

    def test_shell_maps_touch_to_the_ds_screen(self):
        """The remaster's lower screen is 320x240; the game's is 256x192.

        Passing raw 3DS coordinates was a pre-existing bug (the old 1:1 art
        was offset by (32,24)). Both physical axes now scale by 4/5, and
        scripted input still passes DS coordinates unchanged.
        """
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("touchX = touch.px * 4 / 5;", text)
        self.assertIn("touchY = touch.py * 4 / 5;", text)
        touch = text[text.index("if (held & KEY_TOUCH) {"):]
        touch = touch[:touch.index("\n    }")]
        self.assertIn("4 / 5", touch)

    def test_shell_keeps_native_3d_on_an_old_3ds(self):
        """Old 3DS uses the same layout with smooth3d off, not a fallback."""
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("sCompose.smooth3d = sNew3ds ? 2 : 0;", text)
        self.assertIn("sCompose.wide = sIniWide;", text)
        self.assertIn("static int sIniWide = 1;", text)

    def test_shell_reserves_linear_memory_for_compose(self):
        """Compose needs linear memory beside the game's heap."""
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("void __system_allocateHeaps(void)", text)
        # A 64 MiB process (an Old 3DS) gives the game a bounded heap and the
        # rest to the display. The old test was `remaining > 16 MB * 2`, which
        # a 30.7 MiB Old 3DS fails -- it silently fell through to a half split
        # and the compositor was left 408 KB for the 3D textures.
        self.assertIn("HEAP_O3DS_GAME", text)
        self.assertIn("remaining <= HEAP_SMALL_TOTAL", text)
        self.assertNotIn("HEAP_SMALL_GAME * 2u", text)
        self.assertIn("__ctru_linear_heap_size", text)

    def test_pc_box_start_handles_mode_4(self):
        """The PC box start state must not use an uninitialised next state.

        MOVE POKEMON can arrive as mode 4; the retail start switch has no
        case for it, so the fade returned the previous frame's PCBoxWork
        pointer as the state index and crashed in PCBox_RunStateMachine.
        Patch 1909 initialises/defaults it and guards the dispatch.
        """
        patches = sorted((REPO / "patches" / "pokeheartgold").glob("1909-*.patch"))
        self.assertTrue(patches, "patch 1909 is missing")
        text = "\n".join(p.read_text() for p in patches)
        self.assertIn("enum PCBoxState PCBox_State_Start(PCBoxWork *work)", text)
        self.assertIn("enum PCBoxState nextState = PC_BOX_STATE_MOVE_MODE_INPUT;", text)
        self.assertIn("nextState = PC_BOX_STATE_MOVE_MODE_INPUT;", text)
        self.assertIn("return PCBox_StartFadeIn(work, nextState);", text)
        self.assertIn("int layout = 3;", text)
        self.assertIn("*state >= PC_BOX_STATE_EXIT", text)
        self.assertIn("*state = PC_BOX_STATE_START;", text)

    def test_trainer_approach_task_is_passed(self):
        """The trainer walk-up must pass the task, not a bare no-arg call.

        The public prototype once said IsDone(void) while the definition took
        SysTask *; ARM read r0 (the ScriptEnv slot address) and dereferenced
        NULL at the first trainer in line of sight. Patch 1908 fixes the
        prototype and the call.
        """
        patches = sorted((REPO / "patches" / "pokeheartgold").glob("1908-*.patch"))
        self.assertTrue(patches, "patch 1908 is missing")
        text = "\n".join(p.read_text() for p in patches)
        self.assertIn("BOOL TrainerApproach_IsDone(SysTask *task);", text)
        self.assertIn("void TrainerApproach_Delete(SysTask *task);", text)
        self.assertIn("TrainerApproach_IsDone(approach)", text)
        self.assertIn("TrainerApproach_Delete(approach)", text)

    def test_shell_has_a_console_only_on_its_error_screens(self):
        """NitroPort_Log writes to stderr, which the console draws on screen.

        A console initialised at startup put every log line over the bottom
        screen; it belongs to the two "can't start" paths only -- the pack
        error and the mandatory display error.
        """
        text = (self.APP / "main_3ds.c").read_text()
        self.assertEqual(text.count("consoleInit("), 2)
        for name in ("static void PakErrorScreen", "static void DisplayErrorScreen"):
            error = text[text.index(name):]
            error = error[:error.index("\n}")]
            self.assertIn("consoleInit(", error)

    def test_shell_caps_the_frame_rate_at_60_by_default(self):
        """A frame under the refresh is not a 60 Hz frame.

        The log-9 run measured 87 Hz (mean total 11.49 ms): once the display
        work fit under one refresh, gfxSwapBuffers returned early, and the only
        clock the emulated DS has is the V-blank raised once per OnFrame. The
        shell holds frame starts to SYSCLOCK_ARM11 / fps with a tick deadline.
        port.ini's fps key may only say 60 (default) or 30; 30 is a comparison
        mode that halves the game's speed because the clock is per frame.
        """
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("#define DEFAULT_FPS 60", text)
        self.assertIn("sFrameCap = DEFAULT_FPS;", text)
        self.assertIn('IniValue("fps", value, sizeof(value))', text)
        self.assertIn('"[perf] frame cap %d Hz"', text)
        self.assertIn("SYSCLOCK_ARM11 / (u64)sFrameCap", text)
        self.assertIn("half speed", text)
        # Anything other than 60 or 30 falls back to the default.
        read = text[text.index("static void ReadFrameCap"):]
        read = read[:read.index("\n}")]
        self.assertIn("fps != 60 && fps != 30", read)
        self.assertIn("DEFAULT_FPS", read)
        # Read before the game's thread can raise its first V-blank.
        self.assertLess(text.index("ReadFrameCap();"),
                        text.index("threadCreate(GameThread"))

    def test_shell_waits_after_the_draw_never_before_it(self):
        """The cap is a sleep after the frame, not another V-blank before it.

        The drawn frame goes to the drawers and is presented at the next
        V-blank; the frame-start deadline stays after that hand-off. A frame
        that already overran returns from the wait immediately, and one more
        than a whole period behind re-anchors the deadline instead of bursting
        to catch up.
        """
        text = (self.APP / "main_3ds.c").read_text()
        draw = text[text.index("static bool DrawFrame"):]
        draw = draw[:draw.index("\n}")]
        self.assertIn("NitroPort_Compose2DOffered(", draw)
        self.assertNotIn("svcSleepThread", draw)
        self.assertNotIn("WaitForFrameCap", draw)
        frame = text[text.index("void NitroPort_OnFrame"):]
        frame = frame[:frame.index("\n}")]
        self.assertIn("WaitForFrameCap(frameStart)", frame)
        self.assertLess(frame.index("DrawFrame()"),
                        frame.index("WaitForFrameCap(frameStart)"))
        wait = text[text.index("static void WaitForFrameCap"):]
        wait = wait[:wait.index("\n}")]
        self.assertIn("svcGetSystemTick()", wait)
        self.assertIn("(sNextFrameTick - now) * 1000000000ULL / SYSCLOCK_ARM11", wait)
        self.assertIn("svcSleepThread((s64)ns)", wait)
        self.assertIn("now - sNextFrameTick > period", wait)
        self.assertIn("sNextFrameTick = now + period", wait)
        self.assertIn("sNextFrameTick += period", wait)

    def test_check_is_declared_for_the_program(self):
        text = (self.APP / "heartgold.h").read_text()
        self.assertIn("int Heartgold_CheckOverlays(void);", text)


class PixelFilterPresetTests(unittest.TestCase):
    """The X+R pixel-filter cycle and its lower-screen toast (source wiring).

    The presets are cheap-to-expensive, the 3D texture filter is changed on
    the display thread only, and the toast is drawn into the lower screen's
    published layers without touching the upper screen's stereo.
    """

    APP = REPO / "hgss" / "game" / "app"
    PORT = REPO / "3ds_port" / "game" / "app"

    def test_shell_calls_the_runtime_setter(self):
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("NitroPort_ComposeSetFilter(p->filter2d, p->filter3d)",
                      text)

    def test_combo_is_detected_before_scripted_input(self):
        text = (self.APP / "main_3ds.c").read_text()
        combo = text.index("filterCombo = (held & (KEY_X | KEY_R))")
        scripted = text.index("HgssScript_HeldButtons(")
        self.assertLess(combo, scripted)
        # Physical X and R are masked while the combo is held; the scripted
        # buttons are OR'd afterwards and stay untouched.
        mask = text.index("buttons &= (uint16_t)~(HGSS_BUTTON_X | HGSS_BUTTON_R)")
        self.assertLess(mask, scripted)

    def test_presets_are_the_five_asked_for(self):
        text = (self.APP / "main_3ds.c").read_text()
        for label in ("NEAREST/NEAREST", "SHARP/NEAREST", "SHARP/SHARP",
                      "SHARP/XBRZ-S", "SCALE2X/XBRZ-S"):
            self.assertIn(f'"{label}"', text)
        self.assertIn("COMPOSE_FILTER_NEAREST, 0", text)
        self.assertIn("COMPOSE_FILTER_SHARP,   0", text)
        self.assertIn("COMPOSE_FILTER_SHARP,   2", text)
        self.assertIn("COMPOSE_FILTER_SHARP,   4", text)
        self.assertIn("COMPOSE_FILTER_SCALE2X, 4", text)
        # The default is SHARP/NEAREST, index 1.
        self.assertIn("static int sFilterPreset = 1;", text)

    def test_toast_is_lower_screen_under_and_over(self):
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("layers->under[i] = color;", text)
        self.assertIn("layers->over[i] = (uint16_t)(0x8000u | color);", text)
        self.assertIn("layers->overAlpha[i] = 16;", text)
        self.assertIn("toast_draw(layers);", text)
        self.assertIn("if (!top)", text)

    def test_toast_expires_by_tick(self):
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("#define TOAST_SECONDS 2", text)
        self.assertIn("#define TOAST_TICKS ((u64)TOAST_SECONDS * SYSCLOCK_ARM11)", text)
        self.assertIn("svcGetSystemTick() + TOAST_TICKS", text)
        self.assertIn("svcGetSystemTick() >= until", text)

    def test_texture_filter_is_not_set_from_the_shell(self):
        text = (self.APP / "main_3ds.c").read_text()
        self.assertNotIn("NitroPort_PicaTextureFilter", text)

    def test_runtime_setter_is_in_the_port(self):
        header = (self.PORT / "port_pica.h").read_text()
        compose = (self.PORT / "port_compose_pica.c").read_text()
        self.assertIn("int NitroPort_ComposeSetFilter(int filter2d, int filter3d);",
                      header)
        self.assertIn("int NitroPort_ComposeSetFilter(int filter2d, int filter3d)\n{",
                      compose)
        self.assertIn("__atomic_store_n(&sFilterRequest", compose)
        self.assertIn("__atomic_exchange_n(&sFilterRequest", compose)
        self.assertIn("NitroPort_PicaTextureFilter(filter3d)", compose)
        # The filter is latched into each published set so a frame drawn with
        # it samples it with the same filter.
        self.assertIn("set->filter2d = filter2d;", compose)
        self.assertIn("set->filter2d == COMPOSE_FILTER_SCALE2X", compose)


class AptSuspendTests(unittest.TestCase):
    """Sleep/HOME must not let a compose cross the GPU handover."""

    APP = REPO / "hgss" / "game" / "app"

    def _function(self, text, start, end):
        at = text.index(start)
        return text[at:text.index(end, at)]

    def test_compose_is_bracketed_by_the_suspend_handshake(self):
        text = (self.APP / "main_3ds.c").read_text()
        loop = self._function(text, "static void GpuComposeLoop", "/* Once a frame")
        # Busy first, then check: the Dekker order with AptHook.
        self.assertGreaterEqual(loop.count("__atomic_store_n(&sComposing, true"), 2)
        self.assertGreaterEqual(loop.count("suspended = __atomic_load_n(&sSuspended"), 2)
        self.assertGreaterEqual(loop.count("if (!suspended)"), 2)
        self.assertGreaterEqual(loop.count("__atomic_store_n(&sComposing, false"), 2)
        # AptHook waits for a compose already in progress before taking the
        # presentation queue, and reports whether it settled.
        hook = self._function(text, "static void AptHook", "/* Wait for the game's thread")
        self.assertIn("__atomic_load_n(&sComposing, __ATOMIC_SEQ_CST)", hook)
        self.assertLess(hook.index("__atomic_load_n(&sComposing"),
                        hook.index("NitroPort_ComposeHold(1)"))

    def test_resume_is_applied_after_apt_hooks(self):
        text = (self.APP / "main_3ds.c").read_text()
        hook = self._function(text, "static void AptHook", "/* Wait for the game's thread")
        self.assertIn("__atomic_store_n(&sResumePending, true", hook)
        self.assertIn("sAptHome", hook)
        self.assertIn("sAptAsleep", hook)
        # The actual resume happens after aptMainLoop returns, so citro3d's
        # own restore hook has run.
        frame = self._function(text, "void NitroPort_OnFrame", "/* The game stopped itself. */")
        self.assertIn("__atomic_exchange_n(&sResumePending", frame)
        self.assertLess(frame.index("if (!aptMainLoop())"),
                        frame.index("__atomic_exchange_n(&sResumePending"))
        self.assertIn("NitroPort_ComposeHold(0)", frame)
        self.assertLess(frame.index("NitroPort_ComposeHold(0)"),
                        frame.index("__atomic_store_n(&sSuspended, false"))


class PatchStackTests(unittest.TestCase):
    """tools/check_patches.py must catch a tree that is missing a patch.

    It compares the commit subject `git am` records with the tree's history,
    because `git apply --check` cannot tell an applied patch from a missing one
    on a cumulative stack (later patches rewrite the same lines).
    """

    def _subject(self, text):
        import tempfile
        from tools.check_patches import patch_subject

        with tempfile.NamedTemporaryFile("w", suffix=".patch", delete=False) as f:
            f.write(text)
            path = f.name
        try:
            return patch_subject(Path(path))
        finally:
            Path(path).unlink()

    def test_subject_strips_the_patch_tag(self):
        self.assertEqual(
            self._subject("From abc\nSubject: [PATCH] gx: send the ortho projection\n\n---\n"),
            "gx: send the ortho projection")

    def test_subject_strips_a_numbered_tag_and_folds_continuation_lines(self):
        # format-patch folds long subjects; git am joins them back with spaces.
        self.assertEqual(
            self._subject("Subject: [PATCH 3/4] save: check the next chunk header\n"
                          " is in range before reading it\n"
                          "\nbody\n"),
            "save: check the next chunk header is in range before reading it")

    def test_the_checker_covers_every_patch_stack(self):
        from tools.check_patches import PATCH_STACKS

        names = [name for name, _ in PATCH_STACKS]
        self.assertEqual(names, ["NitroSDK", "NitroSystem", "pokeheartgold"])


class ThreadLifecycleTests(unittest.TestCase):
    """The game thread must never be freed while it is still running.

    `WaitForGame` exits its loop when `sGameThreadAlive` is false, and a new
    thread has not necessarily executed its first instruction when
    `threadCreate` returns. The flag is therefore set before the thread
    exists; treating "not started" as "ended" freed a live thread's stack on
    hardware and the process died in teardown.
    """

    APP = REPO / "hgss" / "game" / "app"

    def test_the_alive_flag_is_set_before_the_thread_exists(self):
        text = (self.APP / "main_3ds.c").read_text()
        at = text.index("thread = threadCreate(GameThread")
        self.assertIn("sGameThreadAlive = true;", text[max(0, at - 500):at])
        # And a failed create must not leave it claiming a live thread.
        after = text[at:at + 400]
        self.assertIn("sGameThreadAlive = false;", after)

    def test_a_running_thread_is_not_freed(self):
        text = (self.APP / "main_3ds.c").read_text()
        at = text.index("static void WaitForGame(Thread thread)")
        wait = text[at:text.index("\n}\n", at)]
        self.assertIn("R_SUCCEEDED(threadJoin(thread, 5000 * 1000000LL))", wait)
        self.assertLess(wait.index("R_SUCCEEDED(threadJoin(thread, 5000"),
                        wait.index("threadFree(thread);"))
        # A timed-out join leaves the thread alive: freeing it corrupts the
        # stack it is running on. Mark it stuck so the caller exits instead.
        self.assertIn("sThreadsStuck = true;", wait)
        self.assertIn("not freeing it", wait)


class DisplayPerformanceTests(unittest.TestCase):
    """The first performance wave: one compose per game frame, no brightness
    re-renders, a trimmed over publish, startup knobs and [perf] accounting."""

    APP = REPO / "hgss" / "game" / "app"
    PORT = REPO / "3ds_port" / "game" / "app"
    NITRO = REPO / "3ds_port" / "nitro" / "src"

    def _function(self, text, start, end):
        at = text.index(start)
        return text[at:text.index(end, at)]

    def test_one_compose_wake_per_handed_frame(self):
        text = (self.APP / "main_3ds.c").read_text()
        show = self._function(text, "static bool ShowScreen", "static void DrawerMain")
        # A publish must not wake the display thread on its own.
        self.assertNotIn("LightEvent_Signal(&sGpuWake)", show)
        draw = self._function(text, "static bool DrawFrame", "static void GpuComposeLoop")
        # Only the drawers actually handed the frame are counted.
        self.assertIn("take[i] = true;", draw)
        # Add per handed drawer, not store: a drawer from the frame before
        # may still be finishing and about to decrement.
        self.assertIn("__atomic_add_fetch(&sDrawFramePending, 1", draw)
        self.assertNotIn("__atomic_store_n(&sDrawFramePending", draw)
        drawer = self._function(text, "static void DrawerMain", "static bool StartDrawer")
        self.assertIn("__atomic_sub_fetch(&sDrawFramePending, 1", drawer)
        self.assertIn("== 0", drawer)
        # Exactly one wake in the drawer, after the last one finishes.
        self.assertEqual(drawer.count("LightEvent_Signal(&sGpuWake)"), 1)
        self.assertLess(drawer.index("__atomic_sub_fetch(&sDrawFramePending"),
                        drawer.index("__atomic_store_n(&d->busy, false"))
        # The V-blank thread keeps its wake.
        vblank = self._function(text, "static void VBlankMain", "static bool VBlankStart")
        self.assertIn("LightEvent_Signal(&sGpuWake)", vblank)

    def test_unchanged_render_refreshes_master_bright(self):
        text = (self.APP / "main_3ds.c").read_text()
        render = self._function(text, "static bool RenderScreen", "static bool ShowScreen")
        skipped = render[render.index("if (!drawn)"):]
        self.assertIn("int bright = NitroPort_Render2DMasterBright(engine, 1);",
                      skipped)
        # A changed brightness must publish the untouched layers, or the
        # compositor keeps the old GPU brightness and fades stick.
        self.assertIn("if (bright != layers->masterBright)", skipped)
        self.assertIn("layers->masterBright = bright;", skipped)
        self.assertIn("return true;", skipped)
        self.assertLess(skipped.index("layers->masterBright = bright;"),
                        skipped.index("return true;"))

    def test_master_bright_is_not_in_the_small_signature(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        small = self._function(text, "static u32 EngineSmallSignature",
                               "static u32 EngineVramSignature")
        # MASTER_BRIGHT at 0x6C: the compositor applies it on the GPU.
        self.assertIn("regs[0x6C / 4] = 0;", small)
        self.assertEqual(small.count("regs[0x6C / 4] = 0;"), 2)

    def test_publish_skips_an_empty_over_layer(self):
        text = (self.PORT / "port_compose_pica.c").read_text()
        self.assertIn("int hasOver = l->has3d && set->overBottom > 0;", text)
        self.assertIn("hasOver, w, h, OVER_ROWS(l), set->under, set->over", text)

    def test_port_ini_has_the_display_keys(self):
        text = (self.APP / "main_3ds.c").read_text()
        read = self._function(text, "static void ReadDisplayOptions",
                              "/* `script=PATH` in port.ini")
        for key in ('IniValue("filter"', 'IniValue("stereo"', 'IniValue("wide"'):
            self.assertIn(key, read)
        for value in ('"sharp"', '"nearest"', '"smooth"', '"scale2x"'):
            self.assertIn(value, read)
        for value in ('"on"', '"off"', '"1"', '"0"'):
            self.assertIn(value, text)
        self.assertIn("sCompose.filter = sIniFilter;", text)
        self.assertIn("sCompose.stereo = sIniStereo;", text)
        self.assertIn("sCompose.wide = sIniWide;", text)
        # Read after the frame cap and before the config is filled.
        self.assertLess(text.index("ReadFrameCap();"), text.index("ReadDisplayOptions();"))
        self.assertLess(text.index("ReadDisplayOptions();"),
                        text.index("memset(&sCompose, 0, sizeof(sCompose));"))
        for line in ('"[perf] filter=%s"', '"[perf] stereo=off',
                     '"[perf] wide=off'):
            self.assertIn(line, text)

    def test_perf_lines_and_drawer_counters(self):
        text = (self.APP / "main_3ds.c").read_text()
        drawer = self._function(text, "typedef struct Drawer", "static Drawer sDrawers")
        for field in ("u64 ticks;", "u64 renderTicks;", "u64 publishTicks;",
                      "unsigned count;", "unsigned drawn;"):
            self.assertIn(field, drawer)
        perf = self._function(text, "static void PerfLog", "static void WaitForFrameCap")
        for api in ("NitroPort_PicaTimes(", "NitroPort_ComposeStats(",
                    "NitroPort_PicaTextureStats(", "NitroPort_PicaCmdStats(",
                    "NitroPort_Gx3dThreadStats(", "NitroPort_Gx3dProducerWaitMicros(",
                    "NitroPort_Gx3dThreadWakeups(", "sSoundMixTicks"):
            self.assertIn(api, perf)
        for line in ('"[perf] display:', '"[perf] geometry:', '"[perf] audio:',
                     '"[perf] drawer %d'):
            self.assertIn(line, perf)
        # FRAMETIME separates the hand-off from the rest of the interval, and
        # carries DrawFrame's two phases apart (the audits attributed the
        # "13.5 ms hand-off" to three different things, so it is measured).
        self.assertIn('"FRAMETIME frame=%lu game_us=%llu state_us=%llu total_us=%llu "', text)
        self.assertIn('"hblank_us=%llu handoff_us=%llu hb_keep_us=%llu hb_dma_us=%llu hb_irq_us=%llu"',
                      text)
        # The walk itself must be split: on an Old 3DS it is 9.5-13.8 ms when
        # the game arms it and 8 us when it does not, and the three parts have
        # different fixes.
        self.assertIn("void NitroPort_HBlankSplit(u64 *keep, u64 *dma, u64 *irq);", text)
        self.assertIn("NitroPort_HBlankSplit(&sFrameHbKeep", text)
        self.assertNotIn("FRAMETIME frame=%lu logic_us=%llu draw_us", text)


class PerformanceWaveTwoTests(unittest.TestCase):
    """The second performance wave: honest instruments, optimised game
    objects, and the Old 3DS memory profile."""

    APP = REPO / "hgss" / "game" / "app"
    PORT = REPO / "3ds_port" / "game" / "app"

    def _function(self, text, start, end):
        at = text.index(start)
        return text[at:text.index(end, at)]

    def test_log_lines_fit_and_flush_off_the_frame_path(self):
        text = (self.APP / "main_3ds.c").read_text()
        at = text.index("#define LOG_LINE ")
        size = int(text[at + len("#define LOG_LINE "):].split()[0])
        # 220 truncated every [perf] display line on hardware mid-field.
        self.assertGreaterEqual(size, 512)
        self.assertIn("setvbuf(sLog, sLogBuf, _IOFBF, sizeof(sLogBuf));", text)
        log = self._function(text, "void NitroPort_Log(const char *fmt, ...)\n{", "\n/* The 3DS's buttons")
        # No per-line SD write: flushes are once a second, and the perf block
        # (the lines a run is judged by) flushes explicitly.
        self.assertNotIn("fflush(sLog);", log)
        self.assertIn("FlushLogLocked()", log)
        self.assertIn("FlushLog();", text)
        # The ring still holds the last lines for the exception log.
        self.assertIn("fprintf(sLog, \"%s\\n\", sRing[at]);", text)

    def test_frame_time_counters_share_one_anchor(self):
        text = (self.APP / "main_3ds.c").read_text()
        frame = self._function(text, "void NitroPort_OnFrame", "/* The game stopped itself. */")
        # total_us is measured at entry, before the pause/restore returns, so
        # it cannot come out smaller than the state_us measured inside it
        # (frame 1800 used to report total 10,810 < state 14,118).
        self.assertLess(frame.index("totalUs = sHaveLastFrameTick"),
                        frame.index("sFrame++;"))
        self.assertLess(frame.index("totalUs = sHaveLastFrameTick"),
                        frame.index("if (sPaused)"))
        self.assertIn("sPrevStateUs = stateUs;", frame)
        self.assertIn("state_us=%llu", frame)
        self.assertIn("prevStateUs", frame)
        self.assertLess(frame.index("FlushLog();"), frame.index("sPrevStateUs = stateUs;"))

    def test_perf_reports_the_memory_the_split_is_made_from(self):
        text = (self.APP / "main_3ds.c").read_text()
        # Without the free-linear and game-heap numbers a run cannot say
        # whether the 10 MB game heap is still right.
        self.assertIn('"[perf] mem: linear_free=%lu KB game_heap_used=%lu KB"', text)
        self.assertIn("linearSpaceFree()", text)

    def test_game_objects_are_compiled_optimised(self):
        import tools.hgss_game as g

        for target in ("3ds", "host32"):
            flags = g.port_cflags(target)
            self.assertIn("-O2", flags, target)
            self.assertNotIn("-O0", flags, target)
        # The sanitizer build keeps its own level (the replay wants debug
        # info and the -O0 behaviour it was verified with).
        source = (REPO / "tools" / "hgss_game.py").read_text()
        self.assertIn("if SANITIZE and target == \"host32\":", source)
        self.assertLess(source.index("flags += SANITIZE_FLAGS"),
                        source.index("flags += [\"-O2\"]"))

    def test_sun_shadow_room_is_opt_in(self):
        port = (self.PORT / "port_gx3d_pica.c").read_text()
        header = (self.PORT / "port_pica.h").read_text()
        shell = (self.APP / "main_3ds.c").read_text()
        # HGSS never turns the sun shadows on, so their 1,438 KB of vertex
        # room was allocated for nothing -- on an Old 3DS that is a third of
        # the linear memory the 3D texture cache was short of.
        self.assertIn("static int sShadowWanted = 1;", port)
        self.assertIn("void NitroPort_PicaSetShadowRoom(int on)", port)
        self.assertIn("void NitroPort_PicaSetShadowRoom(int on);", header)
        self.assertIn("sShadowRoom = sShadowWanted ? SHADOW_VERTICES : 0;", port)
        self.assertIn("NitroPort_PicaSetShadowRoom(sCompose.shadows);", shell)
        self.assertLess(shell.index("NitroPort_PicaSetShadowRoom(sCompose.shadows);"),
                        shell.index("displayReady = GpuStart("))

    def test_classic_readback_images_are_lazy(self):
        port = (self.PORT / "port_gx3d_pica.c").read_text()
        # 1,152 KB of classic-path readback images were allocated at startup
        # and the remaster never calls NitroPort_PicaRender.
        self.assertIn("static int classic_images_ready(void)", port)
        init = self._function(port, "int NitroPort_PicaInit(void)", "void NitroPort_PicaExit(void)")
        self.assertIn("sImages[i]=NULL;", init)
        self.assertNotIn("linearAlloc(IMAGE_BYTES)", init)
        render = self._function(port, "int NitroPort_PicaRender(", "scene_registers(scene);")
        self.assertIn("classic_images_ready()", render)


    def test_old_3ds_asks_for_one_texel_a_pixel(self):
        shell = (self.APP / "main_3ds.c").read_text()
        port = (self.PORT / "port_compose_pica.c").read_text()
        header = (self.PORT / "port_pica.h").read_text()
        self.assertIn("int texScale;", header)
        # Old 3DS: 1 outright, not the automatic 2-then-fall-back ladder (the
        # 2x sets are 13,824 KB and the failed attempt drops the menu and the
        # weather BG on the way down).
        self.assertIn("sCompose.texScale = sIniTexScale != 0 ? sIniTexScale : (sNew3ds ? 0 : 1);",
                      shell)
        self.assertIn("int scale = config->texScale == 1 ? 1", port)
        self.assertLess(port.index("config->texScale == 1"),
                        port.index("!small && (config->filter"))
        # And the log says what was actually used, not what was asked for.
        self.assertIn("%d texel%s a pixel", port)
        self.assertIn('IniValue("texscale"', shell)

    def test_core_1_share_is_logged(self):
        shell = (self.APP / "main_3ds.c").read_text()
        # Every core-1 placement decision depends on the share the system
        # actually grants, and it had never been read back.
        self.assertIn("APT_GetAppCpuTimeLimit(&accepted)", shell)
        self.assertIn("core 1 share: asked %lu%%, the system allows %lu%%", shell)


class PerformanceWaveThreeTests(unittest.TestCase):
    """The third performance wave: the last double hot path, audio off core
    0, the Old 3DS launch-only filter, and the lower screen at 30 Hz."""

    APP = REPO / "hgss" / "game" / "app"
    NITRO = REPO / "3ds_port" / "nitro" / "src"

    def test_in_between_scene_match_is_single_precision(self):
        source = (self.NITRO / "port_gx3d.c").read_text()
        # ndc_of was the last double entry point in the geometry path; the
        # quarter-screen cut-off needs ~1e-7, not soft-float double.
        self.assertIn("static int ndc_of(const int32_t clip[4],float *x,float *y)",
                      source)
        self.assertNotIn("static int ndc_of(const int32_t clip[4],double *x,double *y)",
                         source)
        mid = source[source.index("for(j=0;j<n;j++){"):source.index(
            "sMidMatched=used;")]
        self.assertIn("float x0,y0,x1,y1;", mid)
        self.assertIn("fabsf(x1-x0)>0.5f", mid)
        self.assertIn("float hx=-0.5f*sMotionX/128.0f", mid)
        self.assertIn("floorf(hx*(float)clip[k][3]+0.5f)", mid)
        self.assertIn("float fx=sMotionX/128.0f", mid)
        self.assertIn("floorf(fx*(float)full[3]+0.5f)", mid)
        self.assertIn("floorf(0.5f*fx*(float)clip[k][3]+0.5f)", mid)
        self.assertNotIn("double", mid)

    def test_audio_stays_on_core_0_on_old_3ds(self):
        text = (self.APP / "main_3ds.c").read_text()
        # Tried on core 1, reverted: log 9 vs log 8 in the same field showed
        # core 1 oversubscribed (geometry 61-79% -> 84-100% busy for identical
        # throughput, display starved to 0.0 composes/s). Default core 0 on
        # Old 3DS, -2 on New 3DS; `audiocore=` overrides either for A/B runs.
        self.assertIn('IniValue("audiocore"', text)
        self.assertIn("sIniAudioCore = value[0] - '0';", text)
        self.assertIn("sAudioCore = sIniAudioCore >= 0 ? sIniAudioCore : (sNew3ds ? -2 : 0);",
                      text)
        self.assertIn("AUDIO_THREAD_STACK,\n"
                      "                                AUDIO_THREAD_PRIORITY, sAudioCore, false);",
                      text)
        # A failed pin falls back to core 0 rather than staying silent.
        self.assertIn("threadCreate(AudioThread, NULL, AUDIO_THREAD_STACK,\n"
                      "                                    AUDIO_THREAD_PRIORITY, 0, false);",
                      text)
        self.assertIn("on core %d", text)
        # The share must be requested before the audio thread exists: a
        # core-1 threadCreate before it fails and falls back to core 0
        # (log-o3ds-soulsilver-8's "on core 0").
        main = text[text.index("int main(int argc, char **argv)"):]
        self.assertLess(main.index("RequestCore1Share();"), main.index("AudioStart();"))
        # The per-interval line names the core it measured and counts the
        # mixer starvation the move is judged by.
        self.assertIn('"[perf] audio: %.1f%% of core %d (%.1f ms mixing in %.1f s), %u dropped of %u"',
                      text)
        self.assertIn("ndspGetDroppedFrames()", text)

    def test_filter_cycle_is_launch_only_on_old_3ds(self):
        text = (self.APP / "main_3ds.c").read_text()
        # The combo detection itself is unchanged (physical X+R before
        # scripted input, masked while held).
        self.assertIn("filterCombo = (held & (KEY_X | KEY_R))", text)
        # ...but on an Old 3DS it never fires: the profile is fixed at
        # launch (1 texel a pixel; filter3d presets would 2-4x texture
        # memory mid-run with under a megabyte free).
        self.assertIn("if (!sNew3ds) {\n        filterCombo = false;\n    }", text)
        self.assertIn("[perf] Old 3DS profile: launch-time filter only", text)

    def test_lower_screen_is_30hz_on_old_3ds(self):
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn('IniValue("lower30"', text)
        # Default by console: Old 3DS 30 Hz, New 3DS 60 Hz.
        self.assertIn("sIniLower30 = sNew3ds ? 0 : 1;", text)
        drawer = text[text.index("static void DrawerMain"):
                       text.index("static bool StartDrawer")]
        # Only engine B ever skips (it cannot carry the 3D); never a forced
        # redraw, and never on a New 3DS.
        self.assertIn("!sNew3ds && sIniLower30 && ((d->stamp & 1u) != 0)", drawer)
        self.assertIn("!sMustDraw[lowerIdx]", drawer)
        self.assertIn("sToastDirty", drawer)
        self.assertIn("RenderScreen(1, d->aOnTop, d->stamp)", drawer)
        # A skipped frame publishes nothing, so the display reuses the last
        # set instead of advancing its stamp.
        self.assertIn("nothing rendered and nothing published", drawer)
        # Game logic stays 60 Hz: only the lower 2D halves.
        self.assertIn("lower 2D at %s", text)


class PerformanceSliceATests(unittest.TestCase):
    """Slice A: per-phase 2D draw counters plus the empty-line cut.

    The field's drawer render is ~2/3 of the drawer and the wall; before
    caching rows (Slice B) the build must say which phase the time is in.
    """

    APP = REPO / "hgss" / "game" / "app"
    NITRO = REPO / "3ds_port" / "nitro" / "src"
    HEADER = REPO / "3ds_port" / "nitro" / "include" / "nitro_port_render2d.h"

    def test_profile_struct_and_getter_are_declared(self):
        header = self.HEADER.read_text()
        for field in ("uint64_t textTicks;", "uint64_t layerTicks;",
                      "uint64_t objTicks;", "uint64_t mergeTicks;",
                      "uint64_t blendTicks;", "uint64_t windowTicks;",
                      "uint64_t finishTicks;", "uint32_t lines;",
                      "uint32_t textDraws;", "uint32_t layerDraws;",
                      "uint32_t blendLines;", "uint32_t emptyLines;"):
            self.assertIn(field, header)
        self.assertIn("void NitroPort_Render2DProfile(int engine, NitroRender2DProfile *out);",
                      header)

    def test_ticks_come_from_the_3ds_counter_only(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        # port_os.c's pattern: the port never includes libctru's headers.
        self.assertIn("#ifdef __3DS__", text)
        self.assertIn("extern u64 svcGetSystemTick(void);", text)
        self.assertIn("#define R2D_NOW() svcGetSystemTick()", text)
        # Host builds (tests, tools) have no counter: phases read 0, counts
        # stay exact, and no undefined reference is introduced.
        self.assertIn("#define R2D_NOW() 0", text)

    def test_draw_engine_time_per_phase(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        draw = text[text.index("static inline __attribute__((always_inline)) void DrawEngineT"):
                    text.index("static void DrawEngine(")]
        self.assertIn("DrawTextBg(e, bg, bgcnt[bg], dispcnt, y, &t,", draw)
        self.assertIn("prof->textTicks += R2D_NOW() - t0;", draw)
        self.assertIn("prof->textDraws++;", draw)
        self.assertIn("px = LayerLine(e, work, bg, kind[bg], bgcnt[bg], dispcnt, y,", draw)
        self.assertIn("prof->layerTicks += R2D_NOW() - t0;", draw)
        self.assertIn("prof->layerDraws++;", draw)
        self.assertIn("objs = DrawObjs(e, objFrame, dispcnt, y, objLine);", draw)
        self.assertIn("prof->objTicks += R2D_NOW() - t0;", draw)
        self.assertIn("Merge(&t, px, prioOf, p, x0, x1,", draw)
        self.assertIn("prof->mergeTicks += R2D_NOW() - t0;", draw)
        self.assertIn("BlendLine(&blendState, row, enableLine, layer1, anyWindow);", draw)
        self.assertIn("prof->blendTicks += R2D_NOW() - t1;", draw)
        self.assertIn("prof->blendLines++;", draw)
        self.assertIn("prof->windowTicks += R2D_NOW() - t0;", draw)
        self.assertIn("prof->finishTicks += R2D_NOW() - t0;", draw)
        self.assertIn("prof->lines++;", draw)
        # Exactly one empty-line count, in the backdrop-fill cut (a second
        # counter elsewhere would double-count plain-path lines).
        self.assertEqual(draw.count("prof->emptyLines++;"), 1)
        self.assertLess(draw.index("Slice A cut: nothing drawn on the line"),
                        draw.index("prof->emptyLines++;"))
        self.assertIn("void NitroPort_Render2DProfile(int engine, NitroRender2DProfile *out)",
                      text)

    def test_empty_lines_fill_with_backdrop(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        draw = text[text.index("static inline __attribute__((always_inline)) void DrawEngineT"):
                    text.index("static void DrawEngine(")]
        # Only when nothing drew (filled == 0 the row is all zero: it was
        # memset above and no Merge or PUT wrote into it), and only on the
        # plain path (no brighten/darken layer to apply).
        self.assertIn("if (t.filled == 0) {", draw)
        self.assertIn("pair[x] = back2;", draw)
        # Odd widths still terminate exactly (widths are even today).
        self.assertIn("if (width & 1) {", draw)

    def test_perf_logs_the_2d_split(self):
        text = (self.APP / "main_3ds.c").read_text()
        perf = text[text.index("static void PerfLog"):text.index("static void WaitForFrameCap")]
        self.assertIn("NitroPort_Render2DProfile(0, ", perf)
        self.assertIn("NitroPort_Render2DProfile(1, ", perf)
        self.assertIn('"[perf] 2d: text %.1f%%, affine %.1f%%, obj %.1f%%, merge %.1f%%, blend %.1f%%, "',
                      perf)
        self.assertIn("setup %.1f%%", perf)
        self.assertIn("rows %u/%u", perf)
        # Cumulative counters, differenced per interval like the rest.
        self.assertIn("sPerf2d[0] = *a;", perf)
        self.assertIn("sPerf2d[1] = *b;", perf)


class PerformanceSliceBTests(unittest.TestCase):
    """Slice B: scroll-shifted text-BG row reuse.

    A text pixel is a pure function of its map coordinate, so a rendered map
    row serves every screen line showing it at any scroll offset. The key is
    BgKey (map, tiles, palettes — but not scroll) plus the wide-clear variant
    BgKey doesn't cover.
    """

    APP = REPO / "hgss" / "game" / "app"
    NITRO = REPO / "3ds_port" / "nitro" / "src"
    HEADER = REPO / "3ds_port" / "nitro" / "include" / "nitro_port_render2d.h"

    def test_row_cache_holds_whole_map_rows(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        self.assertIn("#define TEXTROW_SLOTS 256", text)
        self.assertIn("u16 px[512];", text)
        # Direct-mapped by row (256 slots cover any frame's window, so no
        # within-frame collisions and no LRU bookkeeping).
        self.assertIn("slot = &table[ty & (TEXTROW_SLOTS - 1)];", text)
        self.assertIn("slot->key == key && slot->ty == (u16)ty", text)

    def test_row_key_is_scroll_invariant_but_content_complete(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        start = text.index("static u64 FoldClearVariant(const Engine *e, int bg, u64 key)\n{")
        fold = text[start:text.index("static u64 TextRowKey(const Engine *e, int bg, u16 bgcnt, u32 dispcnt)\n{",
                                     start)]
        key = text[text.index("static u64 TextRowKey(const Engine *e, int bg, u16 bgcnt, u32 dispcnt)\n{",
                              start):text.index("static const u16 *TextRowCached", start)]
        # Scroll comes from the IO regs, which BgKey never reads: reuse
        # across scroll positions.
        self.assertIn("BgKeyCached(e, bg, BG_TEXT, bgcnt, dispcnt)", key)
        # ...while the wide-clear palette variant BgKey misses is folded in
        # (BgPal4 reads a different table for it, so the key must differ).
        self.assertIn("e->clearBgs", fold)
        self.assertIn("e->clearMask", fold)
        self.assertIn("KeyAdd", fold)

    def test_row_fill_renders_with_text_bg_pixel(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        start = text.index("static const u16 *TextRowCached(const Engine *e, int pe, int bg, u64 key, int ty, u16 bgcnt,\n")
        fill = text[start:text.index("/* (wider canvas) each BG's hand-drawn picture", start)]
        # The fill computes exactly what the old per-pixel tile loop did, so
        # cached and direct pixels are identical by construction.
        self.assertIn("slot->px[i] = TextBgPixel(e, bg, bgcnt, dispcnt, i, ty);", fill)
        # A zero key never hits (empty slots read as key 0): captures drawn
        # without a key use the direct path, as before.
        self.assertIn("if (key == 0) {", fill)

    def test_plain_path_copies_spans_with_put(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        draw = text[text.index("static inline __attribute__((always_inline)) void DrawTextBgT"):
                    text.index("static void DrawTextBg(")]
        # Map-coordinate indexing makes any scroll offset, window shift or
        # segment layout hit; PUT keeps the front-layer/window semantics.
        self.assertIn("TextRowCached(e, e->isA ? 0 : 1, bg, sTextKey[e->isA ? 0 : 1][bg], ty, bgcnt, dispcnt)",
                      draw)
        self.assertIn("PUT(x + i, cachedRow[tx + i], flags);", draw)
        # Hit spans copy whole words like MergeT's fast path (flags 0 or
        # T_LAYER only; row test decides; layerOf kept), general path kept.
        self.assertIn("int wideOk = flags == 0 || flags == T_LAYER;", draw)
        self.assertIn("*(u32a *)(row + x + i) = pair;", draw)
        # Four transparent cached pixels write nothing under PUT's pc_
        # test, so they skip as one word-pair (log-14: 100% rows hit with
        # ~40% text on empty lines -- the loop overhead, not the decode).
        self.assertIn("u32 tz0 = *(const u32a *)(cachedRow + tx + i);", draw)
        self.assertIn("if ((tz0 | tz1) != 0) {", draw)
        # Without a cached row the old tile loop runs as before (captures
        # drawn without a key, or a failed malloc): still present verbatim.
        self.assertIn("u32 entryAddr = rowAddr + (u32)(tx >> 8) * 0x800 + (u32)((tx & 255) >> 3) * 2;",
                      draw)

    def test_keys_are_per_frame_per_engine(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        self.assertIn("static u64 sTextKey[2][4];", text)
        draw = text[text.index("static inline __attribute__((always_inline)) void DrawEngineT"):
                    text.index("static void DrawEngine(")]
        # One BgKey walk per BG per frame, shared by LookUpArt, the gpuBg
        # path and the row cache (was: one walk each = three).
        self.assertIn("u64 frameKey[4];", draw)
        self.assertIn("frameKey[bg] = (kind[bg] == BG_NONE || kind[bg] == BG_3D)", draw)
        self.assertIn("sTextKey[pe][bg] = kind[bg] == BG_TEXT ? FoldClearVariant(e, bg, frameKey[bg]) : 0;",
                      draw)
        self.assertIn("LookUpArt(e, kind, bgcnt, dispcnt, frameKey);", draw)
        self.assertIn("frameKey[gb] != 0 ? frameKey[gb] : BgKeyCached(e, gb, BG_TEXT, bgcnt[gb], dispcnt);",
                      draw)
        # Captures set their own fresh keys (they run outside any DrawEngineT,
        # whose per-frame keys they must not reuse).
        self.assertIn("sTextKey[engine ? 1 : 0][bg] = kind[bg] == BG_TEXT ? TextRowKey(&e, bg, bgcnt[bg], dispcnt) : 0;",
                      text)

    def test_profile_counts_setup_and_row_hits(self):
        header = self.HEADER.read_text()
        self.assertIn("uint64_t setupTicks;", header)
        self.assertIn("uint32_t rowHits;", header)
        self.assertIn("uint32_t rowMisses;", header)


class PerformanceSliceS4Tests(unittest.TestCase):
    """Slice S4: H-blank/timer/sound-pump/DMA attribution counters.

    Additive diagnostics only: paired tick reads, host-safe counters, no
    dispatch, walk-position, VCOUNT or save-format changes. Suggested by the
    implementing subagent; integrated and tested by the lead.
    """

    APP = REPO / "hgss" / "game" / "app"
    NITRO = REPO / "3ds_port" / "nitro" / "src"
    HEADER = REPO / "3ds_port" / "nitro" / "include" / "nitro_port.h"

    def test_hblank_irq_attribution_counters(self):
        text = (self.NITRO / "port_os.c").read_text()
        self.assertIn("static u64 sHBlankIrqBitTicks[NITRO_PORT_IRQ_BITS];", text)
        self.assertIn("static u32 sHBlankIrqBitCalls[NITRO_PORT_IRQ_BITS];", text)
        self.assertIn("if (sHBlankWalking) {", text)
        self.assertIn("sHBlankIrqBitTicks[bit] += PORT_NOW() - t;", text)
        self.assertIn("sHBlankIrqBitCalls[bit]++;", text)
        self.assertIn("sHBlankWalking = 1;", text)
        self.assertIn("sHBlankWalking = 0;", text)
        self.assertIn("void NitroPort_HBlankIrqByBit(NitroPortIrqTime *out)", text)
        self.assertIn("#define PORT_NOW() svcGetSystemTick()", text)
        self.assertIn("#define PORT_NOW() 0", text)

    def test_timer_sync_and_soundpump_counters(self):
        text = (self.NITRO / "port_timer.c").read_text()
        self.assertIn("sSyncTicks += PORT_NOW() - t0;", text)
        self.assertIn("sSyncCalls++;", text)
        self.assertIn("void NitroPort_SyncTimersStats(u64 *calls, u64 *ticks)", text)
        self.assertIn("void NitroPort_NoteSoundPumpTicks(u64 ticks)", text)
        self.assertIn("sSoundPumpTicks += ticks;", text)
        self.assertIn("sSoundPumpCalls++;", text)
        self.assertIn("void NitroPort_SoundPumpTickStats(u64 *ticks, u32 *calls)", text)
        self.assertIn("#define PORT_NOW() 0", text)

    def test_hblank_dma_transfer_count(self):
        text = (self.NITRO / "port_dma.c").read_text()
        self.assertIn("static u32 sHBlankDmaTransfers;", text)
        self.assertIn("sHBlankDmaTransfers++;", text)
        self.assertIn("u32 NitroPort_DmaHBlankTransfers(void)", text)

    def test_new_diagnostic_declarations(self):
        header = self.HEADER.read_text()
        self.assertIn("void NitroPort_HBlankIrqByBit(NitroPortIrqTime *out);", header)
        self.assertIn("} NitroPortIrqTime;", header)
        # DS-ABI u64 is aligned(4): the struct is 12 bytes, stride 12. The
        # shell mirrors it by hand (log-13's b1/b6/b7 garbage was a 16-byte
        # mirror reading a 12-byte table).
        self.assertIn('_Static_assert(sizeof(NitroPortIrqTime) == 12', header)
        shell = (self.APP / "main_3ds.c").read_text()
        self.assertIn('_Static_assert(sizeof(NitroPortIrqTime) == 12', shell)
        self.assertIn("aligned(4)", shell)
        self.assertIn("void NitroPort_SyncTimersStats(u64 *calls, u64 *ticks);", header)
        self.assertIn("void NitroPort_NoteSoundPumpTicks(u64 ticks);", header)
        self.assertIn("void NitroPort_SoundPumpTickStats(u64 *ticks, u32 *calls);", header)
        self.assertIn("u32 NitroPort_DmaHBlankTransfers(void);", header)

    def test_shell_reports_the_hblank_split(self):
        text = (self.APP / "main_3ds.c").read_text()
        # The pump is timed apart from game_us at its call site.
        self.assertIn("NitroPort_NoteSoundPumpTicks(svcGetSystemTick() - pumpStart);", text)
        perf = text[text.index("static void PerfLog"):text.index("static void WaitForFrameCap")]
        self.assertIn("NitroPort_HBlankIrqByBit(bits);", perf)
        self.assertIn("NitroPort_SyncTimersStats(&syncCalls, &syncTicks);", perf)
        self.assertIn("NitroPort_SoundPumpTickStats(&pumpTicks, &pumpCalls);", perf)
        self.assertIn("NitroPort_DmaHBlankTransfers();", perf)
        self.assertIn('"[perf] hbirq: b0 %.2fms/%u b1 %.2fms/%u b2 %.2fms/%u b3 %.2fms/%u "',
                      perf)
        # Last-walk values, never differenced: the per-bit table resets every
        # walk, so a per-interval diff underflows (log-11's garbage line).
        self.assertNotIn("sPerfIrq[", perf)
        self.assertNotIn("sPerfIrq[", text[text.index("static void PerfLog"):])

    def test_o3dsched_display_first_demotion(self):
        text = (self.APP / "main_3ds.c").read_text()
        # Opt-in experiment only: same-core threads don't preempt, so the
        # always-ready geometry worker starves the display on a saturated
        # core 1 (log-9's 0.0 composes/s). Demoting geometry below the
        # event-driven display is the fix candidate; default unchanged.
        self.assertIn('IniValue("o3dsched"', text)
        self.assertIn("sIniSched = 0;", text)
        self.assertIn("sIniSched = 1;", text)
        self.assertIn("o3dsched is Old-3DS-only", text)
        geo = text[text.index("static bool GpuStartGeometry"):text.index("static void GpuStopGeometry")]
        # Demoted below the display (0x32) on an Old 3DS only; New 3DS keeps
        # its geometry placement.
        self.assertIn("sIniSched ? priority + 3 : priority - 1", geo)
        self.assertIn("threadCreate(GeoMain, NULL, 64 * 1024, priority - 1, 2, false)", geo)

    def test_hot_path_has_no_integer_divide_or_modulo(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        draw = text[text.index("static inline __attribute__((always_inline)) void DrawEngineT"):
                    text.index("static void DrawEngine(")]
        tbg = text[text.index("static inline __attribute__((always_inline)) void DrawTextBgT"):
                   text.index("static void DrawTextBg(")]
        # Text maps are 256/512 wide (sTextSizes), so wraparound is a mask;
        # ARMv6 has no integer divider (a libcall per line per BG).
        self.assertIn("& (mapW - 1)", draw + tbg)
        self.assertNotIn("% mapW", tbg)
        # VRAM-display loop: W is the 256-pixel DS width.
        self.assertIn("(i >> 8) * e.width + e.origin + (i & 255)", text)


class PerformanceSliceS3Tests(unittest.TestCase):
    """Slice S3: merge/finish/OBJ hot paths, all provably pixel-identical.

    The T_LAYER merge extension is flag-independent (no T_WINDOW bit, no
    prio filter, layerOf written exactly where PUT would write it). The
    SnapshotOver/finish skips fire only where the skipped loop provably
    writes nothing else. The art-line skip fires only where the art loop
    would continue on every group.
    """

    NITRO = REPO / "3ds_port" / "nitro" / "src"

    def _draw(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        return text[text.index("static inline __attribute__((always_inline)) void DrawEngineT"):
                    text.index("static void DrawEngine(")]

    def test_opaque_pair_merge_covers_t_layer(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        merge = text[text.index("static inline __attribute__((always_inline)) void MergeT("):
                     text.index("static void Merge(Target *t, const u16 *line,")]
        # flags == 0 (as before) plus exactly T_LAYER (no window bit, so no
        # enable check; prioOf NULL, so no priority check): the general path
        # below this is PUT without window/priority, which the word and
        # pixel paths reproduce plus layerOf.
        self.assertIn("if ((flags == 0 || flags == T_LAYER) && prioOf == NULL &&", merge)
        self.assertIn("int writeLayer = (flags & T_LAYER) != 0;", merge)
        self.assertIn("layerOf[x] = layer;", merge)
        self.assertIn("layerOf[x + 1] = layer;", merge)
        # T_PLAIN and any window bit still take the general path.
        self.assertNotIn("flags & ~T_LAYER", merge)

    def test_snapshot_over_skips_effect_free_rows(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        snap = text[text.index("static void SnapshotOver("):text.index(
            "static inline __attribute__((always_inline)) void DrawEngineT")]
        # The non-blend loop needs effect >= 2 AND a first-target bit; when
        # neither can fire it only fills alpha with 16.
        self.assertIn("if (effect < 2 || (bldcnt & 0x1Fu) == 0) {", snap)
        self.assertIn("memset(alpha, 16, (size_t)w);", snap)

    def test_finish_falls_back_to_plain_resolve(self):
        draw = self._draw()
        # The brighten loop darkens even with effect 0/1 when a first-target
        # bit matches (existing behavior, kept), so the plain resolve is
        # only identical with no first-target bits at all (layerOf holds
        # 0-4, backdrop is 5: bits 0-5 covered).
        self.assertIn("if (!(flags & T_LAYER) || (bldcnt & 0x3Fu) == 0) {", draw)

    def test_obj_art_line_skip(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        draw = text[text.index("static BOOL DrawObjs("):text.index(
            "/* ------------------------------------------------------------ the frame */")]
        # Same ay/rows test the art loop starts each group with: skipping
        # writes nothing the loop wouldn't, including the sprites copy.
        self.assertIn("int artHere = 0, ak;", draw)
        self.assertIn("int aay = ArtWrap(y - f->art[ak].dy, H, 256);", draw)
        self.assertIn("if (!artHere) {", draw)


class GpuBgAttributionTests(unittest.TestCase):
    """Wave 2 S5 first code: why text BGs stay on software.

    GpuBgOf runs once per engine-A frame with one silent `return -1` per
    correctness condition; the counters say which fires on the field, and
    that picks the next S5 slice. Zero behavior change: reason codes are
    negative (both downstream uses guard `>= 0`), and counting rides the
    existing profile snapshot/diff pattern.
    """

    APP = REPO / "hgss" / "game" / "app"
    NITRO = REPO / "3ds_port" / "nitro" / "src"
    HEADER = REPO / "3ds_port" / "nitro" / "include" / "nitro_port_render2d.h"

    def test_reject_enum_covers_every_check(self):
        header = self.HEADER.read_text()
        for name in ("SETUP", "HBLANK", "OFF", "MODE", "MOSAIC",
                     "EXT", "BLEND", "BEHIND", "BETWEEN", "OBJ", "OBJBLEND"):
            self.assertIn(f"NITRO_GPUBG_REJECT_{name}", header)
        self.assertIn("NITRO_GPU_BG_REJECTS", header)
        # Any map size routes (the picture and the quad are map-sized);
        # mosaic and ext palettes stay software.
        self.assertNotIn("NITRO_GPUBG_REJECT_SIZE", header)
        # Profile counters ride the shared struct (stdint types only: the
        # DS-ABI u64 alignment split must not move them; see the hbirq fix).
        self.assertIn("uint32_t gpuBgTaken;", header)
        self.assertIn("uint32_t gpuBgReject[NITRO_GPU_BG_REJECTS];", header)
        self.assertNotIn("u64 gpuBg", header)

    def test_every_reject_returns_its_reason(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        gpu = text[text.index("static int GpuBgOf("):text.index("/* The extended palette slot")]
        self.assertEqual(gpu.count("return -(1 + NITRO_GPUBG_REJECT_"), 11)
        for name in ("SETUP", "HBLANK", "OFF", "MODE", "MOSAIC",
                     "EXT", "BLEND", "BEHIND", "BETWEEN", "OBJ", "OBJBLEND"):
            self.assertIn(f"return -(1 + NITRO_GPUBG_REJECT_{name});", gpu)
        self.assertNotIn("return -1;", gpu)
        # Blend mode reported out (NULL-tolerant: the change-detection
        # caller doesn't need it).
        self.assertIn("static int GpuBgOf(const u8 *io, const u8 *oam, int lines, int wanted, int *blend)", gpu)
        self.assertIn("if (blend != NULL) {", gpu)

    def test_draw_counts_taken_and_rejects(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        self.assertIn("prof->gpuBgTaken++;", text)
        self.assertIn("prof->gpuBgReject[-1 - gb]++;", text)
        self.assertIn("prof->gpuBgReject[NITRO_GPUBG_REJECT_MODE]++;", text)
        # The change-detection caller mirrors the old -1 contract (its hash
        # only gets more precise per reason) and counts nothing.
        split = text[text.index("int NitroPort_Render2DSplitIfChanged("):]
        self.assertNotIn("gpuBgTaken", split)
        self.assertNotIn("gpuBgReject", split)
        # Both downstream uses still guard >= 0, so reasons stay negative.
        self.assertIn("if (isA && gpuBg >= 0) {", text)

    def test_shell_reports_rejects_before_the_snapshot(self):
        text = (self.APP / "main_3ds.c").read_text()
        perf = text[text.index("static void PerfLog"):text.index("static void WaitForFrameCap")]
        self.assertIn('"[perf] gpubg: taken %u, reject ', perf)
        # Cumulative counters, differenced like rows -- and read before the
        # snapshot below overwrites them (always-zero otherwise).
        self.assertLess(perf.index("[perf] gpubg:"),
                        perf.index("sPerf2d[0] = *a;"))


class GpuBgNominateTests(unittest.TestCase):
    """Wave 2 S5c: the port nominates text BGs for the quad itself.

    The game flags repeating weather BGs; the port additionally nominates
    1-3. GpuBgOf (unchanged) still proves each one, so nomination moves no
    correctness boundary: untaken BGs draw in software exactly as before.
    """

    APP = REPO / "hgss" / "game" / "app"
    NITRO = REPO / "3ds_port" / "nitro" / "src"
    WIDE_H = REPO / "3ds_port" / "nitro" / "include" / "nitro_port_wide.h"

    def test_nominate_shares_the_repeating_vetoes(self):
        text = (self.NITRO / "port_wide.c").read_text()
        nom = text[text.index("int NitroPort_NominateGpuBgs(void)\n{"):
                    text.index("void NitroPort_SetWideScreen(int extra)\n{")]
        self.assertIn('!NitroPort_OverlayLoadedNamed("overlay5") || NitroPort_OverlayLoadedNamed("battle")',
                      nom)
        self.assertIn("sPartCount[0] > 0", nom)
        self.assertIn("return laidOut ? 0xE : 0;", nom)
        self.assertIn("int NitroPort_NominateGpuBgs(void);", self.WIDE_H.read_text())

    def test_shell_nominates_beside_the_game_flags(self):
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("split.gpuBgs = NitroPort_RepeatingBgs() | NitroPort_NominateGpuBgs();",
                      text)


class GpuBgMapSizeTests(unittest.TestCase):
    """Wave 2 S5b: any text map size + the no-blend flat quad.

    The picture, upload and UV math are map-sized (256x256 path
    byte-identical); a BG nowhere in the blend draws opaque instead of
    staying software. Fallback (mosaic, ext palettes, windows, H-blank,
    brightness-on-it) is unchanged.
    """

    APP = REPO / "hgss" / "game" / "app"
    NITRO = REPO / "3ds_port" / "nitro" / "src"
    HEADER = REPO / "3ds_port" / "nitro" / "include" / "nitro_port_render2d.h"
    PICA = REPO / "3ds_port" / "game" / "app" / "port_pica.h"
    COMPOSE = REPO / "3ds_port" / "game" / "app" / "port_compose_pica.c"

    def test_split_and_layers_carry_size_and_blend(self):
        header = self.HEADER.read_text()
        self.assertIn("int gpuBgW, gpuBgH, gpuBgBlend;", header)
        pica = self.PICA.read_text()
        self.assertIn("int gpuBgW, gpuBgH, gpuBgBlend;", pica)

    def test_drawer_sets_size_from_bgcnt_and_blend_from_gpubg(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        draw = text[text.index("if (split->has3d && split->gpuBgs) {"):
                     text.index("} else if (e->isA && kind[0] == BG_3D) {")]
        self.assertIn("int bgBlend = 1;", draw)
        self.assertIn("GpuBgOf(io, e->oam, e->lines != NULL, split->gpuBgs, &bgBlend)", draw)
        self.assertIn("split->gpuBgW = sTextSizes[(bgcnt[gb] >> 14) & 3][0];", draw)
        self.assertIn("split->gpuBgH = sTextSizes[(bgcnt[gb] >> 14) & 3][1];", draw)
        self.assertIn("split->gpuBgBlend = bgBlend;", draw)

    def test_picture_decode_is_size_general(self):
        text = (self.NITRO / "port_render2d.c").read_text()
        pic = text[text.index("int NitroPort_Render2DBgPicture("):
                     text.index("int NitroPort_Render2DLayer(")]
        # Screen-block aware entries, map-stride rows; 256x256 reduces to
        # the old linear form (block offset 0, stride 256).
        self.assertIn("int mapW = sTextSizes[(bgcnt >> 14) & 3][0], mapH = sTextSizes[(bgcnt >> 14) & 3][1];", pic)
        self.assertIn("(u32)((tx >> 5) + (ty >> 5) * (tilesX >> 5)) * 0x800", pic)
        self.assertIn("out + (size_t)(ty * 8 + y) * (size_t)mapW + tx * 8", pic)
        # Palette/pack paths untouched (test_render.c's 256 goldens hold).
        self.assertIn("c = Rd16(e.bgPltt, index * 2);", pic)

    def test_compositor_upload_draw_and_equality_are_size_aware(self):
        text = self.COMPOSE.read_text()
        self.assertIn("NitroPort_Tile16(l->gpuBgPicture, l->gpuBgW, l->gpuBgH, set->bgTex, BG_TEX_W);", text)
        self.assertIn("NitroPort_Double2xTiled16(l->gpuBgPicture, 256, 256, set->bgTex, BG_TEX_W);", text)
        self.assertIn("a->gpuBgW == b->gpuBgW && a->gpuBgH == b->gpuBgH && a->gpuBgBlend == b->gpuBgBlend;", text)
        # Texels per map pixel (2 for doubled 256 maps, 1 for 1:1 bigger
        # ones); UV wraps at the texture edge = the map edge either way.
        self.assertIn("int texel = (mapW == 256 && mapH == 256) ? 2 : 1;", text)
        self.assertIn("bu1 = bu0 + (float)canvasW * (float)texel / (float)BG_TEX_W;", text)
        self.assertIn("bvTop = 1.0f - (float)(vofs - top) * (float)texel / (float)BG_TEX_W;", text)
        # Flat quad: same 1-bit-alpha replace call, white vertex color.
        self.assertIn("} else {", text)
        self.assertIn("bu0, bvTop, bu1, bvBottom, 0xFFFFFFFFu);", text)

    def test_shell_holds_a_whole_map_and_canonicalizes(self):
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("static uint16_t picture[512 * 512];", text)
        self.assertIn("layers->gpuBgW = split.gpuBgW >= 256 ? split.gpuBgW : 256;", text)
        self.assertIn("layers->gpuBgH = split.gpuBgH >= 256 ? split.gpuBgH : 256;", text)
        self.assertIn("layers->gpuBgBlend = split.gpuBgBlend;", text)


class OutputParsingTests(unittest.TestCase):
    def test_error_count(self):
        text = ("a.c:1:1: warning: something\n"
                "a.c:2:1: error: bad type\n"
                "a.c:3:1: fatal error: not found\n")
        self.assertEqual(hg.error_count(text), 2)

    def test_unresolved_symbols_unique_and_ordered(self):
        text = ("m.o: in function `x':\n"
                "m.c:(.text+0x1): undefined reference to `GF_Assert'\n"
                "m.c:(.text+0x2): undefined reference to `Foo_Bar'\n"
                "n.c:(.text+0x3): undefined reference to `GF_Assert'\n"
                "ld: something else\n")
        self.assertEqual(hg.unresolved_symbols(text), ["GF_Assert", "Foo_Bar"])

    def test_no_unresolved(self):
        self.assertEqual(hg.unresolved_symbols("nothing here\n"), [])


if __name__ == "__main__":
    unittest.main()


class PakReaderTests(unittest.TestCase):
    """The program's build-private libpak copy (tools/hgss_game.py)."""

    def test_supported_digests_match_verify_rom(self):
        import importlib.util

        spec = importlib.util.spec_from_file_location(
            "hgss_verify_rom", REPO / "builder" / "verify_rom.py")
        verify_rom = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(verify_rom)
        self.assertEqual(hg.supported_rom_sha1s(), sorted(verify_rom.SUPPORTED))
        self.assertIn("4fcded0e2713dc03929845de631d0932ea2b5a37",
                      hg.supported_rom_sha1s())
        self.assertIn("f8dc38ea20c17541a43b58c5e6d18c1732c7e582",
                      hg.supported_rom_sha1s())

    def test_reader_keeps_platinum_digests_and_adds_hgss(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = hg.pak_reader_source(Path(tmp))
            text = path.read_text(encoding="utf-8")
            self.assertIn("Generated by tools/hgss_game.py", text)
            for digest in hg.supported_rom_sha1s():
                self.assertIn(f'matches_sha1(digest, "{digest}")', text)
            # The vendored file's own allowlist stays in the table.
            self.assertIn("0862ec35b24de5c7e2dcb88c9eea0873110d755c", text)
            self.assertIn("ce81046eda7d232513069519cb2085349896dec7", text)
            body = text[text.index("static int supported_sha1"):]
            body = body[:body.index("\n}")]
            self.assertEqual(body.count("return "), 1)
            self.assertLess(body.index("4fcded0e"), body.index("0862ec35"))

    def test_reader_source_shape_is_checked(self):
        original = hg.PAK_READER
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp) / "pak.c"
            temp.write_text("int not_the_reader(void) { return 0; }\n")
            hg.PAK_READER = temp
            try:
                with self.assertRaisesRegex(ValueError, "shape changed"):
                    hg.pak_reader_source(Path(tmp) / "out")
            finally:
                hg.PAK_READER = original


class ThreeDsxTests(unittest.TestCase):
    """The .3dsx packaging step after a successful 3DS link."""

    def test_command_is_tool_elf_output(self):
        cmd = hg.three_dsx_command(Path("/dkp/tools/bin/3dsxtool"),
                                   Path("/out/game.elf"), Path("/out/game.3dsx"))
        self.assertEqual(cmd, ["/dkp/tools/bin/3dsxtool",
                               "/out/game.elf", "/out/game.3dsx"])

    def test_command_carries_the_smdh_when_present(self):
        cmd = hg.three_dsx_command(Path("/dkp/tools/bin/3dsxtool"),
                                   Path("/out/game.elf"), Path("/out/game.3dsx"),
                                   Path("/out/game.smdh"))
        self.assertEqual(cmd[-1], "--smdh=/out/game.smdh")
        self.assertEqual(len(cmd), 4)

    def test_no_smdh_tool_means_no_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            old = os.environ.get("DEVKITPRO")
            os.environ["DEVKITPRO"] = tmp
            try:
                self.assertIsNone(hg.make_smdh(Path(tmp) / "game.elf"))
            finally:
                if old is None:
                    os.environ.pop("DEVKITPRO", None)
                else:
                    os.environ["DEVKITPRO"] = old

    def test_host32_is_not_packaged(self):
        self.assertIsNone(hg.package_three_dsx(Path("/nonexistent.elf"), "host32"))

    def test_packages_with_the_generic_smdh_when_the_tool_exists(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            tool = root / "tools" / "bin" / "3dsxtool"
            smdh_tool = root / "tools" / "bin" / "smdhtool"
            tool.parent.mkdir(parents=True)
            tool.write_text(fake_3dsx_tool_script(MINIMAL_3DSX))
            tool.chmod(0o755)
            smdh_tool.write_text(
                "#!/bin/sh\nfor a in \"$@\"; do last=$a; done\nprintf 'smdh' > \"$last\"\n")
            smdh_tool.chmod(0o755)
            elf = root / "pokeheartgold-3ds.elf"
            elf.write_bytes(b"elf")
            old = os.environ.get("DEVKITPRO")
            os.environ["DEVKITPRO"] = str(root)
            try:
                dsx = hg.package_three_dsx(elf, "3ds")
            finally:
                if old is None:
                    os.environ.pop("DEVKITPRO", None)
                else:
                    os.environ["DEVKITPRO"] = old
            self.assertEqual(dsx, root / "pokeheartgold.3dsx")
            self.assertEqual(dsx.read_bytes(), MINIMAL_3DSX)
            self.assertEqual((root / "pokeheartgold-3ds.smdh").read_bytes(), b"smdh")

    def test_packages_next_to_the_elf_without_the_3ds_suffix(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            tool = root / "tools" / "bin" / "3dsxtool"
            tool.parent.mkdir(parents=True)
            tool.write_text(fake_3dsx_tool_script(MINIMAL_3DSX))
            tool.chmod(0o755)
            elf = root / "pokeheartgold-3ds.elf"
            elf.write_bytes(b"elf")
            old = os.environ.get("DEVKITPRO")
            os.environ["DEVKITPRO"] = str(root)
            try:
                dsx = hg.package_three_dsx(elf, "3ds")
            finally:
                if old is None:
                    os.environ.pop("DEVKITPRO", None)
                else:
                    os.environ["DEVKITPRO"] = old
            self.assertEqual(dsx, root / "pokeheartgold.3dsx")
            self.assertEqual(dsx.read_bytes(), MINIMAL_3DSX)


class BuildIdTests(unittest.TestCase):
    """Commit identity: every binary and staged file traces to its commit."""

    def test_git_build_info_matches_the_checkout(self):
        short, full, dirty, status = hg.git_build_info()
        rev = subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                             cwd=REPO, capture_output=True, text=True)
        if rev.returncode != 0:
            self.assertEqual((short, full), ("unknown", "unknown"))
            return
        self.assertEqual(short, rev.stdout.strip())
        porcelain = subprocess.run(["git", "status", "--porcelain"],
                                   cwd=REPO, capture_output=True, text=True)
        self.assertEqual(dirty, bool(porcelain.stdout.strip()))
        self.assertEqual(status, porcelain.stdout)

    def test_build_id_string_marks_dirty_trees(self):
        self.assertEqual(hg.build_id_string("abc1234", False), "abc1234")
        self.assertEqual(hg.build_id_string("abc1234", True), "abc1234-dirty")

    def test_header_written_only_on_change(self):
        with tempfile.TemporaryDirectory() as tmp:
            gen = Path(tmp)
            info = ("abc1234", "f" * 40, True, "")
            self.assertEqual(hg.write_build_id_header(gen, info), "abc1234-dirty")
            header = gen / "build_id.h"
            self.assertIn('#define HGSS_BUILD_ID "abc1234-dirty"', header.read_text())
            mtime = header.stat().st_mtime_ns
            self.assertEqual(hg.write_build_id_header(gen, info), "abc1234-dirty")
            self.assertEqual(header.stat().st_mtime_ns, mtime)
            self.assertEqual(hg.write_build_id_header(gen, ("def5678", "e" * 40, False, "")),
                             "def5678")
            self.assertIn('#define HGSS_BUILD_ID "def5678"', header.read_text())

    def test_build_info_sidecar_pairs_artifact_with_commit(self):
        import hashlib
        with tempfile.TemporaryDirectory() as tmp:
            dsx = Path(tmp) / "pokeheartgold.3dsx"
            dsx.write_bytes(MINIMAL_3DSX)
            manifest = hg.write_build_info(dsx, "heartgold", "3ds",
                                           ("abc1234", "f" * 40, True, " M x\n"))
            self.assertEqual(manifest, Path(tmp) / "pokeheartgold.build-info.txt")
            text = manifest.read_text()
            self.assertIn("build: abc1234-dirty\n", text)
            self.assertIn("sha: " + "f" * 40 + "\n", text)
            self.assertIn("sha256: " + hashlib.sha256(MINIMAL_3DSX).hexdigest() + "\n",
                          text)

    def test_boot_banner_logs_the_build(self):
        text = (REPO / "hgss" / "game" / "app" / "main_3ds.c").read_text()
        self.assertIn('starting (build %s)", HGSS_BUILD_ID', text)
        self.assertIn('__has_include("build_id.h")', text)
        # Host-tool builds without the generated header still compile.
        self.assertIn('#define HGSS_BUILD_ID "unknown"', text)


class StageTests(unittest.TestCase):
    """Unique staged names: two builds never share a filename."""

    def test_staged_base_names_carry_the_commit(self):
        self.assertEqual(st.staged_base("pokeheartgold", "abc1234", False),
                         "pokeheartgold-abc1234")
        self.assertEqual(st.staged_base("pokesoulsilver", "abc1234", True),
                         "pokesoulsilver-abc1234-dirty")

    def test_pick_staged_path_skips_identical_dedupes_divergent(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp)
            path, already = st.pick_staged_path(dest, "pokeheartgold-abc1234", "d")
            self.assertEqual((path.name, already), ("pokeheartgold-abc1234.3dsx", False))
            path.write_bytes(b"one")
            same, already = st.pick_staged_path(dest, "pokeheartgold-abc1234",
                                                st.sha256_of(path))
            self.assertEqual((same, already), (path, True))
            other, already = st.pick_staged_path(dest, "pokeheartgold-abc1234",
                                                 "different")
            self.assertEqual((other.name, already),
                             ("pokeheartgold-abc1234-2.3dsx", False))

    def test_source_build_info_prefers_the_build_time_sidecar(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "pokeheartgold.3dsx"
            src.write_bytes(MINIMAL_3DSX)
            (Path(tmp) / "pokeheartgold.build-info.txt").write_text(
                "build: abc1234-dirty\nsha: " + "f" * 40 + "\nstatus:\n M x\n")
            short, full, dirty, status, inferred = st.source_build_info(src)
            self.assertEqual((short, full, dirty, inferred),
                             ("abc1234", "f" * 40, True, False))
            self.assertIn("M x", status)

    def test_stage_file_validates_names_manifests_and_skips(self):
        import stat as statmod
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "pokeheartgold.3dsx"
            src.write_bytes(MINIMAL_3DSX)
            (Path(tmp) / "pokeheartgold.build-info.txt").write_text(
                "build: abc1234\nsha: " + "f" * 40 + "\nstatus:\n")
            dest = Path(tmp) / "sd"
            path, already = st.stage_file(src, dest)
            self.assertEqual((path.name, already),
                             ("pokeheartgold-abc1234.3dsx", False))
            self.assertEqual(st.sha256_of(path), st.sha256_of(src))
            self.assertEqual(statmod.S_IMODE(path.stat().st_mode), 0o666)
            sidecar = dest / "pokeheartgold-abc1234.build-info.txt"
            self.assertIn("sha256: " + st.sha256_of(src), sidecar.read_text())
            _, already = st.stage_file(src, dest)
            self.assertTrue(already)


class ThreeDsxValidationTests(unittest.TestCase):
    """The check that caught the folded -0x06800000 relocation."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write(self, name, payload):
        path = self.root / name
        path.write_bytes(payload)
        return path

    def test_clean_file_has_no_problems(self):
        self.assertEqual(hg.validate_3dsx(self.write("a.3dsx", MINIMAL_3DSX)), [])

    def test_absolute_word_outside_the_image_is_reported(self):
        # 0xfa607000 is exactly what NitroPort_LcdcVram - 0x06800000 packs to.
        path = self.write("bad.3dsx", one_word_3dsx(0xFA607000))
        problems = hg.validate_3dsx(path)
        self.assertEqual(len(problems), 1)
        self.assertIn("0xfa607000", problems[0])
        self.assertIn("absolute", problems[0])

    def test_an_ordinary_pointer_is_accepted(self):
        self.assertEqual(hg.validate_3dsx(self.write("ok.3dsx", one_word_3dsx(0x00123456))), [])

    def test_relative_word_with_a_bad_subtype_is_reported(self):
        header = struct.pack("<IHHIIIIII", 0x58534433, 0x20, 8, 0, 0, 4, 0, 0, 0)
        heads = struct.pack("<II", 0, 1) + struct.pack("<II", 0, 0) + struct.pack("<II", 0, 0)
        path = self.write("rel.3dsx", header + heads + struct.pack("<I", 0x20000000)
                          + struct.pack("<HH", 0, 1))
        problems = hg.validate_3dsx(path)
        self.assertEqual(len(problems), 1)
        self.assertIn("relative", problems[0])

    def test_garbage_is_not_a_3dsx(self):
        with self.assertRaisesRegex(ValueError, "magic"):
            hg.validate_3dsx(self.write("x.3dsx", b"dsx"))

    def test_packaging_refuses_a_file_the_loader_would_reject(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            tool = root / "tools" / "bin" / "3dsxtool"
            tool.parent.mkdir(parents=True)
            tool.write_text(fake_3dsx_tool_script(one_word_3dsx(0xFA607000)))
            tool.chmod(0o755)
            elf = root / "pokeheartgold-3ds.elf"
            elf.write_bytes(b"elf")
            old = os.environ.get("DEVKITPRO")
            os.environ["DEVKITPRO"] = str(root)
            try:
                with self.assertRaises(hg.ThreeDsxError):
                    hg.package_three_dsx(elf, "3ds")
            finally:
                if old is None:
                    os.environ.pop("DEVKITPRO", None)
                else:
                    os.environ["DEVKITPRO"] = old


class RomHeaderTests(unittest.TestCase):
    """The cartridge header the shell hands the game."""

    def test_game_codes_cover_both_games(self):
        header = (REPO / "hgss" / "game" / "app" / "heartgold.h").read_text()
        codes = re.findall(r'#define HEARTGOLD_ROM_GAME_CODE "([A-Z0-9]{4})"', header)
        self.assertEqual(sorted(codes), ["IPGE", "IPKE"])  # SoulSilver, HeartGold
        self.assertIn("#if defined(SOULSILVER)", header)
        source = (REPO / "hgss" / "game" / "app" / "heartgold.c").read_text()
        self.assertIn("HEARTGOLD_ROM_GAME_CODE", source)
        self.assertNotIn('"IPGE"', source)
        self.assertNotIn('"IPKE"', source)

    def test_game_code_agrees_with_the_builder(self):
        import importlib.util

        spec = importlib.util.spec_from_file_location(
            "hgss_verify_rom", REPO / "builder" / "verify_rom.py")
        verify_rom = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(verify_rom)
        self.assertEqual(verify_rom.GAME_CODES["IPKE"], "Pokémon HeartGold (USA)")
        self.assertEqual(verify_rom.GAME_CODES["IPGE"], "Pokémon SoulSilver (USA)")
