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
- Fixture gate: `test_gx3d_listcache` bit-compare green (overflow premise
  updated: 5000-piece feed to actually overflow 4096 tags).
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

## Exp-2d entries 2048 (done, neutral -> reverted to 1024)

- Hypothesis: 1024 still thrashes (29k evicts); 2048 lets entries survive.
- Result: hits 29%->39%, evicts still high; worker/draws/wall IDENTICAL
  (composes 22.6/s, geometry 30.46ms, skipped 150).
- Conclusion: marginal hits cover cheap small runs (Amdahl shrugs).
  REVERTED to 1024 (saves ~112KB bss for zero gain). Table sizing done;
  further worker gains must come from transform cost, not caching.

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

## Exp-5b vsync-off (done, neutral -> keep vsync on)

- Hypothesis: host present backpressure (occluded-window throttling /
  swapbuffers blocking) paces the guest; vsync-off unblocks it.
- Candidate: same fifo binary, `use_vsync=false` via probe env (config
  restored after; user setting untouched).
- Result: composes 22.6->21.2/s (noise), wall same, geometry/drawer same.
- Conclusion: host presentation is NOT the limiter. Keep vsync on
  (no tearing, same speed).

## Standing numbers (Azahar, 80% clock, lower30+drawerAB, twin-ish binary)

- Ticks ~50-52/s wall (cap never binds; work-paced). Presents ~20/s wall
  (draws ≈ scenes-pub ~150 + 2D-new ~76 per 600 ticks).
- Gaps: ticks +15%, presents +50%.
- Draw equation: draws ≈ max(scenes-pub, upper-new) + lower-new (overlap).
  scenes-pub is the only scalable term (content-limited 2D otherwise).

## Exp-6 normal-keyed lighting cache (done, kept as work elimination)

- A0 census: 96% of packets runs LIT. Map geometry reuses few normal
  directions (axis boxes); same normal + same lights + same material =
  same lit color. Cache keyed by (normal, light-gen, material) with the
  SAME math (bit-exact, fixture-gated). Potential: lighting share of the
  34% vertex cost at ~5-10x per hit.
- Design (no new flag, unconditional): 64-entry direct-mapped table keyed
  by (generation, normal value, polygon attr); generation bumps at the 8
  sites that used to clear the 1-entry cache (matrix/light/material
  commands + reset). BEGIN_VTXS no longer clears: attr rides in the key,
  so same-material runs reuse across polygons (the old clear was a
  single-entry artifact). Hit/miss census in `[gx3d] shadecache`.
- Correctness gates: `test_gx3d_listcache` bit-compare green (same math,
  memoized on full dependency key). Replay path (`replay_polygon`) sets
  colors directly, no shade between attr swap/restore — safe.
- Candidate: `build/d1-shade-ss` (twin flags + shade change, fresh out dir).
- Watch: shadecache hits/misses, worker ms/scene, skipped, draws, wall.
- Result: 296k hits / 577k lookups (51%!) at frame 1104; worker 30.88ms
  (vs 30.46), skipped 149 (same), draws 224 (same), composes 22.4/s (same).
  Cache works; lighting is NOT the binding cost (clip transform + scan
  dominate the vertex budget).
- Conclusion (guide §15): KEEP, labeled honestly — work-elimination proof
  positive (~300k shade computations skipped/interval, bit-exact same math),
  performance proof neutral. 768B + ~20 lines, fixture-gated, zero-risk;
  may help light-heavy scenes (battles). Tree stays lean otherwise.
## Exp-7 float clip kernel (done, neutral -> reverted)

- Hypothesis: submit_vertex's 4 int64-smull rows dominate the vertex share;
  float32 on hardware VFP cuts that kernel ~3-4x.
- Candidate: `build/d1-floatxf-ss` (twin flags + FLOATXF; tolerance gate
  green, screenshot phase-identical to shade run).
- Result: worker 30.46->30.03ms (-1.5%, noise); skipped 150->170, draws
  224->192, composes 22.4->19.2/s (all three down together = slightly
  slower run, not noise in one counter). The int path (12 smull + adds +
  predicted compares) was already optimal; int<->float converts ate the
  VFP gain. The vertex ticks live elsewhere (staging/record/lighting
  misses), NOT the mult kernel.
- Conclusion (guide §15): REVERTED flag + plumbing + tolerance test. Tree
  lean; no dead flags. Lesson: kernel-level substitution without a profile
  pointing at that kernel is guessing.
- Wall-metrology fix: probe now stamps `wall-{mark}.txt` at target//2 and
  target frames (game.log is written once at completion, so external
  waiters can never split it; the stale-marker /tmp/azmeta.txt approach
  is retired).

## Exp-8 evict-cause census (done: 100% churn — structural insight)

- Result: evictSame=0, evictAddr=0, churn=46549/46549. EVERY eviction is a
  novel key. Keys stream without repeats beyond burst-hits: per-scene heap
  reallocations + walking-camera content = endless novel runs. Cross-scene
  content caching would need arrival-side identity (Exp-3b revived); parked.
- WALL METROLOGY (first true numbers): wall-600→1200 = 10.0s → **60 ticks/s
  wall = FULL SPEED already**. The 52/s premise was boot-contaminated.
  Ticks gap = ZERO. Remaining: presents ~22/s → 30/s needs draws 224→300
  per 600 ticks (+34%).
- STRUCTURAL INSIGHT (SD log): 120 scenes = 1440 vtx → **12 verts/scene**.
  Scenes are TINY; per-RUN machinery (lookup+scan/box ≈ 5k ticks/run) dwarfs
  transform (~1.5k ticks/vert). Float/shade did nothing because the mult
  kernel was never the fat — per-run overhead is.
- Next (Exp-9): small-run skip bypass — runs under N words execute directly
  without the skip machinery (bit-exact: bypass falls through to the normal
  execute path). Wins on visible small runs (the majority per Exp-3a),
  washes on hidden ones.
- (Discriminator design: evictSame/evictAddr/evictChurn on the existing log
  line; counters only, no behavior change. Kept in-tree as churn telemetry.)

## Scene-flow census (done: worker 2x oversupply confirmed)

- Result (az-flow-00, 600→1200): flow queued 300 done 299 dropped 0,
  skipped 149; GPU scenes drawn 150; wall 10.0s/600 frames (60 ticks/s).
- Reading: NO post-completion waste (dropped=0) — every completed scene
  publishes. But the display consumes only ~150 of 299 published serials;
  mailbox newest-wins overwrites the rest before display reads them.
  Worker oversupplies 2x; display take/draw cadence is the cap, NOT worker
  throughput. Worker-diet work is USELESS for draws from here.
- Next: draw-trigger census (scene-draws vs serials-skipped on the loop
  line) decides whether pairing waits or draw cadence drops the serials.
