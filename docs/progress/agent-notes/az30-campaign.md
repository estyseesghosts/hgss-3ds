# Azahar 30fps campaign log (2026-11-06)

Deliverable: full-speed game + 30 presented fps on Azahar, N3DS model off,
emulated CPU 80%. Frozen inputs: `~/hgss-private/soulsilver.sav` (Goldenrod),
`benchmark/goldenrod-bench.txt` (470 inputs, end frame 4200),
`benchmark/port.ini` base. All runs SoulSilver, frame window 600-1200 unless
noted. Emulator figures are emulator figures (never hardware timing).

Measurement rules: wall A/B decides (wall seconds for boot+1200, game pace,
screenshots). Guest `[perf]` counters are guest-cycle time; do not convert to
wall without stating the assumption. One primary hypothesis per experiment.

## Exp-0 baseline (twin, az30-base.ini)

- Hypothesis: establish numbers before changing anything.
- Baseline artifact: `build/d1-twin-ss` (`078beff…`, tag
  `078beff-dirty-gxhot-gxidx-listcache-statededup`), config az30-base.ini
  (= frozen port.ini + explicit partialB=on/partialBChar=off/bandPubB=on,
  diags off, lower30/drawerAB off).
- Result (600-1200): game ~50 ticks/s wall; composes 11.3/s (emulated);
  drawer 23.0/s @27.03ms; top 6.25ms x228, bottom 16.35ms x228;
  uploads 13.0MB flushed/interval; geometry 30.2 scenes/s @31.33ms, 95% busy,
  242 skipped; publishes 27.5/s; lag 4. Screenshot: correct Goldenrod field.
- Conclusion: game logic near full speed; presentation ~5-6/s wall. The
  mountain is presents, not ticks.

## Exp-1 lower30=on + drawerAB=on (config-only, same twin binary)

- Hypothesis: offering engine B every other tick + A-priority publish removes
  lower work from the drawer's critical path, raising drawer throughput and
  composition rate.
- Candidate config: az30-lower.ini (az30-base + lower30/drawerAB on).
- Activation: both logged at boot; 120 lower deferred; bottom renders 228->32.
- Work proof: drawer 27.03->16.40ms/frame, 23.0->29.0/s; flushed bytes
  13.0MB->4.0MB (-69%); bottom total -70%.
- Result: composes 11.3->13.7/s (+21%). Game pace unchanged.
- Correctness: screenshots match; no asserts. (Hardware: PENDING; lower30
  stays default-off for hardware drops per standing rule.)
- Conclusion: beneficial, keep. Adopted as campaign base config.

## Display-gating diagnostics (diag builds, no behavior change)

- Loop passes outnumber draws ~7:1 (956 vs 132); skip reasons: nonew 824,
  wait2d 0, midlate 0, nobuf 0. Per-draw cost 4.5ms (pick 0.01, prepare 2.74,
  stage 1.47, end 0.26); skips 0.01ms. VBlanks run 1:1 with ticks (600/600).
- Per-screen need (steady): upper 118-122, lower 12 — draws ≈ needA+needB
  exactly. Wakes exceed passes (VBlank-driven + drawer/scene signals).
- Publish outcomes: upper 281 calls, mostly unchanged (reuse 187/flat 55);
  lower 12-13 calls, all new. Software pictures renew ~10/s (emulated).
- Geometry: done 300 (30/s) incl. 242 skipped (worker overload drops);
  published ≈ 58/interval ≈ 6/s. G3 census: 75862 lookups, 3% hits, 70%
  untagged (FIFO origin) — bounds cache can't touch them as designed.
- A0 worker sample: hidden scan 62%, vertex 34%, matrix 3% of worker time.
- Conclusion: display consumes nearly everything available (~13/s draws vs
  ~13/s new content). Presents are CONTENT-starved: scenes-pub ~6/s +
  2D-new ~7/s. Raising presents needs (a) more published scenes (geometry
  throughput; skip mechanism amplifies worker gains) and/or (b) more
  2D-newness per tick.

## Exp-2 indexed-off A/B (queued)

- Hypothesis: G2 indexed prep is pure loss in this scene (hardware log 36:
  identical vertex bytes + index overhead + ~1.3s/120 scenes prep).
- Baseline: twin (`-gxhot-gxidx-listcache-statededup`, `56539dd0`).
- Candidate: `build/d1-noidx-ss` (`-gxhot-listcache-statededup`, `7d4db664`),
  same az30-lower.ini. One variable: HGSS_GX3D_INDEXED off.
- Pending run.

## Exp-4 force-software upper (diag + sw2d=on) (running)

- Hypothesis: draws follow content newness; forcing full software raster
  (no GPU-BG takes) makes every upper publish new (scroll changes software
  every tick) and draws should jump ~2x+.
- Candidate: diag binary + az30-sw2d.ini (az30-lower + sw2d=on). Reference
  path by construction (bit-exact); costs drawer CPU.
- Decides secondarily whether scenes drive draws (residual
  draws-upper-new-lower-new => scene share => has3d verdict).
- Pending run. Screenshots must match baseline pixel-for-pixel (upper).
