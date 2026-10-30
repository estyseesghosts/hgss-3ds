#!/usr/bin/env python3
"""Exact comparison of two capture directories (determinism check).

Usage: compare_runs.py DIR_A DIR_B
Compares decoded pixels (PNG metadata/compression differences are ignored).
Exit 0 identical, 1 different, 2 usage/missing.
"""
import sys
from pathlib import Path

from PIL import Image


def pixels(path):
    with Image.open(path) as im:
        return im.convert("RGBA").size, im.convert("RGBA").tobytes()


def compare(a, b):
    a, b = Path(a), Path(b)
    na = {p.name for p in a.glob("*.png")}
    nb = {p.name for p in b.glob("*.png")}
    problems = [f"only in A: {n}" for n in sorted(na - nb)] + [f"only in B: {n}" for n in sorted(nb - na)]
    for n in sorted(na & nb):
        if pixels(a / n) != pixels(b / n):
            problems.append(f"differs: {n}")
    return problems, len(na & nb)


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    if not (Path(argv[1]).is_dir() and Path(argv[2]).is_dir()):
        print("both arguments must be directories", file=sys.stderr)
        return 2
    problems, n = compare(argv[1], argv[2])
    for p in problems:
        print(p)
    print(f"{n} frame(s) compared, {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
