# Half A session — 2026-11-06 (lead integration + A0 start)

## Follow-up: L4 counter accounting

The existing `build/halfA-a0-field2/game.log` does contain the A0 redraw
lines. At frame 1200 it reports 283 modelled L4 frames: 59 whole, 1
reported unchanged, and no band takes. The remaining 223 are explained by
the identical-signature return in `PartialBTry`: it returned unchanged
without incrementing `unchangedZero`. That omission is now fixed and the
fade fixture checks that five identical-signature returns are counted.
The 221 unmodelled attempts are separate: they return before `frames++`
and use the legacy path. `tryPostRender=0` is consistent with the retained
state already being valid; no 223 backend renders are implied.

The A0 B redraw breakdown (regs 49, palette 34, OAM 4, VRAM 29,
unchanged 117, mustDraw 1) describes legacy decisions, not the reasons
for the 59 L4 whole fallbacks. The reason for zero in-game band takes is
therefore still unestablished. The capture used build ID
`584305e-dirty-gxhot-gxidx-listcache-statededup-a0diag`, SHA-256
`2b27acbb512c16f95da9a70f12d81fc5f299771e37056765b7dc5be6afce0e50`;
`partialBDiag` was off. A fresh build is not a substitute for a fresh
emulator capture, and neither capture establishes Old 3DS timings.

Fresh Azahar smoke after the counter fix: `build/halfA-a0-counterfix-field`
uses the same build ID, SHA-256
`a20ddc25b198ba427ee60b5c30c8048c81684a16d3ad2b5b11f21b3204717b7c`,
with partialB, partialBDiag, bandPubB and bandPubBDiag on. At frame 1200:
286 modelled frames = 227 unchanged + 59 whole, zero band takes and zero
mismatches; 220 unmodelled, zero under mismatches, 161 refresh adopts,
2 epilogue invalidates, 163 epilogue/range entries, zero try-post-render.
A0 B reports regs 47, palette 34, OAM 4, VRAM 30, unchanged 117,
mustDraw 1. Those legacy counters do not explain L4's whole fallbacks;
zero mismatches does not validate band takes because none occurred. Probe
restored the save, config and log and removed its temporary port.ini.

## Follow-up: fallback attribution and indexed eligibility

The frame-2400 final diagnostic smoke (`build/halfA-a0-idle-probe-field`,
SHA-256 `9c1dddb8099c6369ccf4c5ecfe9cfce1cfd5507402242558c52f54d430a4afdf`)
accounts for 178 L4 whole redraws: 116 shared-state, 61 BG-character,
one oversized OBJ band. Shared rest terms overlap: 113 register, two
layout, two VRAM-control and one palette. Register word 0x50 changed in
112 of those; BG-character changes were all BG1. These are emulator
functional counts, not Old 3DS timings. The shared terms retain the full
safe fallback; bit changes do not alone prove a narrower draw is safe.

Added A0-only branch and rest-term counters (including per-register change
bits and last transition), sampled worker timing, packet-BEGIN G4 candidate
census, PICA stage/byte counters and low-priority per-core idle probes with
a startup calibration. Worker samples are inclusive wall ticks and cover
packet commands only; they include timer overhead and preemption. G4
candidate counts omit other eligibility requirements. Idle is a throughput
proxy relative to startup calibration, including system share; Azahar's
core placement makes its idle percentages non-hardware data. The overlap
ledger, fully keyed source census, full stage attribution, and idle-probe
hardware shutdown/overhead validation remain open.

The original G2 gate tested the global outline option and never indexed
the field even with no allocated outline buffers. The flagged path now
uses the captured scene's outline-enable bit and the compositor's actual
outline-buffer availability; shadow and possible outline passes retain
arrays. At frames 2400/3600 the emulator reported 120 indexed preparations
per 120 scenes, but the batch and compact vertex-byte counts were identical:
9,495,360 each, plus 678,240 index bytes per 120 preparations. Per-triangle
0.1-pixel expansion can prevent equality of shared vertices; this is a
plausible explanation, not a measured hardware result. The emulator is not
grounds to drop the experiment. A separate three-changed-BG sentinel bug
was fixed and covered by the whole-render fixture.

Matching A0-off candidate and no-G baseline builds succeeded and booted to
frame 6000 in serial Azahar probes. Final screenshots match outside the
location-banner rectangle, but captured IO/background memory differ (six
BG bytes), so these are not same-state pixel-equivalence evidence. Screen,
title and battle replay/hardware gates still apply. Three validated binaries:
`halfA-current-baseline-ss` SHA-256 `94bcabb0c5c795eb38baca14e57f9e22a16669e26dd1a3bf9ab78032dda23271`,
`halfA-a0-twin-ss` SHA-256 `a8f49964ee47a8eadb9c34ffe3277dcea891a74e387a61a018c1c168581cb050`,
and the diagnostic above. The latest diagnostic adds an idle-probe shutdown
hardening step (keeps probe stacks until process exit) and has SHA-256
`c6771b609eae318b5cedc297f6f4b7ea36f02c0a387b60e276873396f5508b19`;
`build/halfA-a0-idle-final-field` verifies its startup, 1200-frame reason
counters and idle output in Azahar. The probe forcibly stops the emulator,
so it does not verify HOME shutdown. They are drafts, not a completed D1
drop.

## Hardware logs 34–36 and changes driven by them

`log-o3ds-soulsilver-34.txt` is the no-G baseline and has no partialB
option; `35` is the G+A0 diagnostic with refHashB on and no partialB;
`36` is the G+A0 diagnostic with partialB/partialBDiag/bandPubB and its
diagnostic on. These are not a matched uninstrumented A/B. All booted
Old 3DS, used the Goldenrod script and closed cleanly with no audio drops;
35 ended at frame 6477, while 34 and 36 passed frame 9600.

At frame 4200, log 36 counted 158 partialB whole decisions: 151 BG1
character changes and seven shared-state changes, zero band takes. Logs 35
and 36 each recorded steady blocks of 120 indexed scenes with identical
batch and compacted vertex-byte counts (9,535,680), plus 681,120 index
bytes; compaction took around 1.3 seconds wall per 120 preparations. This
is real hardware evidence of useless work in that scene, though the runs
do not isolate all G flags or diagnostic overhead.

New default-off `partialBChar=on` retains exact bytes for text BG tiles,
maps changed tiles to conservative visible rows, and falls back for H-blank
scroll, mosaic, map changes, multiple changing layers, allocation failure
and wide bands. `partialBDiag` now compares BG bands as well as OBJ bands
against a whole render. Host ASan/UBSan fixtures cover 4/8-bpp, scroll,
unreferenced and offscreen tiles, map/char changes, mosaic, missing memory,
and an intentional stale-pixel diagnostic failure and full recovery.

The flagged indexed path now uses a bounded advisory probe for nearby
identical vertices; if none are found it keeps the array path unchanged.
False negatives affect potential performance only. In the scripted Azahar
field smoke `build/halfA-o3ds36-final-field`, by frame 2400: 54 character
bands, 864 band lines, zero dual-render mismatches; G2 reported 120/120
no-reuse skips in sampled preparation blocks. These are functional emulator
results, not hardware timings.

That smoke exposed a false L5 subset warning: older texture sets can need
historical uploads outside the current L4 band. The checker now compares
the hint only to changes in the current shadow generation, while still
counting and executing every physical upload. At frame 2400 it reports
2160 physical bands, zero current-frame subset violations. The extra old
uploads remain real work and are not suppressed. The final diagnostic
artifact for this smoke has SHA-256
`411ac3a7858777ecf1c068815e22f9419600c3cfa5464105b5f7c4ff96cd376b`.

Log 35 also reports four `refHashB` diagnostic disagreements at frames
2890, 2900, 3474 and 3478. The diagnostic forced redraws at each event;
the old log does not record disagreement direction or prove whether the
changed bytes affected pixels. Standalone refHash remains experimental.
Code review found two independent concrete signature defects: affine maps
have one-byte entries (the old scan paired entries and could miss tile
graphics), and extended bitmap BGs need the full-hash fallback. The L4
OBJ-palette hash also used live palette bytes despite a taken render state;
it now uses the same snapshot as the renderer. Host fixtures cover the
odd affine-map byte, bitmap fallback and a write between snapshot and draw.

The lightweight rescan run `build/halfA-o3ds36-refrescan-field` reproduced
two disagreement pairs at frames 897/905 and 4097/4105. At each first
event the reference hash was unchanged while the full hash changed; an
immediate second reference scan changed while an immediate full rescan did
not. The following event reported the inverse disagreement, with both
rescans stable. This demonstrates that those emulator pairs observed a
VRAM write between the sequential reference/full scans. It is consistent
with, but does not establish the cause of, log 35's hardware events. The
diagnostic still forces a whole redraw on every disagreement; rescan runs
only on disagreement in A0 builds. A prior full-VRAM-copy trace was removed
because its continuous overhead changed scheduling and suppressed the
reproduction.

Final rebuilt artifacts after the signature fixes and low-overhead rescan:
diagnostic `build/halfA-o3ds36-fixes-ss/pokesoulsilver.3dsx` SHA-256
`438594c4d8b23bb362714e2339747a5ffa77190c6417bc98c49f10de9115c488`;
A0-off candidate `build/halfA-o3ds36-candidate-ss/pokesoulsilver.3dsx`
SHA-256 `0931f83fcb70b11cfb2771214b0a2dccdf49a046ddab0d0bdd5097756867b518`.
Both validate_3dsx successfully. `build/halfA-o3ds36-combined-final-field`
exercised the final diagnostic with partialB/partialBChar/partialBDiag and
bandPubB/bandPubBDiag on: by frame 2400, 54 character bands, 864 lines,
zero dual-render mismatches and zero current-frame subset violations; two
120-scene PICA reports each skipped compaction for no sampled reuse.
`build/halfA-o3ds36-candidate-final-field` exercised the final A0-off
candidate through frame 2400 with partialB/partialBChar/bandPubB on and
both dual-path diagnostics off: 55 character bands. Neither establishes
Old 3DS timing or replaces the hardware correctness gates.

Role: lead/integrator for Half A of `HGSS_Old_3DS_Speed_Plan.md`
(geometry + lower screen; native menus are Half B, untouched).
Lane G (geometry) and Lane L (lower screen) wave 1+2 were implemented
by scoped-implementer subagents per AGENTS.md (their reports:
`halfA-laneG-wave1.md`, `halfA-laneL-wave1.md`,
`halfA-laneG-wave2.md`, `halfA-laneL-wave2.md`; integration notes:
`halfA-integration-wave1.md`, `halfA-integration-wave2.md`).
Lead work this session: A-M1, cross-lane integration, correctness
fixes, build-ID/tool plumbing, test updates, A0 diagnostic start.

Tree state at session end: on top of `584305e`, everything below
uncommitted. `check_patches.py`: **87/87, 0 missing**.
`git diff --check` clean. Full `make test` / `port-tests` NOT run
(container-only parts fail on macOS per prior notes); targeted suites
below are green.

## What landed

**Wave 1 (integrated + hardened by lead)**
- **A-M1** (`port_compose_pica.c`, `main_3ds.c`): Platinum lower-menu/map
  textures (768 KiB linear) are lazy now — allocated on first menu demand
  via `EnsureLowerMenuTextures`, `SetAvailable(0)` at init, OOM path
  identical to the old eager-failure path. HGSS never demands them.
  Linear low-water tracking added to the `[perf] mem` log (names
  >=256 KiB drops so the 4.7→2.6 MiB transition can be correlated).
- **G1/G2** (lane G): hotloop micro-opts + indexed triangle submission.
  Lead rewrite of `NitroPort_Gx3dCompactBatches`: bounded 32-entry
  dedupe window (was O(n^2)), caller-provided per-slot scratch (no
  per-scene malloc), clean arrays-fallback on failure. Fixture extended
  with failure-protocol cases.
- **L1/L2/L3** (lane L): functional-but-off `lower30`, A-before-B
  drawer ordering with 3-tick staleness cap, referenced-range engine-B
  hash. Lead fixes: OBJ palette range 0x200→0x400 (icon-only changes
  were invisible), unmodelled display modes fall back to full hash,
  diag disagreements force "changed", deferred frames excluded from
  render accounting, `drawB` staleness fix, `bSkips` telemetry.
- **Build plumbing** (`tools/hgss_game.py`, `nitro/Makefile`,
  `tests/test_hgss_game.py`): `HGSS_GX3D_{HOTLOOP,INDEXED,LISTCACHE,
  STATEDEDUP}` + `HGSS_A0_DIAG` flow to APP cflags *and* libnitro.a,
  with `-gxhot/-gxidx/-listcache/-statededup/-a0diag` build-ID suffixes,
  sidecar fields, `.gx3d-mode`/`.a0-mode` SDK-object invalidation
  markers, and matching unit tests (incl. stale-fixture
  `gpu_scroll_reuse` 2→3-arg update and two Half A contract updates:
  backface-cull HOTLOOP variant, `lower30` now functional).
- **A0 start**: thread-topology logging (O3THR-0: requested vs
  `svcGetProcessorID`/`svcGetThreadPriority` actual), legacy redraw
  reasons per engine (flat/reuseHit/reuseScroll/forced/firstNote/
  layoutChanged/regs/palette/oam/misc/vramChanged/unchanged +
  bytes/scanlines), L4/L5 `[perf]` counter wiring + mustDraw tracking,
  new-statics accessor + header decls.

**Wave 2 (integrated as-is, lane-tested)**
- **G3** bounds cache (epoch + sampled hash + 30-scene full audit with
  auto-disable) + **G5** matrix/state dedupe; **L4** per-part engine-B
  dirty-band redraw + **L5** band-diff publish accounting. Lane L found
  and fixed a real L3 bug: ext-palette LCDC mask `0xAFFFF` aliased
  H-bank bytes to `0x88000` (never hashed); now `0xBFFFF`.

## Verification (this session)

- Targeted suites green: FlagTests+BuildIdTests (20 tests),
  PerformanceWaveThreeTests (6), gpu_scroll_reuse (1),
  gx3d_indexed + gx3d_listcache + render2d_partial + publish_band
  (10 tests, incl. 12.2x scanline reduction fixture: 392 vs 4800),
  W5 texture-hint (2 tests, confirms lane-G header changes didn't
  regress the pre-existing texture experiment).
- Azahar smokes (Old 3DS model, frozen Goldenrod, frame 1200 unless
  noted): baseline HG+SS (field correct, lazy-menu log line present,
  linear_free 17085 KB); L-flags (0 diag disagreements, 141 deferred,
  ref 994 KiB vs full 8.4 MiB); G-flags (index buffers exactly +48 KiB,
  visuals correct); full+G+L combined (G3 census live: 74734 lookups,
  2559 hits, 381 inserts, 125 evicts, 0 sample-fails, 4 audits 0 fails);
  2400-frame phase-separation run.
- Artifacts carry distinct build IDs + sidecars, e.g.
  `584305e-dirty-gxhot-gxidx-listcache-statededup-a0diag`
  (sha256 `2b27acbb…`).

## Problems met and how they were solved

1. **Experiment flags never reached libnitro.a.** `tools/hgss_game.py`
   only set APP cflags; the SDK makefile forwarded only
   `HGSS_PALETTE_HASH`. Result: every prior `-gxhot` Azahar run was a
   no-op for HOTLOOP, and G's wave-2 builds never compiled the
   LISTCACHE/STATEDEDUP regions. Fixed by mirroring the palette
   precedent: `ifeq` blocks in `3ds_port/nitro/Makefile`, marker files
   invalidating the affected SDK objects on toggle, APP-side `-D` for
   the PICA-side STATEDEDUP. **Lesson: a flag without a propagation
   test is a no-op claim — the new FlagTests cover APP *and* the
   marker invalidation path.**
2. **Two latent compile errors surfaced only once flags activated**
   (`u64` in a header TUs include without NitroSDK types →
   `uint64_t`; `sStencilOpKnown` used before declaration). Same root
   cause as (1): dead-by-default code that never compiled.
3. **Timestamp-based staleness reasoning is unreliable here.**
   `ar`/relink mtimes order oddly under this build; an identical-hash
   relink is the real freshness proof (deterministic output), plus
   content checks (`nm`/strings for new symbols). Do not infer
   staleness from `ls -l` ordering.
4. **L3 ext-palette aliasing** (above): caught by lane L's fixture
   work, fixed in the shared range function both paths use.
5. **Pre-existing test failures were Half A contract changes** (the
   two `PerformanceWaveThree` tests) plus a stale 2-arg fixture call;
   updated in-tree with the new contracts, not waived.

## Open questions (not resolved this session)

1. **L4 in-game dormancy (largest open item).** Fixtures prove band
   takes with bit-exact output, but every Azahar run shows
   0 obj/bg takes, 0 mismatches, and a `tryPostRender` counter stuck at
   0 despite 282 counted Try entries. Ruled OUT by evidence: stale
   binary (fresh-dir builds + deterministic relink + `nm`-confirmed
   symbols), wrong flags (sidecar + log banner confirm), crashes or
   restarts (single boot banner, game completes), underPtr mismatch
   (counter 0 — first Try entry must be unmodelled, consistent with
   boot-phase VRAM-display/title states), duplicate `sPartial` or
   duplicate render2d.o (0 duplicate symbols; single archive member).
   NOT ruled out: (a) the smoked a0 binary may predate the newest
   A0-reporting wiring (tryPostRender/8-arg Debug/perf-log block) —
   the hash-verified relink test covered a *different* out dir, so a
   clean A0 rebuild + re-smoke is still required before concluding
   anything; (b) a genuine engagement gap (e.g. rest churn every
   frame — the new A0 redraw/ mustDraw counters exist precisely to
   answer this, but their first smoke predates confirmation they
   print). NEXT STEP (in order): clean rebuild of the A0 diagnostic
   from current source, verify `[a0] redraw` lines appear, re-smoke
   with `partialB=on`, read tryPostRender + mustDraw + rest-term
   breakdown. Do NOT touch L4 logic until that data exists.
2. **O3THR-0 (thread topology on hardware).** Azahar reports *all*
   threads on core 0 — the emulator does not honor core affinity, so
   this is explicitly NOT a hardware result. The A0 `[a0] thread`
   lines exist for the hardware drop to answer it.
3. **A0 remainder** (ledger overlap-subtraction, idle probes, geometry
   stage counters, display-list source census, A0-only diagnostic +
   uninstrumented twin, D1 drop packaging): designed, not yet
   implemented. G4 prototype and L6-conditional likewise await the A0
   census + hardware.
4. **Allocator hook for G3** (heap free/alloc → epoch bump) needs a
   patch-stack change + the DS matching gate (container-only); deferred.
   The 30-scene audit + auto-disable is the interim backstop.
5. **A-M1 second half**: linear low-water logging is in; attributing
   the 4.7→2.6 MiB drop to a named owner still needs the A0 ledger.

## Artifact inventory (uncommitted working tree)

- Build dirs with sidecars: `halfA-base-{hg,ss}` (baseline),
  `halfA-gxidx-ss`, `halfA-g3g5-{hg,ss}`, `halfA-full-ss` (all G + L
  runtime flags smoke-tested), `halfA-a0-ss`
  (`584305e-dirty-gxhot-gxidx-listcache-statededup-a0diag`).
  Azahar capture dirs alongside (`halfA-*-field*`, incl. a 2400-frame
  phase-separation run).
- New unit-test files: `tests/test_gx3d_indexed.py`,
  `tests/test_gx3d_listcache.py`, `tests/test_render2d_partial.py`,
  `tests/test_publish_band.py` + fixtures `gx3d_indexed.c`,
  `gx3d_listcache.c`, `render2d_partialB.c`. (The `texture_cache_hint*`
  / `texture_hash_palette*` / `port_texture_hint.h` files are W5's,
  not this session's.)
- Follow the drop schedule in `HGSS_Old_3DS_Speed_Plan.md` §2 for any
  hardware run: same save/script/`port.ini`, stereo slider off, same
  scene through the idle window, run to frame 9600.
