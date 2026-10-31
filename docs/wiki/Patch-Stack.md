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
| Hardware layer, vendored byte-identical | `GabeSud0/pokeplatinum-3ds` | `359fbec80a5c0664d4fa72caaae87465b640f9d6` | `platinum.lock` |

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
- **The `pokeheartgold` stack has `1001` and `1101`.** The phase-1 log covers `1001` only. The record does not say why `1101` sits in the `11xx` series.

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

Only one patch edits an existing line. In `0401` the original `#ifdef SDK_ARM9` becomes `#elif defined(SDK_ARM9)` under the new `#ifdef SDK_PORT_3DS`. Patches `0801` and `0901` wrap an existing forward declaration in `#ifndef SDK_PORT_3DS` without changing its text. Across all 12 patches, the only removed line is the one in `0401`.

The macro is `SDK_PORT_3DS` and not Platinum's `SDK_CW` or `__MWERKS__`. Patch `0101` gives the reason: the port's rule is one guard for port-only behaviour, so the MWCC build never takes a port branch ([patches/NitroSDK/0101](../../patches/NitroSDK/) message).

## Why the DS build is unchanged

There are three pieces of evidence, and they are not the same strength.

1. **Preprocessor identity (per patch, by hand).** The patch messages say that the output of `gcc -E -P` for each touched file is identical with and without the patch when `SDK_PORT_3DS` is not defined. The messages of `0201`, `0401`, `0501`, `0801` and `1101` state this check. The phase-1 record says the same for `1001`. There is no automated check in the repo yet. The plan's patch-guard job is not in `port.yml` ([gameplan.md](../../gameplan.md), "CI workflows").
2. **The byte-identical ROM gate.** This is the strong one. The phase-1 record says the tree with the NitroSDK patches `0101` to `0901` and NitroSystem `0301` built the HeartGold US ROM, and its SHA1 was `4fcded0e2713dc03929845de631d0932ea2b5a37` with 0 failed checks ([docs/progress/phase-1.md](../progress/phase-1.md), "DS-side matching"). That run took 18 minutes 43 seconds. Patch `1001` was not in that run. Neither was `1101`, which was added later. The record says a full rebuild with `1001` is still to be run.
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
| pokeheartgold | 1101 | `lib/include/nnsys/g2d/g2d_CellTransferManager.h` | Adds the return type `int` to one prototype, on the port only. The patch message says GCC 14 and later reject a declaration with implicit `int`, and that the definition already returns `int`. | No test. The message says `gcc -E -P` is identical without the macro. The phase-1 record does not list this patch. Its open question about this header is answered by the patch, but the record is not updated. |

Files in `patches/` (the exact names, in the order bootstrap applies them):

```text
patches/NitroSDK/
  0101-fx-C-versions-of-the-matrix-asm-functions-SDK_PORT_3.patch
  0201-sdk-C-versions-of-small-hw-lock-FIFO-asm-functions-S.patch
  0301-sdk-GCC-fixes-for-os_thread-os_valarm-and-OSi_AlarmH.patch
  0401-os-C-versions-of-the-CPSR-interrupt-functions-SDK_PO.patch
  0501-cp-os-C-versions-of-context-save-restore-and-divider.patch
  0601-sdk-port-hooks-for-IRQ-flag-clear-software-DMA-and-m.patch
  0701-card-CARD_SpiWait-stub-with-no-IR-peer-SDK_PORT_3DS.patch
  0801-math-C-version-of-MATH_QSort-SDK_PORT_3DS.patch
  0901-os-C-versions-of-OS_IrqHandler-and-OS_IrqHandler_Thr.patch
patches/NitroSystem/
  0301-nns-GCC-count-leading-zeros-for-g2d-canvas-helpers-S.patch
patches/pokeheartgold/
  1001-types-64-bit-integers-4-byte-aligned-as-on-the-DS-SD.patch
  1101-nnsys-give-NNS_G2dInitCellTransferStateManager-its-r.patch
```

## The port-side symbols the patches call

Several patches call functions the port layer defines. These exist in the vendored `3ds_port/nitro`: `NitroPort_DmaStart`, `NitroPort_Gx3dSyncResults`, `NitroPort_IrqThreadSwitch`, `NitroPort_SaveContext`, `NitroPort_LoadContext`, `NitroPort_RaiseIrq`, `NitroPort_Halt`, `CPi_PortUpdateDiv` and `CPi_PortUpdateSqrt`, and the saved CPSR `OSi_PortCpsr`. The check was a text search of the vendored tree. No HGSS link has been made, so the contract is checked by reading, not by a build.

## Known gaps in the stack

The phase-1 record lists these Platinum patches as not done for HGSS:

- `0005` memory map and I/O remap.
- `0010`, `0011`, `0014`, `0027` overlay IDs, libpak, overlay hooks and static initializers.
- `0013` timer hooks.
- `0024`, `0025` 3D command ports.
- `0018` to `0023` ARM7 sound, to be rewritten against `sub/`, not rebased.

The NitroSystem equivalents of Platinum 0003, 0004 and 0005 are also not done. The phase-1 record lists all 14 SDK source files that still fail to parse as port-replaced: they are excluded from the HGSS source list and not patched ([SDK-Differences](SDK-Differences.md)).

## Adding or changing a patch

[CONTRIBUTING.md](../../CONTRIBUTING.md) says to edit inside `build/upstream/`, commit there, and export with `git format-patch`. The phase-1 record used a scratch clone so that `build/upstream/` stayed clean. Either way, the patch has to be exported from a clean commit on top of the pin, and the steps are:

1. Make the change in a commit on top of the pinned decomp.
2. Export it with `git format-patch`. Use `--relative=lib/NitroSDK` for an SDK patch, `--relative=lib/NitroSystem` for NitroSystem, and no `--relative` for the `pokeheartgold` stack. Put the file under `patches/` with the next number in its stack.
3. Keep the message: what the MWCC or DS behaviour is, and the source file and line it comes from.
4. Run `tools/bootstrap.py --clean` and the matching test in `tests/sdk/`.

The review rules for a patch are in [Contributing](Contributing.md).
