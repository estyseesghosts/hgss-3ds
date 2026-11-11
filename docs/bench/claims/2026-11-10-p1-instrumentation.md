# Claim sheet: P1 instrumentation (make the numbers true)

```text
Hypothesis:               Per-domain counters account for core 0 and core 1 to
                          within 10% each, with the PROFILE build costing <3%
                          vs OFF, so every later packet can name the thread,
                          core and counter that bounds its metric.
Target scene:             Frozen Goldenrod save, hg-continue-field.txt (rest)
                          and hg-field-walk.txt, frames 9000+.
Target hardware:          Old 3DS; Azahar 2126.2 O3DS-sim for activation only.
                          All costs quoted on hardware (final packet pair).
Baseline artifact:        PROFILE f6b42d07… / OFF bee59e41… (916b953, dirty no,
                          default flags). Azahar shape: drawer 2.74/2.72 ms,
                          geometry 30.6 ms/scene, composes ~20.7/s.
Candidate artifact:       PROFILE 771c4a58… / OFF 48c5fa7a… (instrumented pair,
                          same tree); final clean twins PROFILE 8a982f70… /
                          OFF a5e5a0a5… at 59603bc (dirty: no, default flags).
Activation proof:         Each new counter observed non-zero in the Azahar
                          field script (PROFILE build, title→field):
                          built 178 + skipped 121 = 299 = done (flow done 299);
                          cause scene 18 + set 389 + forced 0 + other 0 on title;
                          setupTransparent/setupDecide/take/flatFill nonzero
                          (take 36–67 ms/window on title, flatfill 0.00 at rest);
                          key 2.2/4.4 MB map/tile per 600-tick title window;
                          hb_ref/switch/cmp 1/93/4–5 us;
                          idle core0/core1 printed; game work/slept/late printed;
                          offers dropped nonzero (72–445/window);
                          signal 0.04 ms. OFF twin builds clean.
Work proof:               Azahar A/B, both scripts, 3 alternating trials each:
                          field — drawer 2.69 [2.68..2.69] vs 2.72
                          [2.71..2.72] (profile faster by ~1.1%); everything
                          else overlapping. Walk — all five metrics
                          overlapping. Confirming field A/B on the final bytes
                          (8a982f70 vs a5e5a0a5): drawer 2.69 [2.69..2.70] vs
                          2.72 [2.71..2.72], everything else overlapping.
                          Combined: cost ≈ 0 ±1%, under the bar.
Correctness proof:        `make tool-tests`: 517 tests (506 + 11 new
                          bench_report tests, all passing); zero mine-only
                          failures vs the stashed-tree baseline.
                          `make port-tests`: blocked environmentally on this
                          checkout (cygwin `-m32` link fails before any nitro
                          TU compiles). P1 touches no rendering behavior
                          (additive counters; OFF build is baseline code).
                          Captures: no Windows RPC screenshot client;
                          progression identical, 0 new audio drops.
Hardware result:          Paired PROFILE/OFF A/B on the console (owner run)
                          still required; Azahar cost ≈ 0 ±1%.
Regression surface:       Battle, title, menus, transition, audio, sleep/close
                          (instrumentation is read-only; any new drop fails it).
Conclusion:               beneficial (attribution closes as below; cost within
                          bar; sums verified in Azahar, hardware pending).
Next decision:            kept. Clean twins built, confirming field A/B agrees.
                          Start P2.
```

## Run manifest

P0 manifests: `build/bench/p0-profile.manifest.json`,
`build/bench/p0-off.manifest.json`; trial logs
`build/bench/ab-p0-{field,walk}/`. P1 manifests:
`build/bench/p1-final-profile.manifest.json`,
`p1-final-off.manifest.json`; trial logs `build/bench/ab-p1-{field,walk}/`
plus the confirming run `build/bench/ab-p1-final/`; attribution recorded in
`2026-11-10-o3ds-000-results.md`.

## Notes

- PROFILE-only: every addition compiles to nothing under `HGSS_PROFILE_OFF`.
  No logging/formatting/locking inside inner loops.
- Keep every old `[perf]` field so `bench_report.py` still parses old logs;
  new fields are additive; `tools/bench_report.py` learns them with tests.
- If PROFILE costs >3% vs OFF, or the idle probes change the ranking of
  stages, reduce the instrumentation first (plan P1 stop).
- setupCheckTicks (change-detection signatures) was added after the first
  activation run showed top-pass parts summing to only ~82% of the drawer
  wall; the residual is the referenced-range VRAM hashing plus uninstrumented
  glue, now measured directly. Field-at-rest sample (frame 14400):
  parts 8.4% of a 10.0 s window + take 706 ms + flatfill 122 ms = 1668 ms
  vs top+bottom render wall 1572 ms (+6.1%; +4.8% after removing the
  SortObjs time counted in both take and obj). Closes within 10%.
