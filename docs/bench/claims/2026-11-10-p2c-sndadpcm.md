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
Correctness proof:        Bit-identity PROVEN on host: the existing
                          differential test (test_matches_reference: 300
                          randomized scenarios x 16 channels incl. ADPCM
                          formats vs the pristine branched reference,
                          sample- AND voice-state-exact) passes with
                          HGSS_SND_ADPCM=1: 81 checks, 0 failures (same as
                          flag-off). NOTE: the stock host test binaries
                          segfault on 64-bit MSYS (the harness truncates
                          pointers for -m32, which this toolchain lacks);
                          proven via 64-bit builds at image-base 0x400000.
                          No test changes needed — the harness already covers
                          the changed forms. Captures n/a (audio); game
                          progression identical in Azahar runs.
Hardware result:          Paired A/B on the console (owner run).
Regression surface:       Battle (many voices), title, menus, transition,
                          sleep/close, save.
Field A/B (Azahar, rest): composes 21.00 [20.30..21.70] vs 22.35
                          [21.90..22.80] (adpcm better); geom 30.66 [30.56..30.82]
                          vs 31.23 [31.00..31.44] (base better); rest overlap.
                          Contradictory micro-deltas = emulator noise, not
                          effects. [perf] audio reads 0.0 ms / 0 dropped of 0
                          in BOTH arms and no [sndprof] line ever logs: the
                          software mixer path (and therefore the ADPCM diet)
                          does not measurably execute in Azahar at all. So the
                          section-2 activation (ADPCM voices non-zero) and work
                          proof (mix share) are unobtainable here — not failed,
                          unmeasurable. Bit-identity stands proven on host;
                          no functional regression on any Azahar metric.
Walk A/B (Azahar, walking): every metric overlaps (frame_ms 8.02
                          [7.87..8.08] vs 8.04 [7.89..8.09]; composes 21.10
                          [20.10..21.70] vs 21.35 [20.80..22.00]; publishes
                          60.00 both; drawer 2.69 [2.69..2.82] both; geom
                          30.69 [30.53..30.85] vs 30.63 [30.54..30.80]) --
                          inconclusive, as the field arm was. The walk's
                          counters tell the same story: [perf] audio reads
                          0.0 ms / 0 dropped of 0 in BOTH arms and no
                          [sndprof] line appears in any of the six walk logs
                          (grep: 0 hits), so the software mixer path does not
                          measurably execute in Azahar at all. Functional
                          no-regression on both scripts: confirmed.
Conclusion:               DEFERRED (not reverted): proven-safe via bit-identity,
                          but the accept's core (audio share movement) needs a
                          console, which never runs per-step diagnostics. Do NOT
                          flip the default on vacuous Azahar evidence. Stage the
                          base-vs-ADPCM pair for the next hardware run, where
                          mix share (26% of core 0 in log-1) is measurable.
Next decision:            no flip; keep flag off by default. P2c is closed as
                          deferred pending hardware: both frozen scripts are
                          covered (no functional regression, nothing measurable
                          in the emulator) and the accept needs the console.
```

## Run manifest

Manifests + trial logs under `build/bench/ab-p2c-{field,walk}/`.

## Notes

- The campaign never reached a verdict on SND_ADPCM (Exp-22 was still
  "building"): the plan premise overstates it. This A/B is the verdict.
- Narrowing interpolation precision needs a stated range proof per
  changed site; the differential test gates every change before any A/B.
```
