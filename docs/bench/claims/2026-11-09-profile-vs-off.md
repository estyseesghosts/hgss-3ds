# Claim sheet: instrumentation overhead (PROFILE vs OFF)

**Date:** 2026-11-09 · **Status:** measured in Azahar — no detectable overhead;
hardware check pending

```text
Hypothesis:               The always-on instrumentation (26 svcGetSystemTick
                          reads per engine draw in port_render2d.c, plus the
                          geometry diagnostics) costs a measurable fraction of
                          the Old-3DS frame budget, so timings quoted from a
                          PROFILE build are not ground truth.
Target scene:             HeartGold save (AAR, 2 badges, Goldenrod), frozen
                          script hg-continue-field.txt, samples from frame
                          9000 on (warm-up samples dropped)
Target hardware:          Azahar 2126.2, Old-3DS mode, 268 MHz profile,
                          window focused; then Old 3DS hardware
Baseline artifact:        PROFILE sha256 c949cd23b29f58d0... (5,834,596 B)
Candidate artifact:       OFF sha256 8d380c8b29d1497f... (5,831,892 B),
                          build banner 295ddef-dirty-profoff
Activation proof:         libnitro.a nm: port_render2d.o `U svcGetSystemTick`
                          present in PROFILE, absent in OFF; runtime logs:
                          71 [gx3d] lines (PROFILE) vs 3 boot lines (OFF);
                          banners 295ddef-dirty vs 295ddef-dirty-profoff
Work proof:               OFF removes 26 tick reads per engine draw and the
                          geometry diagnostic formatting; counters that remain
                          are plain memory increments
Correctness proof:        Screenshots at the same scene: field, NPCs and MENU
                          identical; no rendering path changed
Hardware result:          not run
Conclusion:               Azahar: inconclusive by overlap of every metric —
                          medians within 0.3% (comp/s 20.90 vs 20.95, drawer
                          4.61 vs 4.62 ms, geometry 30.97 vs 31.02 ms,
                          publishes 60.00 both). Run-to-run spread is ~±3%,
                          so any overhead is below the emulator's noise.
                          This is a bound, not proof of zero cost.
Next decision:            keep PROFILE as the default (its counters are what
                          work proof needs); use the -profoff build for any
                          timing claim and re-run this same A/B on the Old 3DS
                          where the CPU budget is the real one
```

## Results (3 alternating trials each, 200 s per trial, frames 9000+)

| metric | PROFILE median [min..max] | OFF median [min..max] | verdict |
|---|---|---|---|
| frame_ms | 7.95 [7.91..8.09] | 7.99 [7.91..8.09] | ranges overlap |
| composes/s | 20.90 [20.40..21.50] | 20.95 [20.20..21.50] | ranges overlap |
| publishes/s | 60.00 [60.00..60.00] | 60.00 [60.00..60.00] | ranges overlap |
| drawer ms | 4.61 [4.61..4.62] | 4.62 [4.62..4.62] | ranges overlap |
| geometry ms | 30.97 [30.83..31.05] | 31.02 [30.85..31.10] | ranges overlap |

Logs, per-trial files and `summary.json`:
`build/bench/ab-goldenrod-profile-vs-off/`.

## Method

`tools/bench_ab.py` alternates the two artifacts on identical save, script and
ini, 3 trials each, re-focuses the Azahar window, drops warm-up samples and
reports median plus min/max per metric. Overlapping ranges are reported as
inconclusive.

## Why not just trust the counters

The 600-frame `[perf]` log is negligible; the tick reads and geometry logging
are not free, and the guide requires measuring the profiler rather than
assuming it (`PICA200-INSTRUMENTATION.md` §B). This sheet is that measurement.
