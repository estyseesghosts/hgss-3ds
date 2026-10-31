#!/usr/bin/env python3
"""Compare a built NDS ROM against a reference dump, region by region.

Usage: rom_compare.py REFERENCE.nds BUILT.nds [--elf main.elf]

Prints, for header / ARM9 / ARM7 / FNT / FAT / overlay tables / banner, the
number of differing bytes. The ARM9 binary is BLZ-decompressed first (the
decomp compresses it with compstatic), then differences are clustered and,
with --elf, mapped to symbols via `arm-none-eabi-nm`. Written to localize
matching-build failures; it was how the dead-stripped sections were found
(docs/progress/phase-0.md). Reads game data: run it locally, commit nothing it
prints beyond symbol names and counts.
"""
import bisect
import struct
import subprocess
import sys


def blz_decode(d):
    """Nintendo backward LZ ("BLZ", as used by compstatic) decompression."""
    if struct.unpack_from("<I", d, len(d) - 12)[0] == 0x2106C0DE:  # nitro code footer
        d = d[:-12]
    inc = struct.unpack_from("<I", d, len(d) - 4)[0]
    hdr = d[-5]
    enc = int.from_bytes(d[-8:-5], "little")
    n = len(d)
    raw = bytearray(n + inc)
    start = n - enc
    raw[:start] = d[:start]
    pc, rc, mask, flags = n - hdr, n + inc, 0, 0
    while rc > start:
        if mask == 0:
            pc -= 1
            flags, mask = d[pc], 0x80
        if flags & mask:
            pc -= 2
            pos = (d[pc + 1] << 8) | d[pc]
            ln, pos = (pos >> 12) + 3, (pos & 0xFFF) + 3
            for _ in range(ln):
                rc -= 1
                raw[rc] = raw[rc + pos]
        else:
            pc -= 1
            rc -= 1
            raw[rc] = d[pc]
        mask >>= 1
    return bytes(raw)


def regions(d):
    u = lambda o: struct.unpack_from("<I", d, o)[0]
    return {"header": (0, 0x4000), "arm9": (u(0x20), u(0x2C)), "arm7": (u(0x30), u(0x3C)),
            "fnt": (u(0x40), u(0x44)), "fat": (u(0x48), u(0x4C)), "y9": (u(0x50), u(0x54)),
            "banner": (u(0x68), 0x840)}


def ndiff(a, b):
    n = min(len(a), len(b))
    d = [i for i in range(n) if a[i] != b[i]]
    return len(d), (d[0] if d else None), len(a) != len(b)


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    a, b = open(argv[1], "rb").read(), open(argv[2], "rb").read()
    ra, rb = regions(a), regions(b)
    for k in ra:
        (oa, sa), (ob, sb) = ra[k], rb[k]
        n, first, sized = ndiff(a[oa:oa + sa], b[ob:ob + sb])
        print(f"{k}: {n} differing bytes" + (f", first at +{first:#x}" if first is not None else "") +
              (", SIZE DIFFERS" if sized else ""))
    A = blz_decode(a[ra["arm9"][0]:][:ra["arm9"][1]])
    B = blz_decode(b[rb["arm9"][0]:][:rb["arm9"][1]])
    print(f"arm9 decompressed: {len(A):#x} vs {len(B):#x}")
    n = min(len(A), len(B))
    runs, s, last = [], None, 0
    for i in range(n):
        if A[i] != B[i]:
            if s is None or i - last > 16:
                if s is not None:
                    runs.append((s, last))
                s = i
            last = i
    if s is not None:
        runs.append((s, last))
    print(f"arm9 decompressed: {sum(e - s + 1 for s, e in runs)} differing bytes in {len(runs)} clusters")
    if "--elf" in argv and runs:
        out = subprocess.run(["arm-none-eabi-nm", "-n", argv[argv.index("--elf") + 1]],
                             capture_output=True, text=True).stdout
        syms = sorted((int(l.split()[0], 16), l.split()[2]) for l in out.splitlines() if len(l.split()) == 3)
        keys = [x[0] for x in syms]
        for s, e in runs[:20]:
            addr = 0x2000000 + s
            i = bisect.bisect_right(keys, addr) - 1
            print(f"  {addr:#x} {syms[i][1]}+{addr - syms[i][0]:#x} ({e - s + 1} bytes)")
    return 1 if runs or any(ndiff(a[ra[k][0]:][:ra[k][1]], b[rb[k][0]:][:rb[k][1]])[0] for k in ra) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
