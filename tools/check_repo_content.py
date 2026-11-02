#!/usr/bin/env python3
"""Fail if the repository index contains game content or a build artifact.

This is the pre-push safety net for the monorepo. It reads `git ls-files`
(tracked files only), so an untracked save or pack is not an error; adding it
to the index is. Run it before a public push and in CI.

Usage:
  python3 tools/check_repo_content.py [--root DIR] [--max-bytes N]
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

# Suffixes that never belong in the source repository. Build outputs and
# game/SDK content. `.png` and `.wav` are handled separately because the tree
# contains three original, non-game assets from Platinum/the shell.
FORBIDDEN_SUFFIXES = {
    ".nds", ".srl", ".dsi", ".3ds", ".cia", ".cci", ".cxi",
    ".sav", ".dsv", ".pak", ".srm",
    ".3dsx", ".smdh", ".elf", ".map", ".shbin", ".o", ".d",
    ".exe", ".dll", ".7z", ".zip", ".dmp", ".pyc",
    ".mp3", ".ogg", ".flac",
}
FORBIDDEN_PREFIXES = (
    "build/", "dist/", "tests/golden/", ".worktrees/", ".opencode/",
    "private/", "extracted/", "data_pack/", "roms/", "saves/",
)
# The only images/audio allowed. They are original Platinum/shell assets, not
# game content: Platinum's CIA icon and chime, and the shell's generic icon.
ALLOWED_BINARY = {
    "3ds_port/game/cia/icon.png",
    "3ds_port/game/cia/chime.wav",
    "hgss/game/assets/placeholder-icon.png",
}
DEFAULT_MAX_BYTES = 4 * 1024 * 1024


def tracked(root: Path) -> list[str]:
    proc = subprocess.run(["git", "-C", str(root), "ls-files", "-z"],
                          capture_output=True, check=True)
    return [p for p in proc.stdout.decode("utf-8", "replace").split("\0") if p]


def problems(root: Path, max_bytes: int = DEFAULT_MAX_BYTES) -> list[str]:
    found: list[str] = []
    for rel in tracked(root):
        suffix = Path(rel).suffix.lower()
        if rel.startswith(FORBIDDEN_PREFIXES):
            found.append(f"{rel}: tracked build/content directory")
            continue
        if suffix in FORBIDDEN_SUFFIXES:
            found.append(f"{rel}: forbidden suffix {suffix}")
            continue
        if suffix in {".png", ".jpg", ".jpeg", ".gif", ".wav"} and rel not in ALLOWED_BINARY:
            found.append(f"{rel}: image/audio not on the allowlist")
            continue
        path = root / rel
        try:
            size = path.stat().st_size
        except OSError as exc:
            found.append(f"{rel}: {exc}")
            continue
        if size > max_bytes:
            found.append(f"{rel}: {size} bytes, over the {max_bytes}-byte limit")
    return found


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--max-bytes", type=int, default=DEFAULT_MAX_BYTES)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    try:
        files = tracked(root)
    except subprocess.CalledProcessError:
        print("check_repo_content: not a git checkout", file=sys.stderr)
        return 2
    bad = problems(root, args.max_bytes)
    if bad:
        print("check_repo_content: the index contains content that must not be shared:",
              file=sys.stderr)
        for line in bad:
            print(f"  {line}", file=sys.stderr)
        return 1
    print(f"check_repo_content: {len(files)} tracked files; no game content, build artifacts "
          f"or oversized files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
