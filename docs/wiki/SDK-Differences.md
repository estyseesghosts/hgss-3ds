# SDK differences

This page explains how HGSS's copy of Nintendo's system library (NitroSDK, and the NitroSystem libraries on top of it) differs from the copy the Platinum port was written for. It summarizes [docs/sdk-diff.md](../sdk-diff.md) and [docs/port-wiring.md](../port-wiring.md). Where those two files give a status, this page gives the same status. Both now report the comparison as complete; nothing below is marked "not yet compared".

Related pages: [Architecture](Architecture.md), [Patch-Stack](Patch-Stack.md), [Glossary](Glossary.md).

## The two SDKs

The port was written against one SDK. The game uses another.

| | HGSS (this port) | Platinum port |
| --- | --- | --- |
| NitroSDK | `lib/NitroSDK` and `lib/include/nitro`, in the decomp at the pin in `upstream.lock` | `ntrtwl/NitroSDK` (4.2.30001), fetched as a subproject |
| NitroSystem | `lib/NitroSystem` and `lib/include/nnsys` | `ntrtwl/NitroSystem` (071126.1) |
| ARM7 code | C in `sub/lib/src`, 145 files | `libraries/*/src/ARM7` in ntrtwl |

Source: [docs/sdk-diff.md](../sdk-diff.md), header table.

The short reason this is hard: HGSS (2009) shipped with a later SDK than Platinum (2008), so the two have different layouts and internals, while exposing the same game-facing functions ([gameplan.md](../../gameplan.md), Phase 1, step 3). The plan warns that "the SDK revision gap is larger than it looks" (Phase 1 pitfalls, and risk 1 in the risk register), and the record bears that out.

## Layout of the trees

HGSS's NitroSDK has 190 `.c` files under `lib/NitroSDK/src`. The Platinum SDK has 273. The headers are split across four places in HGSS: `lib/include/nitro` (public), `lib/NitroSDK/include`, `include-mw` and `include-pokediamond` ([docs/sdk-diff.md](../sdk-diff.md), section 1).

File names also differ. For example, `CARD_backup.c` in HGSS corresponds to `card_backup.c` in Platinum's tree, under a different path. Matching by normalized name pairs only 213 of the roughly 570 to 620 files.

The sdk-diff record says a line-by-line diff of the two trees is not a usable method. HGSS is a reconstruction that matches the DS build; the Platinum SDK is closer to the original sources. Equivalent code looks very different in the two. So the comparison in the record is by structure and behaviour.

The vendored port's Makefiles expect Platinum's layout (`libraries/<lib>/src`). They cannot drive HGSS's tree as they are. `tools/hgss_nitro.py` solves this without editing them: a shim under `build/hgss-sdk`, plus `SDK=`, `SDK_SRCS=`, `SDK_INCLUDES=` and `ARM7_SOUND_SRCS=` on the make command line ([docs/port-wiring.md](../port-wiring.md), [Build-Reference](Build-Reference.md#7-how-the-vendored-port-build-is-wired-and-why-it-fails-today)).

## Assembly written in MWCC's inline `asm`

This is the biggest practical difference. The DS SDK has small functions that must use exact ARM instructions: the interrupt context save and restore, matrix maths, the count-leading-zeros instruction, and the quicksort. The original code writes them in MWCC's inline `asm` form inside C files.

- **What it means for GCC.** GCC cannot compile MWCC's `asm` syntax. Those functions stop the GCC build.
- **How many.** The record counts 37 files under `lib/NitroSDK/src` that contain `asm` functions or blocks, and about 41 more in NitroSystem, MSL_C, NitroDWC, `src` and `sub`. The record marks these counts as approximate ([docs/sdk-diff.md](../sdk-diff.md), section 2).
- **The three `.s` files** in `lib/NitroSDK` are data only (`fx_sincos.s`, `wvr_data.s`, `mb_data.s`). The assembly is in the C files.
- **How it is handled.** Each function gets a C version under `SDK_PORT_3DS`, and the original `asm` stays under `#else`. The patches in `patches/` do this. See [Patch-Stack](Patch-Stack.md) for the list.

The C versions are only as good as the reading of the asm behind them, so each patch carries a test:

- The matrix functions were checked against the definitions, and against an interpreter for the asm subset outside the repo, per the patch message.
- `MATH_QSort` was checked against the call sequence of the original ROM code, using an ARM emulator. That check needs the user's own ROM and is local only ([tools/qsort_rom_oracle.py](../../tools/qsort_rom_oracle.py) header).

Some asm does not translate directly, and the port replaces the behaviour instead. The DS's cycle-counting delay `OS_SpinWait` returns at once on the port. The DS's cache, tightly-coupled-memory and protection-unit setup is accepted and ignored. `OS_Halt`, which waits for an interrupt on the DS, advances one frame ([3ds_port/nitro/README.md](../../3ds_port/nitro/README.md), "Nothing to do on the 3DS"; patch `0401` and `0201` messages).

## The PXI message protocol

PXI is the message channel between the ARM9 (the game) and the ARM7 (the services) ([3ds_port/nitro/src/pxi.c](../../3ds_port/nitro/src/pxi.c) header).

- **Tags match.** The 17 PXI tags, from `EX` to `CTRDG_Ex`, have the same names and values in both SDKs. HGSS does not define `PXI_FIFO_TAG_CTRDG_PHI` (value 17); the port's `pxi.c` uses it, so patch 1201 adds it under `SDK_PORT_3DS` only (the DS enum is unchanged, `PXI_MAX_FIFO_TAG` stays 32) ([docs/sdk-diff.md](../sdk-diff.md), section 3).
- **Commands match by number, not by name.** The port's virtual ARM7 decodes commands by name, such as `RTC_PXI_COMMAND_*`, `SPI_PXI_COMMAND_PM_*` and `PM_UTIL_*`. HGSS's ARM9 code uses literal numbers instead. Every number was checked and matches: RTC `0x10/0x11/0x12/0x27`, touch `0/1/2/0x10`, power `0x60-0x67` and `0x70-0x74`, mic `0x40/0x41/0x42/0x51`, utility LED 1/2/3, backlights 4-9, power-off 14. The shared framing bits match too (`START 0x02000000`, `END 0x01000000`, command in bits 8-14, result in bits 0-7). So a compat header defining the Platinum names with HGSS's numbers is correct at the command layer ([docs/sdk-diff.md](../sdk-diff.md), sections 3.1-3.5).
- **Consequence.** The names alone are not enough: the port's RTC/touch/power decoders must follow HGSS's multi-word request sequences and reply parsing word for word. That means HGSS-specific decoders in new files (not done). The mic has no decoder in the port at all; it is deferred to Phase 7 (`port_mic.c`).
- **Payloads compared.** The per-command request words, reply words (`(cmd << 8) | status`, auto-sampling notifications without the END bit), and the shared buffers (RTC date/time at `0x027FFDE8`, touch point at `0x027FFFAA`, mic address/data at `0x027FFF90/94`) are all verified with file and line citations ([docs/sdk-diff.md](../sdk-diff.md), section 3).

## Shared memory structures

- **`OSSystemWork`.** HGSS's field list has 40 entries and Platinum's has 41. The only difference is naming. HGSS has `u8 padding3[2]` where Platinum has two `u8` fields, `ctrdg_SetModuleInfoFlag` and `ctrdg_IsExisting`, which belong to the GBA slot. Both cover the same 2 bytes. The offsets are now verified by running summation over the HGSS header: `real_time_clock` at `0x1E8`, `mic_last_address` at `0x390`, `mic_sampling_data` at `0x394`, `touch_panel` at `0x3AA`, `command_area` at `0x3FE`, `0x400` bytes total ([docs/sdk-diff.md](../sdk-diff.md), section 4).
- **Sound shared work.** `SNDSharedWork` is 0x280 bytes in HGSS, and its members match Platinum's `work.h`. Both have a 0x14-byte padding block at offset 0xC, with no named members ([docs/sdk-diff.md](../sdk-diff.md), section 5).
- **ARM7 sound driver.** HGSS has no ARM7 sound source in `lib/NitroSDK`. Its driver is in `sub/lib/src`. Platinum's virtual ARM7 sound patches (0018 to 0023) were written against the Platinum driver, so they have to be rewritten against `sub/`, not rebased. The entry-point mapping is verified: the port's seven `N7_SND_*` names correspond one-to-one to `SND_Init`, `SND_CommandProc`, `SND_UpdateExChannel`, `SND_SeqMain`, `SND_ExChannelMain`, `SND_UpdateSharedWork` and `SND_CalcRandom` in `sub/lib/src` ([docs/sdk-diff.md](../sdk-diff.md), section 5).
- **Threads and interrupts.** `OSContext` (68 bytes to `SP_SVC`, 72 to `CPContext`, 100 total), `OSThread` (INFINITY field order), `OSThreadInfo` (16 bytes) and `OSIrqCallbackInfo` (DMA0-3 = 0-3, TIMER0-3 = 4-7, table max 22) are identical on both sides ([docs/sdk-diff.md](../sdk-diff.md), section 9.1).

## Graphics commands

The G3 command table has 77 opcodes, and every value matches between the two SDKs ([docs/sdk-diff.md](../sdk-diff.md), section 6). The port's 3D patches change how the registers are accessed, not the table.

## The save backup API and the IR card calls

- **A new backup type.** HGSS adds `CARD_BACKUP_TYPE_FLASH_4MBITS_EX`, defined as the 4 Mbit flash type with vendor 255 (`lib/include/nitro/card/common.h`, line 54). The game tries it first and falls back to `FLASH_4MBITS` (`src/save.c`, lines 1093 to 1096).
- **Resolved.** HGSS's identify core fills identical parameters for both 4 Mbit types (the vendor bits only matter for 8 Mbit chips), but the decision lives in the PXI reply: `CARD_IdentifyBackup` sends `CARD_REQ_IDENTIFY` and succeeds only on a success reply. The port answers that request itself and accepts only the plain 4 Mbit type (`port_card.c`, lines 260-267). So the `_EX` probe fails and the game falls back to the plain type, which succeeds with identical parameters. No change needed ([docs/sdk-diff.md](../sdk-diff.md), section 7).
- **The IR card calls.** `CARD_SpiWait*` exists only in HGSS. It is the bus used by the Pokéwalker infrared code and by a presence probe in the field code. Patch `0701` stubs it for now, so it reports no peer ([docs/sdk-diff.md](../sdk-diff.md), section 8).

## Compiler-level fixes

Some differences are not in the SDK's design. They come from how GCC and MWCC read the same C. The patch files record these kinds of fix:

- A prototype with no return type, which MWCC accepts as `int`. GCC 14 and later reject it. Patch `1101` adds the `int` on the port only.
- An unnamed parameter in a function definition. MWCC accepts it; GCC 10 rejects it. Patch `1201` names it on the port branch.
- A header that repeats the CRC structs and typedefs the public header defines. MWCC accepts the repeat; GCC rejects it. Patch `1201` takes them from the public header under the macro.
- `OS_IrqHandler`'s `OSIrqMask` parameter is only an alias for `r0` in the DS asm and the C body never reads it; the port calls it with no argument. Patch `1201` makes the port prototype and definition `(void)`.
- Two MI DMA registers are spelled as absolute DS addresses instead of `HW_REG_BASE + offset`, so the memory-map override misses them. Patch `1302` derives both from `HW_REG_BASE` on the port.
- A function that the header declares with external linkage, but that the source defines `static`. MWCC keeps the definition local. GCC refuses the mismatch. Patch `0301` drops `static` on the GCC branch.
- `__declspec(noinline)`, which GCC does not know. Patch `0301` uses `__attribute__((noinline))` on the GCC branch.
- `u64` alignment, which changes struct layouts. Patch `1001` fixes it. See [Save-Compatibility](Save-Compatibility.md).

## The survey: 243 to 270 of 284

The survey is `tools/sdk_compile_survey.py`. It runs `gcc -fsyntax-only` on every `.c` file under `lib/NitroSDK/src` and `lib/NitroSystem/src`, with the port's flags and `-DSDK_PORT_3DS`. A file counts as parsing if GCC reports no error. It does not link or run anything ([tools/sdk_compile_survey.py](../../tools/sdk_compile_survey.py), docstring).

The total is 284 files. That is 190 in NitroSDK plus 94 in NitroSystem, which I counted in the pinned tree.

- **Baseline, before any patch:** 243 of 284 parse, at 32-bit ([docs/progress/phase-1.md](../progress/phase-1.md)).
- **With all 15 patches:** 270 of 284 parse with gcc 13, 269 with gcc 10 ([docs/progress/STATE.md](../progress/STATE.md)). The extra gcc-10 failure is `ctrdg/CTRDG_common.c`; `SND_command.c` was fixed by patch 1201.

### The 14 files that still do not parse

The phase-1 record lists these 14 files. It says each one is port-replaced, excluded from the HGSS source list, and not patched ([docs/progress/phase-1.md](../progress/phase-1.md), "Remaining SDK parse failures"):

- `crt0.c`
- `dgt_hash2_arm4_small.c`
- `mi/mi_memory.c`, `mi/mi_swap.c`, `mi/mi_uncompress.c`
- `os/os_cache.c`, `os/os_exception.c`, `os/os_protectionRegion.c`, `os/os_protectionUnit.c`, `os/os_reset_boot.c`, `os/os_reset_clear32.c`, `os/os_reset_system.c`, `os/os_tcm.c`
- `os/os_alarm.c`

The repo gives a reason for some of them, and this page keeps to those:

- `os_alarm.c` is not in the DS ROM. `main.lsf` links `os_alarm_irq_handler.o` and not `os_alarm.o` ([patches/NitroSDK/0301](../../patches/NitroSDK/) message). It still fails to parse under GCC at line 151, and that is why it is excluded rather than patched.
- The cache, protection-unit and tightly-coupled-memory files configure the DS's ARM946 CPU. The port's README says the port accepts and ignores that setup ([3ds_port/nitro/README.md](../../3ds_port/nitro/README.md), "Nothing to do on the 3DS").
- `mi_memory.c` also defines `MI_Copy32B`, `MI_Copy36B`, `MI_Copy48B` and `MI_Copy64B`, which the port's `mi_memory.c` lacks. Excluding the file leaves those functions without a home ([docs/progress/phase-1.md](../progress/phase-1.md)).

The record does not check each file against the port file that replaces it. Treat "port-replaced" as the record's claim, not a verified mapping.

## The port layer against HGSS: 29 of 46

Separately from the SDK survey, the record checks the port's own sources, which were written for the Platinum SDK. The check uses a shim that exposes only `nitro*`, `nnsys*` and `cw*` from `lib/include`. The shim is needed because HGSS's `stdarg.h`, `stddef.h` and `string.h` would hide the system versions the port needs ([docs/port-wiring.md](../port-wiring.md), "Measured state").

- **29 of 46 port sources compile** (it was 26 before patches 1201, 1301 and 1302).
- **17 fail for 15 named causes.** The causes include header names that differ or are missing (for example `nitro/spi/common/type.h` is not in HGSS, whose nearest file is `spi/common/pm_common.h`), a `card_common.h` include where HGSS has `CARD_common.h`, `MIi_Cpu*` signature mismatches, and missing MI DMA timing names. The full table is in [docs/port-wiring.md](../port-wiring.md); the live state is [docs/progress/STATE.md](../progress/STATE.md).
- **The real build:** `tools/hgss_nitro.py` drives the vendored Makefiles and reaches 9 objects before errors — 44 `error:` lines. That is the number to drive down.
- **The vendored unit tests are not built.** `make port-tests` says it is not implemented and fails on purpose. The 11 SDK host suites and the tool tests are green; those are tests of the patches, not the vendored suites ([docs/progress/STATE.md](../progress/STATE.md)).
- **The source list is not fixed.** About 30 Platinum SDK sources have no HGSS counterpart. About 60 map only in part, because HGSS's SDK contains only what HGSS uses ([docs/port-wiring.md](../port-wiring.md), "Source list").
- **The suggested design now exists:** `tools/hgss_nitro.py` builds the out-of-tree shim under `build/` and passes command-line overrides (`SDK=`, `SDK_SRCS=`, `SDK_INCLUDES=`, `ARM7_SOUND_SRCS=`), leaving the vendored Makefiles unchanged.

## What the comparison settled, and what work it leaves

The sdk-diff record is now complete. What it settled:

- `OSThread` and `OSContext` layouts, the IRQ table, and the interrupt handler offsets (section 9.1). The handler C replacements (patches 0401/0501/0901) apply to confirmed-identical offsets.
- The payload formats of the RTC, touch, power and mic PXI requests (sections 3.2-3.5).
- NitroSystem differences for Platinum's five NitroSystem patches (section 9.2): the GCC-build fixes are covered where they apply (NNS 0301 for CLZ, pokeheartgold 1101 for the return type); the sound-capture division has no HGSS counterpart; the GXFIFO-forwarding, queued-3D-transfer and OAM-disable-bit equivalents are located but not yet written.
- The `OSSystemWork` offsets (section 4) and the ARM7 sound entry-point mapping (section 5).

What is still work, not comparison: writing the compat headers, the HGSS PXI decoders, the three owed NitroSystem equivalents, and the ARM7-sound rewrite. Those are tracked in [docs/progress/STATE.md](../progress/STATE.md) under "Next actions".
