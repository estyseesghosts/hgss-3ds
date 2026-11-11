# Claim sheet: P2a geometry flags (listcache+statededup+fusehash+lazyinsert)

```text
Hypothesis:               The four geometry flags together remove repeated
                          command-interpretation work in built scenes
                          (list-cache hits skip hidden-vertex scans;
                          fusion/dedup cut repeated state work; lazy insert
                          cuts insert/evict traffic), without changing pixels.
Target scene:             Frozen Goldenrod save, hg-continue-field.txt (rest)
                          and hg-field-walk.txt, frames 9000+.
Target hardware:          Old 3DS; Azahar 2126.2 O3DS-sim for activation and
                          work proof. Costs on hardware (final packet pair).
Baseline artifact:        PROFILE dc183349… (620fa70, dirty no, default flags;
                          code-identical to HEAD apart from docs-only commits).
Candidate artifact:       PROFILE +LISTCACHE +STATEDEDUP +FUSEHASH +LAZYINSERT
                          (same tree, env flags only; SHA recorded at build).
Activation proof:         List-cache hit counters non-zero in both scripts;
                          (statededup/fuse/lazy have their own census lines —
                          record which fire).
Work proof:               Bounding-box scan work per built scene falls
                          (P1 `built N (ms/built)`: candidate ms/built < baseline
                          beyond trial spread); list-cache hit rate reported;
                          total worker busy ticks per window falls or holds.
Correctness proof:        `make port-tests` gx3d suites bit-identical
                          (fixtures unchanged); host fixtures unchanged;
                          `make tool-tests` no new failure; captures at fixed
                          script frames unchanged from baseline.
Hardware result:          Paired A/B on the console (owner run).
Regression surface:       Battle, title, menus, transition, audio, sleep/close.
Field result (Azahar, rest): REVERT the bundle. Baseline 176 built @
                          46.1 ms/built, 124 skipped; bundle 150 built @ 52.5
                          ms/built (+14% per built scene), 150 skipped.
                          Composes 20.85 [20.30..21.50] vs 15.00 [15.00..15.10]
                          (-28%, non-overlapping). The flags engage (list-cache
                          28311 lookups / 9238 hits / 551 inserts, audits 4 fail
                          0, fuse-mismatch 0), but per-scene overhead (tags,
                          audits, census, lazy filter, evict traffic) pushes the
                          saturated worker over the skip cliff: fewer scenes
                          built, fewer composes. No audit/verify failures
                          (no stop). Walk A/B confirms: composes 22.45 [21.90..22.80] vs 15.05
                          [15.00..15.30] (base better, -33%); geom_ms 31.10
                          vs 29.96 (blended-denominator effect). Bundle
                          reverted on both scripts; no default flip.
Conclusion:               harmful to throughput in Azahar (revert as a bundle;
                          no per-flag fishing). Hardware could differ, but the
                          plan accepts on the Azahar work proof + OFF agreement,
                          and the work proof fails here.
Next decision:            revert (no default flip). Do not weaken the flag
                          tests. Record the walk outcome below when it lands.
```

## Run manifest

Manifests + trial logs under `build/bench/ab-p2a-{field,walk}/`
(`bench_ab.py` manifest + `p2a-*.manifest.json`).

## Notes

- One hypothesis: the four flags as a bundle (they shipped as a bundle in
  the campaign: "ship shape (fusehash+lazy)" + tables + statededup). If the
  bundle wins, per-flag split is NOT required by the plan; if it is neutral,
  do not go fishing per flag — revert the bundle.
- Env vars reach lib via `3ds_port/nitro/Makefile` and app via
  `tools/hgss_game.py`; FUSEHASH/LAZYINSERT are lib-only, LISTCACHE/
  STATEDEDUP are lib+app. The flip must touch each flag in the right place(s).
```
