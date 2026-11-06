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

- Ticks **60/s wall, full speed** (wall-600→1200 = 10.0s, confirmed
  az-evict-00 and az-flow-00; the old 50-52/s figures were
  boot-contaminated). Ticks gap = ZERO.
- Presents ~23-24/s wall (draws 236/600 ticks = 150 scene-driven + ~86
  2D-new). Need draws 300/600 (+27%).
- Draw equation: draws ≈ scenes-consumed (~150) + 2D-new (~86); scenes
  consumed track worker completions 1:1, so scenes-published is the
  scalable term (content-limited 2D otherwise).

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

## Flow correction (done: the "oversupply" verdict was wrong)

- `sSwapsDone` increments on the skip path too (`work_op` skip branch,
  `port_gx3d.c`): skipped scenes count as "done". So flow "done 299" =
  ~150 genuinely completed + 149 skipped-and-counted, NOT 299 completions.
- Corrected pipeline per 600 ticks: queued 300, skipped 149
  (pre-processing drops), completed ~150, published ~150 (dropped 0),
  consumed/drawn 150. NO waste anywhere — a 1:1:1 pipeline, and the
  WORKER IS THE BINDING CONSTRAINT after all. Worker-diet work is back
  on the table; skip-tuning cannot help (nothing completed is dropped).
- Lesson: read the counter's write sites before building a theory on it;
  "done" conflated two outcomes with opposite implications.

## Exp-9 small-run bypass (done, neutral -> reverted)

- Hypothesis: scenes average ~12 verts, so per-run skip machinery (~5k
  ticks/run) dwarfs transforming a few verts (~1.5k each). Runs opening
  in a tiny entry execute directly (bit-exact: bypass only skips the
  skip attempt).
- Result: fixture FAILED the behavior gate (inserts 27->1, hits 16k->4k).
  Root cause: entry word-count is a FRAGMENTATION artifact, not run
  size — the 37-word test feeds (and real GXFIFO flush cuts) split runs
  across entries, so the bypass starved the cache of inserts.
- Conclusion: REVERTED fully (hook, counter, log field). No dead code.
  Lesson: size proxies must measure the run, not its container.

## Draw-trigger census az-trig-00 (done: serials advance 1:1)

- New counters at the upper-screen draw site (`ComposeDbgStats` +2):
  scene-driven draws and published serials skipped between them.
- Result (600→1200): scene-draws 150, serials-skipped **0**; ComposeFrame
  gap always 1 (`sDropped=0`); shell takes newest via
  `Gx3dNewerScene`/`AcquireScene`.
- Reading (with the flow correction): takes track completions 1:1 —
  ~150 completed, ~150 taken, 150 scene-driven draws, zero serials
  skipped anywhere. There is no pool of completed-but-unconsumed
  serials; the 149 not drawn were never processed (pre-processing
  skips). So raising completions (worker diet) raises scene-driven
  draws ~1:1, up to ~300/600 for 30fps. The census stays in-tree as
  the take-side proof for future display-policy experiments.

## Exp-13 run-path cost split (diag-only, building)

- Hypothesis (one): per-run overhead (~165 runs/scene vs 12 verts/scene)
  is the worker's binding cost; its split (listcache_try incl. full-L
  verify walk vs scan walk vs insert full-hash second pass vs box test)
  decides the next diet. No behavior change: all new code is
  `#ifdef HGSS_A0_DIAG` (twin builds byte-unaffected when off).
- Discriminator (single, at the decision point): sampled 1/64 scenes
  (existing A0 pattern), try ticks/calls vs scan ticks/calls + walked
  words per region (try-verify L / scan fetches incl. cross-entry
  singles / insert full-hash L). Ratios only — emulator wall ticks
  rank regions; hardware proves any later speedup.
- Suspects, in order: (1) insert full-hash second pass per miss
  (~47k/600 ticks, 100% churn — `full` consumed only by the
  every-30-scenes audit); (2) hit-path full-L verify walk (~51k);
  (3) try lookup fixed cost x184k. If (1) dominates, Exp-14 fuses the
  full/sample hash into the scan's fetch loop (same values, one pass
  eliminated — audit keeps working unchanged).
- Candidate: `build/d1-runsplit-ss` (twin flags + HGSS_A0_DIAG=1, fresh
  out dir). Baseline: d1-ctl-ss. Activation gate: `[a0] runpath` line
  present with nonzero calls. Verdict threshold: name the biggest
  region by ticks AND words; diet must target it.
- RESULT (emulator; ratios only — ranks regions, hardware proves any
  later speedup): clean run (drawer 29.5/s, game_us 5415 — diag-only
  confirmed). Sampled 1/64: try ~1500-1630 ticks/call (1365/1128
  calls), scan ~4300-5410 ticks/call (1026/794 calls, miss-only) →
  scan ≈ 2.2x try ticks, ≈3x per call. Walked words/interval:
  try-verify ~8.0k, scan-fetch ~5.5-5.9k, insert-full ~7.3-7.5k.
  insert-full EXCEEDS scan-fetch (insert re-walks full L incl. params;
  the scan skips params via i+=). ~748 ticks per fetched word ⇒ per-run
  FIXED costs dominate, not walks. Suspect refined: insert's
  per-start tag verification (nStarts × list_find_tag, up to 257
  starts on fragmented runs) + entry fill + full-walk, paid per miss
  for entries that 100%-churn says never re-hit.
- Exp-14 follows (counters only, no behavior change): hits-by-jumps +
  inserts-by-jumps histograms. If fragmented runs (jumps>0) never
  re-hit, Exp-15 stops inserting them (scan fallback is always safe).
- RESULT (az-split-01, same diag binary + histogram): jumps hits
  47888/1651/0/0, inserts 46645/1473/0/0 — 97% single-entry, 3%
  two-entry, ZERO spans ≥3. Fragmented runs re-hit PROPORTIONALLY
  (1651 hits / 1473 inserts ≈ jumps-0 rate) → hypothesis REFUTED.
  Dropping fragmented inserts would lose 3.3% of hits to save 3.1%
  of inserts: ~neutral coverage for 3% insert savings. NOT the diet.
  Runpath split replicated almost exactly (try 2151046/1365, scan
  4775511/1026, words 7964/5927/7278 — try-verify/insert-full
  identical to split-00: deterministic content).
- Standing numbers: scan ≈ 2.2x try ticks; ~748 ticks/fetched word ⇒
  per-run FIXED costs dominate. Scan-inner still unsplit (walk/decode
  vs box/verdict+apply vs insert bookkeeping+walk) — Exp-15 splits
  it (diag ticks, no behavior change), then the diet targets the
  named region. No more telemetry after that: diet or accept.

## Exp-16 fuse insert hash into scan (building)

- Hypothesis (one): the insert's second L-walk (~26% of scan-inner,
  7.3k vs 5.9k scan-fetch words/interval) is redundant — the scan
  already visits every word of a single-entry run in linear order.
  Hashing at visit time (first word + fetches + param skips) yields
  the identical full FNV; sample offsets read directly (10 masked
  ring reads, same entry). Multi-entry runs (3%) keep the old walk.
- Design: scan accumulates `fuseFull` (always under LISTCACHE; ~1-2%
  added scan cost); insert takes it, uses it iff nStarts==1, reads
  sample directly. Under A0_DIAG the old walk still runs as a
  self-verification (fuseMismatch counter, walked values win).
  Safety net even if fused is wrong somewhere: per-hit sample check
  + 30-scene full audit auto-disables the cache (perf to baseline,
  never wrong pixels).
- Correctness gates: host fixture bit-identical outputs (fused values
  exercised through hits; no DISABLED) + device audits fail 0 +
  fuseMismatch 0 + screenshots.
- Candidate: `build/d1-fuse-ss` (twin flags + HGSS_A0_DIAG=1, fresh out
  dir — self-verification active; ship build later without A0).
  Baseline: d1-ctl-ss + az-split-02 runpath/scaninner numbers.
  Watch: worker ms/scene (expect -3-8%), scanInsertTicks drop,
  fuseMismatch 0, audits fail 0, draws, wall.
- RESULT az-fuse-00 (correctness: PROVEN): fuse-mismatch 0 both
  intervals, audits 8 fail 0, sample-fail 0, no DISABLED — fused full
  FNV + direct sample reads are bit-identical to the old walk
  in-game, across 49k inserts. Worker 31.36 ms/scene flat vs 31.32 —
  EXPECTED: the A0 self-verification walk preserves the removed walk
  on sampled scenes, masking the prize; fuse hashing overhead itself
  ≈ +0.1% (negligible). This build cannot show the speedup.
- Next: ship-shape binary (same source, A0 off) for the true number:
  `build/d1-fuse2-ss`. Watch worker ms/scene (expect -3-8%),
  draws, audits, wall. Keep threshold (Exp-6 rule): work-elimination
  positive + perf neutral-or-better + zero-risk → keep.
- RESULT az-fuse-01 (ship, A0 off): worker 31.38 ms/scene vs twin
  30.98–31.21 — FLAT (neutral). Draws 302, game_us 5347, audits 8
  fail 0, fuse-mismatch 0, screenshots phase-matched. VERDICT: KEEP
  per Exp-6 rule — one full L-walk per insert eliminated (~46k
  walks/600 ticks, count-proven), bit-identical (mismatch-0 +
  audits + host-identical), zero risk (audit auto-disable net),
  perf neutral on emulator. Honestly labeled: work elimination,
  not a speedup.
- FLAG (staged-pair hygiene): twin + fuse shared one build tag and
  would have shown IDENTICAL launcher entries. Fuse is now a real
  experiment flag `HGSS_GX3D_FUSEHASH` (`-fusehash` tag/SMDH/sidecar,
  Makefile + gx3d_mode marker + smdh_names plumbed): OFF = old walk
  (twin control, zero overhead — hashing sites gated out), ON =
  fused insert + A0 self-verify. Staged files renamed accordingly;
  the ambiguous `-2` card file superseded before any run.
- Rebuild sequence: A0+fusehash device build re-proves mismatch-0
  after gating → ship twin-off + ship fuse-on (fresh out dirs,
  A/B telemetry-identical) → stage pair → hardware runs.
- STAGED for Old3DS (card B9STRAPON, saves/port.ini/scripts untouched):
  - `3ds/pokesoulsilver-926b99a-dirty-gxhot-gxidx-listcache-statededup.3dsx`
    (twin control, fusehash off; sha256 c3fe47fb…; HBL shows the tag)
  - `3ds/pokesoulsilver-926b99a-dirty-gxhot-gxidx-listcache-statededup-fusehash.3dsx`
    (candidate, fusehash on; sha256 ed2eaf0f…; HBL shows …-fusehash)
  - Superseded same-tag card files (untagged twin + ambiguous -2)
    removed before any run; pre-existing pokesoulsilver.3dsx left alone.
  - Emulator A/B (az-ctl3-00 vs az-fuse-01): worker 31.03 vs 31.38
    (neutral), draws 305 vs 302, game_us 5389 vs 5347 — pair is
    matched; hardware decides whether the eliminated walks pay.
  - Hardware watch: geometry ms/scene (twin vs fuse), scenes drawn,
    draws, wall pace, audits fail 0 both, screenshots if convenient.
    Expectation: fuse ≤ twin on ms/scene (walks gone); size unknown
    (ARM11 cache behavior vs JIT).
- HARDWARE VERDICT log-o3ds-00 (twin runs 01:45+01:52, fuse 02:00;
  one stale a50da64 archive ignored): twin#1 ≈ twin#2 within noise
  (geometry 39–41 vs 38–42 ms/scene; scenes drawn 465 vs 472 by
  frame 4200; drawer/audio shapes identical) — twin reproduces.
  Fuse == twin everywhere (geometry 38.4–41.4, scenes drawn 462,
  draws/drawer same, audits 28–40 fail 0, sample-fail 0, no
  DISABLED, jumps consistent). VERDICT: fuse NEUTRAL on hardware,
  SAFE — kept as work elimination (Exp-6 rule), not a speedup.
- CONFIG GAP found (slice 0): az30-lower.ini (all emulator probes)
  runs lower30=on + drawerAB=on, but the card port.ini runs both
  OFF ("test flags off"). The emulator-vs-hardware drawer chasm
  (15ms vs 250ms+ bottom wall) is substantially CONFIG, not just
  ARM11-vs-JIT. Emulator drawerAB correctness already proven by
  10+ probes (menus sane in every screenshot) — no new probe needed.
- Exp-17 HARDWARE TEST (staged, no build): port_A.ini = card
  port.ini + drawerAB=on ONLY (lower30 stays off to separate it
  from Exp-1's combined emulator +21%). Mechanism: B renders only
  when fresh/forced/incoherent (staleness ≤2 ticks); bSkips/"lower
  deferred" counter proves engagement; need-gate asymmetry (top
  calls≈need 1:1, bottom calls 30–135 vs need 0–21) predicts
  bottom renders collapse toward need. Watch: drawer bottom
  calls+wall, worker completions (freed core?), composes, menu
  cursor/touch response (user-observed), audits. lower30-only
  (port_B.ini) staged only after this reads out.
- RESULT log-o3ds-40.txt (twin + START → port_A.ini, drawerAB=on,
  13931 frames): engagement YES (lower deferred 12–16/600frames
  walking, 22–43 idle). Bottom calls 28–34 → 23–32 in-window
  (~-15%); bottom per-call wall UNCHANGED (~250–350ms) — the
  per-call cost is intrinsic (true CPU and/or per-call waits), NOT
  redundancy; deferral only trims count. Drawer ms/frame 300→210
  (~-30%) via fewer renders + smaller publishes. Throughput FLAT:
  worker 38–42 ms/scene, completions ~72/600frames, composes
  8.5–9.4/s, publishes 4–5/s walking — identical to baseline.
  VERDICT: drawerAB drawer-positive, throughput-NEUTRAL. ini stays
  on card (harmless); not a lever. Freed core did NOT convert →
  worker not drawer-preempt-bound (audio-preempt still open —
  audio program now definitive either way).
- SLOW-MO measured: run durations vs frames give ~19–25 game
  frames/s on hardware (~40% speed) — part of what "unresponsive"
  means. Presents are repeats+draws mixed (composes ~2x draws);
  distinct presents ≈ 4–5/s wall. Goal gap restated: 30 distinct
  needs ~213 completions/600frames (6–7x current 70–72).
- RESULT az-slot3-01 (slot3 vs twin, emulator): takes 612→829,
  front commits 0→181/600, blend rejects 0→143 (BG3 alpha states
  fall back to software blend — correct, costs CPU there).
  Raster lines 37056→8064 (−78%), row accesses −~50%, draws
  300→366 / publishes 30→37 (cheap metadata republishes for quad
  scroll). Worker 30.90 (flat), composes 23.3 (flat, content-gated
  as always), screenshot phase-perfect, partialB mismatches 0.
  VERDICT: mechanism proven (front commits, raster collapses);
  throughput flat on emulator (expected). Open: BG3 blend-state
  takes (TEV feasibility review needed); hardware wall proof
  awaits user runs. KEEP behind flag; twin pristine.
- RESULT log-o3ds-01 slot3 runs (hardware): NEUTRAL (worker,
  drawer, composes, scenes all match twin; audits clean).
  CORRECTION: the earlier "ext rejects" read was a field miscount
  — they are MODE rejects (24→114 growing): BG3 evaluated via the
  3rd slot but failing kind (non-text/affine: large-bitmap?) where
  the emulator's BG3 takes cleanly. Take loop runs only ~30/600
  frames on hw (vs ~306 emu) with zero other rejects — silent
  gating (has3d/gpuBgs) or content-phase difference.
  DIVERGENCE CAVEAT: frame-anchored metrics may compare different
  map areas across platforms (slow-mo shifts script alignment)
  and even across binaries (BSS layout → boot timing). Twin×2
  matched, so same-binary comparisons hold; cross-binary and
  cross-platform phase alignment is now suspect — match by content
  markers (scenes drawn, BG config), not frame numbers.
- NEXT: full-script (4200f) emulator probe to reproduce the hw
  content phase on emulator (MODE-reject window?) with full
  tooling; then BG3-kind forensics from captures. No more hw
  conclusions from frame-aligned cross-platform reads.
- RESULT az-slot3-02 (rebuild with forensic): replicates 01
  (takes 827, region 181/323, drawer 32.3/s), zero `gate kind`
  lines (no MODE rejects on emulator, as designed), screenshot
  perfect. Added one-shot `[2d] gate kind` forensic at the MODE
  reject (bg+kind+bgcnt+dispcnt, mirrors dsp-gate pattern) so the
  next hardware run names BG3's kind directly. No behavior change;
  render2d partial + listcache gates green.
- Exp-22 audio ADPCM diet (SND_ADPCM flag; building): [sndprof]
  verdict is 98% ADPCM, Mix 94% of audio CPU, no capture. The
  inner loop's nibble-bit branches (~50/50 music data) and
  saturate become branchless arithmetic (exact: shifts exact,
  mults by 0/1 exact, mod-2^32 associativity under -fwrapv;
  compiler emits SSAT, verified in object). Divides were already
  gone (compiler; zero idiv/uidiv/lmul in object). Host
  differential proof: both flag states bit-identical to pristine
  over 24k vectors (exhaustive step/nibble/predictor grid +
  streams + full mix matrix). Expected ~5-10% of mixer (~2% core);
  hardware audio% line decides. Structural audio (decode cache /
  NDSP channels) reviewed separately, not started.
- §13 REVIEW: BG3 blend-state takes (PARKED, not built). DS alpha
  = src×EVA+dst×EVB with uniform factors: representable via PICA
  vertex-alpha IFF EVA+EVB==16 (darkening sums are not — no
  constant dst factor); DS brightness = integer formula vs PICA
  f16 TEV/varyings (LSB rounding risk → tolerance gate + hw proof
  required). The 143 rejects are likely banner brightness fades, a
  conservative-exact reject, not proven representable. Prize
  ~1-4% drawer core for real precision risk. Stop condition met
  before generalizing: needs (1) EVA/EVB/brightness value census
  in bench, (2) tolerance-gated fixtures, (3) hardware proof.
  Revisit only with all three.
- Box diet PARKED (no new telemetry to size it): point-run
  shortcut (lo==hi → 4 mults, exact) is the only safe variant but
  its share is uncounted and I won't add counters for it; int64→
  float repeats Exp-7's convert-trap; 32-bit risks overflow the
  existing RANGE guards were built for. ARM11 box share (31% of
  scan-inner, from the batch run) keeps it first in line IF a
  future profile splits it — not before.
- Offs-divide idea KILLED by reading: m*(L-1)/8 divides by
  CONSTANT 8 (compiler shifts; no libcall possible). No edit.
- Skip-tuning (behind≥2 → ≥1) KILLED by reasoning: trades
  completions for latency (fewer full scenes, fresher display).
  Serves responsiveness, not the 30-presents goal. Noted as a
  latency lever, not built.
- AZAHAR LANES EXHAUSTED (honest inventory): worker micro-diets
  banked/dead/parked; drawer need-gated; takes mapped (await hw
  forensics); audio diet built+verified (awaits hw); box/try need
  hw-split evidence (batch run has it — diet design is the open
  work, not more telemetry); lower30/slot3/batch/lazy+fuse all
  await user runs. No more emulator-only experiments until hw
  reads out — churning single-digits would violate §15.
- PENDING-HARDWARE QUEUE (all staged except adpcm): batch-profile
  (staged ✓), slot3 (staged ✓), lower30 port_B (staged ✓),
  adpcm-diet (d1-adpcm-ss built+verified, UNSTAGED — stage on
  card return: twin exists there for the A/B).
- NEXT: batch audio-profile + A0-runpath in ONE binary → ONE
  hardware run gives audio split AND true ARM11 run-path ratios
  (walk:box:insert:try) to de-risk all future diets. Emulator
  micro-diets paused (tapped); box-float needs hardware profile
  first (Exp-7 lesson).
- STAGED d1-batch-ss (`...-statededup-a0diag`, twin flags + A0 +
  snd counters, sha 61f70930…): emulator-verified ([a0] lines
  alive, screenshots identical, no [sndprof] — this Azahar mutes
  audio). ONE hardware run, base port.ini (no hold): reads audio
  split + ARM11 runpath off [sndprof]/[a0] lines. FTP detour
  retired (card remounted).
- Exp-19 content-repeat census (done, REFUTED): 582 sampled inserts,
  same-base repeats 0, moved ~4%. Heap churn is genuinely novel
  content (walking camera), not re-emitted blocks — content-keyed
  entries would buy ~nothing. Failed decisively and cheaply; census
  removed (tree lean). Lesson: the churn is content-novelty, which
  also bounds ALL cross-scene geometry caching (compiled lists
  would churn identically).
- Exp-20 lazy insert (building): with 100% churn, inserts serve
  only burst re-hits (same-src ping-pong); novel runs pay
  tag-verify + 64-bit strided divides + 64-entry census scan +
  fill + evict traffic for entries that die unused. Gate: a small
  worker-private direct-mapped seen-filter keyed by the insert key
  (src, keyWord, coords); first sight sets, second sight inserts.
  Safety: filter never authorizes anything — inserts store current
  scan values by construction, sample/audit guards unchanged, no
  epoch/invalidation needed ever. Expected: ~90% of inserts gone
  (~23% scan-inner); measured via scanInsertTicks + inserts/evicts
  collapse. Zero precision/pacing/cross-thread risk (removes
  synchronous work). Behind CONTENTKEY... no — LAZYINSERT flag
  (twin stays pristine); keep iff A/B wins.
- RESULT az-lazy-00 (lazy-only vs twin-ship): inserts 46800→10171
  (−78%), evicts 46221→9612 (−79%), hits 51528→59703 (+16% —
  fewer evictions → longer-lived entries → MORE re-hits, bonus).
  audits 8 fail 0, sample-fail 0, no DISABLED, screenshot
  phase-perfect, host bit-identical all modes. Worker 31.38→30.80
  (−1.9%), skipped 150→138, draws 302→316, wakes 407→567 (more,
  shorter; longest 59.1→53.7ms — watch on hardware). VERDICT: KEEP
  (Exp-6 rule: massive work elimination + bit-exact + neutral-better).
  Emulator mutes the prize (JIT-cheap bookkeeping); hardware
  (fill/evict/divide/census traffic) should amplify. Next: lazy+fuse
  combo Azahar test, then hardware when runs resume.
- RESULT az-lazyfuse-00 (combo vs twin-ship): worker 31.38→30.52
  (−2.7%, additive-ish: fuse ~0 + lazy −1.9%), inserts 46800→10529,
  hits 51528→61233 (+19%), fuse-mismatch 0, audits clean,
  screenshot perfect. VERDICT: combo stacks cleanly; ship shape
  (fusehash+lazy) is the current performance head. Worker descent
  this campaign (emulator): 31.38 → 31.38 → 30.80 → 30.52, every
  step bit-exact.
- Exp-18 lower30/drawerAB 2x2 separation (emulator, twin binary,
  ini-only; completes Exp-1's confounded square):
  neither 228 draws / 22.8 composes / drawer 22.9/s@27.47ms;
  lower30-only 227 / 22.7 / 25.7/s@22.03ms (render 22.87→16.90);
  drawerAB-only 237 / 23.7 / 28.9/s@16.90ms (200 deferred);
  both-on 236–238 / 23.7 / ~30/s@~15ms. VERDICT: Exp-1's +21% was
  drawerAB's doing; lower30 trims drawer render ~26% with flat
  throughput. All four screenshots phase-matched, menus sane
  (lower30 half-rate B visually clean). lower30 earns hardware
  candidacy → staged port_B.ini (base + lower30=on, drawerAB off);
  SELECT-boot runs it whenever convenient. Combined-on-hardware
  still untested (later, if either single reads out).
- Hardware reality check (mush-free counts): presents ≈ 8.5–9/s
  (emulator 23.6/s does NOT predict hardware presents — 2.6x off;
  emulator keeps activation/correctness/count duty only, per guide).
  Completions ≈ 70/600 ticks (7/s); skipped ≈ 230/600 (38%).
  ms/scene divides by done-INCL-skips: muddy denominator, report
  completions separately from now on. Goal 30 presents is 3.3x away.
- NEW FINDING audio: 24–27% of core 0 is REAL mixing CPU (timer
  wraps only NitroPort_SoundRender; sleep path outside it), 0
  dropped. ~3ms per 12s window for 512-frame blocks = ~6x worse
  than a reasonable 16ch ARM11 mixer. Biggest cleanly-measured fat
  on core 0 (worker/drawer walls are preemption-mush on shared
  core 0: drawer bottom 200–340ms/call × 2.5/s exceeds 100% alone,
  proving waits dominate). Mixer structure: ≤171-sample stretches,
  lock per stretch, Mix + sequencer sound_tick INSIDE the timed
  region; capture/reverb path mixes frame-by-frame (field music
  capture use unknown). NEXT: audio split profile (Mix vs
  sound_tick vs lock, capturing flag, active channels) — one
  device run names the hog.
- Post-fuse survey (all killed cheaply, mostly by reading):
  put32 = RAM memcpy (not MMIO); skip path already cheap (RING_SKIP
  bypasses work_packets — no scan on skips); publish check = lock
  wait, no hashing (drawer-hash diet dead); ensure_clip flag-guarded,
  puts cheap; untagged-preskip impossible (no free verdict);
  singles-preskip nonexistent (singles bypass work_packets already);
  prio shuffle zero-sum (nobody idles); mid-scene interp
  worker-negative (record+match+replay per scene at 94% busy).
- Gap math (mush-free counts): presents ≈ completions + const 2D
  (draws==need exactly; need = 150 scene + ~87 2D per 600 ticks).
  30 presents needs ~213 completions (−30% worker). Banked certain:
  ~0-4%. Identified risky remainder: box-32% (~7%, precision risk),
  try-fixed (un-split, small), execute (mined twice). Sum < gap.
  Emulator micro-diets are tapped; ARM11 ratios may differ (ring
  chase + int64 could dominate there — fuse may pay on device).
- NEXT: hardware truth (twin + fuse ship staged for SD runs) and/or
  box diet with tolerance gate. No more emulator telemetry.
- PROGRAM PIVOT (authorized: structural rewrite, Azahar-only until
  hardware-worthy): filed 2.6x emulator-vs-hardware present gap +
  3.3x hardware gap to goal mean micro-diets can't reach it.
  - Slice 0 (lead, now): map drawer bottom-screen path first-hand
    (raster/convert/upload/need-gate, why ~30 calls vs need) —
    bottom render wall dominates hardware wall; bit-exact oracle
    non-negotiable for everything after.
  - Audio profile (delegated, ses_e4d9e3979ffeuIU3IYxT5Gg65u):
    count-heavy Mix-vs-tick split + formats + capture in
    port_snd.c/port_sound_chip.c, counters + periodic log only.
  - Standing honesty: drawer-bottom(~30% wall share, CPU share
    unproven) + audio-half(~12%) + worker bits(~5%) ≈ 1.8x rosiest
    case, still short of 3.3x. Report ceiling cuts explicitly if
    numbers demand it; no flag/test-only paths as features.

## Exp-12 produce-side run records (done, reverted — harmful/neutral)

- Hypothesis: worker decode-scans (~28-50% of worker) re-decode VTX words
  the game thread already walked. Game-thread run records (bounds,
  end-coords/state, VTX count, full FNV) keyed by 4-word content prefix
  (position-free: survives heap rotation, resume states, cross-scene
  repeats) let the worker structure-scan opcodes + verify by hash instead
  of decoding. Game thread has headroom (game_us ~5.4ms of 16.6ms).
- Design (one file, no flag, fixture-gated bit-exact): produce tracks
  cursor/boundaries/state/FNV in `scan_packets` + `produce`; worker
  `ann_assemble` does prefix walk, structure+hash walk, record verify,
  then rejoins the shared tail (box test, apply, legacy insert).
  Coverage is [BEGIN-param word .. terminator word], exactly the insert
  walk's range. FNV mismatches fall back to legacy (counted); audits and
  screenshots gate correctness.
- Issues faced and fixed:
  - Off-by-one (produce skipped the BEGIN param word; worker includes
    it): fixed, `beginParam` = opParamStart.
  - Duplicate `beginParam` assignment (stale `+1` overwrote the correct
    index on every BEGIN): found via bp=1-vs-prefix contradiction,
    removed; single writer now.
  - Command-skip rewrite: the first version hashed command words only
    and skipped params (params never hashed); rewritten word-sequential
    mirroring the worker's fetch/shift/advance mechanics.
  - Stale-binary confusion (twice): a failed `cc` left the old binary in
    place while censuses were read from it. Now: check `build exit: 0`
    before every run; never trust output without it.
  - Fixture populations disjoint: 37-word feeds fragment runs through
    sDec singles, which taint records (24k taints); surviving records
    belong to runs that always entry-hit. So the big fixture proves
    safety (bit-exact green) but cannot prove engagement.
  - Replay theory checked and dead: fixture `ops` zero `sInterp`, so the
    mid-scene replay path never engages; silence has another cause
    (above).
  - Fresh-buffer phase added (same bytes, novel held addresses) to force
    entry-misses; still 0 ann hits — under diagnosis alongside:
  - Smoke test (`ANN_SMOKE`, 6 deterministic scenes): checksums match
    ref, 12 lookups, 0 hits, **6 verifyFails** — prefix matches but full
    hash differs with worker L = produce nw − 1 systematically (27 vs
    28, 38 vs 39). Tail evidence: produce hashed [..., END, BEGIN-next],
    i.e. closes one word late. Open/close-position tracing in progress.
- Watch: verifyFails must reach 0 with annHits>0 in smoke before any
  Azahar probe; game_us must not grow (ticks at exactly 60.0/s).
- VERDICT (emulator, Azahar N3DS-off CPU-80%, az30-lower.ini, 1200-frame
  frozen script; all figures emulator, not hardware timing):
  - Hypothesis: worker ms/scene drops via run skips. FAILED — geometry
    ms/scene flat: ann 31.83 vs twin-control 30.98 (scenes/s 29.8 vs
    29.9, identical hidden-lists output ~74k accepted / ~810k verts /
    ~268k polys both sides, flow 300/299 both, audits fail 0, no
    DISABLED). ~50% record hit rate (56.9k/112.3k) converts to no
    measurable worker work elimination (~42% of hits fall back at the
    terminator gate; skips that land save negligible work).
  - Harm signal: drawer collapse 3/3 ann probes (11-14 frames/s
    @42-59ms, publish-A ~21ms with check-wait ~12ms, game_us ~160) vs
    2/2 twin-control probes clean (29-30 frames/s @15-16ms, publish-A
    ~5ms, game_us 5351). Same binary flags except Exp-12; twin purity
    verified (0 `annrun:` strings in ELF); screenshots phase-matched
    (menu open both sides, CHECK vs NEXT = one input step apart, phase
    marker only); wall pace full-speed 60 ticks/s on all five runs.
    Geometry output identical across the split, so cause "different
    workload/corruption" is OUT — remaining shape is scheduling/
    pacing: worker wakeups nearly doubled (815 vs 429) with same
    scenes/s, i.e. the skip path changes worker batch pacing and the
    display thread pays in check-wait/preemption (guide §8/§14: wall
    inflation under overlapping threads is not proof of renderer CPU).
  - Claim sheet: baseline d1-ctl-ss
    sha256 d307960e52a8f5be0ca4431dc116cd326f55575aba5d21a556ceb3cea0a9f424
    (build 926b99a-dirty-gxhot-gxidx-listcache-statededup, no Exp-12);
    candidate d1-ann-ss
    sha256 a4ccf8823a38bfdf64be904f173d9ce1e2ce0b0d5590f70ec3efa05c6ce6b40a
    (same flags + uncommitted Exp-12, 12-word prefix). Activation proven
    (in-game `annrun:` census every probe: 110-112k lookups, ~50% hits).
    Correctness held (bit-exact fixtures, audits 0, screenshots match).
    Work elimination: none measured. Performance: regressed on emulator.
  - Conclusion: HARMFUL/NEUTRAL with cause (no work eliminated +
    emulator presentation regression + pacing perturbation). REVERTED
    fully (port_gx3d.c, vertex header, fixture smoke/fresh phases,
    test asserts) — no bisect: either half's outcome still leaves the
    primary hypothesis failed, and either outcome ends in revert; the
    pacing lesson (skips re-batch worker wakes; display pays) is
    recorded for future skip designs.
  - Kept knowledge: (1) silent-killer series (4) + loop-guard fix
    (`for(;;)` + fetch-time bounds check) — any future produce-side
    walker must mirror worker fetch/shift/advance exactly, incl.
    block-final terminators; (2) 12-word content prefix reaches VTX
    coords and discriminates same-material runs; (3) vfail assert
    policy (big-suite hits>vfails tripwire, smoke strict zero) for any
    future cross-thread record design; (4) worker-bound pipeline is
    1:1:1 — skips that don't remove worker ms/scene are pure overhead.
## Exp-12 produce-side bounds annotation (building)

- Hypothesis: worker decode-scans (~28-50% of worker) re-decode VTX words
  the game thread already walked; annotating per-entry bounds at produce
  lets the worker union bounds + structure-scan opcodes (~3 ticks/word
  vs ~15 decode) for the same skip verdicts. Game thread has headroom
  (game_us ~5.4ms of 16.6ms budget); worker is saturated.
- Design (one file, no flag, fixture-gated bit-exact):
  - Produce: cursor tracks VTX ops (same vertex math) in scan_packets +
    produce; per packets entry, min/max/verts noted into ListTag next to
    the existing tag (tied to the words, safe across wrap/reuse).
  - Worker: annotated path unions entry bounds + opcode-only structure
    scan (extents, end-state, exact VTX count, last-VTX decode only);
    exotic ops / singles / missing tags fall back to legacy full decode.
    Box test / apply / insert tail shared. Same bounds math -> bit-exact.
  - Game-thread cost guard: FRAMETIME game_us must not grow (ticks at
    exactly 60.0/s; any regression reverts).
- Watch: A0 hidden share, worker ms/scene, accepted rate (first-entry
  looseness only ever loses skips, never correctness), draws, wall.
- Order: az-trig-00 draw-trigger probe first (binary built), Exp-12 after.
- RESOLVED the silent-killer series (each found by walling off one
  assumption at a time): (1) off-by-one (produce skipped the BEGIN param
  word the worker walk includes); (2) duplicate `beginParam` assignment
  (stale `+1` overwrote the correct index on every BEGIN); (3) params
  never hashed (first version hashed command words only); (4) block-final
  terminators dropped (`while(j<n)` exits after the final fetch, before
  its bytes process — worker loops guard only the fetch; with ENDs so
  often last, no run ever closed at END and every close slipped a word
  late). After (4), smoke goes fully green (annHits>0, zero verify
  fails, bit-exact).
- 12-word prefix (was 4, then 8): same-material runs share state-head
  prologues; 12 words reach VTX coordinates, which discriminate.
- Verify-fail variance understood, not feared: same binary gives 0 vs
  ~5000 vfails depending on worker/producer interleave timing (logging
  on/off flips it; scene outputs byte-identical either way). These are
  cross-thread staleness races — a lagging worker meets records
  overwritten by same-prologue variants and correctly falls back — so
  the big suite asserts hits>vfails (mechanism wins more than it
  wastes) instead of an absolute zero, while smoke holds the strict
  zero-vfail coverage proof. Systematic breakage (the ±1-word bug:
  0 hits vs thousands of fails) still trips it.
- Fixture forensics worth keeping: 37-word feeds fragment runs through
  sDec singles (which correctly taint records — the worker walk covers
  packets only); the big suite therefore proves safety but not
  engagement, hence `ANN_SMOKE` (same runs, fresh held buffers, 6
  scenes) asserting hits>0 + verifyFails==0 + bit-compare.
