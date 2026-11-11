# Claim sheet: P6 the H-blank walk on the game thread

```text
Hypothesis:               The emulated H-blank walk costs ~2.5 ms of an
                          Old 3DS frame, and most of that is work the
                          scene does not need: the per-line register copy
                          (0x70 bytes x 2 engines x 192 lines) and the
                          compare scan that follows it (43 KB read back
                          from RAM). A line's registers only matter when
                          the picture differs from line to line, so the
                          comparison can be made as each line is kept —
                          against the line before, which is still in
                          cache — instead of in a separate pass over the
                          whole array afterwards.
Target scene:             Frozen Goldenrod save, hg-continue-field.txt
                          (rest, frames 9000+); the H-blank phase's
                          sub-timers are in every FRAMETIME line.
Target hardware:          Old 3DS; Azahar 2126.2 O3DS-sim for the
                          sub-timer split (its ticks are the emulated
                          ARM11's, so the shape transfers); the console's
                          numbers are in the hardware packets.
Baseline artifact:        P3, commit 9759707, clean: PROFILE
                          f26ec12d9f1a6416… (5846072 B), OFF
                          d80bb827e375e048… (5835456 B).
Candidate artifact:      P6, the same tree with the walk's compare fused
                          into the copy loop (SHA recorded at the build).
Activation proof:         The FRAMETIME line's hb_cmp_us (the compare
                          scan's ticks) and hb_keep_us; and the A0
                          diagnostic's count of walks where the fused
                          answer and the old scan disagree.
Work proof:               The H-blank phase's ticks per frame
                          (keep/dma/irq/ref/sw/cmp) before and after;
                          the bytes copied per frame.
Correctness proof:        The host test's H-blank cases (a handler that
                          writes per line -> the lines differ and the
                          effect is kept; a handler that writes nothing
                          drawn -> no line differs) plus the A0
                          diagnostic's scan comparison on every walk.
Hardware result:          Paired A/B on the console (owner run).
Regression surface:       Battle (the line-slides-apart transition), the
                          field's poison wave, window wipes, the title
                          screen, audio, sleep/close.
Conclusion:               see below
Next decision:            keep / revise bounded issue / revert / archive
```

## The premise, measured

Old 3DS, the P1 PROFILE build (`hgss-private/log-1/log-20110110-072845-620fa70-profoff.txt`,
frame 25200, hardware ticks):

```
[25200] FRAMETIME frame=25200 game_us=179 state_us=2647 total_us=2826 hblank_us=2639 handoff_us=2 hb_keep_us=483 hb_dma_us=0 hb_irq_us=831 hb_ref_us=1 hb_sw_us=69 hb_cmp_us=768
[25200] [perf] hbirq: b0 0.00ms/0 b1 0.45ms/193 b2 0.00ms/0 …; sync 1200/10.46ms; pump 1.69ms/600; dma 0 xfers
```

| part | µs/frame | what it is |
|---|---|---|
| `hb_irq` | 831 | 192 dispatches (the SDK's IRQ dispatcher) + the game's H-blank handler (450) |
| `hb_cmp` | **768** | the compare scan: 192 lines x 2 engines x 112 bytes, read back from RAM |
| `hb_keep` | 483 | the per-line copy: 192 lines x 2 engines x 112 bytes, written into a 43 KB array |
| `hb_sw` | 69 | the deferred thread switch (one per walk) |
| `hb_ref` | 1 | the per-walk GPU refresh |
| | **2.15 of 2.64** | attributed of the H-blank phase |

Azahar's split has the same shape (`hb_irq 785 / hb_cmp 473 / hb_keep 497 /
hb_sw 92` of `hblank_us 2490`), and its ticks are the **emulated ARM11's**
— a 112-byte `memcmp` costs ~330 ARM11 cycles, which is exactly the
473 µs the scan takes — so the shape transfers to the console.

The plan's premise holds (≈2.5 ms per frame). The plan's permitted
reductions are the copy and the scan; the dispatch is off limits ("avoid:
skipping dispatches").

## What the walk does, and what it costs

`3ds_port/nitro/src/port_os.c`, `NitroPort_HBlankLines()`:

```c
for (y = 0; y < 192; y++) {
    KeepLine(y);            /* copy 0x70 bytes x 2 engines into a 43 KB array */
    HBlank(y, irq, dma);    /* the dispatch */
}
/* … after the walk: */
for (y = 1; y < 192 && !active; y++) {          /* the compare scan */
    active = memcmp(regs[0][y], regs[0][0], 112) != 0 ||
             memcmp(regs[1][y], regs[1][0], 112) != 0;
}
```

The array is 43 KB — larger than the Old 3DS's 16 KB L1 — so the scan
reads it all back from RAM, and the copies write into it cold. The scan's
only job is to answer one question: *do any two lines differ?* ("some line
differs from the first" and "some line differs from the one before it" are
the same question, because the array is a sequence.)

## The change

The comparison moves into the copy loop, where the line before's copy is
still in cache:

- the array's contents are **unchanged** (the same `KeepLine` calls, in the
  same order) — so the renderer reads exactly what it read before;
- `active` is decided during the walk instead of after it, by comparing
  each line with the one before it, and the comparison stops at the first
  difference (as the old scan did);
- the walk's copy timer covers the compare as well, so the sub-timer split
  stays honest and no extra timer reads are added.

## What it does not do (and what that would take)

The **copy** itself (483 µs) is the other half. Skipping it needs the
walk to know that the game wrote nothing during it — and the only
detection available is the same 112-byte comparison, so the comparison
would have to happen anyway. Doing it properly means changing the
*representation*: keep the distinct per-line states in a small table plus a
per-line index (192 bytes per engine) instead of 192 full copies, and let
the renderer read `regs[same[y]]`. That touches the renderer (four places)
and the display-state snapshot (which copies the whole 43 KB array too).
It is a separate claim sheet, not part of this packet.

## Run manifest

- The sub-timer split before and after: the `FRAMETIME` lines of the A/B
  trial logs (`build/bench/ab-p3-field/`, `build/bench/ab-p3-walk/`) and
  the hardware logs (`hgss-private/log-1/`).
- The host tests: `make port-tests` (the H-blank cases in
  `3ds_port/nitro/tests/test_nitro.c`).
- Hardware packets: `build/bench/sd-hg-p3/`, `build/bench/sd-hg-p3-off/`
  (candidate) with `build/bench/sd-hg-p1/`, `build/bench/sd-hg-p1-off/`
  (baseline).

## Notes

- The two builds' drawer's `end`/`keep`/`cmp` numbers come from the same
  FRAMETIME line, so the comparison is within one run.
- Azahar numbers are functional coverage only; the performance claim needs
  the console.
```
