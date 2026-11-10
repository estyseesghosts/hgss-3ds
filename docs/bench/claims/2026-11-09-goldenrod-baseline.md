# Claim sheet: Goldenrod field baseline (Packet A)

**Date:** 2026-11-09 · **Status:** baseline recorded; hardware run pending

```text
Hypothesis:               None yet (baseline only). The question this run
                          answers: which stage binds in the frozen scene?
Target scene:             HeartGold save (player AAR, 2 badges, Goldenrod
                          City), boot -> CONTINUE -> field, no further input;
                          frames 9000-18000
Target hardware:          Azahar 2126.2, Old-3DS mode (is_new_3ds=false,
                          268 MHz profile, smooth3d=0); NOT hardware evidence
Baseline artifact:        295ddef + uncommitted tools/ (build 295ddef-dirty);
                          pokeheartgold.3dsx sha256 c949cd23b29f58d0...
                          flags: docs/bench/port-baseline.ini
Candidate artifact:       none (baseline)
Activation proof:         n/a
Work proof:               per sample (600 frames): drawer 4.61 ms/frame,
                          geometry 30.8-31.2 ms/scene at 92-93% busy,
                          2 GPU BG takes/frame, 60 publishes/s, 21 composes/s
Correctness proof:        screenshots confirm the field and the MENU screen
Hardware result:          pending (this sheet is the Azahar control)
Regression surface:       n/a
Conclusion:               Azahar-O3DS field: the geometry worker is the only
                          stage near saturation (92% at ~31 ms/scene); the
                          drawer is cheap (4.6 ms). The title screen (same
                          build) is drawer bound (50+ ms, zero BG nominations)
Next decision:            keep as baseline; measure the same scene on Old 3DS
                          before choosing an optimization
```

## Measured per 120 scenes (Goldenrod, frames 9000-18000)

| Counter | Value | Per scene |
|---|---|---|
| input vertices | 1,683,600 | 14,030 |
| input polygons | 539,520 | 4,496 |
| output triangles | 113,637 | 947 |
| clipped | 482,241 | 4,019 |
| culled | 12,960 | 108 |
| hidden-list attempts accepted | 117,209 of 133,080 | 976 |
| vertices skipped by hidden lists | 1,090,801 | 9,090 |
| shadecache hits / misses | 4.70 M / 1.30 M | — |

Field: 30 scenes/s produced, 21 composed/s, publishes 60/s, `need upper` 202
per 600 VBlanks, `scene-draws` 177, `serials-skipped` 0, lag 4, 0 dropped.

Title (same session, frames 4200-14400): drawer 48-54 ms/frame at 16 fps,
composes 36-38/s, geometry 31 ms/scene, `gpubg taken 0` with **every reject
counter zero**. Cause: the title's 3D is on the **bottom** engine, so the top
engine's `split->has3d` is false and the nomination branch never runs; the
bottom engine has no GPU-BG path at all (`split.gpuBgs` is only set for
engine 0 / top). The top logo and the bottom's 2D therefore stay software.

## What this changes about the plan

- The user-visible problem is not one bottleneck. The **title/menus** are
  software-2D bound and the engine-A GPU-BG path is dormant there; the
  **field** already takes 2 GPU BGs per frame and its drawer is cheap
  (4.6 ms), while the G3 geometry worker is 92% busy at ~31 ms/scene.
- Packet C (retained BG) targets the title/menu cost. Packet E (G3
  display-list fast path) targets the field cost. The frozen Goldenrod scene
  that the guides name as the stress case is, in this build, an **E** case.
- Neither conclusion is a hardware claim. The Old 3DS packet in
  `build/bench/sd-hg/` must run first.

## Open needs

- Old 3DS run of `hg-continue-field` (idle) and `hg-field-walk` (moving).
- A battle save or a scripted route to grass, for Packet B (mirror bug).

## Instrumentation status of these numbers

These figures came from the PROFILE build. The matched PROFILE-vs-OFF A/B
([2026-11-09-profile-vs-off.md](2026-11-09-profile-vs-off.md)) found no
detectable overhead in Azahar (every metric's range overlapped; medians within
0.3%). Treat them as accurate within the run-to-run spread (~±3%) until the
same A/B runs on hardware.
