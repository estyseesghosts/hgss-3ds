#!/usr/bin/env python3
"""Build the vendored 3ds_port/nitro against HGSS's SDK without editing the vendored Makefiles.

Usage (inside the dev container, after tools/bootstrap.py):
  hgss_nitro.py shim                 create build/hgss-sdk (symlinks, generator stub) and print the make overrides
  hgss_nitro.py make [TARGET=host32] [-- make args]   run `make -C 3ds_port/nitro` with the overrides
  hgss_nitro.py list                 print the HGSS SDK source list that is compiled, and the excluded files

How it works (docs/port-wiring.md): the vendored Makefile compiles `$(SDK)/libraries/<x>.c` for each entry of
SDK_SRCS. `build/hgss-sdk/libraries/hgss` is a symlink to build/upstream/lib/NitroSDK/src, so the list can name
HGSS's real files (`hgss/os/os_thread.c`). Headers: a shim include dir with only nitro*/nnsys*/cw* from
lib/include (its stdarg.h/stddef.h/string.h would shadow the system ones), HGSS's flat SDK include dirs, and
hgss/compat/include (new, tracked: headers the port includes that HGSS names differently or lacks).
Command-line variables override the Makefile's own assignments, so nothing vendored changes.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUILD = REPO / "build"

# HGSS SDK sources the port replaces with its own code (3ds_port/nitro/src), or that make no sense on the
# port. Matches docs/progress/phase-1.md ("port-replaced") plus the libraries the vendored Makefile documents
# as not built.
EXCLUDE_FILES = {
    "crt0.c", "dgt_hash.c", "dgt_hash2_arm4_small.c",
    "mi/mi_memory.c", "mi/mi_swap.c", "mi/mi_uncompress.c",
    "os/os_cache.c", "os/os_tcm.c", "os/os_alarm.c",
    "wvr.c",
}
EXCLUDE_PREFIXES = ("os/os_exception", "os/os_protection", "os/os_reset_", "pxi/", "ctrdg/", "mb/")


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


def make_shim(root):
    sdk = BUILD / "hgss-sdk"
    inc = sdk / "include"
    (sdk / "libraries").mkdir(parents=True, exist_ok=True)
    inc.mkdir(exist_ok=True)
    links = {sdk / "libraries" / "hgss": src_dir(root)}
    for n in ("nitro", "nitro.h", "nnsys", "nnsys.h", "cw", "cw.h", "MSL.h", "MSL_C", "ansi_params.h", "size_t.h"):
        links[inc / n] = root / "lib" / "include" / n
    for link, target in links.items():
        if link.is_symlink() and Path(os.readlink(link)) != target:
            link.unlink()
        if not link.exists() and not link.is_symlink():
            link.symlink_to(target)
    gen = sdk / "gen" / "nitro" / "fx"
    gen.mkdir(parents=True, exist_ok=True)
    # The vendored rule runs `python3 gen_fx_const.py CSV OUT`; HGSS's generator is a C tool and bootstrap.py has
    # already produced lib/include/nitro/fx/fx_const.h, so the stand-in copies it.
    (gen / "fx_const.csv").write_text("# stand-in: see tools/hgss_nitro.py\n")
    (gen / "gen_fx_const.py").write_text(
        "import shutil, sys\n"
        f"shutil.copyfile({str(root / 'lib/include/nitro/fx/fx_const.h')!r}, sys.argv[2])\n")
    return sdk


def overrides(root, target, out=None):
    sdk = make_shim(root)
    keep, _ = sdk_sources(root)
    nitro = REPO / "3ds_port" / "nitro"
    gen = f"{out}/gen" if out else f"{nitro}/build/{target}/gen"
    incs = [f"-I{nitro}/include", f"-I{gen}", f"-I{REPO}/hgss/compat/include",
            f"-I{sdk}/include",
            # the decomp's own order puts the game's include/ before the SDK dirs: several SDK files
            # `#include "global.h"` and must get the game's, not include-pokediamond's
            f"-I{root}/include", f"-I{root}/include/library"] \
        + [f"-I{root}/lib/NitroSDK/{d}" for d in ("include", "include-mw", "include-pokediamond")]
    extra = [f"OUT={out}", f"GEN={out}/gen"] if out else []
    return extra + [f"SDK={sdk}", "SDK_SRCS=" + " ".join("hgss/" + k for k in keep), "ARM7_SOUND_SRCS=",
            "SDK_INCLUDES=" + " ".join(incs)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["shim", "make", "list"])
    ap.add_argument("--root", default=str(BUILD / "upstream"))
    ap.add_argument("--target", default="host32")
    ap.add_argument("--out", help="private output dir (absolute) so parallel builds do not share build/<target>")
    a, extra = ap.parse_known_args()
    a.args = [x for x in extra if x != "--"]
    root = Path(a.root).resolve()
    if a.cmd == "list":
        keep, drop = sdk_sources(root)
        print(f"{len(keep)} compiled, {len(drop)} excluded\nexcluded: " + " ".join(drop))
        return 0
    ov = overrides(root, a.target, a.out)
    if a.cmd == "shim":
        print("\n".join(ov))
        return 0
    cmd = ["make", "-C", str(REPO / "3ds_port/nitro"), f"TARGET={a.target}", *ov, *a.args]
    return subprocess.call(cmd)


if __name__ == "__main__":
    sys.exit(main())
