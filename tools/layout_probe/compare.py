#!/usr/bin/env python3
"""Extract `probe[]` from an ELF object and diff two extractions.

  compare.py extract OBJ.o NAMES      -> prints "label value" lines
  compare.py diff A.txt B.txt         -> exit 1 on any difference

The MWCC result is committed as numbers only (mwcc.txt: one "label value" line
per element); the labels are struct and member names from the decomp's own
headers, not game data.
"""
import sys


def extract(obj, names):
    from elftools.elf.elffile import ELFFile

    f = ELFFile(open(obj, "rb"))
    sym = next(s for s in f.get_section_by_name(".symtab").iter_symbols() if s.name == "probe")
    sec = f.get_section(sym["st_shndx"])
    off = sym["st_value"]
    size = sym["st_size"]
    data = sec.data()[off:off + size]
    endian = "little" if f.little_endian else "big"
    vals = [int.from_bytes(data[i:i + 4], endian) for i in range(0, len(data), 4)]
    labels = [l for l in open(names).read().split("\n") if l]
    if len(vals) != len(labels):
        sys.exit(f"{len(vals)} values but {len(labels)} labels")
    return list(zip(labels, vals))


def parse(path):
    return [(l.rsplit(" ", 1)[0], int(l.rsplit(" ", 1)[1])) for l in open(path).read().split("\n") if l]


def main(a):
    if len(a) == 3 and a[0] == "extract":
        for l, v in extract(a[1], a[2]):
            print(l, v)
        return 0
    if len(a) == 3 and a[0] == "diff":
        x, y = parse(a[1]), parse(a[2])
        bad = 0
        if [l for l, _ in x] != [l for l, _ in y]:
            print("label lists differ")
            return 1
        for (l, v), (_, w) in zip(x, y):
            if v != w:
                bad += 1
                print(f"DIFF {l}: {a[1]}={v} {a[2]}={w}")
        print(f"{len(x)} values compared, {bad} difference(s)")
        return 1 if bad else 0
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
