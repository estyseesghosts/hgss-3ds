# hgss/compat/include

Compatibility headers so the vendored `3ds_port/` (written against the
ntrtwl NitroSDK that pokeplatinum-3ds builds) compiles against HGSS's SDK
without editing `3ds_port/`. On the include path via
`tools/hgss_nitro.py` (`-Ihgss/compat/include`, before the shimmed and flat
SDK dirs) and `tools/port_compile_survey.py` (same order).

Reference sources, in order of preference:

- **ntrtwl** — `https://github.com/ntrtwl/NitroSDK.git` at the revision
  pokeplatinum pins (`eaae40f1`, via its `subprojects/NitroSDK.wrap`):
  exact values for names HGSS dropped or renamed. Locally:
  `/work/pokeplatinum-3ds/build/NitroSDK/include/nitro/...` in the container.
- **HGSS** — `build/upstream/lib/include/...` (public) and
  `lib/NitroSDK/include/...` / `include-pokediamond/...` (flat):
  structures shared with HGSS ARM9 code, which must keep HGSS's layout.

Two header kinds:

- **New paths** (no HGSS counterpart): full contents, ntrtwl guard names.
- **Wrappers** (`#include_next` + additions): HGSS owns the path. The
  wrapper guard is always compat-specific (`HGSS_COMPAT_*`) — reusing HGSS's
  guard would skip the chained header. Additions are `#ifndef`-guarded where
  HGSS might already define them.

## Files

| Header | Kind | Source |
| --- | --- | --- |
| `card_common.h` | dual-world, see below | flat `CARD_common.h` (+ 3 ntrtwl request codes) |
| `nitro/spi/common/type.h` | new | ntrtwl `spi/common/type.h` whole; PM register/utility/result/LED/backlight/baud block from ntrtwl `spi/common/pm_common.h`; `PMSound*` enums from ntrtwl `spi/ARM9/pm.h`; includes HGSS `spi/common/pm_common.h` for `PM_TRIGGER_*`/`PM_PAD_LOGIC_*` |
| `nitro/rtc/common/fifo.h` | new | ntrtwl `rtc/common/fifo.h` whole |
| `nitro/rtc/common/type.h` | new | ntrtwl `rtc/common/type.h` whole |
| `nitro/snd/common/command.h` | new | flat `SND_command.h` (gives HGSS's `struct SNDCommand`, layout-identical to ntrtwl's) + `SNDCommand` typedef, `SND_COMMAND_*`, `SNDCommandID` from ntrtwl `snd/common/command.h` |
| `nitro/snd/common/main.h` | new | `SND_PROC_INTERVAL` (`0xAA8`) + `SND_MESSAGE_*` from ntrtwl `snd/common/main.h`; forward `typedef struct SNDSharedWork SNDSharedWork` (see below) |
| `nitro/dgt/dgt.h` | new, forwards to flat `dgt.h` | `#include <dgt.h>` (flat; now carries both Hash2 field sets under `SDK_PORT_3DS`, patch 1202) + `DGTHash1Context`/`DGTHash2Context` typedefs + HMAC/overlay declarations matching `dgt.c` |
| `nitro/ioreg.h` | new | selects `hw/ARM9(io_reg.h)` / `hw/ARM7(io_reg.h)` like ntrtwl's, plus `<nitro/hw/mmap.h>` for `HW_REG_BASE` (HGSS keeps it in `mmap_shared.h`, which ntrtwl's io_reg chain pulls but HGSS's does not) |
| `nitro/cp/divider.h` | new | ntrtwl `cp/divider.h` verbatim + `<stdint.h>` for `INT64_MIN` (Platinum got it from its patched `types.h`; HGSS's has none) |
| `nitro/cp/sqrt.h` | new | ntrtwl `cp/sqrt.h` verbatim |
| `nitro/hw/ARM9/mmap_global.h` | new | HGSS `hw/ARM9/mmap.h` + `HW_ROM_HEADER_BUF_END (HW_MAIN_MEM + 0x007fff60)` (ntrtwl value, consistent with HGSS `BUF + 0x160`) |
| `nitro/hw/ARM9/mmap.h` | wrapper | `#include_next` + same `HW_ROM_HEADER_BUF_END`; this is what makes it visible to `port_boot.c` |
| `nitro/mi/uncompress.h` | wrapper | `#include_next` + ntrtwl compression types, `MIUnpackBitsParam`, missing entry points; also includes `<nitro/mi/stream.h>` because ntrtwl's `mi.h` does and HGSS's does not (`svc.c` relies on the chain) |
| `nitro/mi/stream.h` | new | ntrtwl `mi/stream.h`; the getter has no HGSS definition (link error if ever called; nothing calls it) |
| `nitro/mi/dma.h` | wrapper | `#include_next` + the 6 ARM9 timing selectors HGSS lacks (`IMM/V_BLANK/DISP/DISP_MMEM/CARTRIDGE/GXFIFO`), ntrtwl values + `MI_DmaFill16`/`MI_DmaSend32`/`MI_IsDmaBusy`/`MI_VBlankDmaCopy32Async` declarations (bodies in `src/sdk_standins.c`) |
| `nitro/os/common/interrupt.h` | new | ntrtwl-path shim; includes HGSS `interrupt_shared.h`, adds `OS_IE_FIFO_{SEND,RECV}`, `OS_IE_GXFIFO` |
| `nitro/os/common/interrupt_shared.h` | wrapper | `#include_next` + same three macros (ntrtwl values; `OS_IE_GXFIFO` is `(1UL << REG_OS_IE_GF_SHIFT)`) |
| `nitro/gx/g3x.h` | wrapper | `#include_next` + ntrtwl `GXFifoIntrCond`/`GXFifoStat` enums, `G3X_GetCommandFifoStatus/Count` inlines and `G3X_ResetMtxStackOverflow`, skipped if flat `GX_g3x.h` was already included |
| `nitro/ctrdg.h` | wrapper | `#include_next` + flat `CTRDG_task.h` (`CTRDG_TASK_FUNC`) + `CTRDGBackupType` spelled out with HGSS values (the rest of the flat CTRDG headers redeclare public structs and cannot be included here) |
| `nitro/spi/userInfo.h` | wrapper | `#include_next` + TS200 firmware constants (`NVRAM_CONFIG_DATA_VERSION 5`, `NICKNAME_LENGTH 10`, 6 `LANG_*`; ntrtwl `userInfo_ts_200.h`) |
| `nitro/cp.h` | wrapper | `#include_next` + `cp/divider.h` + `cp/sqrt.h` (ntrtwl's `cp.h` pulls all three; HGSS's pulls only `cp/context.h`) |
| `nitro/cp/context.h` | wrapper | `#include_next` + `CP_RestoreContext` inline over HGSS's `CPi_RestoreContext` (ntrtwl name; patch 0501's function) |
| `nitro/math.h` | wrapper | `#include_next` + `math/rand.h` (new) + `MATH_CountLeadingZeros` family / `MATH_ILog2` (ntrtwl; bodies in `src/sdk_standins.c`) |
| `nitro/math/rand.h` | new | ntrtwl `math/rand.h` verbatim (constants fix the game's RNG sequence) |
| `nitro/math/crc.h` | wrapper | `#include_next` + ntrtwl `MATH_CRC16CCITTInitTable`/`MATH_CRC32InitTable` inlines; `MATH_CRC16InitTable` redirected to ntrtwl's reflected meaning (see below; `HGSS_MATH_KEEP_HGSS_CRC16_INIT` keeps HGSS's) |
| `nitro/math/dgt.h` | wrapper | `#include_next` + `dgt/dgt.h` + the `MATHMD5/SHA1Context` streaming inlines (mirror flat `MATH_dgt.h`, ntrtwl names) |
| `nitro/dgt.h` | new (umbrella) | `dgt/common.h` + `dgt/dgt.h`; HGSS's `nitro.h` never includes dgt |
| `nitro/os.h` | wrapper | `#include_next` + `os/arena.h` (ntrtwl's `os.h` pulls it; HGSS's does not) |
| `nitro/os/arena.h` | wrapper | `#include_next` + ntrtwl `OS_Set/Get/AllocFromMainArena` inlines over HGSS's arena primitives |
| `nitro/os/tick.h` | wrapper | `#include_next` + ntrtwl `OS_TicksToMilliSeconds` conversions over HGSS's `OS_SYSTEM_CLOCK` |
| `nitro/os/system.h` | wrapper | `#include_next` + `OS_WaitInterrupt` declaration (ntrtwl; body in `src/sdk_standins.c`) |
| `nitro/os/systemCall.h` | wrapper | `#include_next` + ntrtwl `SVC_CpuSet/CpuSetFast/GetCRC16/Div/DivRem/Sqrt` declarations (bodies in the port's `svc.c`) |
| `nitro/std/string.h` | wrapper | `#include_next` + ntrtwl `STD_TSNPrintf` declaration (body in `src/sdk_standins.c`) |
| `nitro/mi.h` | wrapper | `#include_next` + `mi/compress.h` (new; HGSS's `mi.h` has no compressor) |
| `nitro/mi/memory.h` | wrapper | `#include_next` + ntrtwl `MI_CpuSend16/32` inlines over HGSS's `MIi_` primitives + `MI_Copy128B`/`MI_Zero48B` declarations (bodies in the port's `mi_memory.c`) |
| `nitro/mi/compress.h` | new | ntrtwl `mi/compress.h` API: LZ/LZEx inlines over HGSS's `MI_CompressLZImpl`, `MI_CompressRL/Huffman` (bodies in `src/sdk_standins.c`), `MI_HUFFMAN_COMPRESS_WORK_SIZE` |
| `nitro/gx/g2_oam.h` | wrapper | `#include_next` + ntrtwl `GXOamAffine`, attribute macros, `GXOamShape/ColorMode/Effect` and the `G2_SetOBJ` inlines (no link dependency) |
| `nitro/gx/g3imm.h` | wrapper | `#include_next` + `G3_LoadMtx44/43`/`G3_MultMtx43/33` declarations (HGSS's `g3imm.c`) + ntrtwl `G3_StoreMtx/RestoreMtx/Scale/TexCoord` register inlines |
| `nitro/gx/gx.h` | wrapper | `#include_next` + `GX_SetMasterBrightness` (over HGSS's `GXx_SetMasterBrightness_`), `GX_LoadBG3Bmp` (over `GX_LoadBG3Scr`), `GX_IsHBlank` (ntrtwl/flat forms) |
| `nitro/fx/fx_cp.h` | wrapper | `#include_next` + `FX_Sqrt`/`FX_DivS32` declarations (HGSS's `fx.c`/`fx_cp.c`) |
| `nitro/fx/fx_trig.h` | wrapper | `#include_next` + `FX_Atan2Idx` declaration (HGSS's `fx_atanidx.c`) |
| `nitro/fx/fx_mtx22.h` | wrapper | `#include_next` + `MTX_Identity22/Scale22/Rot22/Transpose22` declarations (bodies in `src/sdk_standins.c`, ntrtwl stores) |
| `nitro/fx/fx_mtx33.h` | wrapper | `#include_next` + `MTX_Copy33To43/Copy33To44/Transpose33/Scale33/RotAxis33` declarations (bodies in `src/sdk_standins.c`, ntrtwl stores/C) |
| `nitro/fx/fx_mtx43.h` | wrapper | `#include_next` + `MTX_Copy43To33/Copy43To44` inlines (over `MI_Copy36B`/HGSS's `MTX_Copy43To44_`) + `MTX_Transpose43/RotZ43` declarations (bodies in `src/sdk_standins.c`) |
| `nitro/spi/ARM9/pm.h` | wrapper | `#include_next` + ntrtwl `PMBattery`/`PM_BATTERY`/`PM_SUCCESS`/`PM_RESULT` enums, `PM_GetBattery`/`PM_SetBackLightAsync` declarations, and a `u32 PM_GetBackLight` wrapper (HGSS's header says `void`; `pm.c` defines `u32`) |
| `nitro/spi/ARM9/tp.h` | wrapper | `#include_next` + the sync sampling declarations (bodies in `src/sdk_standins.c`, over HGSS's async primitives) |
| `nitro/rtc/ARM9/api.h` | wrapper | `#include_next` + the sync/async date/time declarations (HGSS's `rtc_api.c`) |
| `nitro/card/backup.h` | wrapper | `#include_next` + `CARD_GetBackupTotalSize/PageSize` declarations (bodies in `src/sdk_standins.c`) + program/verify/erase inlines (HGSS `CARDi_RequestStreamCommand` forms, ntrtwl request/mode choices) |
| `nitro/snd.h` | wrapper | `#include_next` + `snd/common/command.h`, `snd/common/main.h`, `snd/ARM9/interface.h` (new; HGSS's `snd.h` has no API) |
| `nitro/snd/ARM9/interface.h` | new | ARM9 sound API declarations matching the flat `SND_interface/command/alarm/work` headers |
| `nitro/snd/common/work.h` | wrapper | `#include_next` + `SNDDriverInfo` with ntrtwl member names (layout-identical to HGSS's shared struct; see below) |
| `nitro/snd/common/bank.h` | new | ntrtwl-named bank/wave structs (layout-identical to HGSS's shared structs; see below) |
| `nitro/snd/common/mml.h` | new | ntrtwl MML opcodes (HGSS's driver decodes the same bytes) |
| `nitro/fs/file.h` | wrapper | `#include_next` + ntrtwl `FS_GetPosition` inline (same struct) + `FS_End` declaration (body in `src/sdk_standins.c`) |
| `nitro/ctrdg/common/ctrdg_common.h` | wrapper | `#include_next` + `CTRDG_IsExisting/IsAgbCartridge` declarations (bodies in the port's `ctrdg.c`) |

## Sources (`src/`, compiled into `libnitro.a` by `tools/hgss_nitro.py`)

| File | Contents |
| --- | --- |
| `sdk_standins.c` | Weak C definitions for SDK functions HGSS stripped (all of `test_nitro.c`/`test_arm7.c`'s needs): the 10 missing `MTX_` functions, `MATH_CountLeadingZeros` pair, the 4 missing `MI_Dma` functions, `MI_CompressRL/Huffman` (ntrtwl algorithms), `OS_WaitInterrupt` (ntrtwl), `PM_GetBattery` (ntrtwl, over HGSS's `PMi_ReadRegister`), the 4 TP sync wrappers (over HGSS's async primitives), `CARD_GetBackupTotalSize/PageSize`, `SND_StartSeq` (over HGSS's `PushCommand_impl`), `FS_End`, `STD_TSNPrintf`. |
| `sdk_data.c` | Weak SDK data the port's GCC build never assembles: `FX_SinCosTable_` (from `lib/NitroSDK/asm/fx_sincos.s`), the `.bss.unified_02` work cluster (`gTpWork` plus interior `.set` aliases, sizes asserted), `_version_NINTENDO_BACKUP`, and the `SDK_SYS/IRQ_STACKSIZE` / `SDK_SECTION_ARENA_DTCM_START` absolute symbols. |

## Deliberate deviations and non-fixes

- **`CARDiCommandArg.dst` vs HGSS `.dest` (naming).** ntrtwl's struct
  member is `dst`; HGSS renamed it `dest` (same offset). `port_card.c`
  uses `dst`. No compat-only fix exists: a `#define dst dest` would also
  rewrite `cardi_common.dst` (HGSS's `CARDiCommon` member, same TU), and
  duplicating the shared struct with a renamed member risks silent layout
  drift. Fixed SDK-side instead (patch 1202, same family as its
  `favouriteColour` case): under `SDK_PORT_3DS` both card headers carry
  `union { u32 dest; u32 dst; }`, so both spellings name offset 16 with an
  unchanged `sizeof(CARDiCommandArg)` (96 bytes).
- **`struct DGTHash2Context` layout.** ntrtwl's (used by the port's `dgt.c`:
  `h0..h4/Nl/Nh/num/data`) differs from HGSS's flat SAFE variant
  (`Intermediate_Hash/...`). Fixed SDK-side (patch 1202): under
  `SDK_PORT_3DS` the flat struct is an anonymous union of both layouts
  (each 104 bytes, so `sizeof` is unchanged), and `nitro/dgt/dgt.h` now
  includes the flat `dgt.h` plus `DGTHash1Context`/`DGTHash2Context`
  typedefs instead of redefining the structs. Hash1 layouts were already
  identical, so `MATH_CalcMD5` interoperates; Hash2 contexts are now shared
  too. The `...ForRms` helpers are declared nowhere here because nothing
  implements them.
- **`SNDSharedWork` stays incomplete.** The flat `SND_work_shared.h`
  defines `struct SNDChannelInfo` with different members than the public
  `snd/common/work.h` typedef, so it cannot be included in a `nitro.h`
  translation unit. Nothing in the port dereferences the type, so a forward
  typedef suffices.
- **`GXFifoIntrCond` vs `mi_dma_gxcommand.c`.** That SDK file defined the
  same enum (and `OS_IE_GXFIFO`) locally at file scope; with this wrapper in
  the chain the enumerators collided. Fixed SDK-side (patch 1202): under
  `SDK_PORT_3DS` the locals are `#ifndef`-skipped when the headers already
  provided them, and still declared when `hgss/compat/include` is off the
  path, so standalone SDK builds keep working.
- **`PM_PAD_KEY_MASK`.** Listed in early notes; the name exists in neither
  ntrtwl nor the port, so it is intentionally absent.
- **`RTC_STATUS2_*`.** Same: in neither ntrtwl's `rtc/common/` headers nor
  `port_rtc.c`; absent.
- **`PM_AMP_*` / `PM_BACKLIGHT_*` (chip enums).** `PMAmpSwitch`/`PMAmpGain`
  already come from HGSS's own `spi/ARM9/pm.h` with ntrtwl's values and are
  not repeated; only the `PMSound*` enums (missing in HGSS) are added.
- **Case trap (`card_common.h` vs `CARD_common.h`).** On a
  case-insensitive filesystem the SDK's quoted `"CARD_common.h"` resolves
  here, so this file branches on `NITRO_H_`: public headers for the port's
  TU, flat-forward for SDK TUs. No SDK TU mixes `<nitro.h>` with flat CARD
  includes, so the public branch fires only for `port_card.c`.
- **`MATH_CRC16InitTable` means the reflected table here.** HGSS's own
  inline builds a CCITT table and HGSS's game code (`src/math_util.c`)
  pairs it with `MATH_CalcCRC16CCITT`; ntrtwl (and the port's test) pairs
  the name with `MATH_CalcCRC16` (check value `0xBB3D`). The wrapper
  redirects the name to ntrtwl's meaning for port/test translation units;
  game code that wants HGSS's meaning must define
  `HGSS_MATH_KEEP_HGSS_CRC16_INIT` (Phase 2 wiring must handle this; see
  `docs/progress/agent-notes/p1-standins.md`).
- **`SNDDriverInfo` / bank structs use ntrtwl member names.** Layouts are
  identical to HGSS's shared structs (same offsets, verified in
  `p1-standins.md`); only the names differ, so the test's synthetic structs
  parse the same bytes HGSS's driver reads.
