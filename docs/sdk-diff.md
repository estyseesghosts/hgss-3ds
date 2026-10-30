# HGSS SDK vs the SDK the Platinum port patches

**Status: PARTIAL (Phase 1 in progress).** Sections marked ✅ were checked
against source (file cited). Sections marked ⏳ are not done; do not rely on
them. Struct comparisons so far are by field list; the offset/size comparison
is the job of the layout probe (`tools/layout_probe/`, not yet written).

Compared trees:

| | HGSS (this port) | Platinum port |
| --- | --- | --- |
| NitroSDK | `lib/NitroSDK` + `lib/include/nitro` in `ctrl-cheeb-del/pokeheartgold` @ `upstream.lock` | `ntrtwl/NitroSDK` @ `eaae40f1…` ("4.2.30001"), via pokeplatinum's `subprojects/NitroSDK.wrap` |
| NitroSystem | `lib/NitroSystem` + `lib/include/nnsys` | `ntrtwl/NitroSystem` @ `21c8fd07…` ("071126.1") |
| ARM7 | C in `sub/lib/src` (145 files) | ntrtwl `libraries/*/src/ARM7` |

Reproduce the file inventory: `python3 tools/sdk_inventory.py <pokeheartgold> <ntrtwl NitroSDK>`.

## 1. Tree and build layout ✅

- HGSS `lib/NitroSDK/src` has 190 `.c`; the ntrtwl SDK has 273. Headers are
  split across `lib/include/nitro/**` (public), `lib/NitroSDK/include`,
  `include-mw` and `include-pokediamond` (the SDK descends from the
  pokediamond decomp). File names differ in case and structure
  (`CARD_backup.c` vs `card_backup.c`, `libraries/<module>/src/…`), so only 213
  of ~570/~620 files match by normalized name, and text diffs are huge even
  for equivalent code (HGSS is a matching reconstruction, ntrtwl is closer to
  the original sources). **Text diffing is not a usable method; compare
  structures and behavior.**
- The vendored `3ds_port/common.mk` expects `build/NitroSDK/include/nitro.h`
  with `<nitro/…>` headers under `$(SDK)/include`, and its `nitro/Makefile`
  lists Platinum-layout sources. HGSS needs `lib/include` on the include path
  and its own source list. Because `3ds_port/` must stay byte-identical, this
  needs an HGSS make fragment passed on the command line or a patch to the
  vendored Makefiles flagged for upstream. **Open decision, see
  `docs/progress/phase-1.md`.**
- `bootstrap.py` links `build/NitroSDK` → `build/upstream/lib/NitroSDK` so the
  vendored paths resolve; the include-path part is not solved yet.

## 2. Assembly ✅ (bigger than the plan assumed)

The plan says HGSS has 3 `.s` files in `lib/NitroSDK`. True, but they are
data only (`fx_sincos.s`, `wvr_data.s`, `mb_data.s`; no `arm_func_start`
anywhere in `lib/` or `asm/`). The assembly is **MWCC inline `asm` in C
files**: 37 files under `lib/NitroSDK/src` contain `asm` functions or blocks
(e.g. `os/os_irqHandler.c`, `os/os_context.c`, `os/os_cache.c`, `mi/mi_memory.c`,
`mi/mi_uncompress.c`, `fx/fx_mtx*.c`, `crt0.c`, `cp/…`, `gx/gxasm.c`), and
about 41 more files in `lib/NitroSystem`, `lib/MSL_C`, `lib/NitroDWC`, `src`
and `sub`. GCC cannot compile MWCC `asm` syntax. Platinum patches 0003/0006
("C versions of the assembly") covered one `.s` plus some functions; HGSS needs a
C replacement or `SDK_PORT_3DS` skip for each of these, decided file by file.
(grep: `\basm\s*\{|asm\s+(void|u32|int|BOOL)` and `^\s*(asm|static asm)\b`; counts
are approximate until the compile pass lists real errors.)

## 3. PXI

- **Tags ✅.** `PXIFifoTag` in `lib/include/nitro/pxi/fifo_shared.h` and
  ntrtwl `include/nitro/pxi/common/fifo.h` have identical names and values for
  tags 0–16 (`EX`…`CTRDG_Ex`); `PXI_MAX_FIFO_TAG = 32` in both. HGSS lacks
  `PXI_FIFO_TAG_CTRDG_PHI` (would be 17); nothing in the port needs it.
- **Command constants ✅ (finding), payloads ⏳.** Platinum's virtual ARM7
  (`3ds_port/nitro/src/port_rtc.c`, `port_pm.c`) uses SDK symbols
  `RTC_PXI_COMMAND_*`, `SPI_PXI_COMMAND_PM_*`, `PM_UTIL_*`. HGSS's `lib/` does
  not define those names: its RTC/PM/TP/MIC code uses literal numbers
  (e.g. `lib/NitroSDK/src/pm.c` handles commands `0x60`, `0x62`,
  `0x70..0x74`). Either a compat header defining the Platinum names with
  HGSS's numbers, or HGSS-only patches adding them, is required. The numeric
  values themselves still have to be compared per command (card, RTC, touch,
  PM, sound, mic).

## 4. `OSSystemWork` ⏳→partial

Field lists: HGSS `lib/include/nitro/os/common/systemWork_shared.h` has 40
entries, ntrtwl `include/nitro/os/common/systemWork.h` has 41. The only
difference is naming: HGSS has `u8 padding3[2]` where ntrtwl has two `u8`s
(`ctrdg_SetModuleInfoFlag`, `ctrdg_IsExisting`), the same 2 bytes. Offsets are
**not yet confirmed**; the layout probe must include `OSSystemWork`. Any
Platinum patch or port code naming `ctrdg_*` needs an HGSS equivalent.

## 5. Sound shared work ✅ / ARM7 driver ⏳

`SNDSharedWork` (HGSS `lib/NitroSDK/include-pokediamond/nitro/SND_work_shared.h`,
size `0x280`: `finishedCommandTag@0`, `playerStatus@4`, `channelStatus@8`,
`captureStatus@A`, 0x14 bytes unknown @C, 16 players × 0x24 @0x20, 16 global
vars @0x260) matches ntrtwl `nitro/snd/common/work.h`
(`vu32 padding[5]` = the same 0x14 bytes, same members). The ARM7 driver in HGSS
is `sub/lib/src/sound_*.c`, not ntrtwl's `libraries/snd/ARM7`. Platinum patches
0018–0023 (virtual ARM7 sound) are written against the latter and **must be
rewritten against `sub/`**, not rebased.

## 6. G3 command table ✅

`G3OP_*` in `lib/include/nitro/gx/g3.h` vs ntrtwl `nitro/gx/g3.h`: 77 opcodes
each, all values identical. Port patches 0024–0026 (command ports, `GXSTAT`,
matrix results) touch `GX`/`G3` register access, not the table.

## 7. Card backup ✅

- `CARD_BACKUP_TYPE_FLASH_4MBITS_EX` exists only in HGSS
  (`lib/include/nitro/card/common.h:54`: `FLASH_4MBITS | (255 <<
  CARD_BACKUP_TYPE_VENDER_SHIFT)`). `src/save.c:1093-1096` tries it first,
  then `FLASH_4MBITS`.
- In HGSS's `CARDi_IdentifyBackupCore` (`lib/NitroSDK/src/card/card_identify_backup.c`)
  `variant` is read from the vendor bits but only consulted for the 8 Mbit
  case; for 4 Mbit (capacity `0x40000`) both types yield the same parameters. So
  if the port's virtual ARM7 card service runs this core, **both identify
  successfully and `_EX` is chosen**. Whether Platinum's `port_card.c` runs this
  code or answers identification itself is ⏳ (must be checked; a hand-written
  answer that rejects vendor 255 would make HGSS fall back to the plain type,
  which is also acceptable but different).

## 8. `CARD_SpiWait*` ✅

HGSS-only API, implemented in `lib/NitroSDK/src/card/card_spi_wait*.c`:
`CARD_SpiWaitInit`, `CARD_SpiWaitReadRange`, `CARD_SpiWaitWriteRange`,
`CARD_SpiWaitGetStatus`, `CARD_Set*WaitCycles`. They poll `reg_MI_MCCNT0`
busy and read/write `reg_MI_MCD0` directly with tick-based waits. Callers:
`src/application/pokewalker/pokewalker_ir.c` and
`src/field/fieldmap.c:774-777` (`FieldMap_DSProtCardSpiCheck` calls
`CARD_SpiWaitGetStatus()` twice and requires the second to be `170` = `0xAA`).
With the port's registers as RAM these loops do not hang but return garbage,
so the planned stub (status `0xAA`, zero-length reads, no peer) is a
`SDK_PORT_3DS` patch to these functions. Real IR is Phase 9.

## 9. IRQ / threads ⏳, RTC/touch/mic payloads ⏳, NitroSystem ⏳

Not yet compared: `OSThread`/`OSContext` layout, IRQ table and handler
assembly (`os_irqHandler.c`, `os_context.c` are inline-asm files, see §2),
RTC/TP/PM/MIC request and reply formats, NitroSystem differences relevant to
Platinum's five NitroSystem patches.
