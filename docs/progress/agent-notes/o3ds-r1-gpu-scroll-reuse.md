# Old 3DS R1 — skip unchanged software scanlines, not metadata

## Scope and baseline

This is a partial **W3/R1** slice of
`HGSS_Old_3DS_Playability_Implementation_Plan.md`, not closure of W1–W3.
The owner requested an actual work-avoidance fix rather than another
diagnostic-only hardware cycle. No scheduling, game-clock, audio, compositor,
game/SDK patches, or benchmark-input changes are included. Battle UI/W2 remains
open. There is no claim of measured hardware acceleration yet.

Run 24 remains the hardware baseline: display-first, build `9228982-dirty`,
moving interval-end samples 2400/3000/3600/4200: mean **7.8 composes/s** and
**27.825 geometry scenes/s**, audio drops zero. These are averages of logged
interval rates, not a frame-delivery histogram. The idle log ends at 5677;
only interval samples 4800/5400 are available, not the full 4800–9600 route.
Runs 23/24 identify different dirty builds, so they are not a strict
same-binary scheduling A/B.

## Actual fix

Rendering could commit two GPU BGs, while change detection excluded scroll
only for the first preliminary `GpuBgOf` result. A second GPU BG's scroll
therefore forced software drawing. Merely masking it would be incorrect:
HGSS's active `hgss/game/app/main_3ds.c` copies GPU metadata only when the
renderer returns a publishable result.

The new path stores **no cached split metadata**. It records only the previous
pixel-buffer identities, conservative hashes, actual committed text-BG mask,
and scroll registers. On GPU-only scrolling it runs the existing setup,
sorting, validation, region commit and quad construction again. Only the
scanline loop is omitted. Current UVs, ordering, dimensions, keys, brightness
and header reach the existing HGSS publication path. No second UV algorithm
or incremental floating-point delta is introduced.

Admission is deliberately narrow: engine A, real 3D, no per-line/window/circle
effects, every displayed non-3D BG actually committed as text, and zero sorted
sprites. OBJ display may be enabled with all OAM entries disabled: the real
SortObjs proves zero sprites, and OAM/DISPCNT remain in the signature. Mixed
software regions and affine BGs keep full rendering. All three buffer addresses
must match. Palette/VRAM/OAM/layout changes force redraw. Forced renders and
blank/off early returns invalidate prior reuse, including the older IfChanged
record through a render serial.

The existing VRAM hash remains; this does **not** claim writer-generation
coverage or eliminate all memory scans. Setup/texture-key work also remains.
New static bookkeeping is 112 bytes on ARM (including 8 bytes added to the
old change record); no new image arrays or allocations. New 3DS scheduling and
launch profile are unchanged; real New 3DS visual/function smoke is pending.

## Local executable verification

`tests/test_gpu_scroll_reuse.py` compiles the actual renderer into a native
synthetic C fixture. HGSS defines u32/s32 as long, which is 64 bits on macOS;
the test-only include shim changes precisely those two typedefs to 32-bit int.
It retains the rest of the SDK header/u64 alignment and asserts widths. This
is **not** a host32 game/link/save-ABI gate or a production SDK change.

Only absent external services (3D image, wide-art/UI services, logging) are
stubbed; register memory, validation, hashing, sorting, pixels, and quads are
the actual renderer. No game content is used. Reference full renders run after
the candidate, so they cannot accidentally establish its reuse history.

- One/two BGs, independently/together scrolling; repeated 511↔0 wrap.
- Four text-map sizes, live and captured registers, static reuse, brightness.
- Palette/tile/OAM/window/H-blank/nomination/layout/affine/engine-B fallbacks.
- Different output buffers, forced render and blank→field restoration.
- Pixel buffers and quad/key metadata compared with full rendering; existing
  line counts prove omitted scanlines, not just identical final pixels.

Commands (native only, from repository root):

```sh
python3 -m unittest discover -s tests -p test_gpu_scroll_reuse.py
HGSS_RENDER_TEST_CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all' \
  python3 -m unittest discover -s tests -p test_gpu_scroll_reuse.py
python3 -m unittest discover -s tests
python3 tools/check_patches.py
```

Results: **757 synthetic checks, 0 failed**, also clean under ASan/UBSan.
Against unchanged `9454557` source the fixture fails (154 failed checks before
the final shared-layout-signature cleanup). Full native Python suite:
**461 tests, no assertion failures, 11 existing toolchain errors, 13 skips**;
the errors are the same unavailable gcc-10/host32/object-format checks as the
baseline. One obsolete source-string assertion expecting a single `gpuBg`
was replaced by committed-mask assertions; executable tests enforce behavior.
Patch stack: **86/86 applied, 0 missing**.

## Remaining gates

Native HG/SS builds and artifact hashes are recorded in STATE when complete.
Real golden scenes, emulator replay, Old/New 3DS smoke, HOME/sleep/close and
hardware performance are **pending**, not passed by these synthetic tests.
No new diagnostic counters or frozen configuration edits are needed to compare
the resulting optimization with run 24 using the existing logs.
