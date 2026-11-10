#!/usr/bin/env python3
"""Stage a frozen benchmark run in an SD-card layout, for a console or Azahar.

Usage:
    python tools/stage_bench.py --game soulsilver \
        --artifact build/game-3ds-ss/pokesoulsilver.3dsx \
        --pak build/soulsilver3ds.pak \
        --manifest build/game-data-ss/manifest.json \
        --save soulsilver.sav \
        --script tests/inputs/ss-continue-field.txt \
        --ini docs/bench/port-baseline.ini \
        --out build/bench/sd-ss

Writes exactly what the port expects:

    <out>/3ds/<app>.3dsx
    <out>/3ds/<folder>/<pak>
    <out>/3ds/<folder>/manifest.json
    <out>/3ds/<folder>/<save>
    <out>/3ds/<folder>/port.ini
    <out>/3ds/<folder>/script.txt

Use --out with a mounted card root (or Azahar's sdmc) to write it straight
there. It never modifies the artifact, the save or the pack; it only copies.

Run tools/bench_manifest.py on the same inputs afterwards so the run carries
its identity.
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))
import prepare_game  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Stage a benchmark run on an SD layout.")
    parser.add_argument("--game", required=True, choices=["heartgold", "soulsilver"])
    parser.add_argument("--artifact", required=True, type=Path)
    parser.add_argument("--pak", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--save", required=True, type=Path)
    parser.add_argument("--script", required=True, type=Path)
    parser.add_argument("--ini", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args(argv)

    info = prepare_game.GAMES["SoulSilver" if args.game == "soulsilver" else "HeartGold"]
    for label, path in (("artifact", args.artifact), ("pak", args.pak),
                        ("manifest", args.manifest), ("save", args.save),
                        ("script", args.script), ("ini", args.ini)):
        if not path.expanduser().resolve().is_file():
            print(f"error: no {label} at {path}", file=sys.stderr)
            return 1

    out = args.out.expanduser().resolve()
    app_dir = out / "3ds"
    data_dir = app_dir / info["folder"]
    data_dir.mkdir(parents=True, exist_ok=True)

    copies = (
        (args.artifact, app_dir / (info["app"] + ".3dsx")),
        (args.pak, data_dir / info["pak"]),
        (args.manifest, data_dir / "manifest.json"),
        (args.save, data_dir / info["save"]),
        (args.ini, data_dir / "port.ini"),
        (args.script, data_dir / "script.txt"),
    )
    for source, dest in copies:
        shutil.copy2(source.expanduser().resolve(), dest)
        print(f"  {dest}")

    print()
    print(f"Staged a {args.game} benchmark run in: {out}")
    print("Copy the 3ds folder to the SD-card root (or point --out at it),")
    print("then run tools/bench_manifest.py on the same inputs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
