#!/usr/bin/env python3
"""Inventory and diff the HGSS in-tree SDK against the Platinum SDK sources.

Usage: sdk_inventory.py HGSS_DECOMP_DIR PLATINUM_NITROSDK_DIR

Files are matched by normalized basename (lower case, only [a-z0-9]), because
the two trees name and place files differently (HGSS: lib/NitroSDK/src/os/
os_alarm.c; ntrtwl: libraries/os/common/src/os_alarm.c). For each matched pair
it prints the number of differing lines (unified diff, whitespace-insensitive)
so the report can point at files worth reading. It does NOT decide whether a
difference matters: that is a manual review recorded in docs/sdk-diff.md.
"""
import difflib
import re
import sys
from pathlib import Path


def norm(p):
    return re.sub(r"[^a-z0-9]", "", p.stem.lower()) + p.suffix.lower()


def collect(root, exts=(".c", ".h", ".s", ".cpp")):
    out = {}
    for p in Path(root).rglob("*"):
        if p.is_file() and p.suffix.lower() in exts:
            out.setdefault(norm(p), []).append(p)
    return out


def strip(path):
    text = Path(path).read_text(errors="replace")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return [re.sub(r"\s+", " ", l).strip() for l in text.splitlines() if l.strip()]


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    hg = collect(Path(argv[1]) / "lib" / "NitroSDK")
    hg.update({k: v for k, v in collect(Path(argv[1]) / "lib" / "include" / "nitro").items() if k not in hg})
    pl = collect(argv[2])
    only_hg = sorted(k for k in hg if k not in pl)
    only_pl = sorted(k for k in pl if k not in hg)
    print(f"HGSS files: {sum(map(len, hg.values()))}  Platinum: {sum(map(len, pl.values()))}")
    print(f"matched names: {len(set(hg) & set(pl))}; only HGSS: {len(only_hg)}; only Platinum: {len(only_pl)}")
    print("\n== only in HGSS ==")
    for k in only_hg:
        print(" ", *[str(p.relative_to(argv[1])) for p in hg[k]])
    print("\n== matched: differing-line counts (0 = identical modulo whitespace/comments) ==")
    rows = []
    for k in sorted(set(hg) & set(pl)):
        a, b = hg[k][0], pl[k][0]
        sa, sb = strip(a), strip(b)
        d = sum(1 for l in difflib.unified_diff(sa, sb, lineterm="", n=0) if l[:1] in "+-" and l[:3] not in ("+++", "---"))
        rows.append((d, str(a.relative_to(argv[1])), str(b.relative_to(argv[2]))))
    for d, a, b in sorted(rows):
        print(f"{d:5d}  {a}  <->  {b}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
