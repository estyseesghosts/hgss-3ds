# HGSS SDK vs the SDK the Platinum port patches

**Status: COMPLETE (Phase 1 item 5, 2026-10-31).** Every section below was
checked against source (file cited). HGSS citations are to `build/upstream`
(`ctrl-cheeb-del/pokeheartgold` @ `upstream.lock`, `8cbb4a19`); Platinum-side
citations are to the ntrtwl reference headers (`NitroSDK` @ `eaae40f1`,
`NitroSystem` @ `21c8fd07`), Platinum's patch series, and the vendored
`3ds_port/`. Struct comparisons below are by field list plus running-offset
arithmetic (natural 32-bit alignment, verified by hand summation); the
`sizeof`/`offsetof` comparison for the save-chunk types remains the job of
the layout probe (`tools/layout_probe/`).

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
- **Resolution (no `3ds_port/` edits).** `tools/hgss_nitro.py` builds an
  out-of-tree shim under the git-ignored `build/hgss-sdk` (a
  `libraries/hgss` symlink to the HGSS sources, a filtered include dir, a
  stand-in `gen_fx_const.py`) and passes command-line overrides
  (`SDK=`, `SDK_SRCS=`, `SDK_INCLUDES=`, `ARM7_SOUND_SRCS=`) to the vendored
  Makefiles, leaving them byte-identical (`tools/hgss_nitro.py:82-90`).
  HGSS names for Platinum headers the port includes live in the tracked
  `hgss/compat/include/`, which the shim puts on the include path. So the
  layout and include-path differences are resolved outside the vendored
  tree; the remaining work is writing the compat headers (the directory
  still holds only `.gitkeep`) and the HGSS source list. See
  `docs/port-wiring.md` ("The shim and overrides (built)") and
  `docs/progress/STATE.md`.

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
- **Command constants ✅ and payloads ✅ (both fully verified).** Platinum's
  virtual ARM7 (`3ds_port/nitro/src/port_rtc.c`, `port_pm.c`, `port_tp.c`)
  uses SDK symbols `RTC_PXI_COMMAND_*`, `SPI_PXI_COMMAND_PM_*`, `PM_UTIL_*`.
  HGSS's `lib/` does not define those names: its RTC/PM/TP/MIC code uses
  literal numbers. The numeric values match Platinum's names **exactly**
  (tables below), so a compat header defining the Platinum names with
  HGSS's numbers is correct for the command layer — but the port's
  decoders still need HGSS-specific versions (new files such as
  `port_pm_hgss.c`, not edits to the vendored ones), because the
  multi-word request sequences and reply parsing must follow HGSS's ARM9
  code word for word. Details per device below.

### 3.1 Shared SPI framing ✅

Both sides use the same framing (`spi/common/type.h:8-16` on the ntrtwl
side; literal words in HGSS's `tp.c`/`pm.c`/`mic.c`):

| Bit(s) | ntrtwl name | HGSS evidence |
| --- | --- | --- |
| `0x02000000` | `SPI_PXI_START_BIT` | TP sampling word `0x03000000` (`tp.c:235`), PM sleep `0x03006000` (`pm.c:198`), MIC stop `0x03004200` (`mic.c:233`) all have it set |
| `0x01000000` | `SPI_PXI_END_BIT` | TP reply gate `data & 0x1000000` (`tp.c:104`); PM second words `0x01010000 \| payload` (`pm.c:204,216,251`) |
| bits 8–14 | `SPI_PXI_RESULT_COMMAND_MASK/SHIFT` (`0x7F00`, 8) | HGSS masks `(cmd << 8) & 0x7f00` (`rtc_internal.c:33`), `(response & 0x7F00) >> 8` (`tp.c:83`), `(data & 0x7F00) >> 8` (`pm.c:169`, `mic.c:162-164`) |
| bit 15 | `SPI_PXI_RESULT_COMMAND_BIT` (`0x8000`) | set on Platinum replies (`port_tp.c:52-54` sends `END \| RESULT_COMMAND_BIT \| (cmd<<8) \| result`); HGSS parsers mask it off with `0x7F00`, so it is accepted and ignored |
| bits 0–7 | `SPI_PXI_RESULT_DATA_MASK` (result code) | `result = data & 0xFF` (`pm.c:170`), `reply = data & 0xFF` (`mic.c:163`), `switch (response & 0xFF)` (`tp.c:106`) |

### 3.2 RTC (`PXI_FIFO_TAG_RTC = 5`) ✅

Request: `RtcSendPxiCommand` (`rtc_internal.c:31-35`) sends one word
`(command << 8) & 0x7f00` via `PXI_SendWordByFifo(PXI_FIFO_TAG_RTC, …)`.
Commands (`rtc_internal.c:11-29`, decimal in source): `16` = READ_DATETIME,
`17` = READ_DATE, `18` = READ_TIME, `39` = WRITE_STATUS2. These equal
ntrtwl's `RTC_PXI_COMMAND_READ_DATETIME 0x10`, `READ_DATE 0x11`,
`READ_TIME 0x12`, `WRITE_STATUS2 0x27` (`rtc/common/fifo.h`).

Reply: `RtcCommonCallback` (`rtc_api.c:163-190`) parses `status = (u8)data`
(line 183) and the command `(data & 0x7F00) >> 8` (line 184), treating
`0x30` as the alarm interrupt and dispatching the rest by
`gRtcWork.operation` — i.e. replies are `(cmd << 8) | status`, exactly what
the port's `NitroPort_HandleRtc` sends
(`port_rtc.c:70-71`: `(command << RTC_PXI_COMMAND_SHIFT) | status`).

Shared buffer: `RTC_RAW_DATE` at `0x027FFDE8`, `RTC_RAW_TIME` at
`0x027FFDEC` (`rtc_api.c:32-33`) = `OSSystemWork` base `0x027FFC00` +
`real_time_clock` offset `0x1E8` (§4). The BCD field extraction
(`rtc_api.c:195-218`: year bits 24–31, month bits 8–12, day bits 16–21,
hour bits 0–5, minute bits 8–14, second bits 16–22) matches ntrtwl's
`RTCRawDate`/`RTCRawTime` bit layouts (`rtc/common/type.h`: year :8,
month :5, day :6, week :3; hour :6, afternoon :1, minute :7, second :7),
which is what `port_rtc.c:44-55` fills.

### 3.3 TP (`PXI_FIFO_TAG_TOUCHPANEL = 6`) ✅

Commands (`tp.c:38-41`): `TP_COMMAND_SAMPLING 0`, `TP_COMMAND_AUTO_START 1`,
`TP_COMMAND_AUTO_STOP 2`, `TP_COMMAND_AUTO_SAMPLING 16` — equal to ntrtwl's
`SPI_PXI_COMMAND_TP_SAMPLING 0x0000`, `TP_AUTO_ON 0x0001`,
`TP_AUTO_OFF 0x0002`, `TP_AUTO_SAMPLING 0x0010` (`spi/common/type.h:21-25`).

Request sequences (`tp.c:233-300`): SAMPLING is one word `0x3000000`
(`START|END|cmd0`, line 235); AUTO_START is two words
`(frequency & 0xFF) | 0x02000100` then `vcount | 0x01010000` (lines
260-265); AUTO_STOP is one word `0x03000200` (line 296). The port's
`NitroPort_HandleTp` accepts exactly this shape: it ignores words without
`SPI_PXI_START_BIT`, reads the command with the `0x7F00>>8` mask, and
treats a second word carrying `SPI_PXI_END_BIT` as the auto-config
completion (`port_tp.c:65-87`).

Replies: `TPi_TpCallback` (`tp.c:81-~150`) takes `response = (u16)data`,
`command = (response & 0x7F00) >> 8` (lines 82-83); command `0x10`
(auto-sampling, line 84) copies the shared raw point with no END check;
all other commands require the END bit (`tp.c:104`) and take the result
from the low byte (`tp.c:106-150`: 0 success, 1 invalid, 2 illegal
status, 3/4 busy/send-error). I.e. replies are
`END | 0x8000 | (cmd << 8) | result`, and auto-sampling notifications are
`(0x10 << 8) = 0x1000` with no END bit — exactly what the port sends
(`port_tp.c:45-46` and `52-54`).

Shared buffer: `TP_RAW_POINT` at `0x027FFFAA` (`tp.c:46`) =
`OSSystemWork.touch_panel` (§4). `TpRawPoint` (`tp.c:49-55`: `x :12`,
`y :12`, `touch :1`, `validity :2`, `unk_27 :5`) is bit-for-bit identical
to ntrtwl's `SPITpData.e` (`spi/common/type.h`: `x :12`, `y :12`,
`touch :1`, `validity :2`, `dummy :5`), which is what `port_tp.c:22-29`
publishes. Touch/off/validity enum values also match (`TP_TOUCH_ON 1`,
`TP_VALIDITY_* 0..3` in `spi/ARM9/tp.h` vs `SPI_TP_TOUCH_*`,
`SPI_TP_VALIDITY_*`).

### 3.4 PM (`PXI_FIFO_TAG_PM = 8`) ✅

Command numbers (`spi/common/type.h:48-60`): `0x60` SYNC, `0x61`
SLEEP_START, `0x62` SLEEP_END, `0x63` UTILITY, `0x64` REG_WRITE, `0x65`
REG_READ, `0x66` SELF_BLINK, `0x67` GET_BLINK, `0x70–0x74` REGnVALUE
replies. HGSS uses all of them: sleep `0x03006000` then trigger/logic
words (`pm.c:198-208`), utility two-word `0x02006300|hi`,
`0x01010000|lo` (`pm.c:210-219`), register write two-word
`0x2000000|0x6400|reg`, `0x01010000|value` (`pm.c:244-253`), register read
single-word `0x03006500|reg` (`pm.c:221-233`), LED-pattern send
`(pattern&0xFF)|0x6600|0x3000000` and LED-pattern read `0x03006700`
(`pm.c:527-555`). Reply dispatch
(`pm.c:163-190`): `0x70–0x74` store the low byte into
`gPmRegisterBuffers`, `0x60` sets `sleepReady`, `0x62` sets `unk_08`,
`0x67` stores the LED pattern — matching the port's
`finish_pm_command`, which answers REG_READ with `REGnVALUE`
(`port_pm.c:71-109`).

Utility numbers (`spi/common/pm_common.h:51-65`: `PM_UTIL_DUMMY 0`,
`LED_ON 1`, `LED_BLINK_HIGH 2`, `LED_BLINK_LOW 3`, `LCD1_ON 4`,
`LCD1_OFF 5`, `LCD2_ON 6`, `LCD2_OFF 7`, `LCD12_ON 8`, `LCD12_OFF 9`, …
`FORCE_POWER_OFF 14`): HGSS sends exactly these — LED mode 1→1, 3→2, 2→3
(`pm.c:266-285`), bottom backlight on/off 4/5 (`PM_LCD_BOTTOM`,
`pm.c:315-322`), top 6/7 (`PM_LCD_TOP`, `pm.c:305-313`), both 8/9
(`PM_LCD_ALL`, `pm.c:323-331`), power-off 14 (`pm.c:355`).

Backlight read bits: `PM_GetBackLight` (`pm.c:396-404`) reports top from
register-0 bit 3 (`reg & 8`) and bottom from bit 2 (`reg & 4`), matching
the port's `PMIC_CTL_BKLT1 (1<<2)` for LCD1 and `PMIC_CTL_BKLT2 (1<<3)`
for LCD2 (`spi/common/pm_common.h:26-27`; `port_pm.c:33-49`).

### 3.5 MIC (`PXI_FIFO_TAG_MIC = 9`) ✅

Commands match ntrtwl's `SPI_PXI_COMMAND_MIC_*` (`spi/common/type.h`):
`0x40` SAMPLING, `0x41` AUTO_ON, `0x42` AUTO_OFF, `0x51` BUFFER_FULL.
AUTO_ON is a 7-word sequence (`MicStartAutoSampling`, `mic.c:213-230`):
`flags|0x4100|0x2000000`, buffer hi `|0x10000`, buffer lo `|0x20000`,
size hi `|0x30000`, size lo `|0x40000`, rate hi `|0x50000`,
`0x01060000|(rate&0xFFFF)`. AUTO_OFF is one word `0x03004200`
(`mic.c:232-234`). Replies (`MicCommonCallback`, `mic.c:152-211`):
`command = (data & 0x7F00) >> 8`, result = low byte; `0x51` invokes the
buffer-full callback, `0x40` copies `MIC_SAMPLE_DATA` into
`sampleResult`. Sample-type flags also match (`mic.c:70-87`:
8bit→0, 12bit→1, signed→2/3, filter-off bit 2, loop bit 4, vs ntrtwl's
`ADMODE/SIGNED/FILTER/LOOP` masks).

Shared buffer: `mic_last_address` at `0x027FFF90` and `mic_sampling_data`
at `0x027FFF94` (`mic.c:20-21`) = `OSSystemWork` offsets `0x390`/`0x394`
(§4).

`3ds_port/nitro` has no mic decoder — only the tag-name string in
`pxi.c:50`. The virtual ARM7 mic is deferred to Phase 7 (`port_mic.c`;
gameplan Phase 7 step "virtual ARM7 mic handler"). Nothing in the port
answers MIC PXI today, so HGSS mic calls are out of scope until then.

## 4. `OSSystemWork` ✅ (offsets verified)

Field lists: HGSS `lib/include/nitro/os/common/systemWork_shared.h` has 40
entries, ntrtwl `include/nitro/os/common/systemWork.h` has 41. The only
difference is naming: HGSS has `u8 padding3[2]` where ntrtwl has two `u8`s
(`ctrdg_SetModuleInfoFlag`, `ctrdg_IsExisting`), the same 2 bytes at the
same position (after `wm_rssi_pool`, before `component_param`).
Any Platinum patch or port code naming `ctrdg_*` needs an HGSS equivalent.

Offsets verified by running summation over the HGSS header in declaration
order (all members naturally aligned on 32-bit): every running offset
matches the header's own `000–3ff` comments, and the struct totals `0x400`
bytes (`0x027FFC00–0x027FFFFF` via `HW_MAIN_MEM_SYSTEM`):

| Field | Offset | Size | Cross-check |
| --- | --- | --- | --- |
| `real_time_clock[8]` | `0x1E8` | 8 | `RTC_RAW_DATE 0x027FFDE8`, `RTC_RAW_TIME 0x027FFDEC` (`rtc_api.c:32-33`) |
| `mic_last_address` | `0x390` | 4 | `MIC_LAST_SAMPLING_ADDRESS 0x027FFF90` (`mic.c:20`) |
| `mic_sampling_data` | `0x394` | 2 | `MIC_SAMPLE_DATA 0x027FFF94` (`mic.c:21`) |
| `padding3[2]` (= ntrtwl `ctrdg_*`) | `0x39A` | 2 | same slot both sides |
| `touch_panel[4]` | `0x3AA` | 4 | `TP_RAW_POINT 0x027FFFAA` (`tp.c:46`) |
| `command_area` | `0x3FE` | 2 | struct ends at `0x400` ✅ |

## 5. Sound shared work ✅ / ARM7 driver ✅

`SNDSharedWork` (HGSS `lib/NitroSDK/include-pokediamond/nitro/SND_work_shared.h`,
size `0x280`: `finishedCommandTag@0`, `playerStatus@4`, `channelStatus@8`,
`captureStatus@A`, 0x14 bytes unknown @C, 16 players × 0x24 @0x20, 16 global
vars @0x260) matches ntrtwl `nitro/snd/common/work.h`
(`vu32 padding[5]` = the same 0x14 bytes, same members). Arithmetic check:
`0xC + 0x14 = 0x20`; 16 players × (`16×s16` + `u32`) = `16×0x24` =
`0x240`, `0x20 + 0x240 = 0x260`; 16 global `s16` = `0x20`,
`0x260 + 0x20 = 0x280` ✅. The ARM7 driver in HGSS
is `sub/lib/src/sound_*.c`, not ntrtwl's `libraries/snd/ARM7`. Platinum patches
0018–0023 (virtual ARM7 sound) are written against the latter and **must be
rewritten against `sub/`**, not rebased. The entry-point mapping the
rewrite needs (`port_snd.c:35-41` declares the `N7_SND_*` names the
virtual ARM7 must provide; `port_snd.c:106,143-144,175-180` calls them):

| Port expects (`N7_…`) | HGSS `sub/lib/src` provides | Notes |
| --- | --- | --- |
| `N7_SND_Init` | `SND_Init` (`sound_main.c:44,63`) | priority arg; `sound_main.c:72-80` interval timer |
| `N7_SND_CommandProc` | `SND_CommandProc` (`sound_command.c:57,69`) | PXI `SOUND` tag callback set at `sound_command.c:65`; `SNDi_SharedWork` bound from `cmd.arg[0]` (`sound_command.c:255`), `finishedCommandTag++` at `:270` |
| `N7_SND_UpdateExChannel` | `SND_UpdateExChannel` (`sound_ex_channel.c:71`) | |
| `N7_SND_SeqMain` | `SND_SeqMain` (`sound_seq_player.c:90`) | HGSS takes `int update`; port passes `BOOL periodic` — same 4-byte value |
| `N7_SND_ExChannelMain` | `SND_ExChannelMain` (`sound_ex_channel.c:120`) | same `int`/`BOOL` note |
| `N7_SND_UpdateSharedWork` | `SND_UpdateSharedWork` (`sound_shared_work.c:26`) | |
| `N7_SND_CalcRandom` | `SND_CalcRandom` (`sound_main.c:43,58`) | returns `u16` both sides |

So the rewrite is mostly renaming `N7_SND_*` to `SND_*` plus the
`int`-vs-`BOOL` signature line; the shared-work layout needs no
translation.

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
  `variant` is read from the vendor bits (line 29) but only consulted for the 8 Mbit
  case (lines 99-113); for 4 Mbit (capacity `0x40000`, lines 67-74) both types
  yield the same parameters.
- The ARM9 `CARD_IdentifyBackup` (`lib/NitroSDK/src/card/CARD_backup.c:54-72`)
  runs that core locally **and then** sends `CARD_REQ_IDENTIFY` over PXI,
  returning success only if the reply is `CARD_RESULT_SUCCESS` (lines 67-72).
  So the decision lives in the port's answer, not the core.
- The port answers identification itself (`3ds_port/nitro/src/port_card.c:260-265`):
  `CARD_REQ_IDENTIFY` returns `SUCCESS` only for
  `cmd->type == CARD_BACKUP_TYPE_FLASH_4MBITS` (and an openable save); every
  other type, including `_EX`, gets `CARD_RESULT_UNSUPPORTED`, and every later
  request for a non-plain type is rejected the same way (lines 266-267).
- Net effect (verified by reading, not yet run): `SaveDetectFlash`
  (`src/save.c:1086-1101`) tries `_EX` first, gets `UNSUPPORTED`, and falls
  back to plain `FLASH_4MBITS`, which succeeds. The chosen type differs from
  hardware (plain instead of `_EX`), but the parameters are identical, so
  this is acceptable and needs no change. The §7 question is closed.

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

## 9. IRQ / threads ✅, NitroSystem ✅

### 9.1 `OSContext`, `OSThread`, IRQ table ✅

All layouts below are identical on both sides (HGSS:
`lib/include/nitro/os/context.h`, `nitro/cp/context.h`,
`nitro/os/common/thread_shared.h`, `nitro/os/interrupt.h`,
`nitro/os/common/interrupt_shared.h`; ntrtwl: `os/common/context.h`,
`os/common/thread.h`, `os/common/interrupt.h`).

- **`OSContext`**: the offset macros are identical — `CPSR 0`, `R0–R12`
  `4–52`, `SP 56`, `LR 60`, `PC+4 64`, `SP_SVC 68`, `CP_CONTEXT 72`.
  So 68 bytes to `SP_SVC`, 72 bytes to `cp_context`. `CPContext` is 3×`u64`
  + 2×`u16` = 28 bytes (`cp/context.h`), hence **100 bytes total on ARM9**.
  (This is the layout the `cp_context` host test pins: 28/100.)
- **`OSThread`**: the `SDK_THREAD_INFINITY` layout, identical field order
  through the end on both sides — `context, state, next, id, priority,
  profiler, queue, link, mutex, mutexQueue, stackTop, stackBottom,
  stackWarningOffset, joinQueue, specific[3], alarmForSleep, destructor,
  userParameter, systemErrno` (HGSS spells the last `u32`, ntrtwl `int`;
  same 4 bytes). The port relies on these fields directly
  (`port_thread.c:203,228` touch `cp_context`; `:285-317` use
  `isNeedRescheduling`/`switchCallback`), so no translation is needed.
- **`OSThreadInfo`**: `isNeedRescheduling u16`, `irqDepth u16`,
  `current`, `list`, `switchCallback` = **16 bytes** both sides.
- **`OSIrqCallbackInfo`**: `{func, enable, arg}` both sides
  (`interrupt_shared.h`). Callback numbers: `DMA0..3 = 0..3`,
  `TIMER0..3 = 4..7` (`interrupt.h:18-25`); `OS_IRQ_TABLE_MAX = 22` on
  ARM9, 25 on ARM7 (`interrupt.h:12-16`). (There is no `os/ARM9/irq.h` in
  the HGSS tree; the content lives in `os/interrupt.h` +
  `os/common/interrupt_shared.h`.)
- The handler assembly (`os_irqHandler.c`, `os_context.c`) was already
  replaced in C by patches 0401/0501/0901; with the layouts above
  confirmed identical, those patches apply to the same offsets.

### 9.2 NitroSystem: Platinum's five NNS patches vs HGSS ✅

Platinum's NitroSystem series is five patches
(`pokeplatinum-3ds/patches/NitroSystem/0001–0005`). Status per patch:

| Platinum patch | What it does | HGSS status |
| --- | --- | --- |
| 0001 GCC build | UTF-16 reads, casts-as-lvalues, `__alloca`, implicit `int`, UB fixes in `g2d_CharCanvas.c`, `g2di_BGManipulator.c`, `g2di_SplitChar.c`, `gfd_FrameTexVramMan.c`, `sndarc_stream.c`, `g2d_Data.h`, `g2di_Char.h` | **Covered where applicable.** The implicit-`int` case exists in HGSS too and is fixed by pokeheartgold 1101 (`NNS_G2dInitCellTransferStateManager` gets its `int`; the definition at `g2d_cell_transfer_manager.c:28` already has it). The MWCC-`asm` CLZ helpers in `g2d_canvas_arrange.c`, `g2d_char_canvas.c`, `g2d_char_canvas_util.c` are fixed by NNS 0301 (`__builtin_clz` with the `x == 0 → 32` guard, tested by `tests/sdk/nns_clz`). The other 0001 items have no HGSS counterpart: no `__alloca` and no `L"…"` wide literals in HGSS `NitroSystem/src`, and no `g2d_Data.h`/`UNPACK_OFFSET_PTR` macro in HGSS headers (grep-verified). |
| 0002 sound capture | Guards `cap->blockSize = bufLen / interval` in `NNSi_SndCaptureStart` (`libraries/snd/src/capture.c:446-452`): `interval` is 0 with no callback (reverb), which SIGFPEs on PC | **Not applicable as written.** HGSS's `lib/NitroSystem/src/snd_capture.c` (121 lines) has no `bufLen / interval` division at all — its volume math is shift-based (`NNSi_SndFaderGet(&state->fader) >> 8`, line 57). No equivalent patch needed unless a divider turns up elsewhere. |
| 0003 GXFIFO stores | Routes single-register `reg_G3X_GXFIFO` stores in `gecom.c` (`NNS_G3dGeBufferOP_N`) and `sbc.c` through `NitroPort_GxFifoWord` | **Done (NitroSystem 0302).** HGSS `g3d_gecom.c:64-65` (`reg_G3X_GXFIFO = op` + `MIi_CpuSend32` in `NNS_G3dGeBufferOP_N`) and the `g3d_sbc.c` projection/billboard stores now go through the port's GXFIFO word hook. |
| 0004 queued 3D VRAM transfers | Adds `NNSi_GfdDoVramTransfer3D` (early texture/palette/clear transfers at `OS_Halt`, marked done via `pSrc = NULL`) so first-use textures are present when the scene draws | **Done (NitroSystem 0302).** HGSS's `NNS_GfdDoVramTransfer` has no `pSrc`-NULL skip and no early-3D entry point; 0302 adds the early transfer pass, and patch 1406 maps the 3D texture pages onto the port's VRAM array. Verified by `test_vram_transfer` (24 checks). |
| 0005 OAM reset | Parks unused OAM entries at `192 \| 0x0200` (y-192 **plus the disable bit**) instead of bare `192`, because the 3DS lower screen shows lines 192–247 | **Done (NitroSystem 0302).** HGSS `NNS_G2dResetOamManagerBuffer` filled `0xc0`; the port now sets the disable bit (`0x0200`) in the hidden-OAM entries. |

## 10. Runtime differences found by the vendored suites (session 5) ✅

These were not visible in a header comparison; each one only appeared when the
port actually executed the code.

### 10.1 The ARM9 sound command flush is two SDK revisions in one tree

`lib/NitroSDK/src/snd/SND_command.c` owns a **4-byte** `sFreeList` pointer,
`sReserveList`, `sWaitingCommandListQueue` and the tag counters.
`lib/NitroSDK/src/snd/snd_flush_command.c` is a later revision whose private
`CommandState` is **40 bytes** and is read through the same `sFreeList`. On the
DS only the later file defines `SND_FlushCommand`, so nothing notices. The port
links both C files into one archive; the later definition read the pointer as a
struct, never saw `sReserveList`, and its `pending/tag` stores landed on the
neighbouring `sCurrentTag`/`sFinishedTag`. No command reached the virtual ARM7,
and `SND_RecvCommandReply(SND_CMD_FLAG_BLOCK)` spun.

Patch 1404 keeps the 4.2 flush in `SND_command.c` under `SDK_PORT_3DS`;
`snd_flush_command.c` compiles only for the DS. Verified by `test_arm7` (676
checks) and `tests/sdk/arm7_sound/test_arm9_flush.c` (24 checks).

### 10.2 Sound registers are indexed by the DS low 16-bit address

`NitroPort_SoundRegisters` is the DS address space's low bits: `SOUNDCNT` at
`0x500`, channel `c`'s control at `0x400 + c*16` (channel 4 at `0x440`),
captures at `0x508`/`0x510`. Patch 1401 subtracted `0x400`, so every channel,
master-volume and capture write landed 0x400 low and the mixer read zeros.
Patch 1405 maps the DS addresses unchanged.

The BIOS volume table is NitroSDK 4.2's real **724-entry** table, not
`round(127 * 10^(dB/20))`: the lowest entries stay non-zero, and a synthetic
sequence with `attack = 0` relied on them. The formula rounded them to 0 and
silenced the channel.

### 10.3 3D texture loads and direct G3 register writes

`lib/NitroSDK/src/gx/gx_load3d.c` copies 3D textures to the raw LCDC address
`0x06800000`. Patch 1406 maps the loader's page numbers onto
`NitroPort_LcdcVram` (a zero entry stays zero, because the loader uses it to
mean "no second block"). The tested texture and texture-palette paths pass
`test_vram_transfer`; the clear-image loaders still use raw addresses and are
not exercised.

`hgss/compat/include/nitro/gx/g3imm.h`'s `G3_StoreMtx`, `G3_RestoreMtx`,
`G3_Scale` and `G3_TexCoord` wrote `reg_G3_MTX_*` directly; the port's geometry
engine only sees `NitroPort_GxDirectWrite` (HGSS's own header already routes the
other G3 helpers that way). Forwarding them fixed six `test_gx3d` checks.

### 10.4 NitroSystem declaration sets and one real layout bug

- Three declaration sets existed for `SND_RecvCommandReply`, `SND_ReadDriverInfo`,
  `SND_SetMasterVolume` and `SND_PrepareSeq`: HGSS's flat headers,
  `lib/include/nnsys/snd_internal.h`, and the port's compat header. The flat
  headers cannot be combined with the public ones (their `SND_work_shared.h`
  redefines `SNDChannelInfo`/`SNDTrackInfo`). The port keeps one set; see
  `hgss/compat/README.md`.
- `nnsys/snd/main.h` uses `NNSSndHandle`, but `nnsys/snd.h` does not include
  `snd/player.h`; a unity file that includes every header in path order fails.
  The compat `nitro/snd.h` pulls the heap/player headers first.
- `gfd_linked_list_tex_vram_man_reset.c` defined the linked-list texture VRAM
  manager as three separate globals, while the manager casts
  `&gLnkTexVramFreeList` to `LnkTexVramManager` and reads +0/+4/+8. GCC's
  `.bss` order put them at 0x0/0x14/0x18, so init wrote through garbage.
  NitroSystem patch 0303 emits one ordered block under `SDK_PORT_3DS`.
- HGSS's NNS headers are all `static inline`, so the vendored weaken step gets
  an empty symbol list; binutils 2.42 rejects it. The harness's `NM` wrapper
  appends one harmless name.

### 10.5 Missing decomp build stage: object aliases

The DS build runs `tools/object_aliases.py` over objects with an
`.alias-map.json` to add labels *inside* a section: the five
`gPokewalkerIr{ImageSizes,StrollSizes,MessageQueue,Transfer,SendBuffer}` aliases
inside `gPokewalkerIrBuffers`/`Sync`/`Progress` (offsets 0x4c/0x54, 0x7c/0x80,
0xc0), and runs of Thumb entry points. The PC harness does not run it yet; the
five aliases are among the last 34 unresolved symbols.
