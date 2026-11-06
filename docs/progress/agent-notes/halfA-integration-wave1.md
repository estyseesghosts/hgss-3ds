# Half A wave-1 integration (lead)

## Scope

Wave 1 of `HGSS_Old_3DS_Speed_Plan.md` Half A: A-M1 (lead) plus
integration of Lane G wave 1 (G1-partial, G2) and Lane L wave 1
(L1, L2, L3). Lane reports: `halfA-laneG-wave1.md`, `halfA-laneL-wave1.md`.
Guide rev 2.0 (`/Users/terezi/hgss/HGSS_Old_3DS_Speed_Plan.md`'s
`old-3ds-guide.md`) read in full before any edit.

## Lead changes on top of the lane slices

### G2 hardening (`port_gx3d_vertex.c`, `port_gx3d_pica.c`, header, fixture)

The wave-1 `NitroPort_Gx3dCompactBatches` had three defects for hardware:

1. **O(n^2) dedup scan** over the whole compacted prefix per vertex.
   A 10k-vertex single batch would cost tens of millions of compares on
   ARM11. Replaced with a bounded 32-entry window (`GX3D_COMPACT_WINDOW`):
   strip/fan reuse is adjacent, distant duplicates stay duplicated, and
   pixels are identical either way. Cost is O(32n) worst case.
2. **Per-batch `malloc`/`free`** per scene (churn + fragmentation).
   Replaced with one caller-provided scene scratch (`sCompactTmp[slot]`,
   per slot like the vertex buffers, allocated once under
   `HGSS_GX3D_INDEXED`, freed in `clean_up`).
3. **Failure protocol**: the old code returned `vertex_count` after
   partially mutating batches on malloc failure, and callers then drew
   with stale indices. Now 0 is returned before any observable mutation
   (extents pre-validated, scratch is caller-owned), and both call sites
   keep the plain arrays path when packing is impossible.

The fixture (`tests/fixtures/gx3d_indexed.c`) additionally covers the
failure protocol: an out-of-range batch packs nothing and mutates nothing.

### L3 exactness (`port_render2d.c`, `nitro_port_render2d.h`, `main_3ds.c`)

1. **OBJ palettes were not hashed**: engine B's OBJ palettes live at
   `Pltt+0x600`; the referenced range covered only `+0x400..+0x5FF`.
   Now hashes `+0x400..+0x7FF` (BG + OBJ). An icon-highlight-only change
   previously kept a stale signature.
2. **Unmodelled display modes fell through to the referenced hash**: VRAM
   display, main-memory display, display-off, BG modes 6/7 and
   NONE/LARGE kinds now set `modelled = 0` and use the full 304 KiB hash,
   matching the plan's "anything not modelled means a full redraw".
3. **Diagnostic disagreement only logged**: `forceChanged` now treats a
   ref-vs-full decision mismatch as changed, so the experiment can never
   produce a stale skip. Disagreements still log with a count.
4. **Attribution**: `NitroPort_Render2DRefHashStats` exposes referenced
   vs full bytes and disagreement count; the periodic `[perf] mem` block
   logs them as `[perf] refHashB:` when L3 or its diagnostic is on.

### L2 accounting (`hgss/game/app/main_3ds.c`)

- Deferred lower screens no longer count as render calls/time
  (`didRender` gating via `d->offerB`), and contribute no fresh picture
  (`drawB = false` default, fixing a stale-`drawB` inflation of the
  `drawn` counter on both the drawerAB-skip and lower30 off-tick paths).
- `bSkips` is now `volatile` with a `logBskips` snapshot and is reported
  in the per-drawer `[perf]` line as `lower deferred`.

### A-M1 lazy lower menu/map (`port_compose_pica.c`, `main_3ds.c`)

- Compose init no longer allocates the 768 KiB of menu/map textures
  (2 x 512x256x2 B + 2 x 256x256x2 B); pointers stay NULL and the init
  logs the held-back amount. Teardown was already NULL-safe.
- `EnsureLowerMenuTextures()` materializes them on first menu demand at
  the `menu[s]` decision (game wants the menu or it is kept shown),
  sets availability on success, and restores the exact old OOM path
  (NULLs + `SetAvailable(0)` + DS lower screen) on failure. HGSS never
  demands them; Platinum demand flows through the unchanged
  `LowerMenuOn()/KeptShown()` signals.
- Linear low-water tracking in the `[perf] mem` block names the tick of
  any >= 256 KiB drop so the 4.7 -> 2.6 MiB transition can be correlated
  with scene changes vs texture-cache growth (partial A-M1 analysis
  item; full owner attribution rides the A0 ledger).

## Correctness gates (wave 1)

- `git diff --check`: pass. `tools/check_patches.py`: 87/87.
- `tests.test_gx3d_clip_matrix` + `tests.test_gx3d_indexed` under
  ASan/UBSan: 3 tests OK, 1,086,876 checks, 0 failures.
- `tests.test_hgss_game.FlagTests/BuildIdTests`: 18 tests OK.
- 3DS builds + `validate_3dsx`: pending (build running).
- Azahar smokes: pending, serial, by the lead after builds land.
- No hardware numbers are claimed anywhere in this wave.

## Known wave-1 limitations / wave-2 queue

- G1 landed only as guarded micro-opts (local aliases, inlined
  light/cull); the substantive specialization (unlit, texgen-0,
  strip single-transform, outcode-only skip) and disassembly proof are
  wave 2.
- G2's 32-entry window is a tuning parameter; the A0 stage counters
  will show pack cost vs bytes saved on hardware.
- L3's per-BG char-range hash scans full maps to find maxTile; a
  generation-keyed tile census (L4 work) can narrow it further.
- A0 (interval ledger, idle probe, stage counters, source census,
  redraw reasons, thread-topology log) starts after wave-1 integration
  builds pass, since it instruments the landed code.
