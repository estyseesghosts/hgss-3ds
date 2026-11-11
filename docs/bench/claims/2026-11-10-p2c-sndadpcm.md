# Claim sheet: P2c SND_ADPCM (ADPCM mixer diet)

```text
Hypothesis:               The ADPCM mixer diet lowers the audio thread's core
                          share (98% of voices are ADPCM; narrower arithmetic
                          where ranges allow; idle channels skipped) with
                          bit-identical output and zero dropped frames.
Target scene:             Frozen Goldenrod save, hg-continue-field.txt (rest)
                          and hg-field-walk.txt, frames 9000+.
Target hardware:          Old 3DS; Azahar 2126.2 O3DS-sim for activation and
                          work proof. Costs on hardware (final packet pair).
Baseline artifact:        PROFILE dc183349… (620fa70, dirty no, default flags;
                          code-identical to HEAD apart from docs-only commits).
Candidate artifact:       PROFILE +SND_ADPCM (same tree, env flag only; SHA
                          recorded at build). Lib-only flag (port_sound_chip.c
                          via 3ds_port/nitro/Makefile).
Activation proof:         ADPCM voice counts non-zero in both scripts (existing
                          [sndprof] lines); mixed-vs-skipped channel split moves.
Work proof:               Audio share of core 0 holds or falls
                          ([perf] audio % + [sndprof] mix ticks/frame before
                          and after) with 0 dropped frames in both scripts.
Correctness proof:        Bit-identity vs the pristine mixer over the existing
                          host vector set (extend the differential test first);
                          any mismatch = revert, no tuning. Captures n/a
                          (audio); game progression identical.
Hardware result:          Paired A/B on the console (owner run).
Regression surface:       Battle (many voices), title, menus, transition,
                          sleep/close, save.
Conclusion:               not established (code not built).
Next decision:            keep (flip default; env 0 disables) / revert if
                          bit-identity fails or any frame drops.
```

## Run manifest

Manifests + trial logs under `build/bench/ab-p2c-{field,walk}/`.

## Notes

- The campaign never reached a verdict on SND_ADPCM (Exp-22 was still
  "building"): the plan premise overstates it. This A/B is the verdict.
- Narrowing interpolation precision needs a stated range proof per
  changed site; the differential test gates every change before any A/B.
```
