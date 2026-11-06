#!/usr/bin/env python3
"""Build the HeartGold/SoulSilver game sources with GCC, then try to link.

Usage (inside the dev container, after tools/bootstrap.py):

    python3 tools/hgss_game.py --target host32 --keep-going --out /work/out/game1
    python3 tools/hgss_game.py --target 3ds    --keep-going --out /work/out/game1
    python3 tools/hgss_game.py --game soulsilver --target 3ds --out /work/out/ss

`--game` selects the decomp's HEARTGOLD (the default) or SOULSILVER define and
names the program accordingly (pokeheartgold-3ds.elf vs pokesoulsilver-3ds.elf;
the packaged .3dsx drops the -3ds). Each variant needs its own --out directory:
the game objects differ, and both programs must not share an object tree.

This is the Phase 2 harness: it compiles the game as the port compiles the
SDK, builds the port's libnitro/libnitrosystem, generates the overlay table
and the Wi-Fi stand-ins, and links pokeheartgold-pc/pokesoulsilver-pc
(host32) or pokeheartgold-3ds.elf/pokesoulsilver-3ds.elf (3ds, through the
libctru/citro3d shell in hgss/game/app/main_3ds.c).

The source lists come from tools/game_sources.py (game src/ C/C++ and the
ARM9 .s data), the overlay ids and table from tools/game_overlays.py, the
generated .naix/msg/fx headers from tools/prepare_game_headers.py, and the
SDK libraries from tools/hgss_nitro.py. Nothing here writes outside --out
and the generated headers under build/upstream (the decomp's build does the
same).

Not every source compiles under GCC yet; --keep-going compiles what it can,
prints the error count per file, then links the objects that were produced
and prints the unresolved symbols. That list is the work queue for the next
slice. The link targets are deliberately named -pc/-3ds.elf, not .3dsx:
packaging (3dsxtool, the SMDH) is Phase 5. Exit status is 0 only when every
object compiled and the link succeeded; 1 means the run reached the link but
there were errors (the useful case).
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TOOLS = REPO / "tools"
DEFAULT_ROOT = REPO / "build" / "upstream"

sys.path.insert(0, str(TOOLS))
import game_sources as gs  # noqa: E402
import hgss_nitro as nitro  # noqa: E402

# The libraries the game's own build compiles from lib/ but the port's
# libnitro.a/libnitrosystem.a do not contain: NitroCrypto (lib/src) and the
# syscall helpers (lib/syscall/src). Everything else under lib/ is either in
# one of those archives, replaced by newlib (MSL_C) or stubbed
# (NitroDWC/NitroWiFi/libCPS/libVCT/dsprot).
GAME_LIB_PREFIXES = ("lib/src/", "lib/syscall/src/")

# Port-replaced or stubbed SDK trees, and MSL_C, must not be compiled a
# second time next to the archives.
SKIP_LIB_PREFIXES = ("lib/NitroSDK/", "lib/NitroSystem/", "lib/MSL_C/",
                     "lib/NitroDWC/", "lib/NitroWiFi/", "lib/libCPS/",
                     "lib/libVCT/", "lib/dsprot/")

# Objects that must not be linked into the single host/3DS program:
# Metrowerks' C library data (the host libc/newlib owns errno, ctype, ...),
# the stubbed Wi-Fi/DWC/CPS/VCT/dsprot libraries, and MSL's C++ runtime data
# (3ds_port/nitro's port_misc.c owns the destructor chain). They are still
# assembled/compiled, so the conversion is checked; the missing symbols they
# would provide show up in the link's unresolved list.
LINK_EXCLUDED_PREFIXES = ("lib/MSL_C/", "lib/NitroDWC/", "lib/NitroWiFi/",
                          "lib/libCPS/", "lib/libVCT/", "lib/dsprot/",
                          "lib/asm/msl_")

# Pure-data objects from a stubbed library that the game's own link needs:
# the lobby chat protocol tables, the wifi_setup screens/keypad tables and
# their bss. Every lib/NitroDWC/asm object is data (no T symbols); the DWC
# functions themselves live in lib/NitroDWC/src and remain stubbed, so only
# the asm data objects are allowed through the NitroDWC exclusion.
LINK_DATA_EXCEPTION_PREFIXES = ("lib/NitroDWC/asm/",)
# ...except the DWC-internal libc state: these reference MSL/glibc's errno
# as an ordinary symbol, which clashes with glibc's TLS definition at link
# time. Every DWC function is stubbed, so nothing in the game reads them.
LINK_DATA_EXCLUDED = ("lib/NitroDWC/asm/dwc_gamespy_bss.o",
                      "lib/NitroDWC/asm/dwc_lib_bss.o",
                      "lib/NitroDWC/asm/dwc_lib_data.o",
                      "lib/NitroDWC/asm/dwc_lib_rodata.o")

# NitroSystem's volume->decibel table, _0210D750, is an alias of NitroSDK's
# SNDi_DecibelTable on the DS: lib/asm/nitro_sdk_data.s emits it as an empty
# .rodata.alias_d750 section and the DS link order places that section right
# before SND_util.o's .rodata.snd_decibel. The port's default section order
# does not: the empty symbol landed on unrelated .rodata, and NNS read pointer
# bytes as decibels (a large positive fader pinned every channel at 0 dB and
# kept the release stop from ever firing, so notes hung until the next key-on).
# Drop the empty section from the SDK data object for the port links and
# provide the table from the port object below.
NNS_VOLUME_ALIAS_SECTION = ".rodata.alias_d750"
NNS_VOLUME_SDK_SOURCE = "lib/asm/nitro_sdk_data.s"
NNS_VOLUME_PORT_SOURCE = REPO / "hgss" / "compat" / "src" / "nns_volume_table.c"
NNS_VOLUME_SYMBOL = "_0210D750"
# 256 entries: 0..127 are SNDi_DecibelTable and 128..255 are the DS's
# .rodata.unified_01 tail that the port source reproduces. snd_player.c indexes
# the table with the fader's high byte too, and the field code parks the fader
# at NNS_VOLUME_FADER_INDEX (GF_SndHandleMoveVolume(..., 128, 15)), so that
# entry must read 0 dB, as it does on the DS.
NNS_VOLUME_TABLE_ENTRIES = 0x100
NNS_VOLUME_TABLE_SIZE = NNS_VOLUME_TABLE_ENTRIES * 2
NNS_VOLUME_FADER_INDEX = 128

PORT_DEFINES = ["-DSDK_ARM9", "-DSDK_TS", "-DSDK_4M", "-DSDK_CODE_ARM",
                "-DSDK_FINALROM", "-DNNS_FINALROM", "-DSDK_PORT_3DS",
                # The DS build keeps assertions (config.mk). Several call
                # sites rely on GF_ASSERT's operand having a side effect
                # (e.g. SpriteTransfer_CreateAllCharTransferTasks builds the
                # char transfer tasks inside GF_ASSERT), so without this the
                # game links but silently skips work.
                "-DPM_KEEP_ASSERTS"]

#: The two games this tree builds. `define` is what the decomp's config.h
#: reads (HEARTGOLD/SOULSILVER); `binary` names the linked program and the
#: packaged .3dsx; `app` is the SMDH short name and the log banner.
GAMES = {
    "heartgold": {
        "define": "HEARTGOLD",
        "binary": "pokeheartgold",
        "app": "pokeheartgold-3ds",
        "title": "HeartGold on 3DS",
    },
    "soulsilver": {
        "define": "SOULSILVER",
        "binary": "pokesoulsilver",
        "app": "pokesoulsilver-3ds",
        "title": "SoulSilver on 3DS",
    },
}
GAME = GAMES["heartgold"]


def game_defines() -> list[str]:
    """The -D that selects this build's game in the decomp's config.h."""
    return ["-D" + GAME["define"]]

WARNINGS = ["-Wall", "-Wuninitialized", "-Wmaybe-uninitialized"]
SECTION_FLAGS = ["-ffunction-sections", "-fdata-sections"]

HOST_FLAGS = ["-m32", "-malign-double", "-fno-pie",
              "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast"]
ARM_FLAGS = ["-march=armv6k", "-mtune=mpcore", "-mfpu=vfp",
             "-mfloat-abi=hard", "-mtp=soft", "-mword-relocations", "-D__3DS__"]

UNDEFINED_RE = re.compile(r"undefined reference to [`']([^']+)'")
ERROR_RE = re.compile(r"(?:^|:\s)(?:fatal )?error:")


# --------------------------------------------------------------- target setup

def devkitpro() -> Path:
    """devkitPro's root (DEVKITPRO, with the container default)."""
    return Path(os.environ.get("DEVKITPRO", "/opt/devkitpro"))


def devkitarm_bin() -> Path:
    """devkitARM's bin/ directory (DEVKITARM, with the container default)."""
    return Path(os.environ.get("DEVKITARM", "/opt/devkitpro/devkitARM")) / "bin"


def target_tools(target: str) -> dict[str, str]:
    """The compile/archive/inspection tools for one target."""
    if target == "host32":
        return {"cc": os.environ.get("HGSS_HOST_CC", "gcc-10"),
                "ar": "ar", "nm": "nm", "objcopy": "objcopy",
                "readelf": "readelf"}
    if target == "3ds":
        prefix = devkitarm_bin() / "arm-none-eabi-"
        return {"cc": str(prefix) + "gcc", "ar": str(prefix) + "gcc-ar",
                "nm": str(prefix) + "nm", "objcopy": str(prefix) + "objcopy",
                "readelf": str(prefix) + "readelf"}
    raise ValueError(f"unknown target {target!r}")


def binary_name(out: Path, target: str) -> Path:
    """The linked program's path for this game and target."""
    if target == "host32":
        return out / f"{GAME['binary']}-pc"
    return out / f"{GAME['binary']}-3ds.elf"


def port_cflags(target: str, cxx: bool = False) -> list[str]:
    """The port's compiler flags (3ds_port/common.mk) for one target.

    The language standard differs for C++ (gcc rejects -std=gnu99 for C++);
    everything else is what every port object is compiled with. The C++
    game files (src/lobby) are built without exceptions and RTTI, so their
    static constructors stay ordinary .init_array entries.
    """
    if target not in ("host32", "3ds"):
        raise ValueError(f"unknown target {target!r}")
    flags = ["-std=gnu++98" if cxx else "-std=gnu99",
             "-fgnu89-inline", "-fno-strict-aliasing", "-fno-short-enums",
             "-fsigned-char", "-fwrapv",
             "-fno-aggressive-loop-optimizations",
             "-fno-delete-null-pointer-checks"]
    flags += PORT_DEFINES
    # Diagnostic build only: never enable texture timing in normal releases.
    if target == "3ds" and os.environ.get("HGSS_TEXTURE_PROFILE") == "1":
        flags += ["-DHGSS_TEXTURE_PROFILE=1"]
    if target == "3ds" and os.environ.get("HGSS_TEXTURE_HINT") == "1":
        flags += ["-DHGSS_TEXTURE_HINT=1"]
    if target == "3ds" and os.environ.get("HGSS_GX3D_HOTLOOP") == "1":
        flags += ["-DHGSS_GX3D_HOTLOOP=1"]
    if target == "3ds" and os.environ.get("HGSS_GX3D_INDEXED") == "1":
        flags += ["-DHGSS_GX3D_INDEXED=1"]
    if target == "3ds" and os.environ.get("HGSS_GX3D_LISTCACHE") == "1":
        flags += ["-DHGSS_GX3D_LISTCACHE=1"]
    if target == "3ds" and os.environ.get("HGSS_GX3D_STATEDEDUP") == "1":
        flags += ["-DHGSS_GX3D_STATEDEDUP=1"]
    if target == "3ds" and os.environ.get("HGSS_A0_DIAG") == "1":
        flags += ["-DHGSS_A0_DIAG=1"]
    flags += game_defines()
    flags += WARNINGS + SECTION_FLAGS
    if cxx:
        flags += ["-fno-exceptions", "-fno-rtti"]
    flags += HOST_FLAGS if target == "host32" else ARM_FLAGS
    if SANITIZE and target == "host32":
        flags += SANITIZE_FLAGS
    else:
        # The game is 1,092 objects and used to carry no -O at all, so GCC's
        # default (-O0) applied to all of them: no inlining, no frame-pointer
        # elision, no IPA. Measured on devkitARM 16.1.0, -O2 shrinks the game's
        # .text from 8,146,868 to 4,512,808 B (-44.6%) and the packaged .3dsx
        # from 9,403,804 to 5,762,900 B -- and on an Old 3DS that freed memory
        # comes straight out of the 64 MiB process budget and grows the linear
        # heap, which is the resource the compositor is short of. -O3 and -Os
        # were measured too: O3 is +17% over O2 for no measured gain, Os only
        # saves a further 0.61 MB and slows hot code. Keep the sanitizer build
        # where it was (the replay wants its debug info and its -O0 behaviour).
        #
        # -fno-ipa-icf: -O2 merges functions that compile to identical bodies,
        # which changes function-pointer identity. This port passes functions
        # around constantly (IRQ table entries, HBlank tasks, SysTask
        # callbacks), so two byte-identical callbacks becoming one is not
        # something the game's code can be assumed to survive. It costs well
        # under 1% of code size.
        flags += ["-O2"]
    return flags


# --sanitize: instrument the game and app objects (and the link) for the
# Phase 4 replay. The vendored archives are built normally; ASan still
# interposes malloc and checks the game's accesses into them.
SANITIZE = False
SANITIZE_FLAGS = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]


def hgss_includes(root: Path, sdk: Path, gen: Path) -> list[str]:
    """Include paths for game sources.

    tools/hgss_nitro.py's list (nitro_port + HGSS's flat SDK headers) plus
    the game's own include directory, its library headers, the generated
    naix/msg headers under files/, and the generated overlay-id header.
    """
    nitro_dir = REPO / "3ds_port" / "nitro"
    return nitro.sdk_includes(root, sdk, nitro_dir, gen) + [
        f"-I{REPO / '3ds_port' / 'pak' / 'include'}",
        # After the system headers: HGSS's lib/include has the game's own
        # <dwc/...> and other library headers, but also Metrowerks
        # stdarg.h/stddef.h/string.h. -idirafter lets the real system headers
        # win while still finding the game's library headers.
        f"-idirafter{root}/lib/include",
        f"-I{root}/files",
        f"-I{root}/files/msgdata",
        f"-I{gen}",
    ]


def include_flags(root: Path, out: Path) -> list[str]:
    """Include paths for a real build (creates the SDK shim's symlinks)."""
    sdk, _ = nitro.make_shim(root)
    return hgss_includes(root, sdk, out / "gen")


# ---------------------------------------------------------------- source sets

def select_sources(data: dict) -> dict[str, list[str]]:
    """C, C++ and asm sources this harness compiles, from game_sources data.

    C/C++: every game source under src/ (the original common.mk lists them
    all), plus the game's own lib/ sources that neither libnitro.a nor
    libnitrosystem.a provides (NitroCrypto and syscall). SDK sources are not
    compiled here: the archives own them, and compiling them twice would
    duplicate every symbol at link time.
    """
    c = list(data["c_sources"])
    cpp = list(data["cpp_sources"])
    for rel in data["port_lib_sources"]:
        if rel.startswith(GAME_LIB_PREFIXES) and not rel.startswith(SKIP_LIB_PREFIXES):
            if rel.endswith(".cpp"):
                cpp.append(rel)
            else:
                c.append(rel)
    return {"c": sorted(set(c)), "cpp": sorted(set(cpp)),
            "asm": sorted(set(data["asm_sources"]))}


def app_sources(target: str) -> list[Path]:
    """The program's own objects: the shared start and stubs, plus the
    target's main (main_pc.c on host32, main_3ds.c on 3ds).

    These are compiled and linked into the program only, never into
    libnitro.a: the MSL helper set the host libc does not provide, the small
    C++ runtime the lobby references, and the multiboot stand-ins. The MSL
    runtime lives under hgss/compat/src because it is a library replacement,
    but it is still an app object (the archives do not need it and must not
    carry it)."""
    if target not in ("host32", "3ds"):
        raise ValueError(f"unknown target {target!r}")
    main = "main_pc.c" if target == "host32" else "main_3ds.c"
    sources = [REPO / "hgss" / "game" / "app" / main,
               REPO / "hgss" / "game" / "app" / "heartgold.c",
               REPO / "hgss" / "game" / "app" / "cpp_runtime.c",
               REPO / "hgss" / "compat" / "src" / "msl_runtime.c",
               REPO / "hgss" / "compat" / "src" / "nns_volume_table.c",
               REPO / "hgss" / "game" / "stubs" / "wm_stubs.c",
               REPO / "hgss" / "game" / "stubs" / "dsprot_stubs.c",
               REPO / "hgss" / "game" / "stubs" / "mb_stubs.c"]
    if target == "host32":
        # --trace-pxi/--trace-irq are host-only diagnostics (linker wraps).
        sources.append(REPO / "hgss" / "game" / "app" / "pc_trace.c")
    else:
        # The 3DS shell's scripted-input parser (plain C, host-testable).
        sources.append(REPO / "hgss" / "game" / "app" / "3ds_input.c")
        # The port's PICA backend and remaster compositor. port_gx3d_pica.c
        # draws the DS's polygons; port_compose_pica.c puts the 2D layers and
        # 3D together at the 3DS screen's resolution. port_pica_abi.c is
        # libctru's struct layout and is compiled separately (without
        # -fno-short-enums); see the build.
        sources.append(REPO / "3ds_port" / "game" / "app" / "port_gx3d_pica.c")
        sources.append(REPO / "3ds_port" / "game" / "app" / "port_compose_pica.c")
        sources.append(REPO / "3ds_port" / "game" / "app" / "port_pica_abi.c")
    return sources


def object_path(out: Path, rel: str) -> Path:
    """out/obj/<checkout-relative path with .o> (main.lsf's object spelling)."""
    return out / "obj" / Path(rel).with_suffix(".o")


# The PICA vertex programs port_gx3d_pica.c expects, in DVLE order: the
# polygons, the sun shadows' map, the sun shadows' scene. They are Platinum's
# (3ds_port/ is byte-identical), assembled by picasso and embedded by bin2s.
GX3D_SHADERS = ("gx3d_passthrough.v.pica", "gx3d_shadow_cast.v.pica",
                "gx3d_shadow_recv.v.pica")


def gx3d_shader_sources() -> list[Path]:
    """The .v.pica files linked into the 3DS program, in DVLE order."""
    app = REPO / "3ds_port" / "game" / "app"
    return [app / name for name in GX3D_SHADERS]


def gx3d_shader_object(out: Path) -> Path:
    """Build the shader blob object for the 3DS program.

    picasso assembles the three vertex programs into one DVLB; bin2s turns it
    into an assembly object whose symbols are gx3d_passthrough_shbin and
    ..._size, which port_gx3d_pica.c references. Both tools ship with
    devkitPro (the same place as 3dsxtool/3dsxdump), so this adds no new
    dependency.
    """
    app = out / "app"
    app.mkdir(parents=True, exist_ok=True)
    shbin = app / "gx3d_passthrough.shbin"
    asm = app / "gx3d_passthrough_shbin.s"
    obj = app / "gx3d_passthrough_shbin.o"
    picasso = devkitpro() / "tools" / "bin" / "picasso"
    bin2s = devkitpro() / "tools" / "bin" / "bin2s"
    assembler = devkitarm_bin() / "arm-none-eabi-as"
    for tool in (picasso, bin2s, assembler):
        if not tool.is_file():
            raise RuntimeError(f"missing {tool} (devkitPro tools)")
    sources = [src for src in gx3d_shader_sources() if src.is_file()]
    if len(sources) != len(GX3D_SHADERS):
        missing = [src.name for src in gx3d_shader_sources() if not src.is_file()]
        raise RuntimeError(f"missing shader sources: {', '.join(missing)}")
    proc = run([str(picasso), "-o", str(shbin), *map(str, sources)])
    if proc.returncode != 0:
        raise RuntimeError(f"picasso failed: {(proc.stderr or proc.stdout).strip()}")
    proc = run([str(bin2s), str(shbin)])
    if proc.returncode != 0:
        raise RuntimeError(f"bin2s failed: {(proc.stderr or proc.stdout).strip()}")
    asm.write_text(proc.stdout, encoding="utf-8")
    proc = run([str(assembler), "-o", str(obj), str(asm)])
    if proc.returncode != 0:
        raise RuntimeError(f"as failed: {(proc.stderr or proc.stdout).strip()}")
    return obj


# --------------------------------------------------------- data pack reader

# The vendored portable libpak reader (3ds_port/pak, embedded in libnitro.a)
# validates the pack header's source-ROM SHA-1 against *Platinum's* two
# digests. 3ds_port/ is byte-identical to platinum.lock, so the HGSS allowlist
# is injected into a build-private copy compiled into the program. The copy is
# linked before libnitro.a, so `pak_open` resolves here and the archive's
# pak_reader.o member is never loaded. See builder/verify_rom.py: the digests
# come from its SUPPORTED table, so the two lists cannot drift.
PAK_READER = REPO / "3ds_port" / "pak" / "src" / "pak.c"
PAK_SUPPORTED_ANCHOR = "static int supported_sha1(const unsigned char *digest)"
PAK_RETURN_ANCHOR = "    return matches_sha1(digest, "


def supported_rom_sha1s() -> list[str]:
    """The ROM SHA-1s builder/verify_rom.py accepts, sorted."""
    import importlib.util

    spec = importlib.util.spec_from_file_location(
        "hgss_verify_rom", REPO / "builder" / "verify_rom.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return sorted(module.SUPPORTED)


def pak_reader_source(out: Path, digests: list[str] | None = None) -> Path:
    """Write the HGSS copy of 3ds_port/pak/src/pak.c into --out and return it.

    The injection is additive: Platinum's digests stay in the table (the
    vendored reader is a general port component), and the two HGSS ones are
    prepended. The anchors are checked so a vendored update that changes the
    reader's shape fails the build loudly instead of silently losing the
    allowlist.
    """
    digests = sorted(digests if digests is not None else supported_rom_sha1s())
    if not digests:
        raise ValueError("no supported ROM SHA-1s")
    source = PAK_READER.read_text(encoding="utf-8")
    if source.count(PAK_SUPPORTED_ANCHOR) != 1 or source.count(PAK_RETURN_ANCHOR) != 1:
        raise ValueError(f"{PAK_READER}: supported_sha1() shape changed; "
                         "update PAK_RETURN_ANCHOR in tools/hgss_game.py")
    lines = [f'matches_sha1(digest, "{digest}")' for digest in digests]
    replacement = ("    return " + lines[0] + " ||\n"
                   + "".join("           " + line + " ||\n" for line in lines[1:])
                   + "           " + PAK_RETURN_ANCHOR[len("    return "):])
    source = source.replace(PAK_RETURN_ANCHOR, replacement, 1)
    dest = out / "gen" / "pak_reader_hgss.c"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(
        "/* Generated by tools/hgss_game.py from 3ds_port/pak/src/pak.c.\n"
        " * The vendored reader keeps Platinum's ROM allowlist (3ds_port/ is\n"
        " * byte-identical to platinum.lock); this copy adds the HeartGold and\n"
        " * SoulSilver US digests from builder/verify_rom.py. Do not edit. */\n"
        + source, encoding="utf-8")
    return dest


def lsf_object_set(data: dict) -> set[str]:
    """The object paths main.lsf links (Static, Autoloads, every overlay).

    src/text_0205B4EC.c is compiled by the original build's wildcard but is
    in no block; linking it would duplicate dialog_box.c's functions.
    """
    return {entry["object"] for block in data["overlay_map"]
            for entry in block["objects"]}


def link_objects(objects: list[Path], data: dict, out: Path) -> list[Path]:
    """The objects the link gets, in link order.

    Only main.lsf's objects (the original build's linked set) and only the
    libraries the port provides itself: Metrowerks' C library data (errno,
    ctype, printf tables) would clash with the host libc/newlib, and the
    stubbed Wi-Fi/DWC/CPS/VCT/dsprot libraries are replaced by
    hgss/game/stubs. The excluded objects are still built, so the conversion
    is checked and the symbols they would provide appear as unresolved.
    """
    allowed = lsf_object_set(data)
    result = []
    for obj in objects:
        rel = obj.relative_to(out / "obj").as_posix()
        if rel not in allowed:
            continue
        if any(rel.startswith(prefix) for prefix in LINK_DATA_EXCEPTION_PREFIXES) \
                and rel not in LINK_DATA_EXCLUDED:
            result.append(obj)
            continue
        if any(rel.startswith(prefix) for prefix in LINK_EXCLUDED_PREFIXES):
            continue
        result.append(obj)
    return result


# ------------------------------------------------------------------ commands

def compile_command(cc: str, flags: list[str], src: Path, obj: Path,
                    language: str) -> list[str]:
    """One C or C++ compile. The language is explicit so a file never
    depends on its extension being recognised."""
    return [cc, *flags, "-x", language, "-c", str(src), "-o", str(obj)]


def asm_command(root: Path, src: Path, obj: Path, cc: str,
                flags: list[str], overlay_ids: Path | None = None) -> list[str]:
    """The tools/s_to_gas.py invocation that assembles one ARM9 .s file."""
    cmd = [sys.executable, str(TOOLS / "s_to_gas.py"), "--root", str(root),
           str(src), "-o", str(obj), "--cc", cc]
    if overlay_ids is not None:
        cmd += ["--overlay-ids", str(overlay_ids)]
    cmd += ["--cflags", *flags]
    return cmd


def asm_flags(target: str, root: Path, out: Path) -> list[str]:
    """Flags for the assembler stage: the target machine plus every include,
    but no C-only -std/-f flags (s_to_gas passes them to the preprocessor,
    which rejects some of them for assembler input)."""
    machine = list(HOST_FLAGS) if target == "host32" else list(ARM_FLAGS)
    return machine + PORT_DEFINES + game_defines() + include_flags(root, out)


def overlay_command(root: Path, out: Path, objcopy: str) -> list[str]:
    """tools/game_overlays.py: group each overlay's data sections and write
    its table and the overlay-ID ld script."""
    gen = out / "gen"
    return [sys.executable, str(TOOLS / "game_overlays.py"),
            "--lsf", str(root / "main.lsf"), "--upstream", str(root),
            "--objdir", str(out / "obj"), "--objcopy", objcopy,
            "--output", str(gen / "overlay_table.c"),
            "--ids-output", str(gen / "overlay_ids.ld"),
            "--header", str(gen / "overlay_ids.h")]


# ------------------------------------------------------------- object aliases

ALIAS_MAP_SUFFIX = ".alias-map.json"

# cpp_runtime.c defines these two destructors itself. The decomp's maps alias
# them to the C destroy routines at offset 0, which is the same operation, but
# the map also pins MWCC's function size (80/160 bytes) and GCC's code for the
# destroy routines is larger, so object_aliases.py's owner check rejects the
# entry. Skip exactly these aliases; every other map entry still runs.
PORT_PROVIDED_ALIASES = frozenset({"_ZN5BytesD1Ev", "_ZN5DequeD1Ev"})


def alias_map_path(src: Path) -> Path:
    """The `.alias-map.json` beside a source (`foo.c` -> `foo.alias-map.json`).

    The decomp's common.mk runs tools/object_aliases.py after compiling any
    source whose sibling map exists. The maps add labels inside one BSS object
    (the five gPokewalkerIr* aliases) and interior FUNC entry points (the lobby
    destructors, which MWCC emits but GCC does not). This is a build stage, not
    missing code: do not hand-write those symbols.
    """
    return src.with_suffix(ALIAS_MAP_SUFFIX)


def object_alias_command(root: Path, obj: Path, alias_map: Path) -> list[str]:
    """The decomp's object_aliases.py invocation for one compiled object.

    The script lives in the --root checkout (it is part of the decompilation,
    not this repo) and rewrites `obj` in place, verifying each owner size and
    every alias offset against the ELF symbol table.
    """
    return [sys.executable, str(root / "tools" / "object_aliases.py"),
            str(obj), str(alias_map)]


def alias_tool_dir(out: Path) -> Path:
    """Private directory holding the aliasing tools' expected names."""
    return out / "gen" / "alias-tools"


def alias_tool_environment(out: Path, tools: dict[str, str]) -> dict[str, str]:
    """An environment whose PATH resolves arm-none-eabi-{objcopy,readelf}.

    object_aliases.py looks those two names up on PATH, and it needs the
    object's own tools: arm-none-eabi-objcopy cannot read an x86 ELF (it
    rejects the format), while the host objcopy/readelf read both. Create
    symlinks under the names the script asks for, pointing at this target's
    objcopy and readelf, and prepend them to PATH. Called once before the
    compile pools start, so the symlinks are never rewritten concurrently.
    """
    directory = alias_tool_dir(out)
    directory.mkdir(parents=True, exist_ok=True)
    for name, tool in (("arm-none-eabi-objcopy", tools["objcopy"]),
                       ("arm-none-eabi-readelf", tools["readelf"])):
        link = directory / name
        target = shutil.which(tool) or tool
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(target)
    env = dict(os.environ)
    env["PATH"] = str(directory) + os.pathsep + env.get("PATH", "")
    return env


def load_alias_configs(alias_map: Path) -> list[dict]:
    """The map's owner/alias configs (a map is one object or a JSON list)."""
    loaded = json.loads(alias_map.read_text(encoding="utf-8"))
    return list(loaded) if isinstance(loaded, list) else [loaded]


def defined_symbol_size(nm: str, obj: Path, name: str) -> int | None:
    """The ELF size of one global defined symbol, or None if absent."""
    proc = run([nm, "-S", "-g", "--defined-only", str(obj)])
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3] == name:
            try:
                return int(parts[1], 16)
            except ValueError:
                return None
    return None


READELF_SYMBOL_RE = re.compile(
    r"\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+(\S+)\s+(\S+)\s+\S+\s+(\S+)\s+(\S+)$")
READELF_SECTION_RE = re.compile(r"\s*\[\s*(\d+)\]\s+(\S+)")


def symbol_section(readelf: str, obj: Path, name: str
                   ) -> tuple[str, int, int] | None:
    """(section name, section-relative offset, size) of one defined symbol.

    The harness compiles with -ffunction-sections/-fdata-sections, so a symbol
    the decomp's map expect in plain `.text`/`.bss` actually lives in
    `.text.<name>`/`.bss.<name>` at offset 0. The alias map has to be
    translated to that layout before object_aliases.py can add labels inside
    the owner (see prepared_alias_map).
    """
    sections = {}
    for line in run([readelf, "-SW", str(obj)]).stdout.splitlines():
        match = READELF_SECTION_RE.match(line)
        if match:
            sections[match.group(1)] = match.group(2)
    for line in run([readelf, "-sW", str(obj)]).stdout.splitlines():
        match = READELF_SYMBOL_RE.match(line)
        if not match:
            continue
        value, size, symbol_type, binding, ndx, symbol = match.groups()
        if symbol != name:
            continue
        if not ndx.isdigit():
            return None
        section = sections.get(ndx)
        if section is None:
            return None
        return section, int(value, 16), int(size)
    return None


def prepared_alias_map(obj: Path, alias_map: Path, out: Path,
                       nm: str, readelf: str) -> Path | None:
    """The map to hand to object_aliases.py, or None when it has nothing to add.

    Three compiler-independent adjustments, all documented and all visible in
    the object afterwards:

    - Entries whose alias cpp_runtime.c already defines are dropped (see
      PORT_PROVIDED_ALIASES).
    - The owner's real section and offset come from the compiled object. With
      -fdata-sections/-ffunction-sections the owner sits in `.bss.<name>` /
      `.text.<name>` at offset 0, while the map names the DS's single `.bss` /
      `.text` and section-relative offsets. The map is rewritten with the real
      section, the real owner offset, and alias offsets shifted by the same
      delta: the labels stay at the same distance inside the same object.
    - A FUNC owner's `size` is MWCC's code size; GCC's code is a different
      length, so it is replaced with the compiled size. OBJECT (BSS) sizes are
      never adjusted: their layout must match the DS, so a mismatch stays
      visible as the tool's owner error.

    Returns the original map when nothing changed and no entry was dropped;
    otherwise a private copy under --out.
    """
    configs = load_alias_configs(alias_map)
    kept: list[dict] = []
    changed = False
    for config in configs:
        aliases = config.get("aliases", [])
        names = {alias["name"] for alias in aliases}
        if names and names <= PORT_PROVIDED_ALIASES:
            changed = True
            continue
        if names & PORT_PROVIDED_ALIASES:
            raise ValueError(f"{alias_map}: an owner mixes cpp_runtime-provided "
                             f"and aliased names: {sorted(names)}")
        owner = config.get("owner", {})
        compiled = symbol_section(readelf, obj, owner["name"])
        if compiled is not None:
            section, actual_offset, actual_size = compiled
            pinned_offset = owner.get("offset", 0)
            pinned_offset = (int(pinned_offset, 0) if isinstance(pinned_offset, str)
                             else pinned_offset)
            delta = actual_offset - pinned_offset
            pinned_section = config.get("section")
            config = dict(config)
            config["section"] = section
            config["owner"] = dict(owner, offset=actual_offset)
            if section != pinned_section or delta != 0:
                changed = True
            if owner.get("type") == "FUNC":
                pinned = (int(owner["size"], 0) if isinstance(owner["size"], str)
                          else owner["size"])
                if actual_size != pinned:
                    config["owner"]["size"] = actual_size
                    changed = True
            config["aliases"] = [
                dict(alias, offset=(int(alias["offset"], 0)
                                    if isinstance(alias["offset"], str)
                                    else alias["offset"]) + delta)
                for alias in aliases]
            if delta != 0:
                changed = True
        kept.append(config)
    if not kept:
        return None
    if not changed:
        return alias_map
    target = out / "gen" / "alias-maps" / (obj.relative_to(out / "obj").as_posix() + ".json")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(kept, indent=2) + "\n", encoding="utf-8")
    return target


VERSION_SECTION = ".version"
VERSION_SECTION_PORT = ".rodata.version"


def object_section_names(readelf: str, obj: Path) -> list[str]:
    """The section names of one ELF object (empty if readelf fails)."""
    proc = run([readelf, "-SW", str(obj)])
    names = []
    for line in proc.stdout.splitlines():
        match = re.match(r"\s*\[\s*\d+\]\s+(\S+)", line)
        if match:
            names.append(match.group(1))
    return names


def align_version_section(obj: Path, readelf: str, objcopy: str) -> str:
    """Merge an input `.version` section into `.rodata` before the link.

    asm/middleware.s holds the middleware version strings in `.version`
    (0xdd bytes in HG). The linker's default script places that orphan last
    in the read-only LOAD segment, whose memory size must be 4-aligned for
    3dsxtool. Renaming the input section to `.rodata.version` makes the
    script's `*(.rodata.*)` own it, so the `.rodata` output section's own
    trailing ALIGN(4) pads the segment. The DS build keeps MWAS's section;
    this runs on the port's assembled object only.
    """
    if VERSION_SECTION not in object_section_names(readelf, obj):
        return ""
    proc = run([objcopy,
                f"--rename-section={VERSION_SECTION}={VERSION_SECTION_PORT}",
                str(obj)])
    return proc.stdout + proc.stderr


def apply_object_aliases(root: Path, src: Path, obj: Path,
                         env: dict[str, str] | None, nm: str | None = None,
                         readelf: str | None = None,
                         out: Path | None = None) -> tuple[bool, str]:
    """Run the alias stage for one object if its source has a sibling map.

    Returns (ok, output). A missing map is not a failure; a tool error is.
    """
    alias_map = alias_map_path(src)
    if not alias_map.is_file():
        return True, ""
    prepared = alias_map
    if nm is not None and readelf is not None and out is not None:
        prepared = prepared_alias_map(obj, alias_map, out, nm, readelf)
        if prepared is None:
            return True, ""
    proc = run(object_alias_command(root, obj, prepared), env=env)
    return proc.returncode == 0, proc.stdout + proc.stderr


def prepare_command(root: Path, out: Path) -> list[str]:
    """tools/prepare_game_headers.py into the checkout, with the overlay ids
    in the build's gen/ (the checkout itself must stay build-generated)."""
    gen = out / "gen"
    return [sys.executable, str(TOOLS / "prepare_game_headers.py"),
            "--root", str(root),
            "--ids-output", str(gen / "overlay_ids.ld"),
            "--header", str(gen / "overlay_ids.h")]


def wifi_stub_command(nm: str, root: Path, objects: list[Path],
                      output: Path) -> list[str]:
    """tools/make_game_wifi_stubs.py over the objects that were compiled."""
    headers = [root / "lib" / "NitroDWC" / "include",
               root / "lib" / "NitroWiFi" / "include",
               root / "lib" / "libVCT" / "include",
               root / "lib" / "libCPS" / "include",
               root / "lib" / "include", root / "include"]
    sources = [root / "lib" / "NitroDWC", root / "lib" / "NitroWiFi",
               root / "lib" / "libCPS", root / "lib" / "libVCT",
               root / "lib" / "dsprot"]
    cmd = [sys.executable, str(TOOLS / "make_game_wifi_stubs.py"),
           "--nm", nm, "--headers"]
    cmd += [str(p) for p in headers if p.is_dir()]
    cmd += ["--sources"] + [str(p) for p in sources if p.is_dir()]
    cmd += ["--output", str(output), "--"]
    cmd += [str(p) for p in objects]
    return cmd


def three_dsx_command(tool: Path, elf: Path, dsx: Path,
                      smdh: Path | None = None) -> list[str]:
    """The 3dsxtool invocation: a Homebrew Launcher program from the ELF.

    The SMDH is optional; when present, 3dsxtool takes it as the third
    argument. The checked-in icon is a generic placeholder (no cart art);
    the cart-derived icon and banner are generated locally in Phase 5."""
    command = [str(tool), str(elf), str(dsx)]
    if smdh is not None:
        command.append(f"--smdh={smdh}")
    return command


def smdh_names(build_id: str | None = None) -> tuple[str, str, str]:
    """Short, unique Homebrew Launcher names: (short, long, publisher).

    Every build shows its commit + experiment flags in the launcher, so two
    different .3dsx files never look identical there. Without a build id this
    is the old generic triple. With one the entry reads, e.g.
    "SS-9930261-dirty-a0diag" / "gxhot gxidx listcache statededup" /
    "SS-9930261-dirty": game + commit + dirty + A0 in the title, the exact
    geometry flag names in the description, game + commit as publisher.

    Lengths are load-bearing, not cosmetic: smdhtool lays each language out
    as short[64] + long[128] + publisher[64] UTF-16 units and copies without
    truncation, so anything past 63/127/63 units overflows into the next
    language's title and garbles every entry (seen on hardware with 72-char
    titles). The caps below keep a spare unit for the NUL.
    """
    if not build_id:
        return (GAME["app"], GAME["title"], GAME["app"])
    game = "SS" if GAME["binary"] == "pokesoulsilver" else "HG"
    # The tag is "{shortsha}[-dirty][-texprof][-texhint][-palhash][-gxhot]
    # [-gxidx][-listcache][-statededup][-fusehash][-lazy][-a0diag]"
    # (experiment_build_id below): the SHA is first and never contains
    # a dash.
    short_sha = build_id.split("-")[0]
    rest = build_id[len(short_sha):]
    base = short_sha + ("-dirty" if rest.startswith("-dirty") else "")
    flags = [part for part in ("gxhot", "gxidx", "listcache", "statededup",
                               "fusehash", "lazy", "a0diag")
             if f"-{part}" in rest]
    title = f"{game}-{build_id}"
    long = " ".join(flags)
    publisher = f"{game}-{base}"
    return (title[:63], long[:127], publisher[:63])


def make_smdh(binary: Path, build_id: str | None = None) -> Path | None:
    """Build an SMDH beside the ELF, or None if the tool is absent.

    build_id is the experiment_build_id() tag (short SHA + -dirty +
    experiment suffixes); it goes into the SMDH short/long/publisher names
    so the Homebrew Launcher shows a unique entry per build. None keeps the
    old generic names (host-tool callers and tests).
    """
    tool = devkitpro() / "tools" / "bin" / "smdhtool"
    icon = REPO / "hgss" / "game" / "assets" / "placeholder-icon.png"
    smdh = binary.with_suffix(".smdh")
    if not tool.is_file() or not icon.is_file():
        return None
    short, long, publisher = smdh_names(build_id)
    proc = subprocess.run([str(tool), "--create", short,
                           long, publisher,
                           str(icon), str(smdh)],
                          capture_output=True, text=True)
    if proc.returncode != 0 or not smdh.is_file():
        return None
    return smdh


class ThreeDsxError(RuntimeError):
    """The packaged .3dsx would be rejected by the console's loader."""


def validate_3dsx(path: Path) -> list[str]:
    """Problems a 3DS loader meets relocating this .3dsx; [] when it is sound.

    Raises ValueError when the file is not a .3dsx at all (truncated or foreign
    output is a packaging bug, not a clean result).

    Why this exists: 3dsxtool decides a word is an absolute pointer because its
    value is at least the image base (0x00100000). A word that is really
    "symbol - 0x06800000" -- what GCC makes of
    NitroPort_LcdcVram + (address - 0x06800000) -- is therefore packed as a
    pointer about 4 GB below the image, and the console's Loader aborts while
    relocating it (Luma: undefined instruction in process "loader"). The
    vendored 3dsxdump refuses the same file; this reproduces its checks. They
    must run before an artifact reaches a card: the whole PC build and every
    host test pass with the bad word in place.
    """
    data = Path(path).read_bytes()
    if len(data) < 0x20 or data[:4] != b"3DSX":
        raise ValueError("not a 3dsx (bad or missing magic)")
    header_size, reloc_hdr_size = struct.unpack_from("<HH", data, 4)
    code, rodata, data_size, bss = struct.unpack_from("<IIII", data, 0x10)
    if header_size < 0x20 or reloc_hdr_size < 8 or reloc_hdr_size % 4:
        raise ValueError(
            f"implausible header (header {header_size:#x}, relocs {reloc_hdr_size:#x})")
    if data_size < bss:
        raise ValueError("data segment smaller than its bss")
    tables = reloc_hdr_size // 4
    stored = (code, rodata, data_size - bss)
    segment_off = header_size + 3 * reloc_hdr_size
    if segment_off + sum(stored) > len(data):
        raise ValueError("truncated segments")
    sizes = [(code + 0xFFF) & ~0xFFF, (rodata + 0xFFF) & ~0xFFF,
             (data_size + 0xFFF) & ~0xFFF]
    counts = [struct.unpack_from(f"<{tables}I", data, header_size + 8 * i)
              for i in range(3)]
    bases = [0x00100000, 0x00100000 + sizes[0],
             0x00100000 + sizes[0] + sizes[1]]

    cursor = segment_off + sum(stored)
    problems: list[str] = []
    start = 0
    for index in range(3):
        words = bytearray(data[segment_off + start:segment_off + start + stored[index]])
        words += bytes(sizes[index] - len(words))
        start += stored[index]
        limit = sizes[index] // 4
        for table in range(2):
            position = 0
            for _ in range(counts[index][table]):
                if cursor + 4 > len(data):
                    raise ValueError("truncated relocation table")
                skip, patch = struct.unpack_from("<HH", data, cursor)
                cursor += 4
                position += skip
                for _ in range(patch):
                    if position >= limit:
                        break
                    value = struct.unpack_from("<I", words, position * 4)[0]
                    where = (f"segment {index} word {position:#x} "
                             f"(address {bases[index] + position * 4:#010x})")
                    if table == 0 and value >> 28:
                        problems.append(f"{where} is marked absolute but holds "
                                        f"{value:#010x} (outside the image)")
                    elif table == 1 and (value >> 28) > 1:
                        problems.append(f"{where} is marked relative but holds "
                                        f"{value:#010x}")
                    position += 1
    return problems


def git_build_info() -> tuple[str, str, bool, str]:
    """The checkout's commit identity for build stamping.

    Returns (short SHA, full SHA, dirty flag, `git status --porcelain`).
    The repo root holds the patch stack, so it -- not the generated
    build/upstream checkout -- is the source of truth. Any git failure
    (missing binary, no repo, odd container) yields "unknown", never an
    exception: identity is diagnostic, not load-bearing.
    """
    try:
        full = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO,
                              capture_output=True, text=True)
        short = subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                               cwd=REPO, capture_output=True, text=True)
        status = subprocess.run(["git", "status", "--porcelain"], cwd=REPO,
                                capture_output=True, text=True)
    except OSError:
        return ("unknown", "unknown", False, "")
    if full.returncode != 0 or short.returncode != 0:
        return ("unknown", "unknown", False, "")
    porcelain = status.stdout if status.returncode == 0 else ""
    return (short.stdout.strip(), full.stdout.strip(),
            bool(porcelain.strip()), porcelain)


def build_id_string(short: str, dirty: bool) -> str:
    """The banner/manifest build tag: short SHA, `-dirty` when the tree was."""
    return short + ("-dirty" if dirty else "")


def experiment_build_id(short: str, dirty: bool) -> str:
    """Distinguish experimental binaries in runtime logs as well as sidecars."""
    tag = build_id_string(short, dirty)
    for variable, suffix in (("HGSS_TEXTURE_PROFILE", "-texprof"),
                             ("HGSS_TEXTURE_HINT", "-texhint"),
                             ("HGSS_PALETTE_HASH", "-palhash"),
                             ("HGSS_GX3D_HOTLOOP", "-gxhot"),
                             ("HGSS_GX3D_INDEXED", "-gxidx"),
                             ("HGSS_GX3D_LISTCACHE", "-listcache"),
                             ("HGSS_GX3D_STATEDEDUP", "-statededup"),
                             ("HGSS_GX3D_FUSEHASH", "-fusehash"),
                             ("HGSS_GX3D_LAZYINSERT", "-lazy"),
                             ("HGSS_A0_DIAG", "-a0diag")):
        if os.environ.get(variable) == "1":
            tag += suffix
    return tag


def write_build_id_header(gen: Path,
                          info: tuple[str, str, bool, str] | None = None) -> str:
    """Write gen/build_id.h (HGSS_BUILD_ID/SHA/DIRTY), only on change.

    main_3ds.c includes it for the boot banner, so any log.txt traces to
    the commit and experiment flags that built it. The header is left
    alone when unchanged, so nothing spuriously recompiles.
    """
    short, full, dirty, _ = info if info is not None else git_build_info()
    build_id = experiment_build_id(short, dirty)
    text = ("/* Generated by tools/hgss_game.py: do not edit. */\n"
            f'#define HGSS_BUILD_ID "{build_id}"\n'
            f'#define HGSS_BUILD_SHA "{full}"\n'
            f'#define HGSS_BUILD_DIRTY {1 if dirty else 0}\n')
    header = gen / "build_id.h"
    if header.is_file() and header.read_text(encoding="utf-8") == text:
        return build_id
    header.write_text(text, encoding="utf-8")
    return build_id


def write_build_info(dsx: Path, game: str, target: str,
                     info: tuple[str, str, bool, str]) -> Path:
    """Write the .build-info.txt sidecar next to a packaged .3dsx.

    Pairs the artifact with its commit, dirty state and sha256 while still
    in --out (this module never writes outside --out); tools/stage_3ds.py
    carries it to the staged copies.
    """
    short, full, dirty, status = info
    manifest = dsx.with_name(dsx.stem + ".build-info.txt")
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    digest = hashlib.sha256(dsx.read_bytes()).hexdigest()
    body = (f"game: {game}\n"
            f"target: {target}\n"
            f"texture_profile: {'yes' if target == '3ds' and os.environ.get('HGSS_TEXTURE_PROFILE') == '1' else 'no'}\n"
             f"texture_hint: {'yes' if target == '3ds' and os.environ.get('HGSS_TEXTURE_HINT') == '1' else 'no'}\n"
             f"palette_hash: {'yes' if os.environ.get('HGSS_PALETTE_HASH') == '1' else 'no'}\n"
             f"gx3d_hotloop: {'yes' if target == '3ds' and os.environ.get('HGSS_GX3D_HOTLOOP') == '1' else 'no'}\n"
             f"gx3d_indexed: {'yes' if target == '3ds' and os.environ.get('HGSS_GX3D_INDEXED') == '1' else 'no'}\n"
             f"gx3d_listcache: {'yes' if target == '3ds' and os.environ.get('HGSS_GX3D_LISTCACHE') == '1' else 'no'}\n"
             f"gx3d_statededup: {'yes' if target == '3ds' and os.environ.get('HGSS_GX3D_STATEDEDUP') == '1' else 'no'}\n"
             f"gx3d_fusehash: {'yes' if target == '3ds' and os.environ.get('HGSS_GX3D_FUSEHASH') == '1' else 'no'}\n"
             f"gx3d_lazyinsert: {'yes' if target == '3ds' and os.environ.get('HGSS_GX3D_LAZYINSERT') == '1' else 'no'}\n"
             f"a0_diag: {'yes' if target == '3ds' and os.environ.get('HGSS_A0_DIAG') == '1' else 'no'}\n"
            f"build: {experiment_build_id(short, dirty)}\n"
            f"sha: {full}\n"
            f"dirty: {'yes' if dirty else 'no'}\n"
            f"built_utc: {now}\n"
            f"file: {dsx.name}\n"
            f"size: {dsx.stat().st_size}\n"
            f"sha256: {digest}\n"
            f"status:\n{status}")
    if not body.endswith("\n"):
        body += "\n"
    manifest.write_text(body, encoding="utf-8")
    return manifest


def package_three_dsx(binary: Path, target: str,
                      build_id: str | None = None) -> Path | None:
    """Package a linked 3DS ELF as a .3dsx next to it, or do nothing.

    Packaging is deliberately after the link: a static link with zero
    unresolved symbols is the Phase 2 bar, and the .3dsx is the runnable
    artifact for the Homebrew Launcher. A missing 3dsxtool is reported, not
    fatal (the ELF is already the proof of the link). build_id (the
    experiment tag) goes into the SMDH names so the launcher entry is
    unique per build; None keeps the old generic names.
    """
    if target != "3ds":
        return None
    tool = devkitpro() / "tools" / "bin" / "3dsxtool"
    dsx = binary.with_name(binary.name.replace("-3ds.elf", "").replace(".elf", "")
                           + ".3dsx")
    if not tool.is_file():
        print(f"   note: {tool} not found; leaving {binary.name} unpackaged")
        return None
    smdh = make_smdh(binary, build_id)
    proc = subprocess.run(three_dsx_command(tool, binary, dsx, smdh),
                          capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"   note: 3dsxtool failed: {(proc.stderr or proc.stdout).strip()}")
        return None
    print(f"   packaged {dsx} ({dsx.stat().st_size} bytes)")
    try:
        problems = validate_3dsx(dsx)
    except ValueError as exc:
        print(f"   error: {dsx.name} is not a usable .3dsx: {exc}")
        raise ThreeDsxError(str(exc)) from exc
    if problems:
        print(f"   error: {dsx.name} would be rejected by the console's loader "
              f"({len(problems)} problem{'s' if len(problems) != 1 else ''}):")
        for problem in problems[:5]:
            print(f"     {problem}")
        print("     the loader aborts on this; fix the relocation, not the .3dsx")
        raise ThreeDsxError(problems[0])
    return dsx


def link_command(cc: str, target: str, app_objects: list[Path],
                 game_objects: list[Path], stub_objects: list[Path],
                 overlay_table: Path, overlay_ids: Path,
                 libs: list[Path], binary: Path,
                 rsp_path: Path) -> list[str]:
    """The link. Game objects go through a response file (over a thousand
    paths); the overlay table is last among the game's objects so its 4-byte
    section tails land at the end (tools/game_overlays.py)."""
    ordered = list(app_objects) + list(game_objects) + list(stub_objects)
    rsp_path.write_text("\n".join(str(p) for p in ordered) + "\n",
                        encoding="utf-8")
    if target == "host32":
        flags = [*HOST_FLAGS, "-no-pie", "-pthread"]
        # --trace-pxi/--trace-irq: the diagnostic wrappers in pc_trace.c (the
        # port layer itself stays byte-identical to platinum.lock).
        wraps = ["-Wl,--wrap=PXI_SendWordByFifo",
                 "-Wl,--wrap=NitroPort_Arm7Reply",
                 "-Wl,--wrap=NitroPort_RaiseIrq",
                 "-Wl,--wrap=OS_WaitIrq"]
        extra = ["-lm", "-pthread", *wraps]
        if SANITIZE:
            # ASan's runtime references the C++ allocation helpers through
            # the MSL new objects; the normal link deliberately has no
            # libstdc++.
            extra += SANITIZE_FLAGS + ["-lstdc++"]
    else:
        # devkitARM's crt0 (3dsx.specs) provides _start and calls main; the
        # program is a normal libctru/citro3d one.
        pro = devkitpro()
        flags = [*ARM_FLAGS, "-specs=3dsx.specs",
                 f"-L{pro / 'libctru' / 'lib'}",
                 f"-L{pro / 'portlibs' / '3ds' / 'lib'}",
                 "-Wl,--undefined=Heartgold_Run", "-Wl,--undefined=NitroMain"]
        extra = ["-lcitro3d", "-lctru", "-lm"]
    cmd = [cc, *flags, "-Wl,--gc-sections",
           "-Wl,--wrap=ErrorHandling_AssertFail", f"@{rsp_path}",
           str(overlay_table), "-Wl,--start-group"]
    cmd += [str(p) for p in libs]
    cmd += ["-Wl,--end-group", str(overlay_ids), *extra, "-o", str(binary)]
    return cmd


# ------------------------------------------------------------------- helpers

def error_count(text: str) -> int:
    """Compiler errors in one file's output (not warnings)."""
    return sum(1 for line in text.splitlines() if ERROR_RE.search(line))


def unresolved_symbols(text: str) -> list[str]:
    """Unique undefined symbols in a linker's output, in first-seen order."""
    seen: dict[str, None] = {}
    for name in UNDEFINED_RE.findall(text):
        seen.setdefault(name, None)
    return list(seen)


ELF_LAYOUT_SECTION_RE = re.compile(
    r"\s*\[\s*\d+\]\s+\S*\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+"
    r"([0-9a-fA-F]+)\s")


def elf_bytes_at(readelf: str, binary: Path, addr: int, count: int) -> bytes | None:
    """The file bytes behind `count` loaded bytes at virtual address `addr`.

    None if no allocated, file-backed section holds the whole range.
    """
    for line in run([readelf, "-SW", str(binary)]).stdout.splitlines():
        match = ELF_LAYOUT_SECTION_RE.match(line)
        if not match:
            continue
        kind = match.group(1)
        vaddr, offset, size = (int(g, 16) for g in match.group(2, 3, 4))
        if kind == "NOBITS" or vaddr == 0:
            continue
        if not (vaddr <= addr and addr + count <= vaddr + size):
            continue
        with binary.open("rb") as handle:
            handle.seek(offset + addr - vaddr)
            data = handle.read(count)
        return data if len(data) == count else None
    return None


def check_nns_volume_table(nm: str, readelf: str, binary: Path) -> tuple[bool, str]:
    """Confirm the linked program's NNS volume table is the full 256-entry table.

    The decomp's empty .rodata.alias_d750 is removed from the SDK data object
    and hgss/compat/src/nns_volume_table.c supplies the table; a future section
    rename or an accidentally dropped app object must fail the build here
    rather than silently take the audio back to a clipped wall with hung notes.
    The field fader parks at NNS_VOLUME_FADER_INDEX, so that entry must be 0 dB:
    a short table reads whatever follows it there.
    """
    try:
        proc = run([nm, "-S", "--defined-only", str(binary)])
    except OSError as exc:
        return False, f"can't run {nm}: {exc}"
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[-1] == NNS_VOLUME_SYMBOL:
            try:
                addr = int(parts[0], 16)
                size = int(parts[1], 16)
            except ValueError:
                break
            if size != NNS_VOLUME_TABLE_SIZE:
                return False, (f"{NNS_VOLUME_SYMBOL} is {size} bytes, "
                               f"want {NNS_VOLUME_TABLE_SIZE}")
            fader = elf_bytes_at(readelf, binary,
                                 addr + 2 * NNS_VOLUME_FADER_INDEX, 2)
            if fader is None:
                return False, (f"{NNS_VOLUME_SYMBOL}[{NNS_VOLUME_FADER_INDEX}] "
                               "is not in the file")
            value = int.from_bytes(fader, "little", signed=True)
            if value != 0:
                return False, (f"{NNS_VOLUME_SYMBOL}[{NNS_VOLUME_FADER_INDEX}] "
                               f"is {value}, want 0 (0 dB)")
            return True, (f"{NNS_VOLUME_SYMBOL} -> "
                          f"{NNS_VOLUME_PORT_SOURCE.name} ({size} bytes, "
                          f"[{NNS_VOLUME_FADER_INDEX}] = 0)")
    return False, f"{NNS_VOLUME_SYMBOL} is not defined"


def run(cmd: list[str], cwd: Path | None = None,
        env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    """Run a child with captured output (tasks print their own summaries)."""
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)


def compile_one(cc: str, flags: list[str], src: Path, obj: Path,
                language: str, log_dir: Path, root: Path | None = None,
                alias_env: dict[str, str] | None = None,
                nm: str | None = None, readelf: str | None = None,
                out: Path | None = None
                ) -> tuple[bool, int, str]:
    obj.parent.mkdir(parents=True, exist_ok=True)
    proc = run(compile_command(cc, flags, src, obj, language))
    text = proc.stdout + proc.stderr
    failures = error_count(text)
    if proc.returncode != 0 and failures == 0:
        failures = 1  # e.g. the compiler itself failed
    failed = proc.returncode != 0
    if not failed and root is not None:
        aliased, alias_text = apply_object_aliases(root, src, obj, alias_env,
                                                   nm, readelf, out)
        if not aliased:
            text += alias_text
            failures = max(1, error_count(alias_text))
            failed = True
    if failed:
        log = log_dir / (str(obj.relative_to(log_dir.parent)) + ".log")
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(text, encoding="utf-8")
        return False, failures, str(log)
    return True, 0, ""


def compile_objects(cc: str, flags: list[str], sources: list[tuple[Path, Path, str]],
                    jobs: int, log_dir: Path, keep_going: bool,
                    root: Path | None = None,
                    alias_env: dict[str, str] | None = None,
                    nm: str | None = None, readelf: str | None = None,
                    out: Path | None = None
                    ) -> tuple[list[Path], list[tuple[str, int, str]]]:
    """Compile (src, obj, language) triples; return objects and failures."""
    built: list[Path] = []
    failed: list[tuple[str, int, str]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        futures = {pool.submit(compile_one, cc, flags, src, obj, language, log_dir,
                               root, alias_env, nm, readelf, out):
                   (src, obj) for src, obj, language in sources}
        for future in concurrent.futures.as_completed(futures):
            src, obj = futures[future]
            ok, errors, log = future.result()
            if ok:
                built.append(obj)
            else:
                failed.append((str(src), errors, log))
                if not keep_going:
                    for pending in futures:
                        pending.cancel()
                    break
    built.sort()
    failed.sort()
    return built, failed


def assemble_objects(root: Path, cc: str, flags: list[str], out: Path,
                     sources: list[str], jobs: int, log_dir: Path,
                     keep_going: bool,
                     alias_env: dict[str, str] | None = None,
                     nm: str | None = None, readelf: str | None = None,
                     overlay_ids: Path | None = None,
                     objcopy: str | None = None
                     ) -> tuple[list[Path], list[tuple[str, int, str]]]:
    """Assemble every .s through tools/s_to_gas.py, then apply any sibling
    .alias-map.json (an .s can own a FUNC alias map too)."""
    built: list[Path] = []
    failed: list[tuple[str, int, str]] = []

    def one(rel: str):
        obj = object_path(out, rel)
        obj.parent.mkdir(parents=True, exist_ok=True)
        proc = run(asm_command(root, root / rel, obj, cc, flags, overlay_ids))
        text = proc.stdout + proc.stderr
        errors = error_count(text)
        if proc.returncode != 0 and errors == 0:
            errors = 1
        ok = proc.returncode == 0
        if ok and readelf is not None:
            align_text = align_version_section(obj, readelf, objcopy)
            if align_text:
                text += align_text
                ok = "error" not in align_text.lower()
        if ok and objcopy is not None and Path(rel).as_posix().endswith(NNS_VOLUME_SDK_SOURCE):
            # See NNS_VOLUME_ALIAS_SECTION: the port supplies the real table
            # from NNS_VOLUME_PORT_SOURCE, so the empty SDK definition must go.
            strip = run([objcopy, f"--remove-section={NNS_VOLUME_ALIAS_SECTION}",
                         str(obj)])
            text += strip.stdout + strip.stderr
            if strip.returncode != 0:
                ok = False
                errors = 1
        if ok:
            ok, alias_text = apply_object_aliases(root, root / rel, obj,
                                                  alias_env, nm, readelf, out)
            if not ok:
                text += alias_text
                errors = max(1, error_count(alias_text))
        return rel, obj, ok, errors, text

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        futures = {pool.submit(one, rel): rel for rel in sources}
        for future in concurrent.futures.as_completed(futures):
            rel, obj, ok, errors, text = future.result()
            if ok:
                built.append(obj)
            else:
                log = log_dir / (str(obj.relative_to(out / "obj")) + ".log")
                log.parent.mkdir(parents=True, exist_ok=True)
                log.write_text(text, encoding="utf-8")
                failed.append((rel, errors, str(log)))
                if not keep_going:
                    for pending in futures:
                        pending.cancel()
                    break
    built.sort()
    failed.sort()
    return built, failed


def weaken_duplicate_symbols(nm: str, objcopy: str, objects: list[Path],
                             out: Path) -> tuple[list[str], int]:
    """Make every symbol defined in more than one object weak, once.

    The original game is one program, so a global defined twice can only be
    a header inline (Metrowerks kept one copy; GCC exports one from every
    object that includes the header) or a symbol of two overlays that never
    coexist on the DS. Both must not stop the port's link. The Platinum
    build does the same census and weaken pass; this is the HGSS harness's
    copy. Returns the duplicate names and how many objects were touched.

    nm is run on batches of files, not one process per object: spawning
    hundreds of children from a thread pool deadlocked once under the
    container's emulation.
    """
    defs: dict[str, list[Path]] = {}
    batch = 100
    for start in range(0, len(objects), batch):
        chunk = objects[start:start + batch]
        proc = subprocess.run([nm, "-g", "--defined-only",
                               *[str(obj) for obj in chunk]],
                              capture_output=True, text=True)
        current: Path | None = chunk[0] if len(chunk) == 1 else None
        for line in proc.stdout.splitlines():
            parts = line.split()
            if line.endswith(":") and len(parts) == 1:
                name = line[:-1]
                current = next((o for o in chunk if str(o) == name), None)
                continue
            if current is not None and len(parts) == 3 and parts[1] in "TDBR":
                defs.setdefault(parts[2], []).append(current)
    duplicates = sorted(name for name, owners in defs.items() if len(owners) > 1)
    if not duplicates:
        return [], 0
    symfile = out / "gen" / "duplicate_symbols.txt"
    symfile.write_text("\n".join(duplicates) + "\n", encoding="utf-8")
    affected = sorted({obj for name in duplicates for obj in defs[name]})
    for obj in affected:
        subprocess.run([objcopy, f"--weaken-symbols={symfile}", str(obj)],
                       check=False, capture_output=True)
    return duplicates, len(affected)


def palette_sdk_mode_matches(out: Path) -> bool:
    marker = out / "sdk" / ".palette-hash-mode"
    wanted = "1" if os.environ.get("HGSS_PALETTE_HASH") == "1" else "0"
    return marker.is_file() and marker.read_text().strip() == wanted


def gx3d_sdk_mode() -> str:
    """Combined toggle state for the geometry flags compiled into libnitro.a
    (port_gx3d.c). Make does not track environment flags; the marker below
    invalidates the affected object whenever any of them toggles."""
    return ",".join("1" if os.environ.get(v) == "1" else "0" for v in
                    ("HGSS_GX3D_HOTLOOP", "HGSS_GX3D_LISTCACHE", "HGSS_GX3D_STATEDEDUP",
                     "HGSS_GX3D_FUSEHASH", "HGSS_GX3D_LAZYINSERT"))


def build_libraries(root: Path, target: str, out: Path, jobs: int) -> tuple[bool, list[Path]]:
    """libnitro.a and libnitrosystem.a through tools/hgss_nitro.py.

    Returns whether both built and the archives that exist. A failed archive
    does not stop the harness: the link then reports the symbols it would
    have provided, which is part of the Phase 2 work queue.
    """
    sdk_out = out / "sdk"
    # Make does not track changed environment flags. Invalidate the one
    # affected generated object whenever this experiment is toggled, including
    # an existing output that predates the marker, so the sidecar is truthful.
    palette_mode = "1" if os.environ.get("HGSS_PALETTE_HASH") == "1" else "0"
    palette_marker = sdk_out / ".palette-hash-mode"
    if not palette_marker.exists() or palette_marker.read_text().strip() != palette_mode:
        (sdk_out / "port" / "port_gx3d_texture.o").unlink(missing_ok=True)
    gx3d_mode = gx3d_sdk_mode()
    gx3d_marker = sdk_out / ".gx3d-mode"
    if not gx3d_marker.exists() or gx3d_marker.read_text().strip() != gx3d_mode:
        (sdk_out / "port" / "port_gx3d.o").unlink(missing_ok=True)
    a0_mode = "1" if os.environ.get("HGSS_A0_DIAG") == "1" else "0"
    a0_marker = sdk_out / ".a0-mode"
    if not a0_marker.exists() or a0_marker.read_text().strip() != a0_mode:
        (sdk_out / "port" / "port_gx3d.o").unlink(missing_ok=True)
        (sdk_out / "port" / "port_render2d.o").unlink(missing_ok=True)
    nitro_cmd = [sys.executable, str(TOOLS / "hgss_nitro.py"), "make",
                 "--target", target, "--out", str(sdk_out), "--subproject", "nitro",
                 "lib", "-k", "-j", str(jobs)]
    nns_cmd = [sys.executable, str(TOOLS / "hgss_nitro.py"), "make",
               "--target", target, "--out", str(sdk_out), "--subproject", "nitrosystem",
               "lib", "-k", "-j", str(jobs)]
    ok = True
    for index, cmd in enumerate((nitro_cmd, nns_cmd)):
        print("+ " + " ".join(cmd), flush=True)
        proc = subprocess.run(cmd)
        if index == 0 and proc.returncode == 0:
            palette_marker.parent.mkdir(parents=True, exist_ok=True)
            palette_marker.write_text(palette_mode + "\n")
            gx3d_marker.write_text(gx3d_mode + "\n")
            a0_marker.write_text(a0_mode + "\n")
        ok = ok and proc.returncode == 0
    libs = [p for p in (sdk_out / "libnitrosystem.a", sdk_out / "libnitro.a")
            if p.is_file()]
    if not ok:
        print("   warning: the vendored library build failed; linking with "
              f"{', '.join(p.name for p in libs) or 'no archives'} and reporting "
              "what is missing", flush=True)
    return ok and len(libs) == 2, libs


# ---------------------------------------------------------------------- main

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--target", choices=["host32", "3ds"], default="host32")
    ap.add_argument("--game", choices=sorted(GAMES), default="heartgold",
                    help="which version to build (default: heartgold); selects "
                    "the decomp's HEARTGOLD/SOULSILVER define and the program name")
    ap.add_argument("--root", type=Path, default=DEFAULT_ROOT,
                    help="patched pokeheartgold checkout (default: build/upstream)")
    ap.add_argument("--out", type=Path, required=True,
                    help="private output directory (objects, libraries, binary)")
    ap.add_argument("--keep-going", action="store_true",
                    help="compile every file, report per-file error counts, then link")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--skip-prepare", action="store_true",
                    help="reuse the generated headers already in --root")
    ap.add_argument("--sanitize", action="store_true",
                    help="host32 only: build with -fsanitize=address,undefined")
    args = ap.parse_args(argv)

    global SANITIZE, GAME
    GAME = GAMES[args.game]
    if args.sanitize:
        if args.target != "host32":
            ap.error("--sanitize is host32 only")
        SANITIZE = True

    root = args.root.resolve()
    out = args.out.resolve()
    if not (root / "main.lsf").is_file():
        print(f"hgss_game: {root} is not a bootstrapped checkout "
              f"(run python3 tools/bootstrap.py)", file=sys.stderr)
        return 2
    # The patch stack is part of the source, and a tree that is missing a
    # patch compiles silently and then misbehaves on hardware: a native tree
    # without 1905 kept a raw 0x04000000 read that aborted the console at the
    # first map load drawing the wireless icon. One `git log` is cheap.
    if root.name == "upstream" and root.parent.name == "build":
        patch_check = subprocess.run(
            [sys.executable, str(TOOLS / "check_patches.py"), "--quiet",
             "--root", str(root.parent.parent)],
            capture_output=True, text=True)
        if patch_check.returncode != 0:
            print(patch_check.stderr, file=sys.stderr, end="")
            print("hgss_game: build/upstream is missing patches; run "
                  "python3 tools/check_patches.py for the list", file=sys.stderr)
            return 2
    tools = target_tools(args.target)
    out.mkdir(parents=True, exist_ok=True)
    log_dir = out / "logs"
    log_dir.mkdir(exist_ok=True)
    gen = out / "gen"
    gen.mkdir(exist_ok=True)
    # Commit identity for the boot banner + build-info sidecar (log.txt
    # must trace to its binary). Content-only change: clean-tree rebuilds
    # stay byte-identical.
    build_info = git_build_info()
    build_id = write_build_id_header(gen, build_info)
    print(f"== build id: {build_id}", flush=True)
    alias_env = alias_tool_environment(out, tools)

    if not args.skip_prepare:
        cmd = prepare_command(root, out)
        print("+ " + " ".join(cmd), flush=True)
        if subprocess.run(cmd).returncode != 0:
            print("hgss_game: header generation failed", file=sys.stderr)
            return 2

    data = gs.all_data(root)
    sources = select_sources(data)
    print(f"== {args.game} {args.target}: {len(sources['c'])} C, {len(sources['cpp'])} C++, "
          f"{len(sources['asm'])} .s")

    cflags = port_cflags(args.target)
    cxxflags = port_cflags(args.target, cxx=True)
    includes = include_flags(root, out)
    overlay_header = out / "gen" / "overlay_ids.h"
    cflags += includes + ["-include", str(overlay_header)]
    cxxflags += includes + ["-include", str(overlay_header)]

    # Game objects: src/**/*.c, src/**/*.cpp, and the game-only lib sources.
    # C and C++ need different standards and, for C++, no exceptions/RTTI.
    c_jobs = [(root / rel, object_path(out, rel), "c") for rel in sources["c"]]
    cxx_jobs = [(root / rel, object_path(out, rel), "c++") for rel in sources["cpp"]]
    print(f"== compiling C/C++ ({len(c_jobs) + len(cxx_jobs)} files, -j{args.jobs})",
          flush=True)
    c_objects, c_failed = compile_objects(tools["cc"], cflags, c_jobs, args.jobs,
                                          log_dir, args.keep_going,
                                          root, alias_env, tools["nm"],
                                          tools["readelf"], out)
    cxx_objects, cxx_failed = compile_objects(tools["cc"], cxxflags, cxx_jobs,
                                              args.jobs, log_dir, args.keep_going,
                                              root, alias_env, tools["nm"],
                                              tools["readelf"], out)
    c_objects += cxx_objects
    c_failed += cxx_failed
    print(f"   {len(c_objects)} compiled, {len(c_failed)} failed")
    if c_failed:
        print(f"   {sum(e for _, e, _ in c_failed)} compiler errors in "
              f"{len(c_failed)} files")
        for src, errors, log in c_failed:
            print(f"     {errors:4d}  {src}  ({log})")

    print(f"== assembling ARM9 .s ({len(sources['asm'])} files)", flush=True)
    aflags = asm_flags(args.target, root, out)
    # The overlay IDs are assembled as numbers (see tools/s_to_gas.py):
    # a link-script absolute symbol would leave R_ARM_ABS32 relocations with
    # values below the image base, which 3dsxtool rejects.
    asm_objects, asm_failed = assemble_objects(
        root, tools["cc"], aflags, out, sources["asm"], args.jobs, log_dir,
        args.keep_going, alias_env, tools["nm"], tools["readelf"],
        gen / "overlay_ids.ld", tools["objcopy"])
    print(f"   {len(asm_objects)} assembled, {len(asm_failed)} failed")
    if asm_failed:
        print(f"   {sum(e for _, e, _ in asm_failed)} errors in "
              f"{len(asm_failed)} files")
        for src, errors, log in asm_failed:
            print(f"     {errors:4d}  {src}  ({log})")

    print("== port libraries", flush=True)
    _, libs = build_libraries(root, args.target, out, args.jobs)
    if not palette_sdk_mode_matches(out):
        print("   error: palette-hash SDK mode was not built; refusing a potentially stale archive", file=sys.stderr)
        return 1

    print("== overlay table", flush=True)
    proc = subprocess.run(overlay_command(root, out, tools["objcopy"]),
                          capture_output=True, text=True)
    print(proc.stdout.rstrip())
    if proc.returncode != 0:
        print(proc.stderr.rstrip(), file=sys.stderr)

    game_objects = link_objects(sorted(c_objects + asm_objects), data, out)

    print("== duplicate symbols", flush=True)
    duplicates, touched = weaken_duplicate_symbols(
        tools["nm"], tools["objcopy"], game_objects, out)
    print(f"   {len(duplicates)} symbols defined in more than one object "
          f"({touched} objects weakened)")
    for name in duplicates[:40]:
        print(f"     {name}")
    if len(duplicates) > 40:
        print(f"     ... and {len(duplicates) - 40} more in "
              f"{out / 'gen' / 'duplicate_symbols.txt'}")

    print("== Wi-Fi stand-ins", flush=True)
    wifi_c = gen / "wifi_stubs.c"
    proc = run(wifi_stub_command(tools["nm"], root, game_objects, wifi_c))
    print(proc.stdout.rstrip())
    if proc.returncode != 0:
        print(proc.stderr.rstrip(), file=sys.stderr)

    app_jobs = [(src, out / "app" / (src.stem + ".o"), "c")
                for src in app_sources(args.target)]
    # port_pica_abi.c must see libctru's enums at libctru's size, so it is
    # compiled without the port's -fno-short-enums (as Platinum's Makefile
    # does); the rest of the app uses the port's flags.
    abi_src = REPO / "3ds_port" / "game" / "app" / "port_pica_abi.c"
    if args.target == "3ds":
        app_jobs = [job for job in app_jobs if job[0] != abi_src]
    if wifi_c.is_file():
        app_jobs.append((wifi_c, out / "gen" / "wifi_stubs.o", "c"))
    # The program's own libpak reader: the vendored one keeps Platinum's ROM
    # allowlist, this copy adds HGSS's (see pak_reader_source).
    pak_c = pak_reader_source(out)
    app_jobs.append((pak_c, out / "app" / "pak_reader_hgss.o", "c"))
    print(f"== compiling the app ({len(app_jobs)} files)", flush=True)
    app_cflags = list(cflags) + [f"-I{REPO / '3ds_port' / 'pak' / 'include'}",
                                 f"-I{REPO / '3ds_port' / 'game' / 'app'}"]
    if args.target == "3ds":
        # main_3ds.c is the only file here that includes libctru's headers;
        # the game and the port keep NitroSDK's.
        app_cflags.append(f"-I{devkitpro() / 'libctru' / 'include'}")
        # (port_cflags carries -O2 now, so this is belt-and-braces for the
        # app half: the PICA backend's per-frame batching benefits and the DS
        # gate never sees these objects.)
        app_cflags.append("-O2")
    app_objects, app_failed = compile_objects(
        tools["cc"], app_cflags, app_jobs, args.jobs, log_dir, args.keep_going)
    if args.target == "3ds":
        abi_obj = out / "app" / "port_pica_abi.o"
        abi_flags = [flag for flag in app_cflags if flag != "-fno-short-enums"]
        abi_ok, abi_errors, abi_log = compile_one(
            tools["cc"], abi_flags, abi_src, abi_obj, "c", log_dir)
        if abi_ok:
            app_objects.append(abi_obj)
        else:
            app_failed.append((abi_src, abi_errors, abi_log))
        try:
            app_objects.append(gx3d_shader_object(out))
        except RuntimeError as exc:
            print(f"   shader build failed: {exc}", file=sys.stderr)
            app_failed.append((Path("gx3d shaders"), 1, ""))
    for src, errors, log in app_failed:
        print(f"     {errors:4d}  {src}  ({log})")
    if app_failed:
        print(f"   {len(app_failed)} app source(s) failed; not linking")
        return 1

    # The overlay table includes <nitro_port.h> only.
    table_src = gen / "overlay_table.c"
    table_obj = gen / "overlay_table.o"
    table_ok, table_errors, table_log = compile_one(
        tools["cc"], port_cflags(args.target) + includes,
        table_src, table_obj, "c", log_dir) if table_src.is_file() else (False, 0, "")
    if not table_ok:
        print(f"     {table_errors:4d}  overlay_table.c  ({table_log})")

    print(f"== link -> {binary_name(out, args.target)}", flush=True)
    binary = binary_name(out, args.target)
    rsp = out / "link.rsp"
    link = link_command(tools["cc"], args.target, app_objects, game_objects,
                        [], table_obj, gen / "overlay_ids.ld",
                        libs, binary, rsp)
    proc = subprocess.run(link, capture_output=True, text=True)
    text = proc.stderr + proc.stdout
    unresolved = unresolved_symbols(text)
    (out / "link.log").write_text(text, encoding="utf-8")
    # Timestamped copy: rebuilding the same --out must not silently lose the
    # previous link log. link.log stays the latest; link-<utc>.log keeps
    # every attempt.
    try:
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S")
        (out / f"link-{stamp}.log").write_text(text, encoding="utf-8")
    except OSError:
        pass
    if proc.returncode == 0 and binary.is_file():
        print(f"   linked {binary}")
        table_ok, table_text = check_nns_volume_table(tools["nm"], tools["readelf"],
                                                     binary)
        print(f"   [sound] {table_text}")
        if not table_ok:
            print("hgss_game: the NNS volume table is missing or the wrong size; "
                  "the port must drop " + NNS_VOLUME_ALIAS_SECTION + " from "
                  + NNS_VOLUME_SDK_SOURCE + " and link " + str(NNS_VOLUME_PORT_SOURCE),
                  file=sys.stderr)
            return 1
        try:
            dsx = package_three_dsx(binary, args.target, build_id)
        except ThreeDsxError as exc:
            print(f"   the .3dsx was not accepted: {exc}")
            return 1
        if dsx is not None:
            manifest = write_build_info(dsx, args.game, args.target, build_info)
            print(f"   build info: {manifest.name}")
        return 0
    print(f"   link failed ({len(unresolved)} unresolved symbols); log: {out / 'link.log'}")
    for name in unresolved:
        print(f"     {name}")
    if not unresolved:
        print(text[-4000:])
    return 1


if __name__ == "__main__":
    sys.exit(main())
