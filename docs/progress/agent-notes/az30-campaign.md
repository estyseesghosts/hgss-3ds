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

## Exp-2 indexed-off A/B (done, neutral)

- Hypothesis: G2 indexed prep is pure loss in this scene (hardware log 36:
  identical vertex bytes + index overhead + ~1.3s/120 scenes prep).
- Baseline: twin (`-gxhot-gxidx-listcache-statededup`, `56539dd0`).
- Candidate: `build/d1-noidx-ss` (`-gxhot-listcache-statededup`, `7d4db664`),
  same az30-lower.ini. One variable: HGSS_GX3D_INDEXED off.
- Result: composes 13.7/s both; scene build 6.18->6.03ms (noise); geometry
  31.68ms/scene, 242 skipped; drawer identical; wall 30s both.
- Correctness: field screenshot sane.
- Conclusion: neutral under Azahar (JIT eats the prep cost; host GL is
  insensitive to the byte difference). Indexed STAYS ON (hardware drop
  decides separately; no reason to fork the campaign binary). No change.

## Exp-4 force-software upper (done, rejected)

- Hypothesis: draws follow content newness; forcing full software raster
  (no GPU-BG takes) makes every upper publish new and draws should jump ~2x.
- Candidate: diag binary + az30-sw2d.ini (az30-lower + sw2d=on). Reference
  path by construction.
- Result: draws 132->109, composes 13.2->10.9/s; drawer 29.0->19.8/s @
  35.76ms/frame (top raster 6.7->21.1ms/call); publishes 30->20.9/s.
  Upper reuse still caught 128/198 (content is content; scroll barely moves
  in this window). Screenshots structurally identical (9.6% pixels differ
  from RPC capture-latency phase drift, verified by banner fade state).
- Conclusion: rejected. Takes stay on. Draws track completions x newness;
  heavier draws lose net.

## Exp-2b tag capacity (done, beneficial)

- Hypothesis: the 512-entry tag ring evicts while the worker lags ~4 scenes,
  showing as untagged lookups; hashed 4096 slots recover them.
- Candidate: `build/d1-fifo-ss` (twin flags + SDK tag change; commit
  b289ec9, so tag `b289ec9-dirty-...`; twin predates instrumentation by a
  behavior-neutral counters-only diff, noted). Same az30-lower.ini.
- Result: untagged collapsed (52k->7.6k cumulative); tagCollide 100k (hash
  pressure is real); singlesBegin 16.8k (FIFO origin confirmed as a second
  pool); worker busy 96->90%, 31.86->29.83ms/scene, skipped 242->199,
  producer wait 363->0.3ms (!!); wakeups 240->527; composes 13.7->14.8/s
  (+8%); drawer 29.0->32.3/s; wall 30s.
- Correctness: screenshots sane; audits 0 fails; no asserts.
- Conclusion: beneficial, keep. The linear 512-scan itself cost ~5% worker;
  producer-wait elimination is the big secondary win. BUT exposed entry
  thrash: 24k inserts into 256 slots (0.8% hits). Next: Exp-2c entry table.

## Exp-2c entry table 256->1024 hashed (done, major win)

- Hypothesis: with tags fixed, the 256-entry table thrashes under pipelined
  scenes; hashed 1024 slots let entries survive the lag window.
- Candidate: same `build/d1-fifo-ss` (same flags; SDK change only).
- Fixture gate: `test_gx3d_listcache` bit-compare green (including an
  updated overflow premise: 5000-piece feed to actually overflow 4096 tags).
- Result: composes 13.7->22.6/s (+65%!), late 60->1, hits 0.8%->29%;
  skipped 242->150; worker busy 93%; wall 30s.
- Correctness: screenshots sane; audits 0 fails; fixtures green.
- Conclusion: keep. The entry table was massively clogged. Draws now
  ≈ scenes-pub (~150) + 2D-new (~70).

## Exp-3a singles scan routing (done, neutral -> reverted)

- Hypothesis: FIFO singles runs never get the hidden-vertex scan; routing
  them through the existing skip_hidden_vertices saves transform on hidden
  singles, with zero hot-path changes.
- Candidate: `build/d1-fifo3a-ss` (twin flags + FIFOCACHE; fixture-proven
  bit-exact incl. a new word-by-word singles phase).
- Result: 32.6k scans fired; worker 31.25ms/scene (vs 30.88), skipped 150
  (same), draws 132 (same), composes 22.5/s (same), wall same. NET ZERO.
- Coherent cause: singles runs here are overwhelmingly visible (scan never
  skips, pure tax) — which also kills Exp-3b's premise (nothing repeat-hidden
  to cache). A 240-vs-400 count scare investigated to a census-timing
  illusion (periodic log mid-phase vs end totals); hook fired 100%.
- Conclusion (guide §15: failed idea established decisively): REVERTED the
  hook + FIFOCACHE flag + plumbing + fifo test. KEPT: tag/entry hashing,
  census fields, fixture singles-phase coverage, overflow premise fix.
  Tree stays lean; no dead flags.
- Abandoned direction (recorded): worker-side content hashing cannot beat
  walk-then-skip for hidden runs (the verification IS the walk); caching
  needs arrival-side identity.

## Next: worker transform cost + R diet

- Remaining geometry lever: per-vertex transform/lighting/clip (A0 sample:
  34% of worker) + PICA staging writes. Options: float32 fast path with
  tolerance fixtures (O3-3D-7 allows tolerance; G1 bit-exact gate must NOT
  be weakened — separate tolerance gate), drawer hash narrowing (lower
  refHash ~1MB/check, upper check 3ms x300).
- R diet and tick pace re-measured after each change; wall A/B decides.
