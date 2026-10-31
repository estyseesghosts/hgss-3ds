#!/usr/bin/env python3
"""Syntax-check every HGSS NitroSDK / NitroSystem source with GCC under the port's flags.

Usage: sdk_compile_survey.py [--root build/upstream] [--lib NitroSDK|NitroSystem|all]
                             [--m32] [-j N] [--json OUT] [-v]

Runs `gcc -fsyntax-only` on each lib/<lib>/src/**/*.c with the Platinum port's
flags (3ds_port/common.mk PORT_CFLAGS, minus optimisation) and HGSS's include
paths (the decomp's own -i/-I list from common.mk MWCFLAGS), -DSDK_PORT_3DS.
This is the Phase 1 progress measure: the count of files that do not parse is
what the SDK patches must bring to zero. It does not link or run anything.
Needs lib/include/nitro/fx/fx_const.h, which the decomp generates with its own host tool:
  make -C build/upstream lib/include/nitro/fx/fx_const.h
Use the container's default gcc (Ubuntu 24.04 gcc 13), as Platinum does.
"""
import argparse
import json
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

DEFINES = ["-DSDK_ARM9", "-DSDK_CODE_ARM", "-DSDK_FINALROM", "-DNNS_FINALROM", "-DSDK_PORT_3DS"]
CFLAGS = ["-std=gnu99", "-fgnu89-inline", "-fno-strict-aliasing", "-fno-short-enums", "-fsigned-char",
          "-fwrapv", "-fno-aggressive-loop-optimizations", "-fno-delete-null-pointer-checks",
          "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-missing-braces",
          "-Wno-unknown-pragmas", "-Wno-unused-but-set-variable", "-Wno-parentheses", "-Wno-switch",
          "-Wno-multichar"]


def includes(root):
    # Same search order as the decomp's MWCFLAGS (common.mk): src, include, include/library,
    # lib/include, then the NitroSDK include dirs.
    return [f"-I{root}/{d}" for d in ("src", "include", "include/library", "lib/include",
            "lib/NitroSDK/include", "lib/NitroSDK/include-mw", "lib/NitroSDK/include-pokediamond")]


def check(args):
    gcc, root, path, m32 = args
    cmd = [gcc, "-fsyntax-only", *(["-m32", "-malign-double"] if m32 else []), *CFLAGS, *DEFINES,
           *includes(root), str(path)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    errs = [l for l in r.stderr.splitlines() if re.search(r": (fatal )?error:", l)]
    return str(path.relative_to(root)), r.returncode, errs, len(re.findall(r": warning:", r.stderr))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="build/upstream")
    ap.add_argument("--lib", default="all")
    ap.add_argument("--gcc", default="gcc")
    ap.add_argument("--m32", action="store_true")
    ap.add_argument("-j", type=int, default=8)
    ap.add_argument("--json")
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    root = Path(a.root).resolve()
    libs = ["NitroSDK", "NitroSystem"] if a.lib == "all" else [a.lib]
    files = sorted(p for l in libs for p in (root / "lib" / l / "src").rglob("*.c"))
    with ThreadPoolExecutor(a.j) as ex:
        res = list(ex.map(check, [(a.gcc, root, p, a.m32) for p in files]))
    bad = [r for r in res if r[1] != 0]
    for f, rc, errs, w in bad:
        print(f"FAIL {f}: {len(errs)} error(s); first: {errs[0][errs[0].find(': ') + 2:][:150] if errs else '?'}")
        if a.v:
            for e in errs[1:6]:
                print("     ", e[-150:])
    print(f"{len(res) - len(bad)}/{len(res)} files parse; {len(bad)} fail; "
          f"{sum(r[3] for r in res)} warnings")
    if a.json:
        Path(a.json).write_text(json.dumps({f: {"rc": rc, "errors": e} for f, rc, e, w in res}, indent=1))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
