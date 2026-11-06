# Half A wave-2 status: integration + L4 dormancy investigation (lead)

Follows `halfA-integration-wave2.md`. All Half A wave-1+2 code is
implemented, host-proven and Azahar-smoked (details there). This note
records a dormancy found in L4's in-game engagement and the evidence so
far. **L4 stays off by default: fixture-proven exact, zero production
risk either way.**

## What works

- Fixtures: 8 tests, 1076 checks, 0 failures — band takes fire on
  icon-animation frames, every band bit-exact vs whole, 392 vs 4800
  scanlines (12.2x) on the icon sequence.
- In-game (Goldenrod field menu, all flags on): 0 diag mismatches
  across every smoke; visuals correct; no asserts; G3 census live
  (74k lookups, 2.5k hits, 0 audit fails).

## What doesn't (yet): takes never fire in-game

`partialB: 283 frames (0 obj-band, 0 bg-band, 1 unchanged, 59 whole)`,
`try-unmodelled 220, try-under-mismatch 0, refresh-adopts 161,
refresh-invalidates 0, epilogue-invalidates 2, epilogue-entries 163`.

## Eliminated by evidence

- **Stale build.** Relinked deterministically (identical SHA-256);
  archive object byte-identical to the fresh compile; log shows the
  newest counters. The numbers reproduce across rebuilds.
- **POWCNT1 swap bit churn.** Masked it in L4's rest (`skipSwap`);
  numbers bit-identical. Not the churn source.
- **Wrong API/underPtr flapping.** 0 under mismatches; `under` stable.
- **Allocator-epoch issues.** N/A (L4 has no cache).
- **Invalidation storms.** 2 epilogue + 0 Refresh invalidates total.
- **Second Try driver.** Single call site; no function-pointer use.
- **Duplicate `sPartial`.** One definition; 0 duplicate symbols at link.
- **Flag off.** `[2d] partialB=on` logged; Try entries counted.

## Open contradiction (needs A0 hardware ledger, not more armchair)

223 `!valid` entries require 223 invalidates-or-initial, but only 2
invalidates exist and adopts (161+1) should stick `valid=1`. The
epilogue ran 163 times while Try claims 282 whole renders. Every
in-code path was audited (no early returns, no alternate renderers,
single caller, single static). A live-watch of `sPartial` via the
Azahar RPC memory read during a run, plus the A0 per-stage ledger,
is the next step — filed as A0's first question.

## Most suspicious remainder

`rest` differed on 59/60 valid entries. `rest` excludes OAM/swap-bit
but still includes BG palettes, H-blank line regs, VRAMCNT, layout
(win serial, wide-art serial, gpuBgs, smoothObjs, circles). One of
these churns per B render in the field menu; per-component rest
logging (deferred to A0 to keep this slice small) will name it.

## Wave-2 integration (was the file's original topic)

Follows `halfA-integration-wave1.md`. Lanes: G wave 2 (G3 bounds cache +
G5 matrix/state dedupe, `halfA-laneG-wave2.md`), L wave 2 (L4 band redraw
+ L5 band-diff publish accounting, `halfA-laneL-wave2.md`).

## Lead-found defects in lane code (all fixed)

1. **G flags never reached libnitro.a.** `tools/hgss_game.py` wired
   `HGSS_GX3D_HOTLOOP/INDEXED` into APP cflags only; `port_gx3d.c` (and
   `port_gx3d_vertex.c`) compile into libnitro.a via
   `3ds_port/nitro/Makefile`, which forwarded only `HGSS_PALETTE_HASH`.
   Every prior `-gxhot` Azahar run silently tested the off-path, and G's
   wave-2 HG/SS builds never compiled the LISTCACHE/STATEDEDUP regions.
   Fixed by mirroring the palette precedent: Makefile `ifeq` blocks for
   all three SDK-side flags, a combined `.gx3d-mode` marker that
   invalidates `sdk/port/port_gx3d.o` on any toggle, APP cflags for the
   pica-side `STATEDEDUP`, `-listcache`/`-statededup` build-ID suffixes
   and sidecar lines, plus FlagTests/BuildIdTests/sidecar/marker tests.
2. **Two latent compile errors exposed once the flags activated.**
   `u64` in `nitro_port_render2d.h` decls (TUs without NitroSDK types;
   now `uint64_t` per that header's convention) and
   `sStencilOpKnown` used before declaration in `port_gx3d_pica.c`
   (declaration hoisted above `draw_state_reset`).
3. **L3 ext-palette range bug (found by lane L).** The H-bank hash
   wrapped LCDC with `0xAFFFF`, aliasing `0x98000` onto `0x88000`:
   H extended-palette bytes were never hashed. Fixed to `0xBFFFF`.
   The wave-1 diag never fired because the field scene enables no
   ext palettes — a coverage hole the L4 fixtures now close.
4. **Two pattern tests encoded superseded contracts.**
   `test_backface_culled...` asserted the literal
   `if(polygon_culled()){` (HOTLOOP spells it with hoisted state);
   now accepts either spelling, order assertions unchanged.
   `test_lower_screen_offered_every_tick` asserted the old
   "lower30 ignored" behavior; rewritten for the L1 contract
   (default off = every tick, on = every other tick).
5. **`tests/fixtures/gpu_scroll_reuse.c`** called the 2-arg
   `EngineVramSignature`; updated to the 3-arg form with
   `NitroPort_Oam` (identical semantics with the flags off).

## Lead-owned wiring (was "decision needed" from lane L)

- `port.ini` switches `partialB/partialBDiag/bandPubB/bandPubBDiag`
  in `hgss/game/app/main_3ds.c` (same IniOnOff pattern, default off,
  startup log lines), calling `NitroPort_Render2DSetPartialB` and
  `NitroPort_ComposeSetBandPublish`.
- Header decls: `SetPartialB`, `PartialBand`, `PartialBStats` in
  `nitro_port_render2d.h`; `ComposeSetBandPublish`,
  `ComposeBandPublishStats` in `port_pica.h`; removed the local
  `extern` in `port_compose_pica.c`.
- `[perf]` reporting: `partialB:` (frames, obj/bg takes, unchanged,
  wholes, band lines, mismatches) and `bandPubB:` (converted/flushed
  bytes, hinted/uploaded bands, subset violations) log lines next to
  the existing `refHashB:` line.
- Accessors added at the port layer (NULL-tolerant out-params):
  `NitroPort_Render2DPartialBStats`,
  `NitroPort_ComposeBandPublishStats`.

## Correctness gates (wave 2)

- `git diff --check`: clean. `check_patches.py`: 87/87.
- Host: render2d_partial + publish_band (8 tests, 1076 checks, 0 fail),
  gpu_scroll_reuse, gx3d clip/indexed/listcache, Flag/BuildId/
  PerformanceWaveThree suites — all pass.
- 3DS: full-flag SS (`-gxhot-gxidx-listcache-statededup`) links,
  packages, `validate_3dsx []`; nm proves the SDK-side cache is
  really in (4 ListCache symbols vs 0 in the base binary).
- Azahar: full-flag + L-runtime-flags smoke (below); diag smoke next.
- No hardware numbers claimed anywhere.

## Explicitly deferred (needs what this checkout lacks)

- **Allocator hook-up** (`OS_AllocFromHeap`/`OS_FreeToHeap` →
  `NoteAlloc`/`NoteFree`): a new `SDK_PORT_3DS`-guarded patch whose
  DS matching gate needs the `/work` container, which does not exist
  here. The cache is safe without it (sampled hash + 30-scene full
  audit + auto-disable); the hook stays a documented lead task for a
  container session.
- **G4 GPU-transform prototype** (PC float24 harness + Azahar + D3
  hardware drop) and **L6 conditional**: wave 3, gated on A0 census +
  hardware.
- **A0 diagnostic build**: lead, after wave-2 smokes (it instruments
  the landed tree).
- **Half A hardware exit check**: user's Old 3DS run.
