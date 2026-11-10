# Claim sheet: <label>

Copy this file to `docs/bench/claims/<date>-<label>.md` for every experiment.
One primary hypothesis per sheet. The fields are the PICA200-BUILDING.md
Packet A / PICA200-INSTRUMENTATION.md §I contract; keep every line even when
the answer is "not established".

```text
Hypothesis:               What costly operation should disappear, and why?
Target scene:             Frozen save, input script, frame window
Target hardware:          Old 3DS; clock mode, core/share, stereo, smooth3d
Baseline artifact:        Git SHA + dirty state + .3dsx SHA-256 + flags + config hash
Candidate artifact:       Git SHA + dirty state + .3dsx SHA-256 + flags + config hash
Activation proof:         Where does the changed code run, and how many times?
Work proof:               Before/after CPU work, bytes, draws, waits, uploads
Correctness proof:        Same-state reference + edge cases (which ones)
Hardware result:          Paired A/B runs and their frame-time distribution
Regression surface:       Battle, title, menus, transition, audio, sleep/close
Conclusion:               beneficial / neutral / harmful / inconclusive, with cause
Next decision:            keep / revise bounded issue / revert / archive
```

## Run manifest

Attach the JSON written by `tools/bench_manifest.py` (or its SHA-256) and the
`log.txt` each run produced. A claim without both is not evidence.

## Notes

- Baseline and candidate must differ in exactly one measured variable.
- `tools/bench_report.py --check-manifest` must pass for both runs.
- Azahar numbers are functional coverage only; the performance claim needs
  the console.
- Record negative results too: "no measured gain" is a conclusion, not a
  failure of the process.
```
