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
Candidate artifact:       PROFILE+instrumentation at this commit (to be built);
                          OFF twin from the same commit. Azahar numbers are
                          activation only.
Activation proof:         Each new counter observed non-zero in both scripts
                          (built vs skipped split sums to done; compose causes
                          sum to composes; top-pass parts sum to within 10% of
                          the pass; idle % printed for both cores).
Work proof:               Attribution table: core 0 and core 1 work by thread
                          plus idle, before and after, each within 10%.
                          No rendering/scheduling/pixel change: OFF-vs-PROFILE
                          A/B on both scripts must agree within trial spread,
                          and captures at fixed script frames must be
                          unchanged from baseline.
Correctness proof:        `make port-tests` and `make tool-tests` show no new
                          failure (Windows baseline: 506 tool tests, 22
                          environment failures); frame captures unchanged.
Hardware result:          Paired PROFILE/OFF A/B on the console (owner run):
                          instrumentation cost <3%, stage ranking recorded.
Regression surface:       Battle, title, menus, transition, audio, sleep/close
                          (instrumentation is read-only; any new drop fails it).
Conclusion:               not established (code not written).
Next decision:            write code / revert if cost >3% or any sum fails to
                          close within 10% (Accept when: built+skipped==done
                          every sample; causes sum; parts within 10%).
```

## Run manifest

P0 manifests: `build/bench/p0-profile.manifest.json`,
`build/bench/p0-off.manifest.json`; trial logs
`build/bench/ab-p0-{field,walk}/`. P1 re-runs both scripts on the instrumented
pair and appends the attribution table to `2026-11-10-o3ds-000-results.md`.

## Notes

- PROFILE-only: every addition compiles to nothing under `HGSS_PROFILE_OFF`.
  No logging/formatting/locking inside inner loops.
- Keep every old `[perf]` field so `bench_report.py` still parses old logs;
  new fields are additive; `tools/bench_report.py` learns them with tests.
- If PROFILE costs >3% vs OFF, or the idle probes change the ranking of
  stages, reduce the instrumentation first (plan P1 stop).
