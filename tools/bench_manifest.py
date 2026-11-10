#!/usr/bin/env python3
"""Write a run manifest for a benchmark/validation run.

Usage:
    python tools/bench_manifest.py --game soulsilver \
        --artifact dist/sd-card/3ds/pokesoulsilver.3dsx \
        --save soulsilver.sav --script tests/inputs/field-goldenrod.txt \
        --ini port.ini --model "Old 3DS" --label goldenrod-idle \
        -o build/bench/goldenrod-idle.manifest.json

Why: the performance evidence contract (PICA200-INSTRUMENTATION.md section B)
requires every run to carry its own identity -- build commit, artifact hash,
flags, save/script/config hashes, model and clock -- because a result without
one cannot be A/B compared. This tool writes that identity to one JSON file.
It reads files and git only; it never changes the build, the save or the game.

The manifest deliberately records what the source tree is, not just what the
binary claims: `git_commit`, `dirty`, and the two pins. The `build-info`
sidecar that tools/hgss_game.py writes next to the .3dsx is folded in when it
exists, because it carries the effective experiment flags of that exact build.

Exit status is 0 on success, 1 on a missing or unreadable input.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import platform
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def git(*args: str) -> str:
    try:
        proc = subprocess.run(["git", *args], cwd=REPO, capture_output=True,
                              text=True, check=False)
    except OSError:
        return ""
    return proc.stdout.strip() if proc.returncode == 0 else ""


def read_lock(name: str) -> dict[str, str]:
    path = REPO / name
    values: dict[str, str] = {}
    try:
        for raw in path.read_text(encoding="utf-8").splitlines():
            line = raw.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()
    except OSError:
        pass
    return values


def file_entry(path: Path) -> dict[str, object] | None:
    if not path.is_file():
        return None
    return {"path": str(path), "bytes": path.stat().st_size,
            "sha256": sha256_of(path)}


def build_info_for(artifact: Path) -> dict[str, object] | None:
    """The sidecar tools/hgss_game.py writes next to the program."""
    sidecar = artifact.with_name(artifact.stem.replace(".3dsx", "")
                                 + ".build-info.txt")
    if not sidecar.is_file():
        return None
    text = sidecar.read_text(encoding="utf-8", errors="replace")
    return {"path": str(sidecar), "sha256": sha256_of(sidecar),
            "text": text}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Write a benchmark run manifest (identity + hashes).")
    parser.add_argument("--game", required=True, choices=["heartgold", "soulsilver"])
    parser.add_argument("--artifact", required=True, type=Path,
                        help="the .3dsx that will be run")
    parser.add_argument("--save", type=Path, default=None,
                        help="the raw 512 KiB save the run starts from")
    parser.add_argument("--script", type=Path, default=None,
                        help="the frame-indexed input script (script.txt)")
    parser.add_argument("--ini", type=Path, default=None,
                        help="the port.ini the run uses")
    parser.add_argument("--model", default="unrecorded",
                        help='hardware/emulator identity, e.g. "Old 3DS" or "Azahar 2126.2 O3DS"')
    parser.add_argument("--clock", default="unrecorded",
                        help="CPU clock / core-share note when the console reports one")
    parser.add_argument("--label", default="run", help="short run label")
    parser.add_argument("--note", default="", help="free-form note")
    parser.add_argument("-o", "--output", type=Path, default=None)
    args = parser.parse_args(argv)

    artifact = args.artifact.resolve()
    if not artifact.is_file():
        print(f"error: no artifact at {artifact}", file=sys.stderr)
        return 1

    now = datetime.datetime.now(datetime.timezone.utc)
    commit = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain"))
    upstream = read_lock("upstream.lock")
    platinum = read_lock("platinum.lock")

    manifest: dict[str, object] = {
        "schema": 1,
        "label": args.label,
        "created_utc": now.isoformat(timespec="seconds"),
        "game": args.game,
        "model": args.model,
        "clock": args.clock,
        "note": args.note,
        "repo": {
            "commit": commit,
            "short": commit[:9] if commit else "",
            "dirty": dirty,
            "decomp_pin": upstream.get("commit", ""),
            "platinum_pin": platinum.get("commit", ""),
        },
        "host": {
            "platform": platform.platform(),
            "python": platform.python_version(),
        },
        "artifact": file_entry(artifact),
        "build_info": build_info_for(artifact),
        "save": file_entry(args.save.resolve()) if args.save else None,
        "script": file_entry(args.script.resolve()) if args.script else None,
        "ini": file_entry(args.ini.resolve()) if args.ini else None,
        "env_toggles": {key: value for key, value in sorted(os.environ.items())
                        if key.startswith("HGSS_")},
    }

    out = args.output
    if out is None:
        out = REPO / "build" / "bench" / f"{args.label}-{now:%Y%m%d-%H%M%S}.json"
    out = out.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"manifest: {out}")
    print(f"  commit {manifest['repo']['short']}{'-dirty' if dirty else ''} "
          f"game {args.game} model {args.model}")
    print(f"  artifact {artifact.name} {manifest['artifact']['sha256'][:16]}... "
          f"{manifest['artifact']['bytes']} B")
    for key in ("save", "script", "ini"):
        entry = manifest[key]
        if entry:
            name = Path(str(entry["path"])).name
            print(f"  {key} {name} {str(entry['sha256'])[:16]}...")
    if manifest["build_info"] is None:
        print("  build-info: none beside the artifact "
              "(flags come from env_toggles only)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
