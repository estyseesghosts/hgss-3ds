# Run 26: stale sorted OBJ state after display disable

## Resume point and hardware evidence

Resumed from the supplied session transcripts. The interrupted session left
the production fix and a failing-before fixture in `../hgss-obj-disabled-state`;
neither had been integrated. This session reviewed that diff and strengthened
the fixture before integration as **f481089**.

Run 26 identifies **79afbd3**. It does **not** demonstrate a Goldenrod gain:

| Interval-end samples | Run 25 | Run 26 |
| --- | ---: | ---: |
| 2400–4200 compositions/s | 7.925 | 8.075 |
| 2400–4200 drawer frames/s | 1.550 | 1.575 |
| 2400–4200 render wall ms/frame | 415.928 | 419.935 |
| 4800/5400 compositions/s | 8.750 | 8.000 |
| 4800/5400 drawer frames/s | 2.700 | 2.350 |
| 4800/5400 render wall ms/frame | 181.790 | 197.745 |

These are means of interval samples, not true CPU times or delivery percentiles.
Later manual play/battle samples are not included. At frame 5400, 24 committed
behind regions still accompany **4608 empty CPU lines = 24 × 192**. The shipped
empty-plane shortcut is not removing those walks on this hardware path.

## Concrete defect and bounded correction

`DrawEngineT` retains an `ObjFrame` per engine. `SortObjs` sets its count when
DISPCNT enables OBJ rendering; previously no code reset that count when OBJ
rendering was disabled. GPU region/atlas selection and the all-text/no-sprite
shortcut subsequently read the old count, even though the ordinary software
sprite drawing correctly checks DISPCNT. A previous scene's ordinary sprite can
therefore be nominated as a ghost GPU quad; even a window sprite (which is not
nominated) can block empty-plane admission indefinitely.

The fix sets **only `objFrame->count = 0`** in the disabled-OBJ branch, before
those consumers. Existing resets of blends/art/taken state remain. Re-enabling
OBJ still sorts current OAM. No OAM contents, cache keys, writer tracking,
allocation, renderer eligibility, scheduler, audio, game clock, benchmark inputs,
or compositor ownership changes.

This defect is reproduced locally. It is a plausible explanation of the run-26
gate failure, **not proof that hardware's persistent count had this value**.
No new diagnostic counters are needed: compare existing empty lines, committed
regions, drawer throughput and display cadence on the unchanged benchmark.

## Executable validation

The actual-renderer fixture now renders an OBJ-enabled scene, disables OBJ
without clearing OAM, renders/scrolls GPU BGs, and re-enables OBJ. It covers
normal, semitransparent and window sprites, with both live and captured state.
It checks empty count, absence of sprite quads/atlas, no CPU scanline walk,
current BG metadata and pixel equivalence to forced full renders, including
restored sprites. Empty over pixels leave alpha untouched: reference and
candidate alpha buffers are given identical prior contents, not compared as
though invisible alpha were freshly written.

- Candidate: **955 checks, 0 failed**, also ASan/UBSan.
- Same expanded fixture against main before this fix (**41501c6**): **22 failed
  checks** (957 evaluated, since the buggy path emits an extra quad).
- `check_patches.py`: **86/86**, 0 missing.
- `make test`: **461 tests, 0 assertion failures, 11 errors, 13 skips**. The
  error cases match the previously documented native toolchain limitations.
- `make sdk-tests`: blocked by native clang's unsupported
  `-fno-aggressive-loop-optimizations`; `make port-tests`: unavailable host32
  native link (armv4t/libc symbol failures). Neither is called passed.
- Native HG/SS: **1092/1092 compiled, 0 failed**, clean links, fresh SDK
  archives, `validate_3dsx` **[]**. Clean build **96b3131**, with versioned
  binaries/sidecars and full hashes recorded in STATE. First build attempt
  omitted `DEVKITARM` and failed library setup; rerun with
  `DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM` succeeded.

Logs: `build/obj-disabled-validation/`. No patch-stack or memory-layout change;
DS matching/layout reruns are not claimed. Host32 links, overlay self-check,
symbol cross-check, real golden/emulator images and Old/New 3DS functional,
visual, HOME/sleep/close and performance gates remain **pending**. W2 battle UI,
W1 timing/pacing closure and the 30-fps finish line are not closed by this fix.
