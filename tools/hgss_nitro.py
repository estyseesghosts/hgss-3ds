#!/usr/bin/env python3
"""Build the vendored 3ds_port libraries against HGSS's SDK without editing the vendored Makefiles.

Usage (inside the dev container, after tools/bootstrap.py):
  hgss_nitro.py shim                 create the shim trees and print the nitro make overrides
  hgss_nitro.py make --target host32 --out /work/out/p1 lib [-k -j8]
                                     run `make -C 3ds_port/nitro` with the overrides
  hgss_nitro.py make --target host32 --out /work/out/p1 test
                                     run `test` across pak, nitro and nitrosystem (see --subproject)
  hgss_nitro.py make --target host32 --out /work/out/p1 test --subproject nitro
                                     run `test` for one subproject only
  hgss_nitro.py list                 print the HGSS source lists that are compiled, and the excluded files

How it works (docs/port-wiring.md): the vendored nitro Makefile compiles `$(SDK)/libraries/<x>.c`
for each entry of SDK_SRCS. `build/hgss-sdk/libraries/hgss` is a symlink to
build/upstream/lib/NitroSDK/src, so the list can name HGSS's real files (`hgss/os/os_thread.c`).
`hgss_compat` (a symlink to hgss/compat/src) adds the port's SDK stand-ins, SDK data and NNS
data; `hgss_sub` (a symlink to sub/lib/src) feeds the genuine ARM7 sound driver to the
Makefile's ARM7 rule, which renames its symbols to N7_*. The nitrosystem Makefile compiles
`$(NNS)/libraries/<x>.c` for each entry of NNS_SRCS: `build/hgss-nns` shims HGSS's flat
lib/NitroSystem/src (as `hgss_nns/`) plus the two files the nitro tests compile directly
(`g3d/src/gecom.c`, `gfd/src/VramTransferMan/gfd_VramTransferManager.c`). Headers: a shim
include dir with only nitro*/nnsys*/cw* from lib/include (its stdarg.h/stddef.h/string.h would
shadow the system ones), HGSS's flat SDK include dirs, and hgss/compat/include (tracked:
headers the port includes that HGSS names differently or lacks). Command-line variables
override the Makefiles' own assignments, so nothing vendored changes.
"""
import argparse
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUILD = REPO / "build"

# HGSS SDK sources the port replaces with its own code (3ds_port/nitro/src), or that make no sense on the
# port. Matches docs/progress/phase-1.md ("port-replaced") plus the libraries the vendored Makefile documents
# as not built. os_terminate_arm9.c defines OS_Terminate, which the port owns (port_os.c).
EXCLUDE_FILES = {
    "crt0.c", "dgt_hash.c", "dgt_hash2_arm4_small.c",
    "mi/mi_memory.c", "mi/mi_swap.c", "mi/mi_uncompress.c",
    "os/os_cache.c", "os/os_tcm.c", "os/os_alarm.c",
    "os/os_terminate_arm9.c",
    "wvr.c",
}
EXCLUDE_PREFIXES = ("os/os_exception", "os/os_protection", "os/os_reset_", "pxi/", "ctrdg/", "mb/")

# HGSS's genuine ARM7 sound driver (sub/lib/src/). The vendored nitro Makefile compiles these with
# -DSDK_ARM7 and renames every global to N7_*, which is what port_snd.c calls (N7_SND_Init,
# NNS_G3dGlb-style renaming for sound). Needs SNDi_DecibelTable from the ARM9 SND_util.c object.
# snd_arm7_data.c is hgss/compat/src's sound-only subset of sub/asm/sound_card_pad_data.s (the
# driver's own BSS: SNDi_Work, the sequence cache window, thread/alarm state, tables); it is
# compiled by the same ARM7 rule so its globals are renamed too.
ARM7_SOUND_FILES = (
    "sound_main.c", "sound_command.c", "sound_ex_channel.c", "sound_seq_player.c",
    "sound_alarm.c", "sound_bank.c", "sound_channel.c", "sound_driver.c",
    "sound_shared_work.c",
)
# ds_sound_tables.c: the two BIOS sound tables (SWIs) and the ARM7 PM control pair
# the driver calls; the port has neither BIOS nor ARM7 SPI.
ARM7_COMPAT_FILES = ("snd_arm7_data.c", "ds_sound_tables.c")

# hgss/compat/src files compiled into libnitro.a (weak symbols: a real SDK definition wins).
# nns_data.c is NNS BSS (no references); nns_tables_gfd.c is the VRAM transfer dispatch table
# the nitro VRAM-transfer test dispatches through. The remaining NNS tables go to libnitrosystem.a.
COMPAT_SRCS = ("sdk_data.c", "sdk_standins.c", "nns_data.c", "nns_tables_gfd.c")
# hgss/compat/src files compiled into libnitrosystem.a (as hgss_nns_compat/).
NNS_COMPAT_SRCS = ("nns_tables_nns.c",)

SUBPROJECTS = ("pak", "nitro", "nitrosystem")

# test_nitrosystem walks a directory of game files (see 3ds_port/nitrosystem/tests/test_nitrosystem.c):
# lowercase .nsbmd models (it requires more than 600) and uppercase .NCGR/.NCLR/.NCER/.NANR/.NSCR
# 2D files. The decompilation's files/ holds a few dozen models, so the fixture pads the count with
# hard links to one real model. Links, not copies: no extra disk, no game bytes in the repo (build/
# is git-ignored). nftw with FTW_PHYS skips symlinks, hence hard links.
NNS_RES_PAD_MODELS = 600
NNS_RES_2D_EXTS = frozenset({".NCGR", ".NCLR", ".NCER", ".NANR", ".NSCR"})


def src_dir(root):
    return root / "lib" / "NitroSDK" / "src"


def sdk_sources(root):
    base = src_dir(root)
    keep, drop = [], []
    for p in sorted(base.rglob("*.c")):
        rel = p.relative_to(base).as_posix()
        if rel in EXCLUDE_FILES or rel.startswith(EXCLUDE_PREFIXES):
            drop.append(rel)
        else:
            keep.append(rel)
    return keep, drop


def nns_sources(root):
    base = root / "lib" / "NitroSystem" / "src"
    return sorted(p.name for p in base.glob("*.c"))


def _link(link, target):
    link = Path(link)
    target = Path(target)
    link.parent.mkdir(parents=True, exist_ok=True)
    if link.is_symlink() and Path(os.readlink(link)) != target:
        link.unlink()
    if not link.exists() and not link.is_symlink():
        link.symlink_to(target)


def make_shim(root):
    sdk = BUILD / "hgss-sdk"
    inc = sdk / "include"
    (sdk / "libraries").mkdir(parents=True, exist_ok=True)
    inc.mkdir(exist_ok=True)
    _link(sdk / "libraries" / "hgss", src_dir(root))
    _link(sdk / "libraries" / "hgss_compat", REPO / "hgss" / "compat" / "src")
    _link(sdk / "libraries" / "hgss_sub", root / "sub" / "lib" / "src")
    for n in ("nitro", "nitro.h", "nnsys", "nnsys.h", "cw", "cw.h", "MSL.h", "MSL_C", "ansi_params.h", "size_t.h"):
        _link(inc / n, root / "lib" / "include" / n)
    gen = sdk / "gen" / "nitro" / "fx"
    gen.mkdir(parents=True, exist_ok=True)
    # The vendored rule runs `python3 gen_fx_const.py CSV OUT`; HGSS's generator is a C tool and bootstrap.py has
    # already produced lib/include/nitro/fx/fx_const.h, so the stand-in copies it.
    (gen / "fx_const.csv").write_text("# stand-in: see tools/hgss_nitro.py\n")
    (gen / "gen_fx_const.py").write_text(
        "import shutil, sys\n"
        f"shutil.copyfile({str(root / 'lib/include/nitro/fx/fx_const.h')!r}, sys.argv[2])\n")

    # NitroSystem shim for the nitrosystem build and the nitro tests that compile NNS files
    # directly (test_gx3d: g3d/src/gecom.c, test_vram_transfer: gfd/.../gfd_VramTransferManager.c).
    # HGSS's NNS sources are flat (lib/NitroSystem/src/g3d_*.c, ...); the shim exposes them as
    # hgss_nns/ and the two direct-compile files at their Platinum paths. Include is filtered to
    # nnsys*, mirroring the SDK shim (nitro headers come from SDK_INCLUDES instead).
    nns = BUILD / "hgss-nns"
    ninc = nns / "include"
    (nns / "libraries").mkdir(parents=True, exist_ok=True)
    ninc.mkdir(exist_ok=True)
    _link(nns / "libraries" / "hgss_nns", root / "lib" / "NitroSystem" / "src")
    _link(nns / "libraries" / "hgss_nns_compat", REPO / "hgss" / "compat" / "src")
    _link(nns / "libraries" / "g3d" / "src" / "gecom.c",
          root / "lib" / "NitroSystem" / "src" / "g3d_gecom.c")
    _link(nns / "libraries" / "gfd" / "src" / "VramTransferMan" / "gfd_VramTransferManager.c",
          root / "lib" / "NitroSystem" / "src" / "gfd_vram_transfer_manager.c")
    _link(ninc / "nnsys", root / "lib" / "include" / "nnsys")
    _link(ninc / "nnsys.h", root / "lib" / "include" / "nnsys.h")
    return sdk, nns


def sdk_includes(root, sdk, nitro, gen):
    return [f"-I{nitro}/include", f"-I{gen}", f"-I{REPO}/hgss/compat/include",
            f"-I{sdk}/include",
            # the decomp's own order puts the game's include/ before the SDK dirs: several SDK files
            # `#include "global.h"` and must get the game's, not include-pokediamond's
            f"-I{root}/include", f"-I{root}/include/library",
            # sub/ headers (ARM7 sound sources): quoted "OS_*.h" must keep resolving to the flat
            # NitroSDK headers, and none of sub/include's names collide with them.
            *[f"-I{root}/lib/NitroSDK/{d}" for d in ("include", "include-mw", "include-pokediamond")],
            *[f"-I{root}/sub/{d}" for d in ("include", "lib/include")],
            # The vendored third_party/stb_image.h (used by port_wideart.c) calls assert() but
            # includes no <assert.h>. Force HGSS's MSL header in by absolute path: <assert.h>
            # would resolve to the game's include/assert.h, which defines only GF_ASSERT.
            "-include", f"{root}/lib/include/assert.h",
            # The vendored port sources also call C99 math without <math.h>; an implicit
            # declaration truncates a double return through int and leaves `isfinite`
            # undefined at link. hgss/compat/include/hgss_port_math.h declares the surface
            # with the standard prototypes (the system header is shadowed by the SDK's).
            "-include", f"{REPO}/hgss/compat/include/hgss_port_math.h"]


def _make_out(subproject, out):
    """OUT= spelling for `make -C 3ds_port/<subproject>`.

    The vendored test recipes run binaries as `./$(TEST_BIN)`, so an absolute OUT=
    makes every test invocation `.//work/out/...`, which never executes. Pass OUT
    relative to the subproject directory instead; it names the same directory.
    GEN/LIBNITRO keep their absolute spellings (used only as paths, never after `./`).
    """
    if out is None:
        return None
    return os.path.relpath(out, REPO / "3ds_port" / subproject)


def nitro_overrides(root, target, out=None, subdir="nitro"):
    sdk, nns = make_shim(root)
    keep, _ = sdk_sources(root)
    nitro = REPO / "3ds_port" / "nitro"
    gen = f"{out}/gen" if out else f"{nitro}/build/{target}/gen"
    extra = [f"OUT={_make_out(subdir, out)}", f"GEN={out}/gen"] if out else []
    return extra + [f"SDK={sdk}",
            "SDK_SRCS=" + " ".join(["hgss/" + k for k in keep]
                                   + ["hgss_compat/" + c for c in COMPAT_SRCS]),
            "ARM7_SOUND_SRCS=" + " ".join(["hgss_sub/" + f for f in ARM7_SOUND_FILES]
                                          + ["hgss_compat/" + f for f in ARM7_COMPAT_FILES]),
            "SDK_INCLUDES=" + " ".join(sdk_includes(root, sdk, nitro, gen)),
            f"NNS={nns}", f"NNS_INCLUDES=-I{nns}/include"]


def write_nns_all_headers(out_gen, root, nns):
    """Pre-generate the NitroSystem header-unity file so the vendored rule stays up to date.

    3ds_port/nitrosystem's Makefile builds $(GEN)/nns_all_headers.c with tools/nns_inline_symbols.py,
    which hardcodes build/NitroSystem (Platinum's meson subproject layout). For HGSS that tree has no
    include/ dir and the script would fail, so mirror it here over the shim: <nitro.h> plus every
    nnsys header, as quoted paths relative to the generated file. Rewritten only on change (then
    touched), exactly like the script, so make never reruns it mid-build.
    """
    inc = Path(nns) / "include" / "nnsys"
    found = sorted(inc.rglob("*.h")) if inc.is_dir() else []
    found = [p for p in found
             if "mcs" not in p.relative_to(nns).parts
             and p.name != "mcs.h" and "win32" not in p.name]
    base = Path(out_gen).resolve()
    lines = ["/* Generated by tools/hgss_nitro.py: every HGSS NitroSystem header. "
             "Mirrors tools/nns_inline_symbols.py. */",
             "#include <nitro.h>"]
    lines += [f'#include "{Path(os.path.relpath(p, base)).as_posix()}"' for p in found]
    text = "\n".join(lines) + "\n"
    dest = Path(out_gen) / "nns_all_headers.c"
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not dest.is_file() or dest.read_text() != text:
        dest.write_text(text)
    else:
        os.utime(dest)


def _write_nm_wrapper(out, target):
    """An nm wrapper for the NitroSystem weaken pass, and its path.

    The vendored rule builds `nns_inline.syms` with

        $(NM) -g --defined-only nns_all_headers.o |
            awk '$2 == "T" { print $3 }' > $@.new

    and every NNS object is then passed through

        objcopy --weaken-symbols=$(NNS_INLINE_SYMS) $@

    HGSS's nnsys headers define every inline function as `static inline`, so
    the unity object has no exported `T` symbols and the list is legitimately
    empty. binutils 2.42's objcopy exits 1 on an empty --weaken-symbols file
    (with no message), which failed the build. Append one name that no object
    defines: objcopy ignores names it cannot find, and the list is private to
    --out. Everything else is passed through to the real nm so the wrapper
    keeps working for the 3DS toolchain.
    """
    base = Path(out) if out else BUILD / "hgss-nns"
    base.mkdir(parents=True, exist_ok=True)
    wrapper = base / "nm-for-weaken.sh"
    devkitarm = os.environ.get("DEVKITARM")
    if target == "3ds" and devkitarm:
        real = f"{devkitarm}/bin/arm-none-eabi-nm"
    else:
        real = os.environ.get("NM", "nm")
    wrapper.write_text(
        "#!/bin/sh\n"
        "# Generated by tools/hgss_nitro.py: see _write_nm_wrapper().\n"
        f"REAL_NM={shlex.quote(real)}\n"
        'case "$*" in\n'
        "  *nns_all_headers.o*)\n"
        '    "$REAL_NM" "$@"\n'
        '    echo "00000000 T hgss_nns_no_inline_symbols"\n'
        "    ;;\n"
        "  *)\n"
        '    exec "$REAL_NM" "$@"\n'
        "    ;;\n"
        "esac\n"
    )
    wrapper.chmod(0o755)
    return wrapper


def nitrosystem_overrides(root, target, out=None):
    sdk, nns = make_shim(root)
    nitrosystem = REPO / "3ds_port" / "nitrosystem"
    gen = out + "/gen" if out else str(nitrosystem / "build" / target / "gen")
    write_nns_all_headers(gen, root, nns)
    ov = nitro_overrides(root, target, out, subdir="nitrosystem")
    nns_srcs = (["hgss_nns/" + n for n in nns_sources(root)]
                + ["hgss_nns_compat/" + c for c in NNS_COMPAT_SRCS])
    extra = []
    if out:
        # common.mk hardcodes this under 3ds_port/nitrosystem/build/<target>;
        # keep it inside --out so two builds never share it (both the unity
        # object and this file are regenerated on every run).
        Path(gen).mkdir(parents=True, exist_ok=True)
        extra += [f"NNS_INLINE_SYMS={gen}/nns_inline.syms",
                  f"LIBNITRO={out}/libnitro.a"]
    extra.append(f"NM={_write_nm_wrapper(out, target)}")
    return ov + ["NNS_SRCS=" + " ".join(nns_srcs)] + extra


def _hardlink(src, dst):
    dst = Path(dst)
    if dst.exists():
        try:
            if dst.samefile(src):
                return
        except OSError:
            pass
        dst.unlink()
    dst.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def ensure_nns_res(root):
    """Build the game-file fixture test_nitrosystem walks (hard links, no copies)."""
    src_files = Path(root) / "files"
    res = BUILD / "hgss-res"
    res.mkdir(parents=True, exist_ok=True)
    if not src_files.is_dir():
        sys.exit(f"{src_files} not found; run tools/bootstrap.py first")
    for p in sorted(src_files.rglob("*")):
        if not p.is_file():
            continue
        if p.suffix in NNS_RES_2D_EXTS or p.suffix == ".nsbmd":
            _hardlink(p, res / p.relative_to(src_files))
        elif p.suffix == ".NSBMD":
            _hardlink(p, res / "extra" / (p.stem + ".nsbmd"))
    seeds = sorted(src_files.rglob("*.nsbmd"))
    if not seeds:
        sys.exit(f"no .nsbmd model in {src_files}")
    pad = res / "pad"
    pad.mkdir(exist_ok=True)
    for i in range(NNS_RES_PAD_MODELS):
        _hardlink(seeds[0], pad / f"model_{i:04d}.nsbmd")
    return res


def run_make(subdir, target, variables, goals):
    cmd = ["make", "-C", str(subdir), f"TARGET={target}", *variables, *goals]
    print("+ " + " ".join(cmd), flush=True)
    return subprocess.call(cmd)


def run_nitrosystem_test(root, target, out):
    ov = nitrosystem_overrides(root, target, out)
    # The binary path doubled as a make goal must use the same OUT spelling the overrides pass
    # (relative to the subproject dir: the vendored recipes run ./$(TEST_BIN)).
    goal = (_make_out("nitrosystem", out) + "/test_nitrosystem") if out else \
        str(REPO / "3ds_port" / "nitrosystem" / "build" / target / "test_nitrosystem")
    rc = run_make(REPO / "3ds_port" / "nitrosystem", target, ov, [goal])
    if rc:
        return rc
    res = ensure_nns_res(root)
    test_bin = f"{out}/test_nitrosystem" if out else goal
    print(f"+ {test_bin} {res}", flush=True)
    return subprocess.call([test_bin, str(res)])


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["shim", "make", "list"])
    ap.add_argument("--root", default=str(BUILD / "upstream"))
    ap.add_argument("--target", default="host32")
    ap.add_argument("--out", help="private output dir (absolute) so parallel builds do not share build/<target>")
    ap.add_argument("--subproject", choices=[*SUBPROJECTS, "all"], default=None,
                    help="which vendored library to build (default: nitro, or all three for goal 'test')")
    a, extra = ap.parse_known_args(argv)
    a.args = [x for x in extra if x != "--"]
    root = Path(a.root).resolve()
    if a.cmd == "list":
        keep, drop = sdk_sources(root)
        print(f"{len(keep)} compiled, {len(drop)} excluded\nexcluded: " + " ".join(drop))
        print(f"arm7 sound: {len(ARM7_SOUND_FILES) + len(ARM7_COMPAT_FILES)} files\n"
              + " ".join("hgss_sub/" + f for f in ARM7_SOUND_FILES)
              + " " + " ".join("hgss_compat/" + f for f in ARM7_COMPAT_FILES))
        print(f"nitrosystem: {len(nns_sources(root))} files (hgss_nns/)")
        print(f"compat: {' '.join('hgss_compat/' + c for c in COMPAT_SRCS)}")
        return 0
    if a.cmd == "shim":
        print("\n".join(nitro_overrides(root, a.target, a.out)))
        return 0
    sub = a.subproject
    if sub is None:
        sub = "all" if a.args == ["test"] else "nitro"
    subs = list(SUBPROJECTS) if sub == "all" else [sub]
    out = a.out
    rc = 0
    for s in subs:
        if s == "pak":
            pak_out = f"{out}/pak" if out else None
            variables = [f"OUT={_make_out('pak', pak_out)}"] if pak_out else []
            rc = run_make(REPO / "3ds_port" / "pak", a.target, variables, a.args or ["test"])
        elif s == "nitro":
            rc = run_make(REPO / "3ds_port" / "nitro", a.target,
                          nitro_overrides(root, a.target, out), a.args or ["test"])
        elif s == "nitrosystem":
            if (a.args or ["test"]) == ["test"]:
                rc = run_nitrosystem_test(root, a.target, out)
            else:
                rc = run_make(REPO / "3ds_port" / "nitrosystem", a.target,
                              nitrosystem_overrides(root, a.target, out), a.args)
        if rc:
            return rc
    return rc


if __name__ == "__main__":
    sys.exit(main())
