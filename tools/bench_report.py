#!/usr/bin/env python3
"""Turn a port log.txt into the per-sample benchmark table the guides require.

Usage:
    python tools/bench_report.py log.txt
    python tools/bench_report.py log.txt --manifest run.manifest.json
    python tools/bench_report.py log.txt --from-frame 1200 --to-frame 3600 --json
    python tools/bench_report.py log.txt --check-manifest run.manifest.json

The 3DS shell writes one sample every 600 frames (`frame N`, `FRAMETIME ...`,
one `[perf]` line per domain, `3D images: ...`). This tool groups those
blocks and prints them as rows, then a summary with median/p95/min/max over
the selected frame window. It does not change the game or the log format; it
only reads them.

`--check-manifest` re-hashes the files a manifest names and refuses (exit 1)
when they no longer match, so a result is never attributed to a stale
artifact, save or script. With `--manifest` and no check, the identity header
is printed so the report is self-describing.

Caveats printed with every report (from PICA200-INSTRUMENTATION.md):
- one sample is a 600-frame aggregate, not a frame-time distribution;
- `total_us` is entry-to-entry wall time (includes preemption and waits), not
  CPU execution time;
- a software run cannot stand in for Old 3DS hardware time.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import statistics
import sys
from pathlib import Path

SAMPLE_START = re.compile(r"^\[?(?:\d+\] )?frame (\d+)$")
FRAMETIME = re.compile(
    r"FRAMETIME frame=(\d+) game_us=(\d+) state_us=(\d+) total_us=(\d+) "
    r"hblank_us=(\d+) handoff_us=(\d+) hb_keep_us=(\d+) hb_dma_us=(\d+) "
    r"hb_irq_us=(\d+)")
DISPLAY = re.compile(
    r"\[perf\] display: ([\d.]+) composes/s, ([\d.]+) ms build, "
    r"([\d.]+) ms GPU/waited \(([\d.]+) ms waiting\), ([\d.]+) publishes/s, "
    r"([\d.]+) ms publish, (\d+) late, (\d+) dropped, lag (\d+), stereo (\d+), "
    r"smooth (\d+); scene build ([\d.]+) ms, GPU ([\d.]+) ms, "
    r"handover ([\d.]+) ms; (\d+) textures converted \((\d+) texels, "
    r"([\d.]+) ms\), command list (\d+)% full, (\d+) draws cut")
GEOMETRY = re.compile(
    r"\[perf\] geometry: ([\d.]+) scenes/s, busy (\d+)% \(([\d.]+) ms/scene\), "
    r"(\d+) skipped")
DRAWER = re.compile(
    r"\[perf\] drawer (\d+) \(core (\d+), ([^)]*)\): ([\d.]+) frames/s, "
    r"([\d.]+) ms/frame, ([\d.]+) ms render, ([\d.]+) ms publish, "
    r"(\d+) drawn, (\d+) lower deferred")
GPUBG = re.compile(r"\[perf\] gpubg: taken (\d+),")
MEM = re.compile(
    r"\[perf\] mem: linear_free=(\d+) KB game_heap_used=(\d+) KB")
MEM_LOW = re.compile(r"\[perf\] mem: linear low-water (\d+) KB")
AUDIO = re.compile(
    r"\[perf\] audio: ([\d.]+)% of core (-?\d+) \(([\d.]+) ms mixing in "
    r"([\d.]+) s\), (\d+) dropped of (\d+)")
LOOP = re.compile(
    r"\[perf\] loop: (\d+) passes \((\d+) draws, (\d+) nonew, (\d+) wait2d, "
    r"(\d+) midlate, (\d+) nobuf\), (\d+) wakes; need upper (\d+) lower (\d+); "
    r"(\d+) vblanks; scene-draws (\d+) serials-skipped (\d+)")
IMAGES = re.compile(r"3D images: (\d+) \(scenes drawn: (\d+)\)")


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_log(text: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    current: dict[str, object] | None = None
    drawers: dict[int, dict[str, float]] = {}
    for raw in text.splitlines():
        line = raw.strip()
        start = SAMPLE_START.match(line)
        if start:
            current = {"frame": int(start.group(1)), "drawers": {}}
            rows.append(current)
            continue
        if current is None:
            # A sample block can be preceded by boot lines; still collect the
            # first one if a FRAMETIME line appears without an opener.
            if FRAMETIME.search(line):
                current = {"frame": int(FRAMETIME.search(line).group(1)),
                           "drawers": {}}
                rows.append(current)
            else:
                continue
        match = FRAMETIME.search(line)
        if match:
            keys = ("frame", "game_us", "state_us", "total_us", "hblank_us",
                    "handoff_us", "hb_keep_us", "hb_dma_us", "hb_irq_us")
            for key, value in zip(keys, match.groups()):
                current[key] = int(value)
            continue
        match = DISPLAY.search(line)
        if match:
            for key, value in zip(
                    ("composes_s", "build_ms", "gpu_waited_ms", "waiting_ms",
                     "publishes_s", "publish_ms", "late", "dropped", "lag",
                     "stereo", "smooth", "scene_build_ms", "gpu_ms",
                     "handover_ms", "tex_converted", "texels", "tex_ms",
                     "cmd_pct", "draws_cut"), match.groups()):
                current[key] = float(value) if "." in value else int(value)
            continue
        match = GEOMETRY.search(line)
        if match:
            current["geom_scenes_s"] = float(match.group(1))
            current["geom_busy_pct"] = int(match.group(2))
            current["geom_ms"] = float(match.group(3))
            current["geom_skipped"] = int(match.group(4))
            continue
        match = DRAWER.search(line)
        if match:
            index = int(match.group(1))
            current["drawers"][index] = {
                "core": int(match.group(2)),
                "fps": float(match.group(4)),
                "ms": float(match.group(5)),
                "render_ms": float(match.group(6)),
                "publish_ms": float(match.group(7)),
                "drawn": int(match.group(8)),
            }
            continue
        match = GPUBG.search(line)
        if match:
            current["gpubg_taken"] = int(match.group(1))
            continue
        match = MEM.search(line)
        if match:
            current["linear_free_kb"] = int(match.group(1))
            current["heap_used_kb"] = int(match.group(2))
            continue
        match = MEM_LOW.search(line)
        if match:
            current["linear_low_kb"] = int(match.group(1))
            continue
        match = AUDIO.search(line)
        if match:
            current["audio_dropped"] = int(match.group(5))
            current["audio_total"] = int(match.group(6))
            continue
        match = LOOP.search(line)
        if match:
            current["draws"] = int(match.group(2))
            current["vblanks"] = int(match.group(10))
            current["scene_draws"] = int(match.group(11))
            continue
        match = IMAGES.search(line)
        if match:
            current["gpu_images"] = int(match.group(1))
            current["gpu_scenes"] = int(match.group(2))
    return [row for row in rows if "total_us" in row]


def interval_fps(row: dict[str, object]) -> float | None:
    """Unused by the table; kept for callers that want the raw inverse."""
    total = row.get("total_us")
    if isinstance(total, int) and total > 0:
        return 1_000_000.0 / total
    return None


def drawer_worst_ms(row: dict[str, object]) -> float | None:
    drawers = row.get("drawers") or {}
    values = [float(entry["ms"]) for entry in drawers.values()]  # type: ignore[index]
    return max(values) if values else None


COLUMNS = (
    ("frame", "frame"),
    ("frame_ms", "frame ms"),
    ("game_ms", "game ms"),
    ("state_ms", "state ms"),
    ("composes_s", "comp/s"),
    ("publishes_s", "pub/s"),
    ("drawer_ms", "drawer ms"),
    ("geom_ms", "geom ms"),
    ("gpubg_taken", "bg takes"),
    ("linear_free_kb", "lin free KB"),
    ("dropped", "disp drop"),
)


def row_values(row: dict[str, object]) -> dict[str, object]:
    total_us = row.get("total_us")
    values: dict[str, object] = {
        "frame": row.get("frame"),
        # The entry-to-entry interval at the sampled frame, in emulated
        # system ticks. Not a rate: in an emulator the emulated clock can
        # outrun wall time, so only hardware runs give a true frame rate.
        "frame_ms": round(int(total_us) / 1000.0, 2) if isinstance(total_us, int) else None,
        "game_ms": round(int(row.get("game_us", 0)) / 1000.0, 2),
        "state_ms": round(int(row.get("state_us", 0)) / 1000.0, 2),
        "composes_s": row.get("composes_s"),
        "publishes_s": row.get("publishes_s"),
        "drawer_ms": drawer_worst_ms(row),
        "geom_ms": row.get("geom_ms"),
        "gpubg_taken": row.get("gpubg_taken"),
        "linear_free_kb": row.get("linear_free_kb"),
        "dropped": row.get("dropped"),
    }
    if values["drawer_ms"] is not None:
        values["drawer_ms"] = round(float(values["drawer_ms"]), 2)
    return values


def summary(rows: list[dict[str, object]]) -> list[tuple[str, float, float, float, float]]:
    metrics: list[tuple[str, list[float]]] = []
    for key in ("frame_ms", "game_ms", "composes_s", "publishes_s", "drawer_ms",
                "gpubg_taken", "linear_free_kb"):
        values = [float(value) for row in rows
                  for value in [row_values(row).get(key)] if value is not None]
        if values:
            metrics.append((key, values))
    out = []
    for key, values in metrics:
        values.sort()
        p95 = values[min(len(values) - 1, int(round(0.95 * (len(values) - 1))))]
        out.append((key, statistics.median(values), p95, values[0], values[-1]))
    return out


def print_markdown(rows: list[dict[str, object]], header: dict[str, object] | None) -> None:
    if header:
        print(f"run: {header.get('label')} game={header.get('game')} "
              f"model={header.get('model')} commit={header.get('commit')}")
    print()
    print("| " + " | ".join(label for _, label in COLUMNS) + " |")
    print("|" + "|".join("---" for _ in COLUMNS) + "|")
    for row in rows:
        values = row_values(row)
        cells = []
        for key, _ in COLUMNS:
            value = values.get(key)
            cells.append("-" if value is None else str(value))
        print("| " + " | ".join(cells) + " |")
    print()
    print("| metric | median | p95 | min | max |")
    print("|---|---|---|---|---|")
    for key, median, p95, low, high in summary(rows):
        print(f"| {key} | {median:.2f} | {p95:.2f} | {low:.2f} | {high:.2f} |")


def check_manifest(manifest: dict[str, object]) -> int:
    """Re-hash every file the manifest names; report drift."""
    problems = 0
    for key in ("artifact", "save", "script", "ini"):
        entry = manifest.get(key)
        if not entry:
            continue
        path = Path(str(entry["path"]))
        if not path.is_file():
            print(f"MISMATCH {key}: {path} is gone", file=sys.stderr)
            problems += 1
            continue
        digest = sha256_of(path)
        if digest != entry.get("sha256"):
            print(f"MISMATCH {key}: {path.name} hash changed", file=sys.stderr)
            problems += 1
        else:
            print(f"ok {key}: {path.name} {digest[:16]}...")
    return 0 if problems == 0 else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Summarise a port log.txt as a benchmark table.")
    parser.add_argument("log", type=Path)
    parser.add_argument("--manifest", type=Path, default=None)
    parser.add_argument("--check-manifest", type=Path, default=None,
                        help="verify the manifest's files still match, then exit")
    parser.add_argument("--from-frame", type=int, default=None)
    parser.add_argument("--to-frame", type=int, default=None)
    parser.add_argument("--json", action="store_true", help="emit JSON instead of a table")
    args = parser.parse_args(argv)

    if args.check_manifest is not None:
        manifest = json.loads(args.check_manifest.read_text(encoding="utf-8"))
        return check_manifest(manifest)

    header = None
    if args.manifest is not None:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        repo = manifest.get("repo", {})
        header = {
            "label": manifest.get("label"),
            "game": manifest.get("game"),
            "model": manifest.get("model"),
            "commit": f"{repo.get('short', '')}"
                      f"{'-dirty' if repo.get('dirty') else ''}",
            "artifact": (manifest.get("artifact") or {}).get("sha256", "")[:16],
            "save": (manifest.get("save") or {}).get("sha256", "")[:16],
            "script": (manifest.get("script") or {}).get("sha256", "")[:16],
        }

    if not args.log.is_file():
        print(f"error: no log at {args.log}", file=sys.stderr)
        return 1
    rows = parse_log(args.log.read_text(encoding="utf-8", errors="replace"))
    if args.from_frame is not None:
        rows = [row for row in rows if int(row["frame"]) >= args.from_frame]
    if args.to_frame is not None:
        rows = [row for row in rows if int(row["frame"]) <= args.to_frame]
    if not rows:
        print("error: no 600-frame samples in this log (or none in the window)",
              file=sys.stderr)
        return 1

    if args.json:
        payload = {
            "manifest": header,
            "samples": [row_values(row) for row in rows],
            "summary": [
                {"metric": key, "median": median, "p95": p95, "min": low, "max": high}
                for key, median, p95, low, high in summary(rows)
            ],
            "caveats": [
                "one sample is a 600-frame aggregate, not a frame-time distribution",
                "total_us is entry-to-entry wall time, not CPU execution time",
                "an emulator run is not Old 3DS hardware time",
            ],
        }
        print(json.dumps(payload, indent=2))
        return 0

    print_markdown(rows, header)
    print()
    print("Caveats: one sample is a 600-frame aggregate (not a distribution); "
          "`frame ms` is the entry-to-entry interval in emulated ticks at that "
          "sample (in an emulator the tick clock can outrun wall time); an "
          "emulator run is not Old 3DS hardware time.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
