# Claim sheet: drawer change detection (referenced VRAM ranges)

**Date:** 2026-11-10 · **Status:** partly measured in Azahar (Old 3DS mode);
hardware run pending

## What was measured, and what was refuted

| claim | result |
|---|---|
| The drawer's cost is the full-VRAM change hash (864 KB engine A + 304 KB engine B per pass) | **Half right.** Engine B's hash is real and expensive: the bottom pass goes **2.52 → 0.63 ms** in Azahar with `refHashB=on` (4x). Engine A's hash **never fires in the field**: A's state changes every pass, so it takes the "changed → re-render" branch and never reaches the hash. `refHashA` therefore changes nothing here (drawer 4.61 ms either way). |
| The top pass's 50 ms on Old 3DS is the hash | **Refuted.** With A referenced the drawer is unchanged. The hardware `[perf] 2d:` line puts 12.1% of a core in `setup` over 67 passes (≈19 ms per pass) with **0 rasterised lines**, so the top pass is the per-frame setup block, not the hash. |

Kept anyway: `refHashA` is correct (0 disagreements) and fires wherever
engine A is static, which the field is not.

## Numbers (Azahar O3DS, frames 9000+, one run each)

| build | top pass | bottom pass | drawer |
|---|---|---|---|
| before | 2.02 ms | 2.52 ms | 4.61 ms |
| engine A referenced | 2.02 ms | 2.52 ms | 4.61 ms |
| engine A + B referenced (shipped) | 2.02 ms | **0.63 ms** | **2.72 ms** |

## Correctness

`refHashDiag=on` compares each engine's referenced signature with the full
VRAM hash on every pass and forces a redraw on any disagreement: **0
disagreements** over boot → title → Goldenrod field (frames 0-13800), engine A
alone and both engines. Engine B's path is enabled by default on that
evidence; menus have not been exercised and are the open gap.

## Next

`setup` is split into `pal` (FindExtPalettes + MakePalettes) and `key`
(BgKeyCached/FoldClearVariant) in the `[perf] 2d:` line. The next hardware log
attributes the 19 ms per pass without a separate diagnostic run, and decides
whether the target is the palette rebuild or the per-BG key walk.
