#!/usr/bin/env python3
"""Normalize, validate and diff HeartGold/SoulSilver .sav images.

Format reference (pokeheartgold at upstream.lock):
  * The save is a 4 Mbit flash image: 524,288 bytes = two mirrors of 0x40000
    (src/save.c: GetChunkOffsetFromCurrentSaveSlot, FlashLoadChunk(n * 0x40000)).
  * Each mirror holds two slots ("main" = slot 0 and "storage" = slot 1).
    Slot 0 starts at offset 0 of the mirror; slot 1 starts at the next 0x100
    boundary after slot 0 (SaveData_InitSlotSpecs).
  * Each slot ends with a 16-byte footer, struct SaveChunkFooter
    (include/save.h): u32 count, u32 size, u32 magic (0x20060623), u16 slot,
    u16 crc.  size is the slot size including the footer.
  * crc = GF_CalcCRC16 over the slot minus its last 16 bytes
    (SaveArray_CalcCRC16MinusFooter).  GF_CalcCRC16 is MATH_CalcCRC16CCITT:
    poly 0x1021, init 0xFFFF, MSB first, no final xor
    (lib/NitroSDK/src/math/MATH_crc.c).
  * Slot sizes depend on the compiled struct sizes, so they are discovered by
    looking for consistent footers rather than hard-coded; --layout overrides.

Usage:
  savetool.py normalize IN [-o OUT]
  savetool.py validate  FILE [--layout SIZE0,SIZE1]
  savetool.py diff      A B [--block 4096]
"""
import argparse
import struct
import sys

SAVE_SIZE = 524288
MIRROR_SIZE = 0x40000
FOOTER_SIZE = 16
SAVE_CHUNK_MAGIC = 0x20060623
FOOTER_FMT = "<IIIHH"

# DeSmuME appends a text footer to raw saves; the raw part is cut at SNIP.
DESMUME_SNIP = b"|<--Snip above here to create a raw sav by excluding this DeSmuME savedata footer:"
DESMUME_END = b"|-DESMUME SAVE-|"

# LOAD_STATUS_* from include/save.h
STATUS_NAMES = {0: "NOT_EXIST", 1: "IS_GOOD", 2: "SLOT_FAIL", 3: "TOTAL_FAIL"}


class SaveError(Exception):
    pass


def crc16_ccitt(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def normalize(data):
    """Return a raw 524,288-byte image, or raise SaveError."""
    if data.endswith(DESMUME_END):
        idx = data.rfind(DESMUME_SNIP)
        if idx < 0:
            raise SaveError("DeSmuME end marker found but no snip marker")
        data = data[:idx]
    if len(data) != SAVE_SIZE:
        raise SaveError(f"size {len(data)} is not {SAVE_SIZE}")
    return data


def _footer_at(img, pos):
    count, size, magic, slot, crc = struct.unpack_from(FOOTER_FMT, img, pos)
    return count, size, magic, slot, crc


def discover_layout(img):
    """Find (size0, size1) from footers that agree with the slot geometry.

    Slot 0 footer sits at size0 - 16 with footer.size == size0.  Slot 1 starts
    at roundup(size0, 0x100) and its footer sits at offset1 + size1 - 16.
    """
    for mirror in (0, MIRROR_SIZE):
        for pos in range(mirror, mirror + MIRROR_SIZE - FOOTER_SIZE + 1, 4):
            count, size, magic, slot, _ = _footer_at(img, pos)
            if magic != SAVE_CHUNK_MAGIC or slot != 0:
                continue
            if size < FOOTER_SIZE or pos - mirror != size - FOOTER_SIZE:
                continue
            off1 = (size + 0xFF) & ~0xFF
            for pos1 in range(mirror + off1, mirror + MIRROR_SIZE - FOOTER_SIZE + 1, 4):
                _, size1, magic1, slot1, _ = _footer_at(img, pos1)
                if magic1 == SAVE_CHUNK_MAGIC and slot1 == 1 and pos1 - mirror == off1 + size1 - FOOTER_SIZE:
                    return size, size1
    raise SaveError("could not find a consistent pair of slot footers; pass --layout")


def slot_spec(layout):
    size0, size1 = layout
    return [(0, size0), ((size0 + 0xFF) & ~0xFF, size1)]


def check_slot(img, mirror, idx, offset, size):
    """Port of ValidateSaveSectorFooter. Returns (valid, count, why)."""
    if offset + size > MIRROR_SIZE:
        return False, 0, "slot exceeds mirror"
    base = mirror + offset
    count, fsize, magic, fslot, fcrc = _footer_at(img, base + size - FOOTER_SIZE)
    if fsize != size:
        return False, 0, f"footer size {fsize:#x} != {size:#x}"
    if magic != SAVE_CHUNK_MAGIC:
        return False, 0, f"bad magic {magic:#x}"
    if fslot != idx:
        return False, 0, f"footer slot {fslot} != {idx}"
    calc = crc16_ccitt(img[base:base + size - FOOTER_SIZE])
    if calc != fcrc:
        return False, 0, f"crc {calc:#06x} != footer {fcrc:#06x}"
    return True, count, "ok"


def counter_compare(a, b):
    """SaveCounterCompare: counters are u32 and may wrap (-1 vs 0)."""
    if a == 0xFFFFFFFF and b == 0:
        return -1
    if a == 0 and b == 0xFFFFFFFF:
        return 1
    if a > b:
        return 1
    return -1 if a < b else 0


def compare_slots(first, second):
    """Port of SaveSlotCheckCompare: (num_good, newer, older); 2 means none."""
    cmp = counter_compare(first[1], second[1])
    if first[0] and second[0]:
        return (2, 1, 0) if cmp < 0 else (2, 0, 1)
    if first[0]:
        return 1, 0, 2
    if second[0]:
        return 1, 1, 2
    return 0, 2, 2


def load_status(main, sub):
    """Port of Save_GetSaveFilesStatus. main/sub are [mirror0, mirror1] checks.

    Returns (status, mirror used or None).
    """
    n_main, newer_main, older_main = compare_slots(*main)
    n_sub, newer_sub, _ = compare_slots(*sub)
    if n_main == 0 and n_sub == 0:
        return 0, None
    if n_main == 0 or n_sub == 0:
        return 3, None
    cm = lambda i: main[i][1]
    cs = lambda i: sub[i][1]
    if n_main == 2 and n_sub == 2:
        if cm(newer_main) == cs(newer_main):
            return 1, newer_main
        if cm(older_main) != cs(older_main):
            return 3, None
        return 2, older_main
    if n_main == 1 and n_sub == 2:
        if cm(newer_main) == cs(newer_main):
            return 2, newer_main
        return 3, None
    if n_main == 2 and n_sub == 1:
        if cm(newer_main) == cs(newer_main):
            return 1, newer_main
        if older_main == 2:
            return 3, None
        if cm(older_main) == cs(older_main):
            return 2, older_main
        return 3, None
    if newer_main == newer_sub and cm(newer_main) == cs(newer_sub):
        return 1, newer_main
    return 3, None


def validate(img, layout=None):
    """Return a report dict; raises SaveError if the image is unusable."""
    if len(img) != SAVE_SIZE:
        raise SaveError(f"size {len(img)} is not {SAVE_SIZE}")
    if layout is None:
        layout = discover_layout(img)
    specs = slot_spec(layout)
    checks = {0: [], 1: []}
    detail = []
    for mirror_no, mirror in enumerate((0, MIRROR_SIZE)):
        for idx, (off, size) in enumerate(specs):
            valid, count, why = check_slot(img, mirror, idx, off, size)
            checks[idx].append((valid, count))
            detail.append((mirror_no, idx, off, size, valid, count, why))
    status, used = load_status(checks[0], checks[1])
    return {"layout": layout, "slots": detail, "status": status,
            "status_name": STATUS_NAMES[status], "newer_mirror": used}


def diff_blocks(a, b, block):
    """Yield (offset, length) runs of differing blocks."""
    n = max(len(a), len(b))
    run = None
    for off in range(0, n, block):
        if a[off:off + block] != b[off:off + block]:
            if run is None:
                run = [off, block]
            else:
                run[1] += block
        elif run is not None:
            yield tuple(run)
            run = None
    if run is not None:
        yield tuple(run)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    n = sub.add_parser("normalize")
    n.add_argument("input")
    n.add_argument("-o", "--output")
    v = sub.add_parser("validate")
    v.add_argument("file")
    v.add_argument("--layout", help="SIZE0,SIZE1 (hex or decimal) instead of discovery")
    d = sub.add_parser("diff")
    d.add_argument("a")
    d.add_argument("b")
    d.add_argument("--block", type=int, default=0x1000)
    args = p.parse_args(argv)

    try:
        if args.cmd == "normalize":
            raw = normalize(open(args.input, "rb").read())
            if args.output:
                open(args.output, "wb").write(raw)
            print(f"ok: {len(raw)} bytes")
            return 0
        if args.cmd == "validate":
            layout = tuple(int(x, 0) for x in args.layout.split(",")) if args.layout else None
            rep = validate(open(args.file, "rb").read(), layout)
            print(f"layout: slot0={rep['layout'][0]:#x} slot1={rep['layout'][1]:#x}")
            for mirror, idx, off, size, valid, count, why in rep["slots"]:
                print(f"mirror {mirror} slot {idx} @{off:#07x} size {size:#07x}: "
                      f"{'valid' if valid else 'INVALID'} count={count} ({why})")
            nm = rep["newer_mirror"]
            print(f"status: {rep['status_name']}; newer mirror: {'none' if nm is None else nm}")
            return 0 if rep["status"] == 1 else 1
        a = open(args.a, "rb").read()
        b = open(args.b, "rb").read()
        runs = list(diff_blocks(a, b, args.block))
        for off, length in runs:
            print(f"{off:#08x}-{off + length:#08x} ({length // args.block} block(s))")
        print(f"{len(runs)} differing run(s)" if runs else "identical")
        return 1 if runs else 0
    except SaveError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
