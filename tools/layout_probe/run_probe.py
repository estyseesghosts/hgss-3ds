#!/usr/bin/env python3
"""Struct-layout probe for the save-block types: MWCC (reference) vs GCC variants.

Usage (inside the dev container; needs pyelftools, e.g. in a venv):
  run_probe.py --root build/upstream [--decomp /work/pokeheartgold]
               [--update-mwcc]          write the MWCC numbers to tools/layout_probe/mwcc.txt
               [--variants g32,g32d,arm] which GCC builds to compare (default all three)

What it does (see gen_probe.py and roots.py for the pieces):
  1. finds the 45 save-chunk root types from src/save_arrays.c;
  2. compiles a translation unit that includes the game headers with GCC -g, walks the DWARF from each root
     through every member (structs, unions, arrays of them) and emits `const unsigned int probe[] =
     { sizeof(T), offsetof(T, m), ... }`; root types defined only inside a .c file get their own TU that
     includes that file;
  3. compiles every probe with GCC (g32: -m32; g32d: -m32 -malign-double; arm: devkitARM with the 3DS flags)
     and, with --update-mwcc, with MWCC under Wine in --decomp (the reference);
  4. diffs each GCC variant against tools/layout_probe/mwcc.txt (numbers only: labels are struct and member
     names from the decomp's own headers). Exit 1 on any difference or if a root type could not be probed.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
sys.dont_write_bytecode = True
import gen_probe  # noqa: E402
import roots as roots_mod  # noqa: E402
import compare  # noqa: E402
import sdk_compile_survey as survey  # noqa: E402

GAME_DEFS = ["-DHEARTGOLD", "-DENGLISH", "-DGAME_REMASTER=0", "-DPM_KEEP_ASSERTS"]
ARM = ["-march=armv6k", "-mtune=mpcore", "-mfpu=vfp", "-mfloat-abi=hard", "-Wno-error=implicit-int"]
MWCC_FLAGS = ("-O4,p -sym on -enum int -lang c99 -Cpp_exceptions off -gccext,on -proc arm946e -msgstyle gcc -gccinc "
              "-i ./src -i ./include -i ./include/library -i ./files -I./lib/include -i ./lib/NitroSDK/include "
              "-i ./lib/NitroSDK/include-mw -i ./lib/NitroSDK/include-pokediamond -ipa file -interworking "
              "-inline on,noauto -char signed -W all").split()


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="build/upstream")
    ap.add_argument("--decomp", default="/work/pokeheartgold")
    ap.add_argument("--files", help="dir with the generated .naix headers (default: DECOMP/files)")
    ap.add_argument("--work", default="/tmp/layout_probe")
    ap.add_argument("--update-mwcc", action="store_true")
    ap.add_argument("--variants", default="g32,g32d,arm")
    a = ap.parse_args()
    root = Path(a.root).resolve()
    work = Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    files = Path(a.files or Path(a.decomp) / "files")
    if not (files / "msgdata" / "msg.naix").exists():
        sys.exit(f"{files}/msgdata/msg.naix missing: the game headers need the decomp's generated .naix files "
                 "(run the decomp's make once, or pass --files)")
    flags = survey.DEFINES + [f for f in survey.CFLAGS if f != "-fgnu89-inline"] + survey.includes(str(root)) \
        + GAME_DEFS + [f"-I{root}/include/library", f"-I{files}"]

    rows = roots_mod.find_roots(root)
    # TU 0: all root types visible from headers.
    gen_probe.cmd_types(str(root), str(work / "t0.c"))
    tus = [("t0", None)]
    r = run(["gcc", "-m32", *flags, "-g", "-fno-eliminate-unused-debug-types", "-c", str(work / "t0.c"), "-o", str(work / "t0.o")])
    if r.returncode:
        sys.exit("types TU does not compile:\n" + "\n".join(l for l in r.stderr.splitlines() if " error" in l)[:2000])
    gen_probe.cmd_probe(str(work / "t0.o"), str(work / "p0.c"), str(work / "p0.names"))
    skipped = [l for l in (work / "p0.skipped").read_text().splitlines() if l]
    probes = [("p0", work / "p0.c", work / "p0.names")]

    # One extra TU per root type defined only inside its own .c file.
    by_type = {t: (fn, src) for fn, t, src in rows}
    for i, typ in enumerate(skipped, 1):
        _, src = by_type[typ]
        inc = Path(src).relative_to("src").as_posix()
        tu = work / f"t{i}.c"
        tu.write_text(f'#include "{inc}"\nextern {typ} *probe_root_0;\n')
        tu.with_suffix(".roots").write_text(typ + "\n")
        r = run(["gcc", "-m32", *flags, "-g", "-fno-eliminate-unused-debug-types", "-c", str(tu), "-o", str(tu.with_suffix(".o"))])
        if r.returncode:
            sys.exit(f"{typ}: source TU does not compile:\n" + "\n".join(l for l in r.stderr.splitlines() if " error" in l)[:1500])
        gen_probe.cmd_probe(str(tu.with_suffix(".o")), str(work / f"p{i}.c"), str(work / f"p{i}.names"))
        if (work / f"p{i}.skipped").read_text().strip():
            sys.exit(f"{typ}: still not visible after including {inc}")
        probes.append((f"p{i}", work / f"p{i}.c", work / f"p{i}.names"))

    def build(name, cmd_prefix):
        out = []
        for tag, pc, pn in probes:
            o = work / f"{name}_{tag}.o"
            r = run([*cmd_prefix, str(pc), "-o", str(o)] if name != "mwcc" else cmd_prefix + [str(pc), "-o", str(o)],
                    cwd=a.decomp if name == "mwcc" else None)
            if r.returncode or not o.exists():
                sys.exit(f"{name}: {tag} did not compile:\n" + "\n".join(l for l in (r.stdout + r.stderr).splitlines() if "error" in l.lower())[:1500])
            out += compare.extract(str(o), str(pn))
        return out

    results = {}
    if a.update_mwcc:
        mw = ["wine", f"{a.decomp}/tools/mwccarm/2.0/sp2p2/mwccarm.exe", *GAME_DEFS, "-DSDK_ARM9", "-DSDK_CODE_ARM",
              "-DSDK_FINALROM", *MWCC_FLAGS, "-c"]
        res = build("mwcc", mw)
        (HERE / "mwcc.txt").write_text("".join(f"{l} {v}\n" for l, v in res))
        print(f"wrote mwcc.txt: {len(res)} values")
    ref = compare.parse(HERE / "mwcc.txt")
    cc = {"g32": ["gcc", "-m32", *flags, "-c"], "g32d": ["gcc", "-m32", "-malign-double", *flags, "-c"],
          "arm": ["arm-none-eabi-gcc", *ARM, *flags, "-c"]}
    bad = 0
    for v in a.variants.split(","):
        res = build(v, cc[v])
        if [l for l, _ in res] != [l for l, _ in ref]:
            print(f"{v}: label list differs from mwcc.txt ({len(res)} vs {len(ref)})")
            bad += 1
            continue
        diffs = [(l, x, y) for (l, x), (_, y) in zip(ref, res) if x != y]
        print(f"{v}: {len(ref)} values, {len(diffs)} difference(s) vs MWCC")
        for l, x, y in diffs[:25]:
            print(f"   {l}: MWCC {x} GCC {y}")
        bad += bool(diffs)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
