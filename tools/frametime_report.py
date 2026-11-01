#!/usr/bin/env python3
"""Turn the shells' frame-time lines into a budget table.

Both shells (once instrumented) write one line per sample:

    FRAMETIME frame=1234 logic_us=820 draw_us=9100 audio_us=0 total_us=16720

`total_us` is the wall time between two frame starts (16,720 us is 59.8 Hz);
the subsystem fields are the time spent inside that frame. The tool prints a
Markdown table plus the frame rate, and can emit JSON for a results archive.

Usage:
  python3 tools/frametime_report.py [LOG ...] [--json OUT] [--markdown OUT]
No log arguments reads standard input. Non-FRAMETIME lines are ignored, so a
`log.txt` from the console can be passed whole.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

LINE = re.compile(
    r"FRAMETIME\s+frame=(?P<frame>\d+)\s+"
    r"logic_us=(?P<logic>\d+)\s+"
    r"draw_us=(?P<draw>\d+)\s+"
    r"audio_us=(?P<audio>\d+)\s+"
    r"total_us=(?P<total>\d+)")

TARGET_HZ = 59.8


def parse(text: str) -> list[dict]:
    samples = []
    for line in text.splitlines():
        match = LINE.search(line)
        if match:
            samples.append({name: int(value) for name, value in match.groupdict().items()})
    return samples


def summary(samples: list[dict]) -> dict:
    if not samples:
        return {"samples": 0}
    fields = ("logic", "draw", "audio", "total")
    result = {"samples": len(samples), "target_hz": TARGET_HZ}
    for field in fields:
        values = [sample[field] for sample in samples]
        values_sorted = sorted(values)
        result[field] = {
            "mean_us": round(sum(values) / len(values), 1),
            "min_us": min(values),
            "max_us": max(values),
            "p95_us": values_sorted[min(len(values) - 1, int(len(values) * 0.95))],
        }
    result["hz"] = round(1_000_000 / result["total"]["mean_us"], 1) if result["total"]["mean_us"] else 0
    result["meets_target"] = result["hz"] >= TARGET_HZ
    result["frames_over_16_7ms"] = sum(1 for sample in samples if sample["total"] > 16720)
    return result


def markdown(result: dict) -> str:
    if result["samples"] == 0:
        return "No FRAMETIME samples.\n"
    rows = [
        "| subsystem | mean | min | p95 | max |",
        "| --- | ---: | ---: | ---: | ---: |",
    ]
    for field in ("logic", "draw", "audio", "total"):
        values = result[field]
        rows.append(f"| {field} (us) | {values['mean_us']} | {values['min_us']} | "
                    f"{values['p95_us']} | {values['max_us']} |")
    verdict = "meets" if result["meets_target"] else "misses"
    rows.append("")
    rows.append(f"{result['samples']} frames, mean {result['hz']} Hz ({verdict} the "
                f"{TARGET_HZ} Hz target); {result['frames_over_16_7ms']} frames over 16.7 ms.")
    return "\n".join(rows) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="*", type=Path,
                        help="shell logs (default: standard input)")
    parser.add_argument("--json", type=Path, help="write the summary JSON here")
    parser.add_argument("--markdown", type=Path, help="write the Markdown table here")
    args = parser.parse_args(argv)

    if args.logs:
        text = "\n".join(path.read_text(encoding="utf-8", errors="replace")
                         for path in args.logs)
    else:
        text = sys.stdin.read()
    samples = parse(text)
    result = summary(samples)
    table = markdown(result)
    print(table, end="")
    if args.json is not None:
        args.json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if args.markdown is not None:
        args.markdown.write_text(table, encoding="utf-8")
    return 0 if samples else 1


if __name__ == "__main__":
    raise SystemExit(main())
