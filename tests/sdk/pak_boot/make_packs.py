#!/usr/bin/env python3
"""Write the synthetic data packs the boot-time pak check test opens.

Synthetic bytes only: no ROM-derived data. The OK pack's header carries a
ROM SHA-1 the *pinned* reader recognises (its table is
pokeplatinum-3ds' — see 3ds_port/pak/src/pak.c). The extractor's HGSS
digests are tested separately as the unsupported case.

Usage: make_packs.py OUTDIR
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO))

from builder.pack import HEADER, VERSION, write_pak  # noqa: E402

# The digest 3ds_port/pak/src/pak.c accepts (Platinum USA Rev 1). A pack
# made from a real HeartGold cartridge carries 4fcded… instead, and the
# pinned reader rejects that: the boot test proves the error message.
READER_SHA1 = "0862ec35b24de5c7e2dcb88c9eea0873110d755c"
HG_SHA1 = "4fcded0e2713dc03929845de631d0932ea2b5a37"
BUILD_ID = "heartgold-nitrofs-1"

FILES = [(0, b"hello.bin!"), (7, b"xyz")]
MANIFEST = {"files": [{"id": 0, "path": "hello.bin", "size": 10},
                      {"id": 7, "path": "data/x.bin", "size": 3}]}


def write_pack(path: Path, *, build_id: str = BUILD_ID,
               rom_sha1: str = READER_SHA1) -> bytes:
    with path.open("wb") as handle:
        write_pak(handle, FILES, build_id=build_id, rom_sha1=rom_sha1)
    return path.read_bytes()


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip())
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)

    ok = write_pack(out / "ok.pak")
    (out / "manifest.json").write_text(json.dumps(MANIFEST), encoding="utf-8")

    write_pack(out / "wrong-build.pak", build_id="platinum-nitrofs-1")
    write_pack(out / "unsupported-rom.pak", rom_sha1=HG_SHA1)

    wrong_version = bytearray(ok)
    struct.pack_into("<I", wrong_version, 8, VERSION + 1)
    (out / "wrong-version.pak").write_bytes(wrong_version)

    (out / "truncated.pak").write_bytes(ok[:HEADER.size + 1])
    corrupt = bytearray(ok)
    corrupt[0:8] = b"BADMAGIC"
    (out / "corrupt.pak").write_bytes(corrupt)

    bad = json.loads(json.dumps(MANIFEST))
    bad["files"][0]["size"] = 11
    (out / "manifest-bad-size.json").write_text(json.dumps(bad), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
