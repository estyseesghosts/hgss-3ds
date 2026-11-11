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
Conclusion:               not established (code not built).
Next decision:            keep (flip all four defaults) / revert per flag if
                          its census stays zero or OFF disagrees / stop and ask
                          if any list-cache audit or verify counter is non-zero.
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
