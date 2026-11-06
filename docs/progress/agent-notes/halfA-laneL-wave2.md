# Half A lane L wave 2 (L4 + L5)

## Slice

L4 (band redraw for engine B) + L5 (band-diff publish accounting for
engine B) from `HGSS_Old_3DS_Speed_Plan.md` Half A, lane L. Wave-1
(L1/L2/L3 + lead hardening) was read first
(`halfA-integration-wave1.md`, `halfA-laneL-wave1.md`); guide rev 2.0
read in full before editing. Why: an animating touchscreen menu goes
from a full-screen raster to tens of scanlines of OBJ work.

## What changed and why

**L4 (`3ds_port/nitro/src/port_render2d.c`, port layer).** The engine-B
signature is now computed in per-part pieces: `rest` (registers minus
OAM, BG palettes, layout) plus per-BG register/map/tile pieces, OBJ
tiles, OBJ palette and extended palettes (`PartialBParts`, filled by the
refactored `EngineBReferencedSignature`; the legacy combined hash with
`parts == NULL` is byte-identical). Retained across frames: the parts,
the OAM copy, and flat map-entry copies per text BG (the "retained
planes" are signature + OAM + map planes, not pixel planes — see
deviation 1). Each frame: parts equal → return 0 without hashing VRAM;
OAM/OBJ-only change → re-render the scanline band covering old+new
sprite bounds through the *unmodified* pipeline over the drawer's
persistent buffer (unchanged lines keep last frame's pixels, so this is
bit-exact by construction); one text BG's map change → band covering
those tiles; anything else (registers, windows, blend, palettes,
brightness-affecting state, unmodelled modes, power/blank, wide/tall
pictures, buffer swaps) → whole render. Behind `partialB`/`partialBDiag`
switches (default off); the diagnostic dual-path-renders every take and
falls back to whole on any mismatch.

**L5 (`3ds_port/game/app/port_compose_pica.c`, publish path only).**
Confirmed engine B already publishes through `publish_bands` at
texScale 1 (the 1-texel path; unchanged pictures convert/flush 0 bytes
via `publish_unchanged` before any conversion). Added, behind
`bandPubB`/`bandPubBDiag` (default off): per-engine converted/flushed
byte counters on the under-texture upload, an uploaded-band count, and
a checked hint — L4's dirty band must cover every uploaded band (the
diff stays authoritative; a shell-side writer such as the toast still
uploads correctly and counts a subset violation). With the switch off,
publish behavior and stats are untouched.

**Correctness fix found by this slice (inherited L3 range bug).** The
extended-palette hash wrapped LCDC with mask `0xAFFFF`, which aliases
the H bank (`0x98000 & 0xAFFFF == 0x88000`): H-bank palette bytes were
never hashed, so an H extended-palette change could serve stale pixels.
Fixed to `0xBFFFF` (I bank unaffected). One line, in the shared range
function, required so the retained planes key on all inputs.

## Design deviations (deliberate, gate-held)

1. **No retained pixel planes.** Instead of compositing an OBJ layer
   over retained BG pixel planes (384 KiB+ of planes plus a parallel
   merge), band lines are re-rendered with the full pipeline and kept
   lines are provably unaffected. Same gate (bit-exact vs whole on every
   frame), same attribution shape, no O3-2D-14 parallel-merge risk.
   Cost: BG raster is redone inside the band (~tens of lines, row-cache
   assisted) rather than skipped.
2. **L5 does not skip diff work.** The toast mutates `under` between the
   drawer-thread render and publish and is invisible to the port layer,
   so skipping the band diff outside L4's band would drop toast pixels.
   The diff stays authoritative (cheap memcmps); L5 contributes the
   counters and the subset diagnostic that proves uploads stay inside
   the band. "Upload only those bands" then holds by construction and is
   counted, not assumed.
3. **Visibility-set changes take wider bands.** Showing/hiding a sprite
   changes the hashed tile-entry chain even when the tiles were already
   referenced, so the band unions all visible sprites' lines (correct,
   conservative). In-place animation keeps tight bands (measured below).

## Files changed

- `3ds_port/nitro/src/port_render2d.c` — L4: `skipOam` through
  `EngineSmallSignature`; per-part `EngineBReferencedSignature`;
  retained state + `PartialBTry` + `RenderRange` (band form of
  `Render`); y-range params on `DrawEngineT`/`DrawEngine`; hook in
  `NitroPort_Render2DSplitIfChanged` (engine B only); H-bank mask fix.
  With `partialB=0` the legacy path is byte-for-byte the old logic
  (the `Render` restructure preserves the early-return/epilogue
  behavior exactly).
- `3ds_port/game/app/port_compose_pica.c` — L5: `sBandPublishB` switch,
  setter, byte/uploaded-band/subset-violation counters, dirty-band hint
  consumption in `publish_bands` (under-texture path only). Nothing else
  in the file touched (the A-M1 hunk at the top of the diff is the
  lead's, already in the tree).
- `tests/fixtures/render2d_partialB.c` (new), `tests/test_render2d_partial.py`
  (new), `tests/test_publish_band.py` (new).
- This report.

## Commands run and results

- `test_render2d_partial` + `test_publish_band`: **8 tests OK** under
  ASan/UBSan (`/Users/terezi/hgss/bin/python`). Fixture: **1076
  checks, 0 failures, 0 diagnostic mismatches**, every frame bit-exact
  vs a whole render (field-menu icon animation, highlight, hide/show,
  wrap-around + offscreen sprites, text-tile edit, scroll, blend,
  fades, windows, affine BG, H-blank regs, semi-transparent/affine/
  OBJ-window sprites, mosaic, mode/power/wide transitions, buffer swap,
  oversized band, invisible churn, char-only and two-BG changes,
  ext-palette change, plus an L4-off control proving 192-line renders).
- Attribution (diag off): **24 icon frames + 1 tile edit = 392 fast
  scanlines vs 4800 whole (12.2x fewer)**; bands [40,56)-ish per icon
  frame, 8 lines for the tile edit. `refHash` bytes +470400 over the
  run (L4 saves raster, not hashing — the referenced ranges are still
  hashed, now split into parts; honest before/after).
- `test_publish_profile`, `test_wide_battle`: pass (6 tests OK).
- Full `unittest discover -s tests`: 492 tests, **2 failures + 12
  errors, all pre-existing or other-work**: the 12 errors are
  container-only toolchain tests (`-m32` gcc-10, devkitARM/overlay
  tools) plus the stale `gpu_scroll_reuse` fixture (calls 2-arg
  `EngineVramSignature`; broken at baseline by the wave-1 L3
  hardening, outside allowed paths); the 2 failures are source-pattern
  tests over `main_3ds.c`/`port_gx3d.c` content changed by *other*
  uncommitted work in the tree (verified: zero diff of mine in those
  files; the tests fail on the others' edits, not mine).
- `python3 tools/check_patches.py`: **87/87, 0 missing**.
- 3DS builds (devkitARM, build-lock held, unique out dirs, removed
  after): nitro `libnitro.a` OK; **HG 1092/1092 + SS 1092/1092
  compiled, 0 failed, 150 assembled, `validate_3dsx []` on both**
  (`pokeheartgold.3dsx` 5,820,716 B, `pokesoulsilver.3dsx` 5,820,772 B).
  No new warnings in touched code (one `%08x`/u32 format warning at
  `port_render2d.c:4526` and an unused-`GpuBgOf` note are pre-existing;
  the latter is dead S5 code the clean TU now surfaces).
- `git diff --check` on touched files: clean. Azahar: not launched
  (lead smokes serially). No hardware run (no device here; no perf
  claims made — scanlines only, not time).

## Bugs the fixtures caught (all fixed, all still exact)

1. The all-visible union **reset** the band instead of adding to it, so
   hiding the last visible sprite returned "unchanged" with a stale
   picture (fixture `memcmp` caught it). Now unions.
2. `changedBg < 0` conflated "none" (-1) with "two changed" (-2):
   char-only/two-BG changes wrongly returned 0. Now `== -1` for the
   fast paths, everything else falls to whole.
3. Newly shown sprites using already-referenced tiles widen the band to
   all visible sprites (chain effect, documented as conservative).
4. Fixture bugs of mine (no-op first moves, wrong affine map address,
   over/alpha canary comparisons): fixed; the over/alpha canaries now
   prove neither path touches those buffers for engine B.

## Memory accounting (guide §18 Q3)

- `.bss`: `sPartial` (~1 KiB OAM + parts/counters, always resident).
- `malloc` only when `partialB=1`: map flats ≤ 4×8 KiB (only enabled
  text BGs; freed on disable) + 96 KiB diagnostic scratch (diag only;
  freed on disable). OOM degrades to whole renders, never stale pixels.
- Publish: counters only. No linear/VRAM/stack changes.

## Known limitations / follow-ups

- Char-data-only changes and multi-BG changes go whole (no retained
  char prefix; no union-band logic). Scroll/affine-param changes go
  whole (no scroll-shift reuse — R1 territory).
- H extended-palette fix noted above; I-bank was already correct.
- `refHashB`/`refHashDiag` and `partialB` are mutually exclusive in
  effect (L4 bypasses the L3 vram-compare path; both default off).
- Counters are port-layer statics; the lead's `[perf]` reporting can
  read them when wiring the switches.

## Decision needed (plain language)

I could not edit `hgss/game/app/main_3ds.c` or headers per the brief,
so the switches exist as default-off setters plus the exact wiring for
the lead (pattern matches `lower30`/`drawerAB`/`refHashB`):

```c
/* main_3ds.c: static ints sIniPartialB = 0, sIniPartialBDiag = 0,
 * sIniBandPubB = 0, sIniBandPubBDiag = 0; in ReadDisplayOptions: */
if (IniValue("partialB", value, sizeof(value))) {
    if (!IniOnOff(value, &sIniPartialB)) { /* log; */ sIniPartialB = 0; }
    NitroPort_Log("[perf] partialB=%s (engine-B band redraw; default off)", sIniPartialB ? "on" : "off");
}
/* ... same for partialBDiag, bandPubB, bandPubBDiag ... */
NitroPort_Render2DSetPartialB(sIniPartialB, sIniPartialBDiag);
/* + NitroPort_ComposeSetBandPublish(sIniBandPubB, sIniBandPubBDiag); */
/* nitro_port_render2d.h: */ void NitroPort_Render2DSetPartialB(int enabled, int diagnostic);
void NitroPort_Render2DPartialBand(int engine, int *kind, int *y0, int *y1);
/* port_pica.h: */ void NitroPort_ComposeSetBandPublish(int enabled, int diagnostic);
/* then delete the local extern declaration in port_compose_pica.c. */
```

No Azahar/hardware run was possible from here; the diagnostic
(`partialBDiag=1`) is built for the lead's hardware drop. Report has no
device numbers.
