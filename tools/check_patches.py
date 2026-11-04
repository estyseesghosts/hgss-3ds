#!/usr/bin/env python3
"""Fail when a patch in patches/ has not been applied to build/upstream.

`tools/bootstrap.py` applies every patch with `git am`, and stops at the first
one that does not apply. But the tree can also arrive some other way -- copied
from another machine, restored from a backup, or patched by hand -- and then
the build silently compiles pre-patch sources. That is not hypothetical: a
native tree was missing 1904, 1905 and 1909, and 1905's absent hunk is a raw
`0x04000000` read that aborts on hardware (WirelessStatusIcon_LoadChar), so a
console run died at the first map load that draws the wireless icon.

Testing each patch with `git apply --check` does not work on a cumulative
stack: a later patch rewrites the same lines, so an applied patch's forward
check fails and its reverse check fails too. Instead this compares the commit
subject `git am` recorded for each patch (the patch's `Subject:` without the
`[PATCH]` marker, which is what `git am --keep-non-patch` leaves) with the
subjects in the tree's history. A subject that is not there is a patch that
was never applied.

    python3 tools/check_patches.py            # exit 1 when something is missing
    python3 tools/check_patches.py --quiet    # only the exit status and stderr
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bootstrap import PATCH_STACKS, ROOT  # noqa: E402  (same table bootstrap uses)

_PATCH_TAG = re.compile(r"^\[PATCH[^\]]*\]\s*")


def patch_subject(patch: Path) -> str | None:
    """The commit subject `git am` gives this patch.

    A format-patch subject can be folded over several lines (each continuation
    starts with a space), and carries a `[PATCH]`/`[PATCH 1/4]` tag that `git am`
    strips. Both have to be undone or the comparison misses patches that are
    applied.
    """
    lines = patch.read_text(errors="replace").splitlines()
    for i, line in enumerate(lines):
        if not line.startswith("Subject:"):
            continue
        parts = [line[len("Subject:"):].strip()]
        j = i + 1
        while j < len(lines) and lines[j].startswith(" ") and lines[j].strip():
            parts.append(lines[j].strip())
            j += 1
        return _PATCH_TAG.sub("", " ".join(p for p in parts if p)).strip()
    return None


def applied_subjects(upstream: Path) -> set[str]:
    result = subprocess.run(["git", "-C", str(upstream), "log", "--format=%s"],
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"cannot read {upstream}'s history:\n{result.stderr}")
    return {line.strip() for line in result.stdout.splitlines()}


def check(root: Path, quiet: bool = False) -> int:
    upstream = root / "build" / "upstream"
    if not upstream.is_dir():
        print(f"{upstream} is missing: run python3 tools/bootstrap.py", file=sys.stderr)
        return 1

    applied = applied_subjects(upstream)
    total = 0
    missing: list[tuple[Path, str]] = []

    for name, _subdir in PATCH_STACKS:
        directory = root / "patches" / name
        if not directory.is_dir():
            continue
        for patch in sorted(directory.glob("*.patch")):
            total += 1
            subject = patch_subject(patch)
            if subject is None:
                missing.append((patch, "(no Subject: line in the patch)"))
            elif subject not in applied:
                missing.append((patch, subject))

    if not quiet:
        for patch, subject in missing:
            print(f"not applied: {patch.relative_to(root)}")
            print(f"             {subject}")
        print(f"{total - len(missing)}/{total} patches applied, {len(missing)} missing")

    if missing:
        print("\nbuild/upstream is missing patches. Apply them in order with:\n"
              "  git -C build/upstream am \"$PWD/<patch>\"\n"
              "or start clean with: python3 tools/bootstrap.py --clean",
              file=sys.stderr)
    return 1 if missing else 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    raise SystemExit(check(args.root.resolve(), args.quiet))


if __name__ == "__main__":
    main()
