#!/usr/bin/env python3
"""Check that a ROM dump is a version this project supports.

Usage:
    python3 builder/verify_rom.py path/to/rom.nds

The ROM is only read: nothing is copied, uploaded or modified. Only clean,
unmodified US dumps of HeartGold or SoulSilver are accepted; trimmed,
patched, header-edited and other-region images are rejected with the reason.

Exit status: 0 a supported dump, 1 an unsupported or damaged dump, 2 usage
(the argument is not a readable file) or a bad command line.

`SUPPORTED` and `sha1_of` are also used by builder/pack.py and
builder/extract_rom.py. tools/hgss_game.py reads SUPPORTED when it generates
the program's libpak reader (a build-private copy of the vendored
3ds_port/pak reader, which only knows Platinum's digests), so the allowlist
cannot drift between the builder and the program.
"""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

# SHA-1 hashes of clean dumps (No-Intro), as listed by the pret decompilation
# (docs/BASE.md). Add a region only together with a new pak build ID: the
# game's file table differs between revisions/regions.
SUPPORTED = {
    "4fcded0e2713dc03929845de631d0932ea2b5a37": "Pokémon HeartGold (USA)",
    "f8dc38ea20c17541a43b58c5e6d18c1732c7e582": "Pokémon SoulSilver (USA)",
}

# The cartridge game code (header +0x0C) of each supported version, named
# exactly as SUPPORTED names its dump (verify() compares the two). The hash
# is authoritative; this only turns "not a DS ROM" and "wrong game" into a
# clear message before the (slower) hash.
GAME_CODES = {
    "IPKE": "Pokémon HeartGold (USA)",
    "IPGE": "Pokémon SoulSilver (USA)",
}

# HeartGold and SoulSilver are 128 MiB cartridges. The header's device-size
# byte (+0x14) is log2(cartridge size) - 17, so 0x0A means 1 << 27 bytes.
EXPECTED_DEVICE_SIZE = 0x0A
ROM_SIZE = 1 << (17 + EXPECTED_DEVICE_SIZE)

HEADER_SIZE = 0x200
GAME_CODE_OFFSET = 0x0C
UNIT_CODE_OFFSET = 0x12
DEVICE_SIZE_OFFSET = 0x14
FNT_OFFSET = 0x40
FNT_SIZE = 0x44
FAT_OFFSET = 0x48
FAT_SIZE = 0x4C


class RomError(ValueError):
    """A dump this project cannot accept; str() is the reason, in words."""


def sha1_of(path: Path) -> str:
    """The SHA-1 of a file, as lowercase hex. Reads it in 1 MiB chunks."""
    digest = hashlib.sha1()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 4], "little")


def _size_text(size: int) -> str:
    if size % (1 << 20) == 0:
        return f"{size >> 20} MiB"
    if size % (1 << 10) == 0:
        return f"{size >> 10} KiB"
    return f"{size}-byte"


def inspect_header(header: bytes, size: int) -> str:
    """Check the header and image size of a clean HG/SS US dump.

    `header` is the image's first HEADER_SIZE bytes, `size` the real file
    size. Returns the version name from GAME_CODES. Raises RomError with an
    actionable reason: these checks run before the hash so a wrong file, a
    trimmed dump or an overdump is named as such, not "unknown SHA-1".
    """
    if len(header) < HEADER_SIZE or size < HEADER_SIZE:
        raise RomError(
            f"the file is {size} bytes, too short for a DS cartridge header "
            f"({HEADER_SIZE} bytes); it is truncated or not a ROM")

    code = header[GAME_CODE_OFFSET:GAME_CODE_OFFSET + 4]
    try:
        game_code = code.decode("ascii")
    except UnicodeDecodeError:
        game_code = ""
    if game_code not in GAME_CODES:
        raise RomError(
            f"the cartridge game code is {game_code!r}, not IPKE (HeartGold) "
            "or IPGE (SoulSilver); this is another game, another region, or "
            "the header was modified")

    unit = header[UNIT_CODE_OFFSET]
    if unit != 0:
        raise RomError(
            f"the cartridge's unit code is {unit} (a DSi-enhanced or "
            "development image); supported dumps are ordinary NDS cartridges")

    device_size = header[DEVICE_SIZE_OFFSET]
    if device_size != EXPECTED_DEVICE_SIZE:
        raise RomError(
            f"the header declares a {_size_text(1 << (17 + device_size))} "
            f"cartridge (device size {device_size:#04x}), not the "
            f"{_size_text(ROM_SIZE)} of a retail HG/SS cartridge; this is a "
            "trimmed, overdumped or modified image")

    if size != ROM_SIZE:
        kind = "shorter than" if size < ROM_SIZE else "longer than"
        hint = (" (a trimmed dump: the header is intact but the data was cut)"
                if size < ROM_SIZE else
                " (an overdump or padding after the cartridge image)")
        raise RomError(
            f"the file is {size} bytes, {kind} the {ROM_SIZE} bytes it "
            f"declares{hint}")

    # FNT/FAT are absolute file offsets in a DS image; both must lie inside
    # the image. This catches a dump whose header was spliced or edited.
    for name, offset_field, size_field in (
            ("FNT", FNT_OFFSET, FNT_SIZE), ("FAT", FAT_OFFSET, FAT_SIZE)):
        offset, table_size = _u32(header, offset_field), _u32(header, size_field)
        if not table_size or offset < HEADER_SIZE or offset + table_size > size:
            raise RomError(
                f"the header's {name} table ({offset:#x}, {table_size} bytes) "
                "lies outside the image; the header is damaged or was modified")

    return GAME_CODES[game_code]


def verify(path: Path, *, digest: str | None = None) -> str:
    """Check one dump; return the supported version's name.

    `digest` lets a caller that already hashed the file pass the SHA-1 (the
    extractor does); otherwise the file is hashed here. Raises RomError.
    """
    path = Path(path)
    try:
        size = path.stat().st_size
    except OSError as exc:
        raise RomError(f"cannot read {path}: {exc}") from exc
    if size < HEADER_SIZE:
        raise RomError(f"the file is {size} bytes, too short to be a DS ROM")

    with path.open("rb") as f:
        header = f.read(HEADER_SIZE)
    name_from_header = inspect_header(header, size)

    if digest is None:
        digest = sha1_of(path)
    digest = digest.lower()
    name = SUPPORTED.get(digest)
    if name is None:
        raise RomError(
            f"SHA-1 {digest} is not a clean HG/SS US dump. It may be patched, "
            "a different region or revision, or a bad dump; re-dump your own "
            "cartridge and do not apply patches or trims")
    if name_from_header != name:
        # Two supported versions never share a digest, so this can only be a
        # header edited to look like the other version (or a broken dump).
        raise RomError(
            f"the file's data is {name}, but its header says {name_from_header}"
            "; the header was modified")
    return name


def describe(digest: str) -> str | None:
    """The supported version's name for a digest, or None."""
    return SUPPORTED.get(digest.lower())


def print_supported(stream=None) -> None:
    stream = sys.stdout if stream is None else stream
    print("Supported versions (clean, unmodified US dumps):", file=stream)
    for digest, name in SUPPORTED.items():
        print(f"  {name}: {digest}", file=stream)
    print("A patched, trimmed, header-modified or other-region dump will not "
          "match.", file=stream)


def main(argv: list[str] | None = None) -> int:
    args = sys.argv[1:] if argv is None else list(argv)
    if len(args) != 1:
        print(__doc__.strip())
        print()
        print_supported()
        return 2

    rom = Path(args[0])
    if not rom.is_file():
        print(f"Not a readable file: {rom}")
        return 2
    try:
        name = verify(rom)
    except RomError as exc:
        print(f"Unsupported ROM: {exc}.")
        print()
        print_supported()
        return 1
    except OSError as exc:
        print(f"Cannot read {rom}: {exc}")
        return 2

    print(f"OK: {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
