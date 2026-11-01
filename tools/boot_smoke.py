#!/usr/bin/env python3
"""Run the PC build until it crashes, stalls or asks for a missing file.

Phase 4 step 1: a machine-readable first-failure report. With no arguments the
tool writes a small synthetic data pack (the file IDs the boot path opens early,
zero-length except a few markers), runs ``pokeheartgold-pc`` against it with
``--stop-at-missing-file`` and reports the first event as JSON. ``--pak`` and
``--manifest`` switch to the real pack.

The runner writes one JSON object per line with ``--events`` (see
hgss/game/app/main_pc.c): ``missing_file``, ``crash``, ``stall``,
``frame_limit``, ``terminate`` and ``reset``. The log is also scanned for the
last boot stage (pack open, save file, game start, each overlay) so the report
says where the game was when it stopped.

Usage:
  python3 tools/boot_smoke.py [--pc PATH] [--pak PAK --manifest JSON]
                              [--frames N] [--stall-seconds N]
                              [--expect-stage STAGE] [--keep DIR] [--json]

Exit status: 0 when a stage was reported (or when --expect-stage matched), 1
when the run produced no event, 2 for setup errors.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))

from builder.pack import write_pak  # noqa: E402

# The build-private reader in tools/hgss_game.py accepts the retail HG/SS
# digests. The pack is synthetic (no ROM bytes), but its header names a
# supported source ROM so the reader gets past that check.
HG_SHA1 = "4fcded0e2713dc03929845de631d0932ea2b5a37"
BUILD_ID = "heartgold-nitrofs-1"

# The boot path asks for overlays by ID before it asks for a NARC. The port
# answers overlay loads from the linked image, but a synthetic pack with the
# same IDs exercises the reader's ID lookup without game data.
SYNTHETIC_IDS = list(range(0, 129))

STAGE_PATTERNS = [
    (re.compile(r"\[heartgold\] save file:"), "save_file"),
    (re.compile(r"\[heartgold\] starting the game"), "game_start"),
    (re.compile(r"overlay (\S+) loaded"), "overlay"),
]

EVENT_STAGES = {"missing_file", "crash", "stall", "frame_limit", "terminate", "reset"}


def synthetic_pack(directory: Path) -> tuple[Path, Path]:
    """Write a synthetic pak + manifest and return their paths."""
    directory.mkdir(parents=True, exist_ok=True)
    files = [(file_id, b"") for file_id in SYNTHETIC_IDS]
    # A couple of non-empty entries so the reader's offsets are exercised.
    files[0] = (0, b"boot-smoke")
    pak = directory / "heartgold3ds.pak"
    with pak.open("wb") as handle:
        write_pak(handle, files, build_id=BUILD_ID, rom_sha1=HG_SHA1)
    manifest = {
        "files": [{"id": file_id, "path": f"file_{file_id:04d}.bin", "size": len(data)}
                  for file_id, data in files],
        "rom_sha1": HG_SHA1,
    }
    manifest_path = directory / "manifest.json"
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
    return pak, manifest_path


def parse_events(path: Path) -> list[dict]:
    events = []
    if not path.exists():
        return events
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(event, dict):
            events.append(event)
    return events


def last_stage(log: str) -> str | None:
    stage = None
    for line in log.splitlines():
        if "[heartgold] save file:" in line:
            stage = "save_file"
        elif "[heartgold] starting the game" in line:
            stage = "game_start"
        else:
            match = re.search(r"overlay (\S+) loaded", line)
            if match:
                stage = f"overlay:{match.group(1)}"
    return stage


def resolve_symbol(pc: Path, address: str) -> str | None:
    """addr2line the faulting pc; None when the toolchain isn't present."""
    try:
        value = int(address, 16)
    except (TypeError, ValueError):
        return None
    addr2line = shutil.which("addr2line")
    if addr2line is None or not pc.exists():
        return None
    proc = subprocess.run([addr2line, "-f", "-i", "-p", "-C", "-e", str(pc), hex(value)],
                          capture_output=True, text=True)
    text = (proc.stdout or "").strip()
    return text or None


def run(pc: Path, pak: Path, manifest: Path, *, frames: int, stall: int,
        timeout: int) -> tuple[int | None, str, list[dict]]:
    """Run the PC build; return (exit code, stderr+stdout, events)."""
    with tempfile.TemporaryDirectory(prefix="boot-smoke-") as temp:
        events_path = Path(temp) / "events.jsonl"
        save = Path(temp) / "heartgold.sav"
        command = [str(pc), str(pak), str(manifest), "--save", str(save),
                   "--frames", str(frames), "--stop-at-missing-file",
                   "--stall-seconds", str(stall), "--events", str(events_path)]
        try:
            proc = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
            code = proc.returncode
            log = (proc.stderr or "") + (proc.stdout or "")
        except subprocess.TimeoutExpired as exc:
            code = None
            log = ((exc.stderr or b"").decode("utf-8", "replace") if isinstance(exc.stderr, bytes)
                   else (exc.stderr or ""))
            log += "\n[boot_smoke] the runner did not exit within the timeout\n"
        return code, log, parse_events(events_path)


def report(events: list[dict], log: str, code: int | None, pc: Path) -> dict:
    event = None
    for candidate in events:
        if candidate.get("event") in EVENT_STAGES:
            event = candidate
            break
    stage = event.get("event") if event else "no_event"
    result = {
        "stage": stage,
        "boot_stage": last_stage(log),
        "frame": event.get("frame") if event else None,
        "exit": code,
    }
    if event:
        for key in ("signal", "address", "pc", "path", "parameter", "seconds"):
            if key in event:
                result[key] = event[key]
        if "pc" in event:
            result["symbol"] = resolve_symbol(pc, event["pc"])
    # The log tail is the human half of the report; keep it short.
    result["log_tail"] = log.splitlines()[-8:]
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pc", type=Path, default=REPO / "build" / "pokeheartgold-pc")
    parser.add_argument("--pak", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--stall-seconds", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--expect-stage", choices=sorted(EVENT_STAGES | {"no_event"}))
    parser.add_argument("--keep", type=Path, help="keep the synthetic pack here")
    parser.add_argument("--json", action="store_true", help="compact one-line JSON")
    args = parser.parse_args(argv)

    if not args.pc.exists():
        print(f"boot_smoke: no PC binary at {args.pc} "
              f"(build with: python3 tools/hgss_game.py --target host32 --out build)", file=sys.stderr)
        return 2
    if (args.pak is None) != (args.manifest is None):
        print("boot_smoke: give --pak and --manifest together", file=sys.stderr)
        return 2

    temp = None
    if args.pak is None:
        if args.keep is not None:
            pak, manifest = synthetic_pack(args.keep)
        else:
            temp = tempfile.TemporaryDirectory(prefix="boot-smoke-pack-")
            pak, manifest = synthetic_pack(Path(temp.name))
    else:
        pak, manifest = args.pak, args.manifest
        if not pak.exists() or not manifest.exists():
            print(f"boot_smoke: cannot read {pak} or {manifest}", file=sys.stderr)
            return 2

    try:
        code, log, events = run(args.pc, pak, manifest, frames=args.frames,
                                stall=args.stall_seconds, timeout=args.timeout)
    finally:
        if temp is not None:
            temp.cleanup()

    result = report(events, log, code, args.pc)
    if args.json:
        print(json.dumps(result, sort_keys=True))
    else:
        print(json.dumps(result, indent=2, sort_keys=True))

    if args.expect_stage is not None:
        return 0 if result["stage"] == args.expect_stage else 1
    return 0 if result["stage"] != "no_event" else 1


if __name__ == "__main__":
    raise SystemExit(main())
