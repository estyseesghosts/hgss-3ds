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

        port_gx3d_pica.c draws a scene into an off-screen PICA target and
        hands the image to the port's image API. port_pica_abi.c is linked
        with it: it exposes libctru's struct layout to the port (the port is
        built with -fno-short-enums, libctru is not).
        """
        names = [p.name for p in hg.app_sources("3ds")]
        self.assertIn("port_gx3d_pica.c", names)
        self.assertIn("port_pica_abi.c", names)
        self.assertIn("pica_classic_stubs.c", names)
        self.assertNotIn("pica_scene.c", names)
        for name in ("port_gx3d_pica.c", "port_pica_abi.c"):
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
        self.assertIn('printf("\\n pokeheartgold-3ds', text)
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
        for hook in ("NitroPort_RenderScreens", "NitroPort_CardFlush",
                     "NitroPort_SetButtons", "NitroPort_SetTouch"):
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
        self.assertIn("ndspChnSetRate(0, (float)AUDIO_RATE)", text)
        # Above the game: a 20-30 ms composite must not starve the mixer.
        self.assertIn("AUDIO_THREAD_PRIORITY 0x2e", text)

    def test_shell_skips_only_the_composite_never_the_blit(self):
        """Both screens are copied every frame; only the composite may skip.

        The framebuffer is double buffered. Skipping the copy of an unchanged
        screen leaves the other buffer showing an older picture and the
        display alternates between them -- the flicker the intro showed, and
        a stale/black field in the bedroom. A new 3D image also forces engine
        A, because the port's own uses3D test does not cover every field.
        """
        text = (self.APP / "main_3ds.c").read_text()
        # Engine A can show 3D and is always composed; only B is skipped.
        self.assertIn("NitroPort_Render2D(0,", text)
        self.assertIn("NitroPort_Render2DIfChanged(1,", text)
        # The image counter is logged so a hardware run can prove the GPU
        # published images (it stayed 0 while no worker existed).
        self.assertIn("NitroPort_Gx3dImageCount()", text)
        self.assertNotIn("NitroPort_RenderScreens(top, bottom);", text)
        frame = text[text.index("static void DrawFrame"):]
        frame = frame[:frame.index("\n}")]
        self.assertIn("Blit(top,", frame)
        self.assertIn("Blit(bottom,", frame)
        self.assertIn("gfxSwapBuffers()", frame)
        self.assertEqual(frame.count("if (topFb != NULL)"), 1)
        self.assertEqual(frame.count("if (bottomFb != NULL)"), 1)

    def test_shell_runs_the_gpu_and_geometry_threads(self):
        """The 3D layer is a worker, not a side effect of the composite.

        On the 3DS the port only converts an image that was handed over by a
        GPU worker; with none, AcquireImage always returned NULL and engine A
        drew black. The shell now starts the geometry thread (scene building
        off the game's thread) and the GPU thread (PICA init, PicaRender,
        submit, convert), flushes the geometry once a frame, and wakes the GPU
        from the port's SWAP callback.
        """
        text = (self.APP / "main_3ds.c").read_text()
        self.assertIn("NitroPort_PicaInit()", text)
        self.assertIn("NitroPort_PicaRender(scene, &dropped)", text)
        self.assertIn("NitroPort_Gx3dFlush()", text)
        self.assertIn("NitroPort_Gx3dThreadMain(&sGeoRunning)", text)
        self.assertIn("NitroPort_Gx3dConvertPending()", text)
        # The camera-plane workaround is gone: the port gives the exact
        # (0,0,0,0) vertex a positive w itself, so the scene is passed on
        # untouched.
        self.assertNotIn("Hgss_PicaDropBehindCamera", text)
        self.assertIn("NitroPort_PicaRender(scene, &dropped)", text)
        # The GPU thread gets the New 3DS's free core, not the game's.
        self.assertIn("sNew3ds ? 2 : -2", text)
        # A strong override: the port's default is a weak no-op.
        self.assertIn("void NitroPort_Gx3dScenePublished(void)", text)
        self.assertNotIn("weak", text[text.index("void NitroPort_Gx3dScenePublished"):
                                    text.index("void NitroPort_Gx3dScenePublished") + 200])
        # Flush happens before the composite that reads the image.
        frame = text[text.index("void NitroPort_OnFrame"):]
        frame = frame[:frame.index("\n}")]
        self.assertLess(frame.index("NitroPort_Gx3dFlush()"),
                        frame.index("DrawFrame()"))

    def test_3d_camera_point_gets_a_positive_w_in_the_port(self):
        """The exact (0,0,0,0) vertex must reach the GPU with w > 0.

        The clipper keeps a vertex at the camera (w + v < 0 is false at 0),
        and the PC reference and the DS title put it at screen (0,0). The
        PICA cannot divide by w = 0, so the scene keeps that screen position
        but stores the smallest positive w (20.12). The scene is passed to
        the GPU untouched: the old HGSS-side drop workaround is gone.
        """
        source = (REPO / "3ds_port" / "nitro" / "src" / "port_gx3d.c").read_text()
        self.assertIn("v->clip[0]==0 && v->clip[1]==0 && v->clip[2]==0 && "
                      "v->clip[3]==0", source)
        self.assertIn("out.clip[3]=1;", source)
        # The workaround's file and call are gone from the program.
        self.assertFalse((self.APP / "pica_scene.c").exists())
        self.assertNotIn("Hgss_PicaDropBehindCamera",
                         (self.APP / "main_3ds.c").read_text())

    def test_shell_falls_back_to_2d_if_the_gpu_will_not_start(self):
        """A failed PICA init or thread must not take the game down.

        The previous hardware builds drew both screens without 3D; that is
        still what a failed GPU start leaves, with a line in the log.
        """
        text = (self.APP / "main_3ds.c").read_text()
        gpu = text[text.index("static void GpuMain"):]
        gpu = gpu[:gpu.index("\nstatic bool GpuStart")]
        self.assertIn("if (!NitroPort_PicaInit())", gpu)
        self.assertIn("sGpuRunning = false", gpu)
        start = text[text.index("static bool GpuStart"):]
        start = start[:start.index("\nstatic void GpuStop")]
        self.assertIn("if (!sGpuRunning)", start)
        self.assertIn("return false", start)
        # Geometry failure returns the engine to the game's thread.
        geo = text[text.index("static bool GpuStartGeometry"):]
        geo = geo[:geo.index("\nstatic void GpuStopGeometry")]
        self.assertIn("NitroPort_Gx3dStopThread()", geo)


        """NitroPort_Log writes to stderr, which the console draws on screen.

        A console initialised at startup put every log line over the bottom
        screen; it belongs to the "can't start" path only.
        """
        text = (self.APP / "main_3ds.c").read_text()
        self.assertEqual(text.count("consoleInit("), 1)
        error = text[text.index("static void PakErrorScreen"):]
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

        Both blits and gfxSwapBuffers stay unconditional and ahead of the wait;
        a frame that already overran returns from the wait immediately, and one
        more than a whole period behind re-anchors the deadline instead of
        bursting to catch up.
        """
        text = (self.APP / "main_3ds.c").read_text()
        draw = text[text.index("static void DrawFrame"):]
        draw = draw[:draw.index("\n}")]
        self.assertIn("gfxSwapBuffers()", draw)
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

    def test_game_code_is_heartgold(self):
        header = (REPO / "hgss" / "game" / "app" / "heartgold.h").read_text()
        match = re.search(r'#define HEARTGOLD_ROM_GAME_CODE "([A-Z0-9]{4})"', header)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(1), "IPKE")   # HG US; SoulSilver is IPGE
        source = (REPO / "hgss" / "game" / "app" / "heartgold.c").read_text()
        self.assertIn("HEARTGOLD_ROM_GAME_CODE", source)
        self.assertNotIn('"IPGE"', source)

    def test_game_code_agrees_with_the_builder(self):
        import importlib.util

        spec = importlib.util.spec_from_file_location(
            "hgss_verify_rom", REPO / "builder" / "verify_rom.py")
        verify_rom = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(verify_rom)
        self.assertEqual(verify_rom.GAME_CODES["IPKE"], "Pokémon HeartGold (USA)")
        self.assertEqual(verify_rom.GAME_CODES["IPGE"], "Pokémon SoulSilver (USA)")
