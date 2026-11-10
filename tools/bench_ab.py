#!/usr/bin/env python3
"""Run matched A/B trials of two .3dsx builds and compare their logs.

Usage (Windows + Azahar):
    python tools/bench_ab.py \
        --game heartgold \
        --artifact-a build/game-3ds/pokeheartgold.3dsx \
        --artifact-b build/game-3ds-off/pokeheartgold.3dsx \
        --name-a profile --name-b off \
        --save build/bench/heartgold.sav \
        --script tests/inputs/hg-continue-field.txt \
        --ini docs/bench/port-baseline.ini \
        --trials 3 --seconds 240 --label goldenrod-profile-vs-off

Why: a single emulator sample cannot separate a real change from run-to-run
noise (PICA200-INSTRUMENTATION.md §B/§I). This tool alternates the two
artifacts on identical inputs, one trial at a time, and reports the median
and the spread of each metric per artifact. It is diagnostic only: it copies
files, launches the emulator and reads the logs; it never changes the build.

Requirements:
- Azahar at --azahar (default C:\\Program Files\\Azahar\\azahar.exe) and an
  SD card root at --sd-root (default %APPDATA%\\Azahar\\sdmc).
- The game folder under the SD root already has the pack, manifest and save
  (tools/stage_bench.py stages them); this tool copies the save, script and
  ini itself so both trials start from identical bytes.
- Keep the machine otherwise idle: Azahar throttles when its window is not
  focused and other CPU load changes the result. The tool re-focuses the
  window each trial.
- The tool closes **any** running azahar.exe before each trial (by image
  name), because a second instance would share the SD card and the logs.

The comparison is deliberately blunt: it prints both artifacts' medians and
the overlap. If the spreads overlap, the result is "inconclusive", which is
the honest answer for emulator timing.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
sys.path.insert(0, str(REPO / "scripts"))
import bench_report  # noqa: E402
import prepare_game  # noqa: E402

AZAHAR = Path(r"C:\Program Files\Azahar\azahar.exe")
DEFAULT_SD = Path(os.environ.get("APPDATA", "")) / "Azahar" / "sdmc"


def kill_emulator() -> None:
    if os.name != "nt":
        return
    subprocess.run(["taskkill", "/IM", "azahar.exe", "/F"],
                   capture_output=True, text=True, check=False)


def focus_emulator() -> None:
    """Bring the Azahar window to the foreground (Windows only)."""
    if os.name != "nt":
        return
    user32 = ctypes.windll.user32
    handles: list[int] = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def callback(hwnd, _):
        pid = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        title = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, title, 256)
        if "Azahar" in title.value:
            handles.append(hwnd)
        return True

    user32.EnumWindows(callback, 0)
    for hwnd in handles:
        user32.ShowWindow(hwnd, 9)          # SW_RESTORE
        user32.SetForegroundWindow(hwnd)


def wait_for_log(path: Path, timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if path.is_file() and path.stat().st_size > 0:
            return True
        time.sleep(1.0)
    return path.is_file()


def run_trial(args, artifact: Path, name: str, trial: int, out: Path) -> Path | None:
    info = prepare_game.GAMES["SoulSilver" if args.game == "soulsilver" else "HeartGold"]
    app_dir = args.sd_root / "3ds"
    data_dir = app_dir / info["folder"]
    data_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(artifact, app_dir / (info["app"] + ".3dsx"))
    shutil.copy2(args.save, data_dir / info["save"])
    shutil.copy2(args.script, data_dir / "script.txt")
    shutil.copy2(args.ini, data_dir / "port.ini")
    log = data_dir / "log.txt"
    log.unlink(missing_ok=True)

    subprocess.Popen([str(args.azahar), str(app_dir / (info["app"] + ".3dsx"))])
    time.sleep(6)
    focus_emulator()
    wait_for_log(log, 60)
    time.sleep(args.seconds)

    dest = out / f"{name}-t{trial}.log"
    if log.is_file():
        shutil.copy2(log, dest)
    kill_emulator()
    time.sleep(2)
    return dest if log.is_file() else None


def metrics(path: Path, skip: int, from_frame: int | None) -> dict[str, float]:
    rows = bench_report.parse_log(path.read_text(encoding="utf-8", errors="replace"))
    rows = rows[skip:]
    if from_frame is not None:
        rows = [row for row in rows if int(row["frame"]) >= from_frame]
    if not rows:
        return {}

    def collect(key):
        values = []
        for row in rows:
            value = bench_report.row_values(row).get(key)
            if value is not None:
                values.append(float(value))
        return values

    result: dict[str, float] = {}
    for key in ("frame_ms", "composes_s", "publishes_s", "drawer_ms", "geom_ms"):
        values = collect(key)
        if values:
            result[key] = statistics.median(values)
            result[key + "_min"] = min(values)
            result[key + "_max"] = max(values)
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Matched A/B trials of two builds.")
    parser.add_argument("--game", required=True, choices=["heartgold", "soulsilver"])
    parser.add_argument("--artifact-a", required=True, type=Path)
    parser.add_argument("--artifact-b", required=True, type=Path)
    parser.add_argument("--name-a", default="A")
    parser.add_argument("--name-b", default="B")
    parser.add_argument("--save", required=True, type=Path)
    parser.add_argument("--script", required=True, type=Path)
    parser.add_argument("--ini", required=True, type=Path)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--seconds", type=float, default=240.0,
                        help="wall seconds per trial (the frozen script should finish inside)")
    parser.add_argument("--skip", type=int, default=2,
                        help="warm-up samples to drop per trial (600 frames each)")
    parser.add_argument("--from-frame", type=int, default=None,
                        help="only count samples at or after this emulated frame "
                             "(e.g. 9000 for this save's field)")
    parser.add_argument("--sd-root", type=Path, default=DEFAULT_SD)
    parser.add_argument("--azahar", type=Path, default=AZAHAR)
    parser.add_argument("--label", default="ab")
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args(argv)

    for label, path in (("artifact-a", args.artifact_a), ("artifact-b", args.artifact_b),
                        ("save", args.save), ("script", args.script), ("ini", args.ini)):
        if not path.expanduser().resolve().is_file():
            print(f"error: no {label} at {path}", file=sys.stderr)
            return 1
    if not args.azahar.is_file():
        print(f"error: no Azahar at {args.azahar}", file=sys.stderr)
        return 1

    args.artifact_a = args.artifact_a.expanduser().resolve()
    args.artifact_b = args.artifact_b.expanduser().resolve()
    args.save = args.save.expanduser().resolve()
    args.script = args.script.expanduser().resolve()
    args.ini = args.ini.expanduser().resolve()
    args.sd_root = args.sd_root.expanduser().resolve()

    out = (args.out or REPO / "build" / "bench" / f"ab-{args.label}").expanduser().resolve()
    out.mkdir(parents=True, exist_ok=True)

    # Identity: the two artifacts and the inputs must be the only variables.
    manifest = out / "manifest.json"
    subprocess.run([sys.executable, str(REPO / "tools" / "bench_manifest.py"),
                    "--game", args.game, "--artifact", str(args.artifact_a),
                    "--save", str(args.save), "--script", str(args.script),
                    "--ini", str(args.ini), "--label", args.label,
                    "--model", "Azahar (Old 3DS mode)", "-o", str(manifest)],
                   check=False)
    identity = json.loads(manifest.read_text(encoding="utf-8"))

    runs: dict[str, list[Path]] = {args.name_a: [], args.name_b: []}
    order = [(args.artifact_a, args.name_a), (args.artifact_b, args.name_b)]
    for trial in range(1, args.trials + 1):
        for artifact, name in order:
            print(f"== {name} trial {trial}", flush=True)
            log = run_trial(args, artifact, name, trial, out)
            if log is not None:
                runs[name].append(log)
            else:
                print(f"   warning: {name} trial {trial} produced no log")

    print()
    print(f"identity: {identity['artifact']['sha256'][:16]} (A) vs "
          f"sha256 of B: {_sha(args.artifact_b)[:16]}")
    print(f"trials: {args.trials} each, {args.seconds:.0f}s per trial, "
          f"warm-up samples skipped: {args.skip}, "
          f"frame window: {args.from_frame if args.from_frame is not None else 'all'}..")
    print()
    table: dict[str, dict[str, float]] = {}
    for name in (args.name_a, args.name_b):
        per_trial = [metrics(log, args.skip, args.from_frame) for log in runs[name]]
        per_trial = [m for m in per_trial if m]
        table[name] = {key: statistics.median(m[key] for m in per_trial)
                       for key in per_trial[0]} if per_trial else {}

    keys = ("frame_ms", "composes_s", "publishes_s", "drawer_ms", "geom_ms")
    lower_is_better = {"frame_ms", "drawer_ms", "geom_ms"}
    print(f"| metric | {args.name_a} | {args.name_b} | verdict |")
    print("|---|---|---|---|")
    for key in keys:
        a = table[args.name_a].get(key)
        b = table[args.name_b].get(key)
        a_lo = table[args.name_a].get(key + "_min")
        a_hi = table[args.name_a].get(key + "_max")
        b_lo = table[args.name_b].get(key + "_min")
        b_hi = table[args.name_b].get(key + "_max")
        verdict = "no data"
        if None not in (a, b, a_lo, a_hi, b_lo, b_hi):
            if not (a_hi < b_lo or b_hi < a_lo):
                verdict = "inconclusive (ranges overlap)"
            else:
                better = args.name_a if ((a < b) == (key in lower_is_better)) else args.name_b
                verdict = f"{better} better"
        print(f"| {key} | {_fmt(a)} [{_fmt(a_lo)}..{_fmt(a_hi)}] | "
              f"{_fmt(b)} [{_fmt(b_lo)}..{_fmt(b_hi)}] | {verdict} |")

    (out / "summary.json").write_text(json.dumps(
        {"identity": identity, "trials": args.trials, "seconds": args.seconds,
         "skip_samples": args.skip, "results": table},
        indent=2) + "\n", encoding="utf-8")
    print()
    print(f"logs and summary: {out}")
    print("Reminder: emulator timing is not Old 3DS hardware time; an "
          "overlapping range is an inconclusive result, not a speedup.")
    return 0


def _sha(path: Path) -> str:
    import hashlib
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _fmt(value: float | None) -> str:
    return "-" if value is None else f"{value:.2f}"


if __name__ == "__main__":
    raise SystemExit(main())
