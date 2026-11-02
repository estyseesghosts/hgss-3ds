# Patch stack

This page explains how the port uses the decomp without editing it, how the patches are numbered and applied, the guard that keeps the DS build unchanged, what evidence exists for that claim, and a catalog of every patch in `patches/`.

Related pages: [Architecture](Architecture.md), [SDK-Differences](SDK-Differences.md), [Contributing](Contributing.md), [Glossary](Glossary.md).

## Why patches

The decomp is the game's source, reconstructed, and it changes often. The port needs the game to build for a 3DS and a PC, and it needs the DS build to stay byte-identical with the retail ROM. Editing the decomp in place would mix the two, and every upstream change would have to be merged by hand.

So the decomp is pinned, fetched at build time, and the port's changes are kept as numbered patch files. Three rules follow from that ([gameplan.md](../../gameplan.md), ground rule 1; [CONTRIBUTING.md](../../CONTRIBUTING.md), "Changing the decompilation or the SDK"):

- The decomp is never edited in place.
- All changes to game, SDK or NitroSystem code are patch files under `patches/`.
- Bumping the pin is a deliberate task that reruns every test.

## The pin

`upstream.lock` names the decomp and its commit. `platinum.lock` names the Platinum port whose `3ds_port/` is vendored.

| Role | Repository | Commit | Lock file |
| --- | --- | --- | --- |
| Decomp, built and patched at build time | `ctrl-cheeb-del/pokeheartgold` | `8cbb4a19ab781611ff1767035dcaea6b47baeded` | `upstream.lock` |
| Hardware layer, vendored from | `GabeSud0/pokeplatinum-3ds` | `359fbec80a5c0664d4fa72caaae87465b640f9d6` | `platinum.lock` |

These are the same values as in [docs/BASE.md](../BASE.md). The pins were taken on 2026-10-30. The plan says to bump them only at phase boundaries, because the decomp renames types often ([gameplan.md](../../gameplan.md), Phase 0, step 3).

The byte-matching targets are the HeartGold US and SoulSilver US ROM SHA1s, `4fcded0e2713dc03929845de631d0932ea2b5a37` and `f8dc38ea20c17541a43b58c5e6d18c1732c7e582` ([docs/BASE.md](../BASE.md)).

## Bootstrap

`python3 tools/bootstrap.py` does this ([tools/bootstrap.py](../../tools/bootstrap.py), docstring):

1. Fetches exactly the pinned commit into `build/upstream/` (a shallow fetch of one commit).
2. Applies the three patch stacks in this order, each with `git am`, so every patch keeps its message:
   - `patches/NitroSDK/*.patch` under `lib/NitroSDK`
   - `patches/NitroSystem/*.patch` under `lib/NitroSystem`
   - `patches/pokeheartgold/*.patch` at the repository root
3. Stops the run if any patch does not apply cleanly. The patch is aborted first.
4. Creates `build/NitroSDK` and `build/NitroSystem` as symlinks, because the vendored port's Makefiles look for the SDKs there.
5. Generates `fx_const.h` with the decomp's own host tool, if that tool is present.

Patches within a stack are applied in sorted filename order, which is their numeric order. `python3 tools/bootstrap.py --clean` deletes `build/upstream` and starts again. Without `--clean`, bootstrap refuses to run if the number of patches already applied differs from the number in `patches/`. The unit tests for this logic are in `tests/test_bootstrap.py` (4 tests in the file).

## Numbering

A patch name is four digits and a short slug, for example `0701-card-CARD_SpiWait-stub-with-no-IR-peer-SDK_PORT_3DS.patch`.

- **The numbers are per stack.** `NitroSDK/0301` and `NitroSystem/0301` are different patches. The first number is the stack's own counter, not a shared one.
- **The NitroSDK numbers are grouped by area.** The phase-1 log gives the groups: `01xx` fx matrix code, `02xx` small hardware, lock and FIFO functions, `03xx` one-off GCC fixes, `04xx` os_system, `05xx` context and divider, `06xx` port hooks, `07xx` card, `08xx` qsort, `09xx` IRQ handler ([docs/decisions.md](../decisions.md), 2026-10-31).
- **The numbers are not Platinum's.** Platinum's series runs 0001 to 0027. The plan expected to rebase those ([gameplan.md](../../gameplan.md), Phase 1, step 4). They could not be applied to HGSS's SDK, so the HGSS series is its own ([docs/progress/phase-1.md](../progress/phase-1.md), "Deviations").
- **The `pokeheartgold` stack is grouped the same way:** `10xx` types and ABI, `11xx` NitroSystem headers, `12xx` GCC-only fixes, `13xx` memory map, `14xx` ARM7 sound, GX and static initializers, `15xx` Phase 2 game GCC fixes, `16xx` decomp build metadata, `17xx` Phase 2/3 runtime and 3DS build fixes, `18xx` boot fixes, `19xx` sanitizer and relocation fixes. It holds `1001`, `1101`, `1201`-`1203`, `1301`-`1305`, `1401`-`1407`, `1501`-`1525`, `1601`-`1603`, `1701`-`1703`, `1710`-`1712`, `1760`-`1763`, `1801`-`1802` and `1901`-`1905` today (62 patches). The NitroSystem stack adds `0301`-`0303`; the NitroSDK stack `0101`-`0901` with gaps (`15` patches). Patches that touch more than one tree (or the decomp root) go here rather than in an SDK stack. Total: **80 patches**.

## The guard

Every change to a DS-facing file is inside the macro `SDK_PORT_3DS`. The macro is defined only by the port's builds, never by the MWCC build. The pattern is:

```c
#ifdef SDK_PORT_3DS
/* the GCC version, or the port's version of the behaviour */
#else
/* the original text, unchanged */
#endif
```

The concrete example is patch `1001`, which makes the 64-bit integer typedefs 4-byte aligned for the port only ([patches/pokeheartgold/1001](../../patches/pokeheartgold/)):

```c
#ifdef SDK_PORT_3DS
typedef unsigned long long int u64 __attribute__((aligned(4)));
typedef signed long long int s64 __attribute__((aligned(4)));
#else
typedef unsigned long long int u64;
typedef signed long long int s64;
#endif
```

Two forms appear in the patch files. Both were counted from the `#if`, `#else` and `#endif` lines in each patch.

- **Replacement with an `#else`.** A hunk replaces an MWCC `asm` function, or a declaration GCC cannot parse, with a C version. The original text stays under `#else`. Most of `0101`, `0201`, `0301`, `0701` and `0901` work this way, and so do some hunks in `0501` and `0601`.
- **Additive guard with no `#else`.** A hunk adds a guarded block and leaves the DS text as it was. Several blocks in `0501` and `0601` are like this, and so is the whole of `1101`.

Only one patch edits an existing DS line. In `0401` the original `#ifdef SDK_ARM9` becomes `#elif defined(SDK_ARM9)` under the new `#ifdef SDK_PORT_3DS`. Patches `0801` and `0901` wrap an existing forward declaration in `#ifndef SDK_PORT_3DS` without changing its text. Across all 80 patches the only removed lines are that one in `0401` and the reformatted lines listed in each patch's message; every patch states a `gcc -E -P` identity check.

The macro is `SDK_PORT_3DS` and not Platinum's `SDK_CW` or `__MWERKS__`. Patch `0101` gives the reason: the port's rule is one guard for port-only behaviour, so the MWCC build never takes a port branch ([patches/NitroSDK/0101](../../patches/NitroSDK/) message).

## Why the DS build is unchanged

There are three pieces of evidence, and they are not the same strength.

1. **Preprocessor identity (per patch, by hand).** The patch messages say that the output of `gcc -E -P` for each touched file is identical with and without the patch when `SDK_PORT_3DS` is not defined. The messages of `0201`, `0401`, `0501`, `0801`, `1101`, `1201`, `1301` and `1302` state this check. The phase-1 record says the same for `1001`. There is no automated check in the repo yet. The plan's patch-guard job is not in `port.yml` ([gameplan.md](../../gameplan.md), "CI workflows").
2. **The byte-identical ROM gate.** This is the strong one. The gate last ran at **78 patches** from `/work/pokeheartgold` (patches `1904` and `1905`, added afterwards, are `SDK_PORT_3DS`-only and their `#else` text is unchanged): both matching builds reproduced the retail ROMs, HeartGold `4fcded0e2713dc03929845de631d0932ea2b5a37` and SoulSilver `f8dc38ea20c17541a43b58c5e6d18c1732c7e582`, `sha1sum -c` OK. The five build-tool input-pin classes were fixed in `1601`/`1602`/`1603`/`1762`/`1763`; patch `1407` is `SDK_PORT_3DS`-only and its `#else` text is unchanged. See [docs/progress/STATE.md](../progress/STATE.md). The gate must run from `/work/pokeheartgold`.
3. **The layout probe.** The MWCC reference values in `tools/layout_probe/mwcc.txt` were produced by MWCC itself. The probe compares GCC against them. This is about struct layout, not code. See [Save-Compatibility](Save-Compatibility.md).

A matching build needs one extra step. `tools/matching-build.sh` adds five `KEEP_SECTION` blocks to the generated `main.lcf`, because the linker in the dev container strips five sections that the retail ROM keeps. The step edits a build artifact, not the decomp. The root cause is not established ([tools/matching-build.sh](../../tools/matching-build.sh) header; [docs/progress/phase-0.md](../progress/phase-0.md)).

The SoulSilver ROM was not rebuilt against the patched tree in phase 1 ([docs/progress/phase-1.md](../progress/phase-1.md)).

## Catalog

Every patch in `patches/`, in stack order. The "Verified by" column names what the repo or the patch message records. Counts are the ones the patch message or phase record states; this page did not rerun them.

| Stack | Patch | Area | What it does | Verified by |
| --- | --- | --- | --- | --- |
| NitroSDK | 0101 | `fx/fx_mtx*.c` (8 files) | Gives the 14 MWCC `asm` matrix functions a plain C version. GCC cannot parse the `asm` form. | `tests/sdk/fx` (host GCC `-m32`; 428 cases per the patch message). An asm interpreter outside the repo (3,011 checks, per the message). |
| NitroSDK | 0201 | 8 files: `os_vram_exclusive_clz.c`, `os_init.c`, `os_spin_lock_id.c`, `os_terminate_proc.c`, `gxasm.c`, `g3x_nop_clear_fifo.c`, `mic.c`, `rtc_api.c` | C versions of small hardware and lock functions: count of leading zeros, the VCOUNT wait, Halt, lock IDs, GX FIFO sends, the NOP clear, and the RTC and mic busy waits. The patch only adds lines. | `tests/sdk/small` (54 cases per the message). `rtc_api.c` and `mic.c` are compile-only, because their waits poll ARM7 words. |
| NitroSDK | 0301 | `os_thread.c`, `os_valarm.c`, `os_alarm_irq_handler.c` | Three GCC-only fixes. In `os_thread.c` the `static` is dropped on the GCC branch to match the header's linkage. In `os_valarm.c`, `__declspec(noinline)` becomes `__attribute__((noinline))`. `OSi_AlarmHandler` gets a C body that calls `OSi_ArrangeTimer`. | No dedicated test in `tests/sdk`. The GCC parse check (`tools/sdk_compile_survey.py`) and reading the link map, per the message. The phase-1 table says "host test"; no such test is in the repo. |
| NitroSDK | 0401 | `os_system.c` | C versions of seven CPSR interrupt functions, on the port's emulated CPSR. Raises a pending IRQ when the I bit is cleared. `OS_SpinWait` returns at once. This is the one patch that edits an existing DS line (`#ifdef SDK_ARM9`, as `#elif`). | `tests/sdk/os_system` (29 checks per the message). |
| NitroSDK | 0501 | `CP_context.c`, `fx_cp.c`, `os_context.c` | C versions of the CP context save and restore, and of `OS_InitContext`, `OS_SaveContext` and `OS_LoadContext`. Hooks run once after the divider and square-root register writes. | `tests/sdk/cp_context` (context and divider tests; 142 cases per phase-1). The message lists the mutation checks that fail when the code is broken. |
| NitroSDK | 0601 | `g3x.c`, `mi_dma.c`, `os_interrupt.c`, `os_tick.c` | Three port hooks. IRQ requests are cleared bit by bit, because the DS's IF register is write-one-to-clear. DMA control writes start the port's software DMA, and the channel-0 dummy transfer is skipped. Matrix result registers are synced before they are read. | `tests/sdk/port_hooks` (three test programs; 10, 60 and 10 checks per the message). Each check was broken on purpose and caught. |
| NitroSDK | 0701 | `card_spi_wait.c` | Stubs the three `CARD_SpiWait*` hardware functions. The status read returns `0xAA` ("IR chip present") with no peer behind it. Reads return a zero-length block and writes are dropped. No register is touched. | `tests/sdk/card_spi_wait` (19 checks). The register page is `PROT_NONE` in the test, so a register access would fault. |
| NitroSDK | 0801 | `math_qsort.c` | A C version of `MATH_QSort` that follows the asm's control flow. The order of equal elements and the sequence of comparator calls both depend on the algorithm, so this is not a generic quicksort. | `tests/sdk/math_qsort` (11,202 checks per the message). `tools/qsort_rom_oracle.py` runs the original ROM machine code in an ARM emulator on the user's own ROM. That check is local only. Phase 1 records 465,985 comparator calls as identical. |
| NitroSDK | 0901 | `os_irqHandler.c` | C versions of `OS_IrqHandler` and `OS_IrqHandler_ThreadSwitch`. The thread switch calls the port's `NitroPort_IrqThreadSwitch`. | `tests/sdk/irq_handler` (10 checks per the message). |
| NitroSystem | 0301 | `g2d_canvas_arrange.c`, `g2d_char_canvas.c`, `g2d_char_canvas_util.c` | Three copies of `CountLeadingZeros` use MWCC `asm`. The GCC copy returns 32 for zero, and `__builtin_clz` otherwise, which matches the `clz` instruction. | `tests/sdk/nns_clz` (checks zero, one, single bits, `0x80000000`, `0xFFFFFFFF` and a random sweep, per the message). |
| pokeheartgold | 1001 | `lib/include/nitro/types.h` | Gives `u64` and `s64` a 4-byte alignment on the port. MWCC lays 64-bit fields in structs out with 4-byte alignment, while GCC's ARM EABI and `-malign-double` use 8. Without this patch, the devkitARM build and the `-malign-double` host build differ from MWCC in 55 save-struct values. | `tests/layout_probe` (804 values; 0 differences after the patch, 55 before, per phase 1). No full DS rebuild with this patch was run. |
| pokeheartgold | 1101 | `lib/include/nnsys/g2d/g2d_CellTransferManager.h` | Adds the return type `int` to one prototype, on the port only. GCC 14 and later reject a declaration with implicit `int`; the definition already returns `int`. | No dedicated test. The message says `gcc -E -P` is identical without the macro. It unblocks the devkitARM 16 build of NitroSystem. |
| pokeheartgold | 1201 | `lib/NitroSDK/src/snd/SND_command.c`, `lib/NitroSDK/include/MATH_crc.h`, `lib/include/nitro/os/irqHandler.h`, `lib/NitroSDK/src/os/os_irqHandler.c`, `lib/include/nitro/pxi/fifo_shared.h` | Four GCC-10 fixes for the port's host32 build: name an unnamed parameter, take the CRC structs from the public header instead of repeating them, make `OS_IrqHandler` take `(void)` (the DS parameter is only an r0 alias), and add `PXI_FIFO_TAG_CTRDG_PHI` (17) for the port's `pxi.c`. | `tests/sdk/gcc_fixes` (4 groups) and `tests/sdk/irq_handler` (10 checks). `gcc -E -P` without the macro is identical on every touched unit. |
| pokeheartgold | 1301 | `lib/include/nitro/hw/ARM9/mmap.h` | Under `SDK_PORT_3DS`, `HW_IOREG`, main memory, ITCM, WRAM, the palette/OAM/VRAM views and their `END`/`SIZE` macros point at the port's arrays (`NitroPort_IoReg` and friends) at the DS sizes and offsets. Same design as Platinum 0001/0005, re-derived for HGSS. The 64-bit `host` target is not supported by this design. | `tests/sdk/memory_map` (35 checks with 1302). |
| pokeheartgold | 1302 | `lib/include/nitro/mi/dma.h`, `lib/NitroSDK/include/MI_dma.h` | `REG_ADDR_DMA0CNT` and `REG_ADDR_DMA0_CLR_DATA` are absolute DS addresses in these headers, so 1301's remap did not reach them. Under the macro they derive from `HW_REG_BASE`; the flat header gains the memory-map include on the port branch only. | `tests/sdk/memory_map` (both addresses and a write). The port_hooks DMA test segfaulted before this patch. |
| NitroSDK | 0302 | `os_arena.c` and the arena/stack linker-symbol users | Replaces the linker-provided arena and stack symbols with definitions the port controls. | SDK suites and the host32 `libnitro.a` link. |
| NitroSDK | 0303 | `MATH_pop.c` | `MATH_CountPopulation` returns `u32` under `SDK_PORT_3DS`. HGSS defines it `u8` but declares it `int`; on ARM EABI the caller reads garbage above the low byte (`0x04080C11` for `0xF0F0F0F1`). | `test_nitro` population-count checks; MWCC text unchanged. |
| NitroSDK | 0502 | `fx_vec*.c`, `fx_mtx44_{ortho,perspective}.c`, `tp.c` | Hooks the remaining divider/square-root register writers, so every consumer updates the port's software DIV/SQRT model. | `tests/sdk/cp_context` / `div_sqrt_writers`; identity checked. |
| NitroSDK | 0602 | `os_tick.c`, `os_timer.c` | Counts the hardware timers in software under the port; the timer alarms are driven by the port's tick. | `test_threads` (timers) and the port's OS timer tests. |
| NitroSDK | 0603 | `fs_rom.c`, `fs_overlay.c`, `FS_archive*.c` | ROM-archive and overlay hooks for the port (the port's arrays and overlay table replace the DS card/overlay hardware). | `test_nitro` FS checks; host32 `libnitro.a`. |
| NitroSDK | 0604 | `g3imm.c`, `g3x.c`, `g3*.c` | 3D command ports: packed G3 commands go through the port's decoder; the matrix stack reports errors instead of touching DS registers. | `test_gx3d`, `test_gx3d_plumbing`, `test_render`. |
| NitroSystem | 0302 | `g3d_gecom.c`, `g3d_sbc.c`, `gfd_vram_transfer_manager.c`, `g3d_oam.c` | GXFIFO word stores, early 3D VRAM transfers and hidden OAM entries through the port's hooks. | `test_gx3d`, `test_vram_transfer`. |
| NitroSystem | 0303 | `gfd_linked_list_tex_vram_man_reset.c` | Emits one ordered `.bss` block (`freeList/freeList4x4/work` at +0/+4/+8) under `SDK_PORT_3DS`. GCC's `.bss` order otherwise makes `NNS_GfdInitLnkTexVramManager` write through garbage. | `test_nitrosystem` (17,122 checks, 0 failed); identity proof 5,155 lines, 0-line diff. |
| pokeheartgold | 1202 | SDK header signatures used by the port sources | Layout-neutral aliases/signatures for `MIi_Cpu*`, `PXI_*`, `DC_StoreRange`, NVRAM `Option`, `DGT_Hash*`, `CARDiCommandArg` (`dest`/`dst`). | `tests/sdk/gcc_fixes` (11 groups). |
| pokeheartgold | 1203 | `mi_dma_gxcommand.c` | Skips the local `GXFifoStat` copies when the compat header supplies them. | `test_nitro`; the build failed with a redeclaration before. |
| pokeheartgold | 1303 | `gx_load2d.c` | The 2D palette/OAM/LCDC loaders use the port's `HW_*` areas instead of raw `0x05000000`/`0x07000000`. | `test_nitro` DMA/GX checks; `test_render`. |
| pokeheartgold | 1304 | RTC shared-work users | The raw RTC words (`0x027FFDE8/EC/EA`) are read through `OSSystemWork.real_time_clock`. | `test_arm7` RTC checks. |
| pokeheartgold | 1305 | touch/PM/MIC shared-work users | `TP_RAW_POINT`, `TP_USER_SETTINGS`, `wmBootBuf` and the mic words go through `OSSystemWork`. | `test_arm7` touch/power; MIC compile path. |
| pokeheartgold | 1401 | `sub/lib/src/sound_*.c`, `lib/dsprot/include/static_init.h` | The genuine ARM7 sound driver is built as `N7_*` and static initializers use section attributes under the port. | `test_arm7`, `test_sound_*`. |
| pokeheartgold | 1402 | SDK headers: timer, overlay, 3D, uncompress | The header halves of the port hooks. | `libnitro.a` build and the suites. |
| pokeheartgold | 1403 | `sub/lib/src/sound_command.c` | Skips the `finishedCommandTag` bump while the ARM7 `SNDi_SharedWork` pointer is NULL. | `test_arm7`; it segfaulted before. |
| pokeheartgold | 1404 | `lib/NitroSDK/src/snd/SND_command.c`, `snd_flush_command.c` | Keeps the ARM9 command flush with the state it updates: the decomp mixes a 4-byte `sFreeList` pointer with a 40-byte `CommandState`. | `tests/sdk/arm7_sound/test_arm9_flush.c` (24 checks); vendored `test_arm7` 676/0 (was a spin). |
| pokeheartgold | 1405 | five `sub/lib/src/sound_*.c` | Absolute sound-register offsets (the array is indexed by the low 16 bits of the DS address) and the real 724-entry volume table. | `test_sound_sequence` 12/0 (was silent); `test_arm7`, `test_sound_chip`. |
| pokeheartgold | 1406 | `lib/NitroSDK/src/gx/gx_load3d.c` | Maps the 3D texture/palette LCDC pages onto `NitroPort_LcdcVram`; zero entries stay zero. | `test_vram_transfer` 24/0 (segfaulted before). |
| pokeheartgold | 1407 | `sub/lib/src/sound_channel.c` | `SND_SetupChannelNoise` writes `0x400 + 16c`/`0x408 + 16c` after the 1405 absolute-index change; the two writes had kept the old relative index and left a looped PCM control word, so a noise note hung until the next PCM setup. **Necessary but not sufficient:** the held-note symptom also needed the port-side NNS volume table (`_0210D750`); see [3DS-Pitfalls](3DS-Pitfalls.md#the-nns-volume-table-is-an-empty-alias). | `tests/sdk/arm7_sound` noise-channel checks (122 checks, 0 failed; 24 fail without it); DS gate at 78. |
| pokeheartgold | 1501 | `lib/src/NitroCrypto` RC4 | Plain-C RC4 under the port. | Crypto checks in the game build. |
| pokeheartgold | 1502 | files where an internal header declares a function non-static | Drops `static` on the port branch (GCC rejects the linkage mismatch). | host32 compile; same pattern reused by 1512/1523/1524. |
| pokeheartgold | 1503 | `src/field/weather_manager.c` | Types the snow tables and copies them instead of assigning arrays. | host32 compile. |
| pokeheartgold | 1504 | `src/field/field_very_tall_grass.c` | Completes the `extern` array element type. | host32 compile. |
| pokeheartgold | 1505 | `src/to91_msl_{array,scalar}_new.cpp` | Drops dynamic exception specifications under the port (C++17 removed them). | host32 compile; later re-pinned in 1602. |
| pokeheartgold | 1506 | `ScriptRunByIndex`, Frontier `GoTo` | Reads the word before using it (MWCC left a local pattern GCC did not). | game behaviour test where present; host32 compile. |
| pokeheartgold | 1507 | `Safari_GetFacingSafariObject` | Reads the two halfwords in the DS order. | host32 compile. |
| pokeheartgold | 1508 | battle locals | Initialises the locals MWCC left unset. | host32 compile. |
| pokeheartgold | 1509 | `lib/include/nnsys/snd_internal.h` | Repeats the port headers' exact `SND_*` types under `SDK_PORT_3DS`; fixes eight game TUs. | host32 compile; contributes to the NNS unity fix. |
| pokeheartgold | 1510 | 15 files, 38 labels | Adds an empty statement after case labels whose next statement is a declaration (C99 6.8.1). | host32 compile; identity proof per file. |
| pokeheartgold | 1511 | 8 files | Uses the port headers' types for redeclared SDK functions (CTRDG, MI compress, RTC, `strncmp`, `__msl_assertion_failed`, CRC table, GX). | host32 compile. |
| pokeheartgold | 1512 | `wifi_plaza_history.c`, `wifi_plaza_time_zone.c` | Drops `static` where the internal header declares the function non-static. | host32 compile. |
| pokeheartgold | 1513 | `src/battle/battle_main.c` | Registers the overlay static initializer with `__attribute__((section(".sinit"), used))`. | host32 compile; harness reports 2 overlays with static initializers. |
| pokeheartgold | 1514 | `src/comm_state_private.h` | Takes `MATHRandContext32` from the port's `rand.h` instead of repeating it. | host32 compile; `comm_state` links. |
| pokeheartgold | 1515 | `src/comm_state.c` | Drops `const` from the `linkedStates` copy destination so `ARRAY_ASSIGN` is legal. | host32 compile. |
| pokeheartgold | 1516 | `src/battle/battle_command.c` | Uses the port's hooked square-root helpers (the local copies bypassed `CPi_PortUpdateSqrt`). | host32 compile; `div_sqrt_writers` covers the hooks. |
| pokeheartgold | 1517 | `src/pokeathlon/pokeathlon_box_graphics.c` | Copies the 5-byte medal row instead of assigning an array. | host32 compile. |
| pokeheartgold | 1518 | `src/battle/battle_hp_bar.c` | Includes its data header from beside the file (the harness has no `-Isrc`). | host32 compile. |
| pokeheartgold | 1519 | `src/to91_msl_{array,scalar}_new.cpp` | `operator new`'s first parameter is `__SIZE_TYPE__`; the scalar nothrow overload drops `try/catch` under `-fno-exceptions`. | host32 compile; source hash re-pinned in 1602. |
| pokeheartgold | 1520 | 4 lobby `.cpp` files | Placement `operator new` first parameter is `__SIZE_TYPE__`. | host32 compile. |
| pokeheartgold | 1521 | `src/lobby/ppw_lobby.cpp`, `ppw_lobby_process.cpp` | Takes `va_list`/`memcmp`/`memcpy`/`memmove`/`memset`/`strlen` from the global namespace. | host32 compile. |
| pokeheartgold | 1522 | `src/lobby/ppw_lobby_players_update_states.cpp` | Writes the `PlayerRecord` designators in declaration order. | host32 compile. |
| pokeheartgold | 1523 | `src/wifi_plaza/wifi_plaza_news.c` | File-scope macro: nothing on the port, `static` on the DS; the header declares the functions non-static. | host32 compile. |
| pokeheartgold | 1524 | `src/nintendo_wfc_connect_unused_session.c` | Same for 57 state functions; the five headerless helpers stay static. | host32 compile. |
| pokeheartgold | 1525 | `src/application/ball_capsule_editor.c` | Section-attribute static initializer; the `__declspec` parse cascade (164 of 165 errors) goes away. | host32 compile; 1092/1092. |
| pokeheartgold | 1601 | `tools/{arm7-,}alarm-partition/profile.json` | Re-pins the 26 header hashes the alarm tools read. | The gate ran past those tools. |
| pokeheartgold | 1602 | `tools/{dwc-iterator-advance,msl-d1-partition,msl-new-partition}/profile.json` | Re-pins the three MSL/DWC profiles: `types.h` (1001), `tree-iterator4.h` (upstream bump), `to91_msl_*_new.cpp` (1505 and 1519). Also documents that the gate must run from `/work/pokeheartgold`. | The gate passed the header and source checks; the debug-info shape pin remains (§7 of the session-5 note). |
| pokeheartgold | 1904 | `src/options_app.c` | Stops the Options app's V-blank callback in the `OptionsApp_Free` call that frees its `BgConfig`, not one frame later. The V-blank in between ran `OptionsApp_OnVBlank` on the freed config, whose NNS free-list header made a layer look scheduled: a DMA from a scribbled pointer, a data abort on the 3DS. Port-only `#ifdef SDK_PORT_3DS`; the `#else` text is unchanged. | PC repro (X → OPTIONS → QUIT) aborted at frame 35028 before, returns to the field after; two further open/close cycles pass. [3DS-Pitfalls](3DS-Pitfalls.md#the-v-blank-survives-the-app-that-owned-its-buffers). |
| pokeheartgold | 1905 | `src/wireless_status_icon.c`, `src/pokemon_summary_scene.c`, `src/field/encounter_effect_particles.c`, `src/battle/battle_system.c`, eight V-blank handlers, the spin trade app, `system_card.c`, `wireless_manager.c` | Replaces 20 raw DS address literals in compiled game code with the port's macros: `reg_GX_DISPCNT`/`reg_GXS_DB_DISPCNT`, `reg_G3_SWAP_BUFFERS`, `HW_DTCM`, `HW_LCDC_VRAM`, `HW_BG_VRAM`/`HW_OBJ_VRAM`, `HW_MAIN_MEM`. On the DS the literals are registers and memory; the port's areas are arrays, so they are unmapped. The battle's `WirelessStatusIcon_LoadChar` read is the log-10 wild-encounter crash and `BattleSystem_SetBackground`'s VRAM copies are log-11's. Port-only `#ifdef SDK_PORT_3DS`; the `#else` text is unchanged. | `tests/test_hw_addresses.py` (7 tests) scans the port-preprocessed sources (7- and 8-digit forms); the 3DS ELF's `LoadChar` loads `NitroPort_IoReg` and `BattleSystem_SetBackground` uses the port's VRAM arrays, and the pre-1905 sources are reported by the scanner. [3DS-Pitfalls](3DS-Pitfalls.md#a-raw-ds-address-is-unmapped-memory). |

Files in `patches/` (the exact names, in the order bootstrap applies them):

```text
patches/NitroSDK/
  0101-fx-C-versions-of-the-matrix-asm-functions-SDK_PORT_3.patch
  0201-sdk-C-versions-of-small-hw-lock-FIFO-asm-functions-S.patch
  0301-sdk-GCC-fixes-for-os_thread-os_valarm-and-OSi_AlarmH.patch
  0302-os-arena-stack-linker-symbol-replacements-SDK_PORT_3DS.patch
  0303-math-MATH_CountPopulation-returns-32-bits-under-the-.patch
  0401-os-C-versions-of-the-CPSR-interrupt-functions-SDK_PO.patch
  0501-cp-os-C-versions-of-context-save-restore-and-divider.patch
  0502-fx-touchpanel-hook-the-remaining-divider-and-square-root-writers.patch
  0601-sdk-port-hooks-for-IRQ-flag-clear-software-DMA-and-m.patch
  0602-os-count-the-hardware-timers-in-software-SDK_PORT_3DS.patch
  0603-fs-rom-archive-and-overlay-hooks-SDK_PORT_3DS.patch
  0604-gx-3d-command-ports-and-matrix-stack-error-SDK_PORT_3DS.patch
  0701-card-CARD_SpiWait-stub-with-no-IR-peer-SDK_PORT_3DS.patch
  0801-math-C-version-of-MATH_QSort-SDK_PORT_3DS.patch
  0901-os-C-versions-of-OS_IrqHandler-and-OS_IrqHandler_Thr.patch
patches/NitroSystem/
  0301-nns-GCC-count-leading-zeros-for-g2d-canvas-helpers-S.patch
  0302-nns-gxfifo-words-early-3d-transfers-hidden-oam-SDK_PORT_3DS.patch
patches/pokeheartgold/
  1001-types-64-bit-integers-4-byte-aligned-as-on-the-DS-SD.patch
  1101-nnsys-give-NNS_G2dInitCellTransferStateManager-its-r.patch
  1201-gcc-fixes-for-unnamed-parameter-duplicate-CRC-header.patch
  1202-gcc-match-SDK-header-signatures-to-the-port-sources.patch
  1203-gx-skip-the-local-GXFifoStat-copies-when-the-port-he.patch
  1301-mmap-DS-memory-areas-become-the-port-s-arrays-SDK_PO.patch
  1302-mmap-the-two-absolute-MI-DMA-register-addresses-foll.patch
  1303-gx-the-2D-VRAM-loaders-use-the-port-s-memory-areas-S.patch
  1304-rtc-read-the-raw-RTC-words-through-OSSystemWork-SDK.patch
  1305-sdk-shared-work-literals-go-through-OSSystemWork-SDK.patch
  1401-sound-arm7-driver-and-static-initializers-SDK_PORT_3DS.patch
  1402-sdk-headers-port-hooks-for-timers-overlays-3d-SDK_PORT_3DS.patch
  1403-sound-do-not-write-the-finished-command-tag-before-t.patch
  1407-sound-write-noise-channel-registers-at-their-real-offsets.patch
  1501-nitrocrypto-plain-C-RC4-under-SDK_PORT_3DS.patch
  1502-Drop-static-where-an-internal-header-declares-the-fu.patch
  1503-weather_manager-type-the-snow-tables-and-copy-them.patch
  1504-field_very_tall_grass-complete-the-extern-array-elem.patch
  1505-MSL-new-drop-dynamic-exception-specifications-under-.patch
  1506-ScriptRunByIndex-and-the-Frontier-GoTo-read-the-word.patch
  1507-Safari-GetFacingSafariObject-read-the-two-halfwords-.patch
  1508-Initialize-the-battle-locals-MWCC-left-unset.patch
  1601-build-re-pin-the-headers-the-port-patch-stack-touche.patch
```

This block is the early catalog. The current 62-patch pokeheartgold stack also
contains `1404`-`1406`, `1509`-`1525`, `1602`/`1603`, `1701`-`1703`,
`1710`-`1712`, `1760`-`1763`, `1801`/`1802` and `1901`-`1905`; `ls
patches/pokeheartgold/` is authoritative.

### Added in session 4

| Stack | Patch | What it does | Verified by |
| --- | --- | --- | --- |
| NitroSDK | 0302 | Arena/stack linker symbols: `OSi_*_ARENA_LO_DEFAULT`, `OSi_SYS_STACKSIZE 0`, `OSi_IRQ_STACKSIZE 2048`, launcher stack in DTCM. | `tests/sdk/os_arena` (22 checks) |
| NitroSDK | 0303 | `MATH_CountPopulation` returns `u32`, so callers with an `int` prototype see the byte zero-extended. | vendored `test_nitro` `test_math` |
| NitroSDK | 0502 | DIV/SQRT hooks in `fx_vec*`, `fx_mtx44_ortho/perspective` and `tp.c` (the writers 0501 left). | `tests/sdk/div_sqrt_writers` (69 checks) |
| NitroSDK | 0602 | Software timers: `OS_SetTimerCount/Control` hooked, `OS_GetTick/Lo` sync first. | `tests/sdk/os_timer` (23 checks) |
| NitroSDK | 0603 | ROM archive answered by libpak; overlay info/image/clear/unload delegate to the port. | `tests/sdk/fs_overlay` (41 checks) |
| NitroSDK | 0604 | `G3_*` immediates and matrix loads go through `NitroPort_GxDirectWrite`; the four GXSTAT stack-error clear sites call the port. | `tests/sdk/gx_3d` (119 checks) |
| NitroSystem | 0302 | GXFIFO words through the port, early 3D VRAM transfers at halt, unused OAM entries hidden. | `tests/sdk/nns_port` (31 checks) |
| pokeheartgold | 1203 | `mi_dma_gxcommand.c`'s local `GXFifoStat`/getter skipped when the compat header supplies them. | vendored `test_nitro`/`test_threads` link |
| pokeheartgold | 1303 | 2D palette/OAM loaders use `HW_*` areas instead of `0x05000000`/`0x07000000` literals. | vendored `test_nitro` DMA section |
| pokeheartgold | 1304 | RTC raw words read through `OS_GetSystemWork()->real_time_clock`. | vendored `test_arm7` `test_rtc` |
| pokeheartgold | 1305 | Touch/PM/MIC shared words read through `OS_GetSystemWork()`. | vendored `test_arm7` `test_touch`/`test_power` |
| pokeheartgold | 1401 | ARM7 sound driver from `sub/lib/src` + `NitroStaticInit` section attribute. | `tests/sdk/arm7_sound` (42 checks) |
| pokeheartgold | 1402 | The SDK-header halves of 0602/0603/0604 (timers, overlay IDs, G3 immediates, `static inline MI_GetUncompressedSize`, `NNSi_GfdDoVramTransfer3D`). | the same suites |
| pokeheartgold | 1403 | The finished-command tag is not written before the ARM7 knows the shared work. | vendored `test_arm7` `test_sound` (no longer crashes) |
| pokeheartgold | 1501-1508 | Phase 2 game GCC fixes: RC4 in C, static/extern, snow tables, incomplete type, C++17 exception specs, three evaluation-order sites, three uninitialised battle locals. | `tests/game/rc4`; see [phase-2.md](../progress/phase-2.md) |
| pokeheartgold | 1601 | Re-pins the alarm-partition header hashes (26 across two `profile.json` files). | DS gate gets past `tools/alarm-partition` |


## The port-side symbols the patches call

Several patches call functions the port layer defines. These exist in the vendored `3ds_port/nitro`: `NitroPort_DmaStart`, `NitroPort_Gx3dSyncResults`, `NitroPort_IrqThreadSwitch`, `NitroPort_SaveContext`, `NitroPort_LoadContext`, `NitroPort_RaiseIrq`, `NitroPort_Halt`, `CPi_PortUpdateDiv` and `CPi_PortUpdateSqrt`, and the saved CPSR `OSi_PortCpsr`. The contract is exercised by the HGSS host32 and 3DS links (0 unresolved) and by `make port-tests`; the 3D port's camera-plane drop is the one deliberate divergence from `platinum.lock` (see [docs/decisions.md](../decisions.md)).

## Coverage after session 4

All of the Platinum patch categories now have an HGSS equivalent:

- **Overlay IDs, libpak, overlay hooks (`0603` + `1402`), static initializers (`1401`).**
- **Timer hooks (`0602` + `1402`).**
- **3D command ports and GXSTAT stack error (`0604` + `1402`).**
- **ARM7 sound (`1401`, with the driver rewritten against `sub/lib/src/sound_*.c`, not rebased) and
  the NULL-tag guard (`1403`).**
- **Memory map (`1301`-`1305`)** — including the four rounds of literal-address fixes that the vendored
  tests found (2D VRAM, RTC, touch/PM/MIC; see
  [../progress/agent-notes/p1-runtime-bringup.md](../progress/agent-notes/p1-runtime-bringup.md)).
- **NitroSystem 0003/0004/0005 (`NitroSystem/0302`).**
- **Sound and game fixes from Phase 2 (`1501`-`1508`).**

### Pitfalls found by running the stack, not just applying it

- A patch can be correct and still be the wrong *layer*: the vendored tests only caught the
  literal-address reads (1303/1304/1305) once they actually executed the code.
- Two decomp build tools (`tools/arm7-alarm-partition`, `tools/alarm-partition`) pin header SHA-256s
  and stop the DS build with "physical input drift" / "production closure drift" when a patched
  header changed. `1601` re-pins them; a future patch touching a pinned header needs the same.
- Exporting a patch without `--relative=lib/NitroSDK` produces doubled paths under `git am`. See
  [Daily-Workflow](Daily-Workflow.md#authoring-a-patch).

## Adding or changing a patch

[CONTRIBUTING.md](../../CONTRIBUTING.md) says to edit inside `build/upstream/`, commit there, and export with `git format-patch`. The phase-1 record used a scratch clone so that `build/upstream/` stayed clean. Either way, the patch has to be exported from a clean commit on top of the pin, and the steps are:

1. Make the change in a commit on top of the pinned decomp.
2. Export it with `git format-patch`. Use `--relative=lib/NitroSDK` for an SDK patch, `--relative=lib/NitroSystem` for NitroSystem, and no `--relative` for the `pokeheartgold` stack. Put the file under `patches/` with the next number in its stack.
3. Keep the message: what the MWCC or DS behaviour is, and the source file and line it comes from.
4. Run `tools/bootstrap.py --clean` and the matching test in `tests/sdk/`.

The review rules for a patch are in [Contributing](Contributing.md).
