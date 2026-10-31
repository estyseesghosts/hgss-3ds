#!/usr/bin/env python3
"""Syntax-check the vendored 3ds_port/nitro/src sources against the patched HGSS SDK headers.

Usage: port_compile_survey.py [--root build/upstream] [--shim build/hgss-shim] [--no-pak]

Measures how far the vendored port layer (written for pokeplatinum's ntrtwl SDK) is from compiling against
HGSS's SDK, without editing it. Headers: a shim include dir that exposes ONLY nitro*, nnsys*, cw* from the
HGSS lib/include (its stdarg.h/stddef.h/string.h would shadow the system ones the port code needs), then
the HGSS flat SDK include dirs, then the port's own include dirs. Prints the count and the distinct first
errors, most common first. Run after `python3 tools/bootstrap.py`, inside the dev container.
"""
import argparse
import collections
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import sdk_compile_survey as s  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="build/upstream")
    ap.add_argument("--shim", default="build/hgss-shim")
    a = ap.parse_args()
    root = Path(a.root).resolve()
    shim = Path(a.shim).resolve() / "include"
    shim.mkdir(parents=True, exist_ok=True)
    for n in ("nitro", "nitro.h", "nnsys", "nnsys.h", "cw", "cw.h"):
        link = shim / n
        if not link.is_symlink():
            link.symlink_to(root / "lib" / "include" / n)
    flags = (s.DEFINES + [f for f in s.CFLAGS if f != "-fgnu89-inline"] + [f"-I{shim}"]
             + [f"-I{root}/lib/NitroSDK/{d}" for d in ("include", "include-mw", "include-pokediamond")]
             + ["-I3ds_port/nitro/include", "-I3ds_port/pak/include", "-I3ds_port/nitro/src"])
    groups, example, results = collections.Counter(), {}, []
    for p in sorted(Path("3ds_port/nitro/src").glob("*.c")):
        r = subprocess.run(["gcc", "-fsyntax-only", "-m32", "-malign-double", *flags, str(p)],
                           capture_output=True, text=True)
        errs = [l for l in r.stderr.splitlines() if re.search(r": (fatal )?error:", l)]
        results.append((p.name, r.returncode))
        if r.returncode:
            m = re.search(r"error: (.*)", errs[0]) if errs else None
            key = m.group(1)[:80] if m else "?"
            groups[key] += 1
            example.setdefault(key, p.name)
    ok = sum(1 for _, rc in results if rc == 0)
    print(f"{ok}/{len(results)} port source files compile against the HGSS headers")
    for key, n in groups.most_common():
        print(f"{n:3d} {key}  (e.g. {example[key]})")
    return 0 if ok == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
