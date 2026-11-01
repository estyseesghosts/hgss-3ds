#!/usr/bin/env python3
"""Golden diff runner for the PC build (Phase 4, step 3).

Replays a scene script through the PC runner, compares both screens against a
reference capture (the user's melonDS frames, or an earlier PC run), aligns on
the first matching frame and writes a JSON report, an HTML summary and a PNG
heat map per differing frame under the output directory.

Scene format (``tests/inputs/<scene>.txt``, documented in ``capture.lua``)::

    every: 10          # capture/compare every N frames
    end: 7000          # last frame
    0 -                # no input at frame 0
    6550 START         # hold START for 6 frames from 6550
    8050 touch 128 176 # touch the lower screen at (128, 176)

The PC runner writes ``frame_%07d.png`` (256x384: upper screen above lower).
melonDS captures are named ``<frame>.png``; both are accepted.

Thresholds: 2D frames must match exactly (``--tolerance 0``); 3D frames
(the opening movie and title) get a per-pixel channel tolerance
(``--3d-tolerance``, default 8) up to ``--3d-until`` (default 6600). The
values are recorded in the JSON report and in docs/decisions.md.

Usage:
  python3 tools/golden/run.py --scene newgame-buttons
  python3 tools/golden/run.py --scene boot-title --reference tests/golden/boot-title
  python3 tools/golden/run.py --scene newgame-buttons --no-run --run /work/shots \
      --reference /work/previous-run
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib
from dataclasses import dataclass, field
from html import escape
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

PRESET_3D_TOLERANCE = 8
PRESET_3D_UNTIL = 6600
ALIGN_WINDOW = 240


# --------------------------------------------------------------------- PNG

def _chunks(data: bytes):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8
    while pos + 8 <= len(data):
        length, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + length]
        yield kind, body
        pos += 12 + length


def read_png(path: Path) -> tuple[int, int, list[bytearray]]:
    """Decode an 8-bit RGB/RGBA/gray PNG into rows of RGB bytes."""
    chunks = list(_chunks(Path(path).read_bytes()))
    header = next(body for kind, body in chunks if kind == b"IHDR")
    width, height, depth, color, compression, filter_method, interlace = struct.unpack(
        ">IIBBBBB", header)
    if depth != 8 or interlace != 0 or compression != 0 or filter_method != 0:
        raise ValueError(f"{path}: unsupported PNG (depth {depth}, interlace {interlace})")
    channels = {0: 1, 2: 3, 6: 4}.get(color)
    if channels is None:
        raise ValueError(f"{path}: unsupported colour type {color}")
    raw = zlib.decompress(b"".join(body for kind, body in chunks if kind == b"IDAT"))
    stride = width * channels
    rows: list[bytearray] = []
    previous = bytearray(stride)
    pos = 0
    for _ in range(height):
        if pos >= len(raw):
            raise ValueError(f"{path}: truncated image data")
        kind = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if kind == 0:
            pass
        elif kind == 1:                      # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif kind == 2:                      # Up
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif kind == 3:                      # Average
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif kind == 4:                      # Paeth
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                up = previous[i]
                up_left = previous[i - channels] if i >= channels else 0
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                predictor = left if (pa <= pb and pa <= pc) else (up if pb <= pc else up_left)
                line[i] = (line[i] + predictor) & 0xFF
        else:
            raise ValueError(f"{path}: bad PNG filter {kind}")
        previous = line
        if channels == 4:
            rgb = bytearray(width * 3)
            for x in range(width):
                rgb[3 * x:3 * x + 3] = line[4 * x:4 * x + 3]
            rows.append(rgb)
        elif channels == 1:
            rgb = bytearray(width * 3)
            for x in range(width):
                value = line[x]
                rgb[3 * x] = rgb[3 * x + 1] = rgb[3 * x + 2] = value
            rows.append(rgb)
        else:
            rows.append(line)
    return width, height, rows


def write_png(path: Path, width: int, height: int, rows: list[bytearray]) -> None:
    """Write an 8-bit RGB PNG (filter 0)."""
    raw = bytearray()
    for row in rows:
        raw.append(0)
        raw += row
    def chunk(kind: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))
    Path(path).write_bytes(data)


# ------------------------------------------------------------------- scene

@dataclass
class Scene:
    name: str
    every: int = 1
    end: int | None = None
    presses: list[tuple[int, str]] = field(default_factory=list)
    touches: list[tuple[int, int, int]] = field(default_factory=list)

    def press_spec(self) -> str:
        return ",".join(f"{frame}:{buttons}" for frame, buttons in self.presses)

    def touch_spec(self) -> str:
        return ",".join(f"{frame}:{x}:{y}" for frame, x, y in self.touches)


def parse_scene(path: Path) -> Scene:
    scene = Scene(name=Path(path).stem)
    for number, raw in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("every:"):
            scene.every = int(line.split(":", 1)[1])
            continue
        if line.startswith("end:"):
            scene.end = int(line.split(":", 1)[1])
            continue
        parts = line.split()
        if len(parts) < 2:
            raise ValueError(f"{path}:{number}: expected FRAME INPUT")
        frame = int(parts[0])
        if parts[1] == "-":
            continue
        if parts[1] == "touch":
            if len(parts) != 4:
                raise ValueError(f"{path}:{number}: touch needs FRAME X Y")
            scene.touches.append((frame, int(parts[2]), int(parts[3])))
        else:
            scene.presses.append((frame, parts[1]))
    if scene.end is None:
        raise ValueError(f"{path}: no end: line")
    if scene.every <= 0:
        raise ValueError(f"{path}: every must be positive")
    return scene


# ------------------------------------------------------------------- frames

FRAME_NAME = re.compile(r"^(?:frame_)?(\d+)\.png$")


def load_frames(directory: Path) -> tuple[int, int, dict[int, Path], list[str]]:
    """Map frame number -> PNG path; return (width, height, frames, warnings)."""
    frames: dict[int, Path] = {}
    warnings: list[str] = []
    for path in sorted(Path(directory).glob("*.png")):
        match = FRAME_NAME.match(path.name)
        if match is None:
            warnings.append(f"ignored {path.name}")
            continue
        frames[int(match.group(1))] = path
    if not frames:
        raise ValueError(f"{directory}: no frame PNGs")
    first = frames[min(frames)]
    width, height, _ = read_png(first)
    return width, height, frames, warnings


def pixel_diff(a: list[bytearray], b: list[bytearray], tolerance: int) -> tuple[int, int]:
    """(differing pixels, max channel delta) between two RGB images."""
    differing = 0
    maximum = 0
    for row_a, row_b in zip(a, b):
        for i in range(0, len(row_a), 3):
            delta = max(abs(row_a[i] - row_b[i]),
                        abs(row_a[i + 1] - row_b[i + 1]),
                        abs(row_a[i + 2] - row_b[i + 2]))
            if delta > maximum:
                maximum = delta
            if delta > tolerance:
                differing += 1
    return differing, maximum


def allowed_tolerance(frame: int, tolerance: int, scene_3d_until: int, tolerance_3d: int) -> int:
    return tolerance_3d if frame <= scene_3d_until else tolerance


def align(run: dict[int, Path], reference: dict[int, Path], *, scene_3d_until: int,
          tolerance: int, tolerance_3d: int, window: int = ALIGN_WINDOW) -> tuple[int, int | None]:
    """Find the frame offset with the longest run of matching frames.

    Returns (offset, first matching run frame). References are indexed by
    their own frame number; run frame F matches reference frame F + offset.
    """
    cached_run: dict[int, list[bytearray]] = {}
    cached_ref: dict[int, list[bytearray]] = {}

    def rows(cache, source, frame):
        if frame not in cache:
            cache[frame] = read_png(source[frame])[2]
        return cache[frame]

    best = (0, None, -1, 0)     # (offset, first, run length, -|offset|)
    for offset in range(-window, window + 1):
        first = None
        length = 0
        for frame in sorted(run):
            ref_frame = frame + offset
            if ref_frame not in reference:
                if first is not None:
                    break
                continue
            tol = allowed_tolerance(frame, tolerance, scene_3d_until, tolerance_3d)
            differing, _ = pixel_diff(rows(cached_run, run, frame),
                                      rows(cached_ref, reference, ref_frame), tol)
            if differing == 0:
                if first is None:
                    first = frame
                length += 1
            elif first is not None:
                break
        candidate = (offset, first, length, -abs(offset))
        if candidate[2] > best[2] or (candidate[2] == best[2] and candidate[3] > best[3]):
            best = candidate
    return best[0], best[1]


def heat_map(path: Path, a: list[bytearray], b: list[bytearray], tolerance: int) -> None:
    """Red where the pixels differ beyond tolerance, dim gray otherwise."""
    rows = []
    for row_a, row_b in zip(a, b):
        out = bytearray(len(row_a))
        for i in range(0, len(row_a), 3):
            delta = max(abs(row_a[i] - row_b[i]), abs(row_a[i + 1] - row_b[i + 1]),
                        abs(row_a[i + 2] - row_b[i + 2]))
            if delta > tolerance:
                value = min(255, 64 + delta * 2)
                out[i], out[i + 1], out[i + 2] = value, 0, 0
            else:
                out[i] = out[i + 1] = out[i + 2] = 0
        rows.append(out)
    write_png(path, len(a[0]) // 3, len(a), rows)


# ------------------------------------------------------------------- report

def write_report(out: Path, scene: Scene, run_dir: Path, reference_dir: Path | None,
                 width: int, height: int, results: list[dict], alignment: dict,
                 tolerance: int, tolerance_3d: int, scene_3d_until: int,
                 note: str | None = None) -> dict:
    report = {
        "scene": scene.name,
        "run": str(run_dir),
        "reference": str(reference_dir) if reference_dir is not None else None,
        "image": {"width": width, "height": height},
        "alignment": alignment,
        "tolerance": {"2d": tolerance, "3d": tolerance_3d, "3d_until": scene_3d_until},
        "frames": results,
        "summary": {
            "frames": len(results),
            "compared": sum(1 for r in results if r["status"] in ("pass", "fail")),
            "passed": sum(1 for r in results if r["status"] == "pass"),
            "failed": sum(1 for r in results if r["status"] == "fail"),
            "reference_missing": sum(1 for r in results if r["status"] == "no_reference"),
        },
    }
    if note:
        report["note"] = note
    report["pass"] = report["summary"]["failed"] == 0
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    rows = []
    for result in results:
        link = (f'<a href="{escape(result["heat_map"])}">heat map</a>'
                if result.get("heat_map") else "")
        rows.append(
            "<tr class=\"{status}\"><td>{frame}</td><td>{differing}</td>"
            "<td>{maximum}</td><td>{allowed}</td><td>{status}</td><td>{link}</td></tr>".format(
                status=result["status"], frame=result["frame"], differing=result["differing"],
                maximum=result["max_delta"], allowed=result["allowed"], link=link))
    summary = report["summary"]
    html = f"""<!doctype html>
<meta charset="utf-8">
<title>golden diff: {escape(scene.name)}</title>
<style>
body {{ font: 14px/1.4 system-ui, sans-serif; margin: 2rem; color: #222; }}
table {{ border-collapse: collapse; }} td, th {{ padding: .2rem .6rem; border-bottom: 1px solid #ddd; }}
tr.fail td {{ background: #fee; }} tr.pass td {{ background: #efe; }}
</style>
<h1>golden diff: {escape(scene.name)}</h1>
<p>{summary['passed']} passed, {summary['failed']} failed of {summary['frames']} frames.
Alignment offset <b>{alignment.get('offset')}</b>
(first match frame {alignment.get('first_match')}).<br>
2D tolerance {tolerance}, 3D tolerance {tolerance_3d} up to frame {scene_3d_until}.</p>
<table><thead><tr><th>frame</th><th>differing px</th><th>max delta</th><th>allowed</th>
<th>status</th><th>heat map</th></tr></thead><tbody>
{chr(10).join(rows)}
</tbody></table>
"""
    (out / "report.html").write_text(html, encoding="utf-8")
    return report


# --------------------------------------------------------------------- run

def run_pc(args, scene: Scene, run_dir: Path, events_path: Path) -> int:
    run_dir.mkdir(parents=True, exist_ok=True)
    command = [str(args.pc), str(args.pak), str(args.manifest),
               "--save", str(args.out / "heartgold.sav"),
               "--frames", str(scene.end), "--stop-at-missing-file",
               "--stall-seconds", str(args.stall_seconds),
               "--screenshots", str(run_dir), "--screenshot-every", str(scene.every),
               "--events", str(events_path)]
    if scene.presses:
        command += ["--press", scene.press_spec()]
    if scene.touches:
        command += ["--touch", scene.touch_spec()]
    print("+ " + " ".join(command), flush=True)
    return subprocess.run(command).returncode


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scene", required=True, help="scene name under tests/inputs")
    parser.add_argument("--scene-file", type=Path, help="explicit scene script path")
    parser.add_argument("--reference", type=Path, help="reference frame directory")
    parser.add_argument("--run", type=Path, help="run frame directory (default: OUT/run)")
    parser.add_argument("--no-run", action="store_true", help="compare --run without running the PC")
    parser.add_argument("--pc", type=Path, default=REPO / "build" / "pokeheartgold-pc")
    parser.add_argument("--pak", type=Path, default=Path("/work/heartgold3ds.pak"))
    parser.add_argument("--manifest", type=Path, default=Path("/work/hg-data/manifest.json"))
    parser.add_argument("--out", type=Path, help="output directory (default: build/golden/SCENE)")
    parser.add_argument("--tolerance", type=int, default=0, help="2D per-channel tolerance (default 0)")
    parser.add_argument("--3d-tolerance", dest="tolerance_3d", type=int,
                        default=PRESET_3D_TOLERANCE)
    parser.add_argument("--3d-until", dest="scene_3d_until", type=int,
                        default=PRESET_3D_UNTIL,
                        help="frames at or before this use the 3D tolerance")
    parser.add_argument("--align-window", type=int, default=ALIGN_WINDOW)
    parser.add_argument("--stall-seconds", type=int, default=5)
    args = parser.parse_args(argv)

    scene_path = args.scene_file or (REPO / "tests" / "inputs" / f"{args.scene}.txt")
    if not scene_path.is_file():
        print(f"run.py: no scene {scene_path}", file=sys.stderr)
        return 2
    try:
        scene = parse_scene(scene_path)
    except ValueError as exc:
        print(f"run.py: {exc}", file=sys.stderr)
        return 2

    out = args.out or REPO / "build" / "golden" / scene.name
    out.mkdir(parents=True, exist_ok=True)
    run_dir = args.run or out / "run"
    reference_dir = args.reference
    if reference_dir is None:
        candidate = REPO / "tests" / "golden" / scene.name
        if candidate.is_dir() and any(candidate.glob("*.png")):
            reference_dir = candidate

    events_path = out / "events.jsonl"
    if not args.no_run:
        if not args.pc.exists():
            print(f"run.py: no PC binary at {args.pc}", file=sys.stderr)
            return 2
        if not args.pak.exists() or not args.manifest.exists():
            print(f"run.py: missing pack/manifest ({args.pak}, {args.manifest})", file=sys.stderr)
            return 2
        if run_dir.exists():
            shutil.rmtree(run_dir)
        code = run_pc(args, scene, run_dir, events_path)
        if code not in (0,):
            print(f"run.py: the runner exited {code} (see {events_path})", file=sys.stderr)
    elif not run_dir.is_dir():
        print(f"run.py: --no-run needs an existing --run directory ({run_dir})", file=sys.stderr)
        return 2

    try:
        width, height, run_frames, run_warnings = load_frames(run_dir)
    except (ValueError, OSError) as exc:
        print(f"run.py: {exc}", file=sys.stderr)
        return 2
    for warning in run_warnings:
        print(f"run.py: {warning}", file=sys.stderr)

    reference_frames = {}
    ref_width = ref_height = None
    if reference_dir is not None:
        try:
            ref_width, ref_height, reference_frames, ref_warnings = load_frames(reference_dir)
        except (ValueError, OSError) as exc:
            print(f"run.py: {exc}", file=sys.stderr)
            return 2
        for warning in ref_warnings:
            print(f"run.py: {warning}", file=sys.stderr)
        if (ref_width, ref_height) != (width, height):
            print(f"run.py: image size {width}x{height} != reference {ref_width}x{ref_height}",
                  file=sys.stderr)
            return 2

    if reference_frames:
        offset, first = align(run_frames, reference_frames, scene_3d_until=args.scene_3d_until,
                              tolerance=args.tolerance, tolerance_3d=args.tolerance_3d,
                              window=args.align_window)
    else:
        offset, first = 0, None
    alignment = {"offset": offset, "first_match": first}

    heat_dir = out / "heat"
    results = []
    for frame in sorted(run_frames):
        ref_frame = frame + offset if reference_frames else None
        entry = {"frame": frame, "reference_frame": ref_frame, "differing": 0,
                 "max_delta": 0, "allowed": 0, "status": "no_reference"}
        if reference_frames and ref_frame in reference_frames:
            allowed = allowed_tolerance(frame, args.tolerance, args.scene_3d_until,
                                        args.tolerance_3d)
            a = read_png(run_frames[frame])[2]
            b = read_png(reference_frames[ref_frame])[2]
            differing, maximum = pixel_diff(a, b, allowed)
            entry.update(differing=differing, max_delta=maximum, allowed=allowed,
                         status="pass" if differing == 0 else "fail")
            if differing:
                heat_dir.mkdir(exist_ok=True)
                name = f"heat/frame_{frame:07d}.png"
                heat_map(out / name, a, b, allowed)
                entry["heat_map"] = name
        elif reference_frames:
            entry["status"] = "no_reference"
        results.append(entry)

    note = None
    if not reference_frames:
        note = ("no reference frames: captured a regression baseline only. "
                "Golden comparison is pending the user's melonDS capture.")
    report = write_report(out, scene, run_dir, reference_dir, width, height, results,
                          alignment, args.tolerance, args.tolerance_3d,
                          args.scene_3d_until, note)
    print(f"run.py: {report['summary']['passed']} passed, "
          f"{report['summary']['failed']} failed, {len(results)} frames; {out}/report.json")
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
