#!/usr/bin/env python3
"""Extract NitroFS from a verified, locally owned HeartGold/SoulSilver ROM.

Usage:
    python3 builder/extract_rom.py path/to/rom.nds -o data_pack/

The ROM is only read. The output directory gets one file per NitroFS file,
under its original path, plus manifest.json (the ROM's SHA-1, and every
file's original FAT ID, path and size). No ROM data is uploaded or changed;
the output is private and must not be committed.

HeartGold/SoulSilver's retail image keeps its 129 ARM9 overlays in the FAT
but not in the FNT (they have file IDs 0..128 and no names). This extractor
keeps every FAT entry — the pack is ID-indexed and some game code opens
files by ID — and names the unnamed overlays from the header's overlay table
as overlay/overlay_NNNN.bin. Any other unnamed FAT entry is named
unnamed/file_NNNN.bin and reported.

Adapted from pokeplatinum-3ds' builder/extract_rom.py (GPL-3.0): the header,
FNT and FAT layouts are the DS's, but the path assumptions and the overlay
handling are HGSS's own.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

try:
    from . import verify_rom
except ImportError:
    import verify_rom


HEADER_SIZE = 0x200
FNT_OFFSET = 0x40
FNT_SIZE = 0x44
FAT_OFFSET = 0x48
FAT_SIZE = 0x4C
ARM9_OVERLAY_OFFSET = 0x50
ARM9_OVERLAY_SIZE = 0x54
OVERLAY_ENTRY_SIZE = 32
MAX_DIRECTORY_DEPTH = 64
MANIFEST_NAME = "manifest.json"


class NitroFSError(ValueError):
    """A NitroFS image (or one of its tables) this extractor will not use."""


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _table_bounds(data: bytes, offset_field: int, size_field: int,
                  name: str) -> tuple[int, int]:
    offset, size = u32(data, offset_field), u32(data, size_field)
    if not size:
        raise NitroFSError(f"the header has an empty {name} table")
    if offset < HEADER_SIZE:
        raise NitroFSError(
            f"the header's {name} table starts at {offset:#x}, inside the "
            "cartridge header (it must be an absolute offset past 0x200)")
    if offset + size > len(data):
        raise NitroFSError(
            f"the header's {name} table ({offset:#x}, {size} bytes) runs past "
            f"the end of the {len(data)}-byte image (a trimmed dump?)")
    return offset, size


def parse_overlay_table(data: bytes) -> list[dict]:
    """The ARM9 overlay table: each overlay's ID, file ID and RAM size.

    HeartGold/SoulSilver names none of these files in the FNT, so the table
    is what gives the FAT entries their paths. An image with no overlay
    table returns an empty list.
    """
    offset, size = u32(data, ARM9_OVERLAY_OFFSET), u32(data, ARM9_OVERLAY_SIZE)
    if size == 0:
        return []
    if offset < HEADER_SIZE or offset + size > len(data):
        raise NitroFSError(
            f"the header's overlay table ({offset:#x}, {size} bytes) lies "
            "outside the image; the header is damaged or was modified")
    if size % OVERLAY_ENTRY_SIZE:
        raise NitroFSError(
            f"the overlay table is {size} bytes, not a multiple of "
            f"{OVERLAY_ENTRY_SIZE}")

    overlays = []
    seen_ids: set[int] = set()
    seen_files: set[int] = set()
    for index in range(size // OVERLAY_ENTRY_SIZE):
        entry = offset + index * OVERLAY_ENTRY_SIZE
        (overlay_id, ram_address, ram_size, bss_size,
         sinit, sinit_end, file_id, reserved) = struct.unpack_from(
            "<IIIIIIII", data, entry)
        if overlay_id in seen_ids:
            raise NitroFSError(f"duplicate overlay ID {overlay_id}")
        if file_id in seen_files:
            raise NitroFSError(f"duplicate overlay file ID {file_id}")
        seen_ids.add(overlay_id)
        seen_files.add(file_id)
        overlays.append({
            "id": overlay_id,
            "file_id": file_id,
            "ram_address": ram_address,
            "ram_size": ram_size,
            "bss_size": bss_size,
            "sinit_start": sinit,
            "sinit_end": sinit_end,
            "reserved": reserved,
        })
    return overlays


def _safe_name(raw: bytes, where: str) -> str:
    try:
        name = raw.decode("ascii")
    except UnicodeDecodeError:
        raise NitroFSError(f"{where}: a file name is not ASCII") from None
    if not name:
        raise NitroFSError(f"{where}: an empty file name")
    if name in (".", ".."):
        raise NitroFSError(f"{where}: unsafe file name {name!r}")
    if any(ch in name for ch in "/\\:"):
        raise NitroFSError(f"{where}: unsafe file name {name!r} (a path separator)")
    if any(ord(ch) < 0x20 or ord(ch) == 0x7F for ch in name):
        raise NitroFSError(f"{where}: a file name contains a control character")
    return name


def parse_nitrofs(data: bytes) -> list[dict]:
    """Every FAT entry as {id, path, size, offset, overlay}, sorted by ID.

    All metadata is validated before anything is written: FNT/FAT/overlay
    table bounds, the FNT tree (no cycles, no unsafe names, no duplicate IDs
    or paths), and every FAT extent (inside the image, past the header).
    """
    if len(data) < HEADER_SIZE:
        raise NitroFSError("Truncated NDS header")
    fnt_offset, fnt_size = _table_bounds(data, FNT_OFFSET, FNT_SIZE, "FNT")
    fat_offset, fat_size = _table_bounds(data, FAT_OFFSET, FAT_SIZE, "FAT")
    if fat_size % 8:
        raise NitroFSError(f"the FAT is {fat_size} bytes, not a multiple of 8")
    count = fat_size // 8
    if count == 0:
        raise NitroFSError("the FAT has no entries")

    fnt = data[fnt_offset:fnt_offset + fnt_size]
    if len(fnt) < 8:
        raise NitroFSError("Truncated FNT root")
    directory_count = u16(fnt, 6)
    if not directory_count or directory_count * 8 > len(fnt):
        raise NitroFSError(
            f"the FNT declares {directory_count} directories, which does not "
            "fit its directory table; the FNT is damaged")

    files: dict[int, str] = {}
    cases: set[str] = set()
    visited: set[int] = set()

    def walk(directory: int, prefix: tuple[str, ...], depth: int) -> None:
        if depth > MAX_DIRECTORY_DEPTH:
            raise NitroFSError("the FNT tree is deeper than any real NitroFS")
        index = directory - 0xF000
        if index < 0 or index >= directory_count:
            raise NitroFSError(
                f"the FNT references directory {directory:#06x}, outside its "
                f"{directory_count}-entry directory table")
        if directory in visited:
            raise NitroFSError("Invalid or cyclic FNT directory")
        visited.add(directory)

        offset, first_id, _parent = struct.unpack_from("<IHH", fnt, index * 8)
        if offset >= len(fnt):
            raise NitroFSError("Invalid FNT subtable offset")
        where = "/".join(prefix) or "the FNT root"
        file_id = first_id
        while True:
            if offset >= len(fnt):
                raise NitroFSError(f"{where}: Unterminated FNT subtable")
            kind = fnt[offset]
            offset += 1
            if kind == 0:
                break
            length = kind & 0x7F
            if not length or offset + length > len(fnt):
                raise NitroFSError(f"{where}: Invalid FNT name")
            name = _safe_name(fnt[offset:offset + length], where)
            offset += length
            if kind & 0x80:
                if offset + 2 > len(fnt):
                    raise NitroFSError(f"{where}: Truncated FNT directory reference")
                child = u16(fnt, offset)
                offset += 2
                walk(child, prefix + (name,), depth + 1)
            else:
                if file_id >= count:
                    raise NitroFSError(
                        f"{where}: file ID {file_id} is past the FAT's "
                        f"{count} entries")
                if file_id in files:
                    raise NitroFSError(f"Invalid or duplicate file ID {file_id}")
                path = "/".join(prefix + (name,))
                if path.casefold() in cases:
                    raise NitroFSError(f"Duplicate FNT path (ignoring case): {path!r}")
                files[file_id] = path
                cases.add(path.casefold())
                file_id += 1

    walk(0xF000, (), 0)

    overlays = parse_overlay_table(data)
    overlay_ids: set[int] = set()
    for overlay in overlays:
        file_id = overlay["file_id"]
        if file_id >= count:
            raise NitroFSError(
                f"overlay {overlay['id']} uses file ID {file_id}, past the "
                f"FAT's {count} entries")
        if overlay["ram_size"] == 0:
            raise NitroFSError(f"overlay {overlay['id']} has no RAM size")
        overlay_ids.add(file_id)
        if file_id not in files:
            path = f"overlay/overlay_{overlay['id']:04d}.bin"
            folded = path.casefold()
            if folded in cases:
                raise NitroFSError(f"Duplicate overlay path: {path!r}")
            files[file_id] = path
            cases.add(folded)

    # Every remaining FAT entry has no name anywhere in the image. Keep it
    # (some code opens by ID) under a name that can never collide with a
    # real NitroFS path.
    unnamed = [file_id for file_id in range(count) if file_id not in files]
    for file_id in unnamed:
        path = f"unnamed/file_{file_id:04d}.bin"
        if path.casefold() in cases:
            raise NitroFSError(
                f"cannot name unnamed FAT entry {file_id}: {path!r} already "
                "exists in the image")
        files[file_id] = path
        cases.add(path.casefold())

    if any(path.casefold() == MANIFEST_NAME for path in files.values()):
        raise NitroFSError(
            "a ROM file is named manifest.json and would collide with the "
            "manifest this extractor writes; refusing to extract")

    entries = []
    for file_id in range(count):
        start, end = struct.unpack_from("<II", data, fat_offset + file_id * 8)
        if end < start:
            raise NitroFSError(
                f"FAT entry {file_id} ends ({end:#x}) before it starts "
                f"({start:#x})")
        if end > len(data):
            raise NitroFSError(
                f"FAT entry {file_id} ends at {end:#x}, past the end of the "
                f"{len(data)}-byte image (a trimmed dump?)")
        if end > start and start < HEADER_SIZE:
            raise NitroFSError(
                f"FAT entry {file_id} starts at {start:#x}, inside the "
                "cartridge header; the FAT is damaged")
        entries.append({
            "id": file_id,
            "path": files[file_id],
            "size": end - start,
            "offset": start,
            "overlay": file_id in overlay_ids,
        })
    return entries


def manifest_text(rom_sha1: str, entries: list[dict]) -> str:
    """manifest.json's text: the ROM's SHA-1 and each file's ID, path and size."""
    files = [{"id": e["id"], "path": e["path"], "size": e["size"]}
             for e in entries]
    return json.dumps({"rom_sha1": rom_sha1, "files": files}, indent=2) + "\n"


def summarize(entries: list[dict]) -> dict:
    """Counts and byte totals for reporting (never paths or content)."""
    return {
        "files": len(entries),
        "overlays": sum(1 for e in entries if e["overlay"]),
        "unnamed": sum(1 for e in entries if e["path"].startswith("unnamed/")),
        "bytes": sum(e["size"] for e in entries),
    }


def extract(rom: Path, output: Path, *, verify: bool = True) -> list[dict]:
    """Extract every NitroFS file and write manifest.json.

    Returns the parse_nitrofs entries (IDs, paths, sizes, offsets, overlay
    flags). `verify=True` requires the ROM to be one of verify_rom.SUPPORTED
    (the normal path; synthetic images in the tests pass verify=False).
    """
    rom = Path(rom)
    data = rom.read_bytes()
    rom_sha1 = hashlib.sha1(data).hexdigest()
    if verify:
        verify_rom.verify(rom, digest=rom_sha1)
    entries = parse_nitrofs(data)  # Validate all metadata before writing.

    output = output.resolve()
    rom_path = rom.resolve()
    if output == rom_path or output in rom_path.parents:
        raise NitroFSError("Output must not overwrite the ROM")
    if output.exists() and not output.is_dir():
        raise NitroFSError(f"Output path {output} exists and is not a directory")
    output.mkdir(parents=True, exist_ok=True)

    for entry in entries:
        target = (output / entry["path"]).resolve()
        if not target.is_relative_to(output):
            raise NitroFSError(f"Unsafe output path: {entry['path']!r}")
        target.parent.mkdir(parents=True, exist_ok=True)
        start = entry["offset"]
        target.write_bytes(data[start:start + entry["size"]])

    manifest = [{"id": e["id"], "path": e["path"], "size": e["size"]}
                for e in entries]
    (output / MANIFEST_NAME).write_text(manifest_text(rom_sha1, entries),
                                        encoding="utf-8")
    return entries


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rom", type=Path, help="Your own verified HG or SS US .nds")
    parser.add_argument("-o", "--output", type=Path, default=Path("data_pack"))
    args = parser.parse_args(argv)
    try:
        files = extract(args.rom, args.output)
    except (OSError, ValueError, UnicodeError) as exc:
        parser.exit(1, f"Extraction failed: {exc}\n")
    counts = summarize(files)
    print(f"Extracted {counts['files']} files ({counts['overlays']} overlays, "
          f"{counts['unnamed']} unnamed) totalling {counts['bytes']} bytes "
          f"to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
