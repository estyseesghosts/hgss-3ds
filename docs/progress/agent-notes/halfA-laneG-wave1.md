# Lane G wave 1: geometry hot loop and indexed submission

## Slice

G1 hot-loop specialization and opt-in G2 indexed vertex compaction. Both are
3DS-only and remain disabled unless `HGSS_GX3D_HOTLOOP=1` and/or
`HGSS_GX3D_INDEXED=1` is set.

## What changed

- The geometry translator has guarded local aliases and a specialized lighting
  and culling path; the default path is unchanged.
- The PICA path can compact identical vertices within each state batch and
  submit the unchanged batch order through a `uint16_t` index stream. It is
  disabled when edge marking or sun shadows are active, so those paths retain
  their existing vertex/pruning semantics.
- Build IDs and sidecars identify `-gxhot` and `-gxidx` while retaining the W5
  texture profile, texture hint, and palette hash fields.
- The synthetic indexed fixture specifically exercises cross-batch source
  overlap and confirms deduplication does not cross state-batch boundaries.

## Validation

- `HGSS_GX3D_TEST_CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all' /Users/terezi/hgss/bin/python -m unittest tests.test_gx3d_clip_matrix tests.test_gx3d_indexed`: **3 tests OK**; geometry fixture **1,086,876 checks, 0 failed**.
- `python3 -m unittest tests.test_hgss_game.FlagTests tests.test_hgss_game.BuildIdTests`: **18 tests OK**.
- `python3 tools/check_patches.py`: **87/87 applied, 0 missing**.
- HG and SS opt-in 3DS builds: **linked and packaged**; final package sizes are HG 5,815,408 bytes and SS 5,815,464 bytes. `validate_3dsx` returned `[]` for both.
- Final sidecars identify `584305e-dirty-gxhot-gxidx` and both experiment fields as `yes`.
- Final ARM disassembly/nm evidence: `NitroPort_Gx3dCompactBatches` is `0x240` bytes, `NitroPort_Gx3dBuildBatches` is `0x928`, and `NitroPort_PicaPrepareScene` is `0x818` in both final ELFs. The compact helper contains explicit `malloc`, `memcpy`, and `free` calls for safe in-place batch packing. The geometry object still contains an `__aeabi_ldivmod` relocation in the existing `scale_view` helper; no claim is made that this unrelated division was removed.

## Limitations

- No Old 3DS timing run, Azahar comparison, or hardware render capture was
  available.
- The prescribed host32 vendored build was attempted earlier but macOS clang
  rejected `-fno-aggressive-loop-optimizations`; the prescribed `/work/out`
  location was also read-only. The full devkitARM syntax probe was inconclusive
  because the manually assembled include set exposed an existing `wint_t`
  header failure; full HG/SS devkitARM builds completed successfully.
- The indexed path intentionally excludes edge-marking and sun-shadow scenes;
  expanding that coverage requires a separate semantic review of pruning and
  shadow-only batches.
