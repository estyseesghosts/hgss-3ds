#!/usr/bin/env python3
"""Build the .3dsx and prepare a folder you can copy straight to your SD card.

Usage:
    python3 scripts/build_3dsx.py --rom /path/to/game.nds
    python3 scripts/build_3dsx.py --pak build/heartgold3ds.pak
    python3 scripts/build_3dsx.py --pak build/heartgold3ds.pak --out /Volumes/3DS

It runs, in order:

1. prepare_game.py, when you pass --rom (verify, extract and pack the ROM).
   With --pak it uses the pack you already built and finds its manifest.
2. tools/bootstrap.py, which fetches the decompilation and applies the
   patches (it says "already at ..." when this was done before).
3. tools/hgss_game.py --target 3ds, which compiles the game and the port and
   packages the .3dsx.
4. A staging folder (dist/sd-card by default) shaped exactly like the root of
   your SD card. HeartGold writes pokeheartgold.3dsx with
   pokeheartgold/heartgold3ds.pak; SoulSilver writes pokesoulsilver.3dsx
   with pokeheartgold-ss/soulsilver3ds.pak:

       <staging>/3ds/<app>.3dsx
       <staging>/3ds/<game folder>/<pak>
       <staging>/3ds/<game folder>/manifest.json

   Copy the "3ds" folder inside it onto the card.  Point --out at a mounted
   card (for example /Volumes/3DS) and this script writes there directly.

Your save file and the app's log.txt are not touched: the app creates them in
the same folder on first start.
"""
from __future__ import annotations

import argparse
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _util as u  # noqa: E402
import prepare_game  # noqa: E402

DEFAULT_OUT = u.REPO / "dist" / "sd-card"
DEFAULT_BUILD = u.REPO / "build" / "game-3ds"


def game_from_pak(pak: Path) -> tuple[str, dict[str, str]]:
    """Which game a pack belongs to, from its documented file name."""
    game = "SoulSilver" if "soulsilver" in pak.name.lower() else "HeartGold"
    return game, prepare_game.GAMES[game]


def find_manifest(pak: Path, info: dict[str, str], explicit: Path | None) -> Path:
    """The manifest that belongs to this pack, or a clear list of where it may be."""
    if explicit is not None:
        manifest = explicit.expanduser().resolve()
        if not manifest.is_file():
            u.fail(f"no manifest at {manifest}")
        return manifest
    candidates = [
        pak.parent / "manifest.json",
        u.REPO / "build" / info["data"] / "manifest.json",
        u.REPO / "build" / "game-data" / "manifest.json",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    u.fail(
        f"cannot find the manifest.json that belongs to {pak.name}.\n"
        "  It is written next to the extracted game files when the pack is built.\n"
        "  Pass it with --manifest PATH (usually build/game-data/manifest.json)."
    )


def bootstrap() -> None:
    u.say("")
    u.say("Fetching the decompilation and applying the patches ...")
    u.checked([u.python(), "tools/bootstrap.py"])


def compile_3dsx(build_dir: Path, jobs: int, game: str, info: dict[str, str]) -> Path:
    u.say("")
    u.say(f"Compiling the game and the port for {game} (this takes a few minutes) ...")
    u.checked(
        [
            u.python(),
            "tools/hgss_game.py",
            "--game",
            game.lower(),
            "--target",
            "3ds",
            "--keep-going",
            "--out",
            build_dir,
            "-j",
            str(jobs),
        ]
    )
    program = build_dir / (info["app"] + ".3dsx")
    if not program.is_file():
        u.fail(
            f"the build finished but left no .3dsx at {program}.\n"
            "  Send the last lines above; the build log names the failing step."
        )
    return program


def verify_program(program: Path) -> None:
    """Use 3dsxdump when devkitPro has it; it is the console loader's view.

    3dsxdump needs an output file on some platforms (Windows refuses a
    bare input), so always give it a throwaway extraction path and parse
    the section summary it prints either way.
    """
    dump = u.which("3dsxdump")
    if dump is None:
        return
    with tempfile.TemporaryDirectory(prefix="pokeheartgold-3dsx-") as tmp:
        out = Path(tmp) / (program.stem + ".elf")
        code, text = u.capture_status([dump, program, out])
    if code != 0:
        u.fail(f"3dsxdump refused the .3dsx (exit {code}):\n{text}")
    last = " / ".join(
        line.strip() for line in text.splitlines() if line.strip().startswith(("CODE", "RODATA", "DATA", "BSS"))
    )
    if last:
        u.say(f"  3dsxdump: {last}")


def stage(program: Path, pak: Path, manifest: Path, info: dict[str, str], out: Path) -> None:
    """Write the SD-card shape: 3ds/<program> and 3ds/<folder>/{pak,manifest}."""
    app_dir = out / "3ds"
    data_dir = app_dir / info["folder"]
    data_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(program, app_dir / (info["app"] + ".3dsx"))
    shutil.copy2(pak, data_dir / info["pak"])
    shutil.copy2(manifest, data_dir / "manifest.json")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Build the .3dsx and stage the SD-card folder.",
        epilog=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--rom", type=Path, help="your own HeartGold/SoulSilver .nds (runs prepare_game.py)")
    source.add_argument("--pak", type=Path, help="an already-built .pak (see scripts/prepare_game.py)")
    parser.add_argument(
        "--manifest",
        type=Path,
        default=None,
        help="the .pak's manifest.json (default: next to the pack, then build/game-data/)",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=DEFAULT_OUT,
        help="where to stage the SD-card folder (default: dist/sd-card; "
        "point it at a mounted card to copy directly)",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=None,
        help="where the compiler writes its output (default: build/game-3ds, "
        "or build/game-3ds-ss for SoulSilver)",
    )
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 1, help="parallel compiler jobs")
    parser.add_argument("--skip-bootstrap", action="store_true", help="do not fetch/patch the decompilation")
    parser.add_argument("--skip-build", action="store_true", help="reuse the .3dsx already in --build-dir")
    args = parser.parse_args(argv)

    out = args.out.expanduser().resolve()

    if args.rom is not None:
        u.say("== 1. Preparing the game data from your ROM ==")
        prepared = prepare_game.prepare(args.rom)
        pak: Path = prepared["pak"]
        manifest: Path = prepared["manifest"]
        game: str = prepared["game"]
        info: dict[str, str] = prepared["info"]
    else:
        pak = (args.pak or u.REPO / "build" / prepare_game.GAMES["HeartGold"]["pak"]).expanduser().resolve()
        if not pak.is_file():
            u.fail(
                "there is no pack to build with yet.\n"
                "  Pass your ROM:   python3 scripts/build_3dsx.py --rom /path/to/game.nds\n"
                "  or make a pack:  python3 scripts/prepare_game.py /path/to/game.nds"
            )
        game, info = game_from_pak(pak)
        manifest = find_manifest(pak, info, args.manifest)

    if not args.skip_bootstrap:
        bootstrap()

    if args.build_dir is not None:
        build_dir = args.build_dir.expanduser().resolve()
    elif game == "SoulSilver":
        build_dir = u.REPO / "build" / "game-3ds-ss"
    else:
        build_dir = DEFAULT_BUILD

    u.say("")
    u.say(f"Game data: {pak.name} ({game})")
    u.say(f"Manifest:  {manifest}")
    if args.skip_build:
        program = build_dir / (info["app"] + ".3dsx")
        if not program.is_file():
            u.fail(f"--skip-build was given but there is no .3dsx at {program}")
        u.say(f"Reusing:   {program}")
    else:
        u.say("")
        u.say("== 2. Building the .3dsx ==")
        program = compile_3dsx(build_dir, args.jobs, game, info)
        verify_program(program)

    u.say("")
    u.say("== 3. Preparing the SD-card folder ==")
    stage(program, pak, manifest, info, out)

    staged_program = out / "3ds" / (info["app"] + ".3dsx")
    staged_pak = out / "3ds" / info["folder"] / info["pak"]
    staged_manifest = out / "3ds" / info["folder"] / "manifest.json"

    u.say("")
    u.say("Done.")
    u.say("")
    u.say(f"  {staged_program}")
    u.say(f"      {u.human_size(staged_program.stat().st_size)}, sha256 {u.sha256(staged_program)}")
    u.say(f"  {staged_pak}")
    u.say(f"      {u.human_size(staged_pak.stat().st_size)}")
    u.say(f"  {staged_manifest}")
    u.say("")
    u.say(f"The SD-card folder is ready in:\n\n  {out}\n")
    u.say('Copy the "3ds" folder inside it onto the root of your SD card, merging it')
    u.say("with what is already there (if --out was the mounted card, it is already")
    u.say("in place).  The card must end up with:")
    u.say("")
    u.say(f"  /3ds/{info['app']}.3dsx")
    u.say(f"  /3ds/{info['folder']}/{info['pak']}")
    u.say(f"  /3ds/{info['folder']}/manifest.json")
    u.say("")
    if game == "SoulSilver":
        u.say(prepare_game.SS_NOTE)
    else:
        u.say(f"Then start {info['app']} from the Homebrew Launcher.  The first")
        u.say(f"start creates {info['save']} next to the pack.  Keep any existing save.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
