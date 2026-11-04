#!/usr/bin/env python3
"""Stage built 3DS artifacts to the SD-card directories with unique names.

Usage:
    python3 tools/stage_3ds.py --hg build/game-3ds/pokeheartgold.3dsx \
        --ss build/game-3ds-ss/pokesoulsilver.3dsx \
        [--also ~/hgss-private/sd-card/3ds]

Rules (so two different builds never share a filename, and any file or
log traces to its commit):

* HG stages to dist/sd-card/3ds/; SS stages to dist/sd-card/3ds/ and
  dist/soulsilver-sd-card/3ds/ (--also adds dirs for both).
* Staged name: <base>-<shortsha>[-dirty].3dsx, e.g.
  pokeheartgold-82c1ab05-dirty.3dsx. Same commit rebuilt clean gets the
  same name; a different dirty state gets -2, -3, ... -- never an
  overwrite of different bits, never a silent duplicate.
* Byte-identical content under the same name is skipped, not recopied.
* validate_3dsx must be [] or the file is refused.
* Each staged file gets a <name>.build-info.txt sidecar (commit, dirty
  state, sizes, sha256, stage time). The binary itself logs its build id
  at boot (main_3ds.c), so log.txt traces the same way.
* Everything is chmod 666 (VFAT SD cards).

Prints the staged paths, sizes and sha256 for STATE.md, plus the exact
SD-card copy instructions.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TOOLS = REPO / "tools"
sys.path.insert(0, str(REPO))
from tools import hgss_game as hg  # noqa: E402

HG_DESTS = [REPO / "dist" / "sd-card" / "3ds"]
SS_DESTS = [REPO / "dist" / "sd-card" / "3ds",
            REPO / "dist" / "soulsilver-sd-card" / "3ds"]


def sha256_of(path: Path) -> str:
    """Hex sha256 of a file."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_build_info(src: Path) -> tuple[str, str, bool, str, bool]:
    """(short, full, dirty, status, inferred) for a built .3dsx.

    Prefers the .build-info.txt sidecar tools/hgss_game.py wrote next to
    it (build-time truth); falls back to the current checkout state and
    marks the result inferred.
    """
    sidecar = src.with_name(src.stem + ".build-info.txt")
    if sidecar.is_file():
        fields: dict[str, str] = {}
        status_lines: list[str] = []
        in_status = False
        for line in sidecar.read_text(encoding="utf-8").splitlines():
            if in_status:
                status_lines.append(line)
                continue
            if line == "status:":
                in_status = True
                continue
            if ": " in line:
                key, value = line.split(": ", 1)
                fields[key] = value
        build = fields.get("build", "unknown")
        dirty = build.endswith("-dirty")
        short = build[:-len("-dirty")] if dirty else build
        return (short, fields.get("sha", "unknown"),
                dirty, "\n".join(status_lines), False)
    short, full, dirty, status = hg.git_build_info()
    return (short, full, dirty, status, True)


def staged_base(binary_stem: str, short: str, dirty: bool) -> str:
    """Unique stem for a staged file: base + commit, -dirty when flagged."""
    return f"{binary_stem}-{short}{'-dirty' if dirty else ''}"


def pick_staged_path(dest_dir: Path, base: str, digest: str) -> tuple[Path, bool]:
    """A free path for base, or the existing identical file.

    Returns (path, already_staged): same-name same-bits is a skip, not a
    copy; same-name different-bits gets -2, -3, ... so two builds never
    share a filename.
    """
    candidate = dest_dir / f"{base}.3dsx"
    n = 2
    while candidate.is_file():
        if sha256_of(candidate) == digest:
            return (candidate, True)
        candidate = dest_dir / f"{base}-{n}.3dsx"
        n += 1
    return (candidate, False)


def stage_file(src: Path, dest_dir: Path) -> tuple[Path, bool]:
    """Validate, uniquely name, copy + manifest + chmod one .3dsx."""
    problems = hg.validate_3dsx(src)
    if problems:
        raise SystemExit(f"stage_3ds: refusing {src.name}: "
                         f"validate_3dsx: {problems[0]}")
    short, full, dirty, status, inferred = source_build_info(src)
    digest = sha256_of(src)
    dest_dir.mkdir(parents=True, exist_ok=True)
    path, already = pick_staged_path(dest_dir, staged_base(src.stem, short, dirty),
                                     digest)
    if not already:
        shutil.copy2(src, path)
        Path(path).chmod(0o666)
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    sidecar = path.with_name(path.stem + ".build-info.txt")
    sidecar.write_text(
        f"file: {path.name}\n"
        f"source: {src}\n"
        f"build: {short}{'-dirty' if dirty else ''}{' (inferred at stage)' if inferred else ''}\n"
        f"sha: {full}\n"
        f"dirty: {'yes' if dirty else 'no'}\n"
        f"staged_utc: {now}\n"
        f"size: {src.stat().st_size}\n"
        f"sha256: {digest}\n"
        f"status:\n{status}",
        encoding="utf-8")
    sidecar.chmod(0o666)
    return (path, already)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--hg", type=Path, help="built pokeheartgold.3dsx")
    ap.add_argument("--ss", type=Path, help="built pokesoulsilver.3dsx")
    ap.add_argument("--also", type=Path, action="append", default=[],
                    help="extra destination dir (repeatable)")
    args = ap.parse_args(argv)
    if args.hg is None and args.ss is None:
        ap.error("nothing to stage: pass --hg and/or --ss")
    jobs: list[tuple[Path, list[Path]]] = []
    if args.hg is not None:
        jobs.append((args.hg, HG_DESTS + args.also))
    if args.ss is not None:
        jobs.append((args.ss, SS_DESTS + args.also))
    for src, dests in jobs:
        if not src.is_file():
            print(f"stage_3ds: missing {src}", file=sys.stderr)
            return 2
        for dest in dests:
            path, already = stage_file(src, dest)
            verb = "already staged" if already else "staged"
            print(f"{verb}: {path} ({path.stat().st_size} B "
                  f"sha256 {sha256_of(path)[:16]}...)")
    first = jobs[0][1][0]
    print(f"SD: delete stale .3dsx from the console folder(s), copy only the "
          f"new files from {first} (HG) and the soulsilver folder (SS).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
