# Half A lane G wave 2: G3 list-bounds cache + G5 state dedupe

## Slice

G3 (cache display-list bounds so hidden lists cost ~nothing) + G5 (matrix-stack
generation with redundant restore/load skips; stencil-op dedupe) in the
geometry lane, behind `HGSS_GX3D_LISTCACHE` / `HGSS_GX3D_STATEDEDUP`
(default off). Follow-up to `halfA-integration-wave1.md` (G1-partial/G2
landed). Guide rev 2.0 read in full before editing; no conflicts with
`gameplan.md` (it does not cover these items).

## What changed and why

The hidden-list shortcut rescans every skipped vertex stream every scene
(~9k vertices/scene in Goldenrod). A repeated in-memory display list is the
same words from the same source each scene, so its object-space bounds and
trailing command state are cached:

- **G3** (`3ds_port/nitro/src/port_gx3d.c`, port-layer change): each
  `OP_PACKETS` handover is tagged with its source address + length
  (`enqueue_packets`; CPU-FIFO single-word writes have no source and are
  never cached). A vertex run (BEGIN_VTXS to its END/BEGIN terminator) is
  keyed by source + list offset + BEGIN-opcode remainder + pre-list
  coordinates + allocation epoch, with a sampled content hash (first, last,
  8 strided words over the linearized payload range). A hit transforms 8
  box corners with the current matrix, runs the existing hidden test, and
  resumes at the recorded terminator — the vertex stream is never scanned.
  Misses scan exactly as before and insert on success (only fully-tagged,
  all-packets runs; singles abort insertion, never correctness).
- **Invalidation**: explicit API, since the allocator lives outside the
  allowed paths: `NoteAlloc`/`NoteFree` (a free evicts overlapping cached
  ranges), `EpochBump`, plus a sampled-hash re-check on every hit and a
  **full-extent hash audit every 30 scenes that auto-disables on mismatch**
  (mandatory, not optional). Stale-heap reuse is the known risk; when in
  doubt the code falls back to scanning.
- **A0 census feed**: per source-block reuse stats (distinct lists, hits,
  scenes) in the `[gx3d] listcache` diagnostic log lines.
- **G5 geometry** (same file): `matrix_command` returns applied/skipped;
  MTXMODE-to-same-mode, MTX_LOAD/IDENTITY of identical matrices, RESTORE of
  an unmodified slot, and zero-offset POP of an unmodified slot are skipped
  (no matrix copy, no clip-dirty, no lighting-cache clear, no GXSTAT
  refresh — all provably identical). Every applied command bumps a new
  `stackGen` (future G4 chunk key; `NitroPort_Gx3dStackGeneration`).
- **G5 PICA** (`3ds_port/game/app/port_gx3d_pica.c`, state-skip only; the
  `HGSS_GX3D_INDEXED`/texture-profile/hint/palette regions are untouched):
  the remaining uncached per-batch call, `C3D_StencilOp`, goes through a
  cached setter reset per pass. Texture/combiner/stencil-test/alpha/blend/
  depth/fog binds were already deduped in the working tree (wave 1); this
  only adds the stencil-op remainder. No batch merging, no boundary changes
  (O3-3D-2).

## Files changed

(Mine only; the tree also carries the lead's uncommitted wave-1
integration in these files. Every new line below is inside
`#ifdef HGSS_GX3D_LISTCACHE` / `#ifdef HGSS_GX3D_STATEDEDUP`.)

- `3ds_port/nitro/src/port_gx3d.c` — G3 cache (tags, entries, census,
  try/insert, audit, public API) + G5 matrix dedupe (`matrix_command`
  returns int with skip branches, `stackGen`, three caller updates,
  diag lines).
- `3ds_port/nitro/include/nitro_port_gx3d_vertex.h` — appended G3/G5 API
  + stats structs (flag-guarded).
- `3ds_port/game/app/port_gx3d_pica.c` — `set_stencil_op` + 8 call-site
  swaps + reset hook.
- `tests/fixtures/gx3d_listcache.c` (new), `tests/test_gx3d_listcache.py`
  (new).
- `docs/progress/agent-notes/halfA-laneG-wave2.md` (this report).

## Commands run and results

- `test_gx3d_listcache` (12,000 synthetic scenes + transitions/rewrites/
  overflow, two binaries with/without flags, stdout bit-compared):
  **pass**, incl. under `-fsanitize=address,undefined`.
- `test_gx3d_clip_matrix` + `test_gx3d_indexed`: **3 tests OK**.
- `python3 tools/check_patches.py`: **87/87**.
- ARM object deltas (`port_gx3d.o`, armv6k): text 42700 → 48904
  (+6204 B); bss +42336 B (256-entry cache + 512 tags + 64 census).
  No new `__aeabi_*` helpers (same 4 undefs as base).
- 3DS HG build + `validate_3dsx`: **pass** — 1092/1092 game objects,
  linked, `pokeheartgold.3dsx` 5,820,700 bytes, `validate_3dsx` → `[]`
  (explicit re-check as well as the in-build gate).
- 3DS SS build + `validate_3dsx`: **pass** — 1092/1092 game objects,
  linked, `pokesoulsilver.3dsx` 5,820,772 bytes, `validate_3dsx` → `[]`.
- Azahar/hardware: NOT run (lead owns all smokes; no speedup claims).

## Design notes / traps hit

- Display lists arrive as ring *copies*; the only stable cross-scene
  identity is the handover source pointer, captured at `enqueue_packets`.
- An early revision keyed the linearized range wrong for in-block
  terminators (double-counted length, hashing next-entry garbage):
  caught because the test showed ~5900 sample-fails; fixed (`L` is
  `end.c.i - (i0-1)` for E==0).
- An early absolute-vs-masked ring-position mix made tags expire after
  256K words; fixed to absolute positions.
- Test streams must respect the port's 20.12 Matrix convention (raw clip
  units in MTX_LOAD row 3, not `<<12`).
- In-place rewrites at unsampled offsets genuinely trip the audit and
  disable the cache by design (stale window ≤ ~30 scenes); sampled
  offsets rescan transparently. The heap-hook wiring is what makes this
  tight in production (below).

## Known limitations / follow-ups

- Cache tables (256 entries / 512 tags) are sized for the experiment;
  hit rate and churn under heap reuse should be re-tuned from A0/hardware
  numbers. Insert-throttling (e.g. skip re-insert after a sample-miss
  storm) is future work.
- The G5 POP-skip only fires for zero-offset POPs with matching free-slot
  content (rare in practice); LOAD/RESTORE/MODE are the productive skips.

## Decision needed (lead)

1. **Flag plumbing** (tools/ is off-limits to this slice): add
   `HGSS_GX3D_LISTCACHE` / `HGSS_GX3D_STATEDEDUP` env→`-D` wiring in
   `tools/hgss_game.py` (mirroring `HGSS_GX3D_HOTLOOP`), plus build-ID
   sidecar fields, if these are to reach hardware drops. Suggested `-D`
   names are already used consistently in code/tests.
2. **Allocator hook-up** (patch-stack job, NOT in this slice): call
   `NitroPort_Gx3dListCacheNoteFree(ptr, size)` from the game-heap free
   path and `NoteAlloc` from the alloc path. Concretely: upstream
   `lib/NitroSDK/src/os/os_alloc.c` — `OS_FreeToHeap` (line 129) knows
   only `ptr`; the freed size lives in the preceding `Cell`
   (`cell->size`, `HEADERSIZE` = 32); a new `SDK_PORT_3DS`-guarded patch
   should invoke the hook with `(ptr, cell->size - HEADERSIZE)` after a
   successful free (and the symmetric `NoteAlloc(ptr, size)` after a
   successful `OS_AllocFromHeap`, line 76). Display lists also flow via
   NitroSystem's `gecom.c`/`sbc.c` geometry buffer (`MIi_CpuSend32` →
   `NitroPort_GxFifoWords`) and GXFIFO DMAs (`port_dma.c`); both already
   carry usable source tokens — no changes needed there. Until the hook
   lands, sampled hash + 30-scene audit + auto-disable is the backstop.
3. The `[gx3d] listcache src …` census rows are shaped for the A0 source
   census; confirm the A0 owner can consume them as-is.
