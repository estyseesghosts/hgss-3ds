# P2 Slice A1: game_sources.py + game_overlays.py

## Done (container `dreamy_elion`, repo at `0b81803`, tree clean before/after)

- `tools/game_sources.py` (new): reads `Makefile`, `common.mk`, `filesystem.mk`,
  `main.lsf` in `--root` (default `build/upstream`). `common.mk` variables
  (`LIB_SUBDIRS`, `NITROSDK_SRC_SUBDIRS`, `SRC_SUBDIR`, `ASM_SUBDIR`) derive the
  lib dir lists the way make builds `LIB_SRC_SUBDIR`/`LIB_ASM_SUBDIR`; plus
  `lib/dsprot/src` + `lib/syscall/src` (built separately, linked via overlay /
  `-lsyscall`). CLI: `--root --game-sources --c-sources --cpp-sources
  --asm-sources --lib-sources --port-lib-sources --overlay-map --json`
  (default with no flags: `--game-sources`; `--json` emits all/selected
  categories with counts for the Makefile).
- `tools/game_overlays.py` (new, adapted from platinum's): full `main.lsf`
  parse (`Static main`, `Autoload ITCM/DTCM` with `(.itcm)`-style section
  suffixes, 129 `Overlay` blocks in declaration order = IDs 0..128). `After`
  chains + `Address` placement for regions (all 129 resolve to MAIN; logic
  kept general). Object names are already checkout paths, so
  `overlay_objects()` maps straight to `objdir/`; `object_source()` resolves
  `.c`/`.cpp`/`.s` (`src/lobby/*.cpp`, `src/to91_msl_*.cpp` included) and
  returns None for build-generated `lib/dsprot` `*_encrypted/_encoded/_decoder`
  objects. `rename_sections()` merges `.sinit` **and** GCC `.init_array` /
  `.ctors` into `ovl_sinit_<name>` (C++ ctors run on `FS_StartOverlay`, never
  at program start); `.data.rel.ro` still excluded. `render_table()` emits
  `gHeartgoldOverlays` / `gHeartgoldOverlayCount` for `NitroPort_SetOverlays`.
  `--ids-output` / `--header` write the `SDK_OVERLAY_<name>_ID` ld script and
  `SDK_PORT_OVERLAY_ID_<name>` header (platinum `prepare_game_overlay_ids.py`
  semantics, single source of truth).
- `tests/test_game_sources.py`, `tests/test_game_overlays.py` (new): synthetic
  trees/specs/ELFs (hand-rolled minimal ELF32/64 builder, no toolchain needed
  for parsing tests); compiler+objcopy tests probe and skip without an ELF
  toolchain; real-root class reads `build/upstream/main.lsf` when present
  (129 overlays, OVY_45 = 133 objects: 77 C + 53 C++ + 3 data, 18 generated
  dsprot objects, all regions MAIN).

## Measured (real checkout, container)

`--c-sources` **1012**, `--cpp-sources` **56**, `--asm-sources` **150**
(80 `asm/` + 70 lib), `--lib-sources` **537**, `--port-lib-sources` **259**,
overlays **129**, overlay objects 913 (+ 836 static + 10 autoload = 1759
`Object` entries). `src/text_0205B4EC.c` is compiled but in no lsf block
(only src file not linked). `gameplan.md`'s "159 .s files" is stale: 150
exist in the five listed dirs. `/work/game_sources.json` written (acceptance).

## Verified

- `python3 -I -m unittest discover -s tests` → exit 0, 95 tests, OK (0 skipped
  in container; 9 skips on macOS: no objcopy/ELF-cc/build-upstream there).
- `python3 tools/game_sources.py --root build/upstream --json` → counts above.
- `python3 tools/game_overlays.py --help` → exit 0; full run with
  `--ids-output/--header` verified (table + 129 IDs both forms).
- Two test bugs fixed, both in test code, neither a product change:
  hand-rolled ELF32 used `e_machine=ARM`, which host objcopy (BFD x86-only)
  rejects → i386 (the 32-bit path tests the ELF32 parser, not ARM relocs);
  sort-order expectations (`MSL_C` < `NitroDWC`).

## Open / follow-ups

- `port_lib_sources` keeps `lib/syscall/src` (2 files); the port may replace
  it later — brief's exclusion list didn't cover it, so kept literally.
- Table symbol is `gHeartgoldOverlays` (platinum uses `gPlatinumOverlays`);
  the future `game/app` must reference it in its `NitroPort_SetOverlays` call.
- `hgss/compat`-style fallback: `game_sources.py` imports `hgss_nitro` only
  for its exclude lists; works without it (no filtering) if absent.
