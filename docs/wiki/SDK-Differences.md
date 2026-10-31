# SDK differences

This page explains how HGSS's copy of Nintendo's system library (NitroSDK, and the NitroSystem libraries on top of it) differs from the copy the Platinum port was written for. It summarizes [docs/sdk-diff.md](../sdk-diff.md) and [docs/port-wiring.md](../port-wiring.md). Where those two files give a status, this page gives the same status. Where they say something is not yet compared, this page says so too.

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

The vendored port's Makefiles expect Platinum's layout (`libraries/<lib>/src`). They cannot drive HGSS's tree as they are. The record leaves the fix open: either a make fragment passed on the command line, or a patch to the vendored Makefiles that goes upstream ([docs/sdk-diff.md](../sdk-diff.md), section 1; [docs/port-wiring.md](../port-wiring.md)).

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

- **Tags match.** The 17 PXI tags, from `EX` to `CTRDG_Ex`, have the same names and values in both SDKs. HGSS does not define `PXI_FIFO_TAG_CTRDG_PHI` (value 17), and the port does not use it ([docs/sdk-diff.md](../sdk-diff.md), section 3).
- **Commands do not match.** The port's virtual ARM7 decodes commands by name, such as `RTC_PXI_COMMAND_*`, `SPI_PXI_COMMAND_PM_*` and `PM_UTIL_*`. HGSS's ARM9 code uses literal numbers instead. The power commands are `0x60`, `0x62` and `0x70` to `0x74` in `pm.c`, and the touch code sends raw words such as `0x3000000` ([docs/port-wiring.md](../port-wiring.md), "PXI protocols differ").
- **Consequence.** A header that only defines the Platinum names would compile, and then the port would answer requests wrongly. The fix is HGSS-specific decoders, in new files. That is not done.
- **Payloads not yet compared.** The record marks the per-command payload formats for card, RTC, touch, power, sound and mic as not yet compared ([docs/sdk-diff.md](../sdk-diff.md), section 3, marked not done).

## Shared memory structures

- **`OSSystemWork`.** HGSS's field list has 40 entries and Platinum's has 41. The only difference is naming. HGSS has `u8 padding3[2]` where Platinum has two `u8` fields, `ctrdg_SetModuleInfoFlag` and `ctrdg_IsExisting`, which belong to the GBA slot. Both cover the same 2 bytes. The record says the offsets are not yet confirmed ([docs/sdk-diff.md](../sdk-diff.md), section 4). The layout probe covers the save-chunk types only, as `tools/layout_probe/roots.py` shows, so the repo does not yet check these offsets.
- **Sound shared work.** `SNDSharedWork` is 0x280 bytes in HGSS, and its members match Platinum's `work.h`. Both have a 0x14-byte padding block at offset 0xC, with no named members ([docs/sdk-diff.md](../sdk-diff.md), section 5).
- **ARM7 sound driver.** HGSS has no ARM7 sound source in `lib/NitroSDK`. Its driver is in `sub/lib/src`. Platinum's virtual ARM7 sound patches (0018 to 0023) were written against the Platinum driver, so they have to be rewritten against `sub/`, not rebased ([docs/sdk-diff.md](../sdk-diff.md), section 5).

## Graphics commands

The G3 command table has 77 opcodes, and every value matches between the two SDKs ([docs/sdk-diff.md](../sdk-diff.md), section 6). The port's 3D patches change how the registers are accessed, not the table.

## The save backup API and the IR card calls

- **A new backup type.** HGSS adds `CARD_BACKUP_TYPE_FLASH_4MBITS_EX`, defined as the 4 Mbit flash type with vendor 255 (`lib/include/nitro/card/common.h`, line 54). The game tries it first and falls back to `FLASH_4MBITS` (`src/save.c`, lines 1093 to 1096).
- **Open question.** The record says that for a 4 Mbit chip both types give the same parameters in HGSS's identify code. Whether the port's card service runs that code, or answers identification itself, is not yet checked ([docs/sdk-diff.md](../sdk-diff.md), section 7).
- **The IR card calls.** `CARD_SpiWait*` exists only in HGSS. It is the bus used by the Pokéwalker infrared code and by a presence probe in the field code. Patch `0701` stubs it for now, so it reports no peer ([docs/sdk-diff.md](../sdk-diff.md), section 8).

## Compiler-level fixes

Some differences are not in the SDK's design. They come from how GCC and MWCC read the same C. The patch files record these kinds of fix:

- A prototype with no return type, which MWCC accepts as `int`. GCC 14 and later reject it. Patch `1101` adds the `int` on the port only.
- A function that the header declares with external linkage, but that the source defines `static`. MWCC keeps the definition local. GCC refuses the mismatch. Patch `0301` drops `static` on the GCC branch.
- `__declspec(noinline)`, which GCC does not know. Patch `0301` uses `__attribute__((noinline))` on the GCC branch.
- `u64` alignment, which changes struct layouts. Patch `1001` fixes it. See [Save-Compatibility](Save-Compatibility.md).

## The survey: 243 to 270 of 284

The survey is `tools/sdk_compile_survey.py`. It runs `gcc -fsyntax-only` on every `.c` file under `lib/NitroSDK/src` and `lib/NitroSystem/src`, with the port's flags and `-DSDK_PORT_3DS`. A file counts as parsing if GCC reports no error. It does not link or run anything ([tools/sdk_compile_survey.py](../../tools/sdk_compile_survey.py), docstring).

The total is 284 files. That is 190 in NitroSDK plus 94 in NitroSystem, which I counted in the pinned tree.

- **Baseline, before any patch:** 243 of 284 parse, at 32-bit ([docs/progress/phase-1.md](../progress/phase-1.md)).
- **With all 11 patches the phase-1 record was written against:** 270 of 284 parse ([docs/progress/phase-1.md](../progress/phase-1.md)).
- **With patch `1101`:** not measured in the repo. The record does not say whether `1101` changes the count.

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

## The port layer against HGSS: 26 of 46

Separately from the SDK survey, the record checks the port's own sources, which were written for the Platinum SDK. The check uses a shim that exposes only `nitro*`, `nnsys*` and `cw*` from `lib/include`. The shim is needed because HGSS's `stdarg.h`, `stddef.h` and `string.h` would hide the system versions the port needs ([docs/port-wiring.md](../port-wiring.md), "Measured state").

- **26 of 46 port sources compile.**
- **20 fail for 17 named reasons.** The causes include header names that differ or are missing (for example `nitro/spi/common/type.h` is not in HGSS, whose nearest file is `spi/common/pm_common.h`), a `card_common.h` include where HGSS has `CARD_common.h`, a `MIi_CpuClear16` signature mismatch, and `OS_IrqHandler()` called with no argument where HGSS's prototype takes one. The full table is in [docs/port-wiring.md](../port-wiring.md).
- **The vendored unit tests are not built.** `make port-tests` says it is not implemented and fails on purpose. The record counts the 9 SDK host suites and the tool tests as green. Those are new tests for the patches, not the vendored suites ([docs/progress/phase-1.md](../progress/phase-1.md), exit checklist).
- **The source list is not fixed.** About 30 Platinum SDK sources have no HGSS counterpart. About 60 map only in part, because HGSS's SDK contains only what HGSS uses ([docs/port-wiring.md](../port-wiring.md), "Source list").

The design the record suggests is an out-of-tree shim under `build/`, with symlinks from Platinum's names to HGSS's files, plus command-line overrides such as `SDK=` and `SDK_SRCS=`. That keeps the vendored Makefiles unchanged. It is a suggestion, not built ([docs/port-wiring.md](../port-wiring.md), "Suggested design").

## What is not compared yet

The sdk-diff record marks these as not compared. Nothing on this page should be read as settled for them:

- `OSThread` and `OSContext` layouts, the IRQ table, and the interrupt handler assembly (section 9).
- The payload formats of the RTC, touch, power and mic PXI requests (section 3).
- NitroSystem differences that matter to Platinum's five NitroSystem patches (section 9). Of those, only the CLZ patch exists in the HGSS stack.
- The `OSSystemWork` offsets (section 4).
