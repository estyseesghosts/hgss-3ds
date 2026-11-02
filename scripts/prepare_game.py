#!/usr/bin/env python3
"""Turn your own Pokémon HeartGold or SoulSilver ROM into the port's data file.

Usage:
    python3 scripts/prepare_game.py /path/to/game.nds
    python3 scripts/prepare_game.py /path/to/game.nds -o build/heartgold3ds.pak

What it does, in order:

1. Checks the ROM is a clean, unmodified US HeartGold or SoulSilver dump.
   Trimmed, patched, header-edited and other-region images are refused with a
   reason.  Your ROM is only read; it is never changed or uploaded.
2. Extracts the game's files into a private folder (build/game-data for
   HeartGold, build/game-data-ss for SoulSilver).  This is the same data the
   DS cartridge holds.
3. Packs that folder into the .pak the port reads and records the ROM's SHA-1
   in the manifest.  The port reads the .pak, not the .nds.

Both games are handled; the tool identifies which one it has and packs it
with that game's own name and build ID (`heartgold3ds.pak` /
`soulsilver3ds.pak`). HeartGold and SoulSilver have separate apps, folders
and saves, so the two can sit on one SD card without touching each other.

Everything this writes is private: do not commit or share the .pak.

Next: `python3 scripts/build_3dsx.py --pak build/heartgold3ds.pak` (or the
SoulSilver pack), or give build_3dsx.py the ROM and it runs this step itself.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _util as u  # noqa: E402

#: Per-game names, taken from docs/wiki/SD-Card-Layout.md so the pack, the
#: build ID and the SD folder cannot drift apart.
GAMES: dict[str, dict[str, str]] = {
    "HeartGold": {
        "folder": "pokeheartgold",
        "pak": "heartgold3ds.pak",
        "build_id": "heartgold-nitrofs-1",
        "data": "game-data",
        "save": "heartgold.sav",
        "app": "pokeheartgold",
    },
    "SoulSilver": {
        "folder": "pokeheartgold-ss",
        "pak": "soulsilver3ds.pak",
        "build_id": "soulsilver-nitrofs-1",
        "data": "game-data-ss",
        "save": "soulsilver.sav",
        "app": "pokesoulsilver",
    },
}
SS_NOTE = (
    "SoulSilver has its own app and folder: start pokesoulsilver from the\n"
    "Homebrew Launcher, with sdmc:/3ds/pokeheartgold-ss/soulsilver3ds.pak.\n"
    "Its save (soulsilver.sav) is separate from HeartGold's and cannot be\n"
    "mixed with it."
)


def sha1_of(path: Path) -> str:
    """The SHA-1 of a file, read in 1 MiB chunks (the ROM is ~128 MiB)."""
    digest = hashlib.sha1()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def identify(rom: Path) -> tuple[str, dict[str, str]]:
    """Run the verifier; return the game's name ('HeartGold') and its names."""
    code, text = u.capture_status([u.python(), u.REPO / "builder" / "verify_rom.py", rom])
    u.say(text)
    if code != 0:
        u.fail("the file above is not a supported game dump (see the reason)")
    game = next((name for name in GAMES if name in text), None)
    if game is None:
        u.fail("the verifier accepted the ROM but did not name the game; send the line above")
    return game, GAMES[game]


def manifest_matches(manifest: Path, rom_sha1: str) -> bool:
    """True when an existing extraction came from this exact ROM."""
    try:
        data = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return False
    return data.get("rom_sha1") == rom_sha1


def prepare(
    rom: Path,
    output: Path | None = None,
    data_dir: Path | None = None,
    *,
    force: bool = False,
) -> dict[str, Any]:
    """Verify, extract and pack one ROM.  Returns the paths and game details."""
    rom = rom.expanduser().resolve()
    if not rom.is_file():
        u.fail(f"no ROM at {rom}")
    for tool in ("verify_rom.py", "extract_rom.py", "pack.py"):
        if not (u.REPO / "builder" / tool).is_file():
            u.fail(f"the repository is incomplete: builder/{tool} is missing")

    u.say("1/3  Checking the ROM...")
    game, info = identify(rom)
    pak = (output or u.REPO / "build" / info["pak"]).expanduser().resolve()
    data = (data_dir or u.REPO / "build" / info["data"]).expanduser().resolve()
    manifest = data / "manifest.json"

    u.say("")
    u.say(f"Preparing Pokémon {game}.")
    u.say("")

    rom_sha1 = sha1_of(rom)
    reuse = not force and manifest_matches(manifest, rom_sha1)
    if reuse:
        u.say(f"2/3  Reusing the files already extracted in {data}")
        u.say("     (use --force to extract them again)")
    else:
        u.say(f"2/3  Extracting the game's files to {data} ...")
        u.checked([u.python(), u.REPO / "builder" / "extract_rom.py", rom, "-o", data])

    if not manifest.is_file():
        u.fail(f"the extractor did not leave a manifest at {manifest}")

    u.say("")
    u.say(f"3/3  Packing into {pak} ...")
    pak.parent.mkdir(parents=True, exist_ok=True)
    u.checked(
        [
            u.python(),
            u.REPO / "builder" / "pack.py",
            data,
            "-o",
            pak,
            "--build-id",
            info["build_id"],
            "--rom",
            rom,
        ]
    )

    u.say("")
    u.say(f"Done.  {pak.name}: {u.human_size(pak.stat().st_size)}")
    u.say(f"  game:     {game}")
    u.say(f"  data:     {data}")
    u.say(f"  manifest: {manifest}")
    if game == "SoulSilver":
        u.say("")
        u.say(SS_NOTE)
    return {
        "game": game,
        "info": info,
        "rom": rom,
        "pak": pak,
        "data": data,
        "manifest": manifest,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Check, extract and pack your own HeartGold/SoulSilver ROM.",
        epilog=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("rom", type=Path, help="your own US HeartGold or SoulSilver .nds")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="where to write the .pak (default: build/heartgold3ds.pak, "
        "or build/soulsilver3ds.pak for SoulSilver)",
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=None,
        help="where to extract the game's files (default: build/game-data "
        "or build/game-data-ss)",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="extract again even if these exact files are already there",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="remove the extracted files after packing (the manifest stays)",
    )
    args = parser.parse_args(argv)

    prepared = prepare(args.rom, args.output, args.data_dir, force=args.force)

    u.say("")
    u.say("Next: build the .3dsx and the SD-card folder with")
    u.say(f"  python3 scripts/build_3dsx.py --pak {prepared['pak']}")
    u.say("or give that script the ROM directly and it will run this step itself.")
    if args.clean:
        for child in sorted(prepared["data"].iterdir()):
            if child.name == "manifest.json":
                continue
            if child.is_dir():
                shutil.rmtree(child)
            else:
                child.unlink()
        u.say("")
        u.say(f"Removed the extracted files from {prepared['data']} (the manifest stays).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
