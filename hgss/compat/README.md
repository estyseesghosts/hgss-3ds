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
| `nitro/mi/dma.h` | wrapper | `#include_next` + the 6 ARM9 timing selectors HGSS lacks (`IMM/V_BLANK/DISP/DISP_MMEM/CARTRIDGE/GXFIFO`), ntrtwl values |
| `nitro/os/common/interrupt.h` | new | ntrtwl-path shim; includes HGSS `interrupt_shared.h`, adds `OS_IE_FIFO_{SEND,RECV}`, `OS_IE_GXFIFO` |
| `nitro/os/common/interrupt_shared.h` | wrapper | `#include_next` + same three macros (ntrtwl values; `OS_IE_GXFIFO` is `(1UL << REG_OS_IE_GF_SHIFT)`) |
| `nitro/gx/g3x.h` | wrapper | `#include_next` + ntrtwl `GXFifoIntrCond` enum, skipped if flat `GX_g3x.h` was already included |
| `nitro/ctrdg.h` | wrapper | `#include_next` + flat `CTRDG_task.h` (`CTRDG_TASK_FUNC`) + `CTRDGBackupType` spelled out with HGSS values (the rest of the flat CTRDG headers redeclare public structs and cannot be included here) |
| `nitro/spi/userInfo.h` | wrapper | `#include_next` + TS200 firmware constants (`NVRAM_CONFIG_DATA_VERSION 5`, `NICKNAME_LENGTH 10`, 6 `LANG_*`; ntrtwl `userInfo_ts_200.h`) |

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
