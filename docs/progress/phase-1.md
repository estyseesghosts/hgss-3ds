# Phase 1 progress

Status: **in progress, one hang left.** The SDK patch stack is now **39 patches**; `make test` is green,
`libnitro.a` builds with zero errors, and the vendored suites run in order: `pak-backed NitroFS` 120
checks, `test_nitro` 702, `test_fs_pak` 120, `test_threads` 68 — all with 0 failures. The single open exit criterion is
`test_arm7`, which passes backup/RTC/power/touch and then **hangs in `test_sound`**. The DS-side gate has
not been rerun since the stack grew past the last passing run.

> **Update 2026-10-31 (session 4).** The compat layer, the remaining patch categories and the vendored
> test wiring all landed. Patches added: `0302` (arena/stack symbols), `0303` (MATH_CountPopulation
> return width), `0502` (remaining DIV/SQRT writers), `0602` (software timers), `0603` (ROM archive and
> overlay hooks), `0604` (3D command ports, GXSTAT stack error), `NitroSystem/0302` (GXFIFO words, early
> 3D VRAM transfers, hidden OAM), `1401` (ARM7 sound driver, static initializers), `1402` (the
> corresponding SDK header halves), `1403` (sound NULL-tag guard), `1203` (GXFifoStat guard), `1303`
> (2D VRAM loader literals), `1304` (RTC shared-work literal), `1305` (touch/PM/MIC literals), `1501-1508`
> (Phase 2 game GCC fixes) and `1601` (alarm-partition header pins). The full runtime debugging log,
> with each crash and its root cause, is [agent-notes/p1-runtime-bringup.md](agent-notes/p1-runtime-bringup.md).
> Phase 2 has started; see [phase-2.md](phase-2.md).
>
> Why the port tests used to "compile and then misbehave": most failures were **DS code addressing
> main/shared memory by literal address** (RTC/touch/PM/MIC/VRAM). Patches 1301/1302 moved those regions
> into the port's arrays, so the literals pointed at unmapped memory. Grep for `0x02…`/`0x05…`/`0x07…`
> pointer literals before assuming a logic bug.


## How to resume (fresh context)

- Container: `cd /Users/terezi/hgss/pokeheartgold-3ds && devcontainer up --workspace-folder .` then
  `devcontainer exec --workspace-folder . bash -c '...'`. Repo inside = `/workspaces/pokeheartgold-3ds`.
- Rebuild the patched tree from scratch: `python3 tools/bootstrap.py --clean` (applies `patches/**`
  in numeric order per stack, generates `fx_const.h`). Expected: **39 patches**.
- Measure: `python3 tools/sdk_compile_survey.py -j 10 --m32` (gcc-13: 270/284) and with
  `--gcc gcc-10` (269/284). Port: `python3 tools/port_compile_survey.py` (46/46) and
  `python3 tools/hgss_nitro.py make --target host32 --out /work/out/x lib -k -j8` (0 errors).
- Tests: `make test` (182 Python tests + 18 SDK suites; exit 0). Vendored:
  `python3 tools/hgss_nitro.py make --target host32 --out /work/out/x test` (all three subprojects)
  or `make port-tests`. One suite: `bash tests/sdk/<name>/run.sh build/upstream`.
- DS-side gate (hard rule 3): move the old reference tree aside and clone `build/upstream` to
  **`/work/pokeheartgold`** (the MSL partition tools pin the absolute source path inside the object),
  `tools/install-ds-toolchain.sh /work/pokeheartgold`, **`make -C tools/mwasmarm_patcher`** (its binary
  is git-ignored; the gate fails without it), `make patch_mwasmarm MWCCVER=2.0/sp2p3`,
  `tools/matching-build.sh /work/pokeheartgold HEARTGOLD` (~18 min), expect ROM SHA1 `4fcded0e…`.
  **Not passed at 39 patches.** Five build tools pin inputs (`tools/arm7-alarm-partition`,
  `tools/alarm-partition`, `tools/msl-new-partition`, `tools/msl-d1-partition`,
  `tools/dwc-iterator-advance`); patches `1601`/`1602` re-pin them, and a new patch that touches a
  pinned header or the `to91_msl_*_new.cpp` sources needs a new pin.
- Patch authoring: clone `build/upstream` to `/work/wt/<name>`, edit, commit, then
  `git format-patch -1 --relative=lib/NitroSDK --start-number N -o /workspaces/pokeheartgold-3ds/patches/NitroSDK`
  (use `--relative=lib/NitroSystem` for NitroSystem; patches touching other trees go to
  `patches/pokeheartgold` with no `--relative`).
  Pattern: `#ifdef SDK_PORT_3DS` C version `#else` original `#endif`; prove DS text unchanged with
  `gcc -E -P`.
- Commit trailer to use: `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` (agents wrote wrong
  model names; normalize with sed before committing patches).

## Done (committed in the port repo)

From-scratch `bootstrap.py --clean` applies **15 patches** (NitroSDK 0101 0201 0301 0401 0501 0601 0701 0801 0901,
NitroSystem 0301, pokeheartgold 1001 1101 1201 1301 1302). Survey: **270/284** files parse (gcc-13, 32-bit,
`-DSDK_PORT_3DS`; 269/284 with the port's gcc-10); baseline was 243. Every patch keeps the original MWCC text
(`#ifdef SDK_PORT_3DS` ... `#else` original `#endif`); each was checked with `gcc -E -P` (without the define,
output identical to unpatched) and a host test where one exists.

| Patch | What | Verification |
| --- | --- | --- |
| NitroSDK 0101 | 14 fx matrix asm functions | 428 cases; asm interpreted independently (3011 checks) |
| NitroSDK 0201 | RtcWaitBusy, MicWaitBusy, OSi_WaitVCount0, OsCountZeroBits, OS_Halt, lock ids, GX FIFO sends, NOP clear | 54 cases |
| NitroSDK 0301 + NNS 0301 | os_thread linkage, `__declspec(noinline)`, OSi_AlarmHandler (the copy in the ROM), 3 clz helpers | host test |
| NitroSDK 0401 | os_system.c CPSR functions on `OSi_PortCpsr` + raise-pending-IRQ-on-unmask | 29 cases |
| NitroSDK 0501 | CP_SaveContext/CPi_RestoreContext, OS_InitContext/Save/LoadContext, divider/sqrt hooks in fx_cp.c | 142 cases; port's HandOver already saves CP per thread, so Save/LoadContext skip it |
| NitroSDK 0601 | IF write-1-to-clear emulation, software-DMA start hooks, matrix-result sync | 80 cases |
| NitroSDK 0701 | `CARD_SpiWait*` stub (no IR peer, status 0xAA) | 19 cases |
| NitroSDK 0801 | MATH_QSort C version | 11,202 cases; **465,985 comparator calls identical to the ORIGINAL ROM machine code** run in unicorn (`tools/qsort_rom_oracle.py`) |
| NitroSDK 0901 | OS_IrqHandler, OS_IrqHandler_ThreadSwitch via `NitroPort_IrqThreadSwitch` | 10 cases |
| pokeheartgold 1001 | `u64`/`s64` aligned(4) | layout probe, below |
| pokeheartgold 1101 | `NNS_G2dInitCellTransferStateManager` gets its return type (devkitARM 16) | cpp identity; no dedicated test |
| pokeheartgold 1201 | GCC-10: unnamed parameter, duplicate CRC headers, `OS_IrqHandler(void)`, PXI PHI tag | `tests/sdk/gcc_fixes` (4 groups), `irq_handler` (10 checks) |
| pokeheartgold 1301 | `HW_*` memory areas become the port's arrays | `tests/sdk/memory_map` (32 checks then) |
| pokeheartgold 1302 | Absolute MI DMA registers follow `HW_REG_BASE` | `memory_map` (35 checks), port_hooks DMA test (was a segfault) |

**Save layout (rule 4): verified.** `tools/layout_probe/run_probe.py` compares 804 `sizeof`/`offsetof` values covering all 45
save-chunk root types and everything reachable from them, MWCC 2.0/sp2p2 (under Wine) vs GCC. Without 1001, devkitARM GCC and
`-m32 -malign-double` differ in 55 values (POKEWALKER, PCStorage, BattleRecord: all hold a `u64`); with 1001, **0 differences**
for host `-m32`, `-m32 -malign-double` and devkitARM. `tests/layout_probe/run.sh` fails on any difference. Note the port's
`host32` flag `-malign-double` is only safe because of 1001.

**DS-side gate (rule 3).** PASSED with 0101-0901 + NNS 0301 (10 patches): HeartGold SHA1 `4fcded0e…`, 0 failed checks,
18m43s (`/work/gate-hg.log`). **Not rerun** with `1001`, `1101`, `1201`, `1301` or `1302`. Their DS branches are the
original text (checked with `gcc -E -P`), but the byte-identical ROM check is the strong evidence and is still owed.
Run it before closing Phase 1.

## Remaining SDK parse failures (14, all port-replaced; exclude from the HGSS source list, do not patch)

`crt0.c`, `dgt_hash2_arm4_small.c`, `mi/mi_memory.c`, `mi/mi_swap.c`, `mi/mi_uncompress.c`, `os/os_cache.c`,
`os/os_exception.c`, `os/os_protectionRegion.c`, `os/os_protectionUnit.c`, `os/os_reset_boot.c`,
`os/os_reset_clear32.c`, `os/os_reset_system.c`, `os/os_tcm.c`, `os/os_alarm.c` (not in the ROM).
Caveat: `mi_memory.c` also defines `MI_Copy32B/36B/48B/64B` that the port's `mi_memory.c` lacks; they need a home.

## Not started / open

- **Compat headers (the next concrete piece).** `3ds_port` includes headers HGSS names differently or lacks:
  `nitro/rtc/common/fifo.h`, `nitro/spi/common/type.h`, `nitro/snd/common/command.h`, `nitro/dgt/dgt.h`,
  `nitro/ioreg.h`, `nitro/hw/ARM9/mmap_global.h`, `card_common.h`, `nitro/cp/{divider,sqrt}.h`. They belong in
  `hgss/compat/include/` (tracked, on the shim include path), not as edits to the vendored files. That directory
  currently has only `.gitkeep`: the W2 slice was designed and measured but its headers never landed.
- **Remaining compile errors (44 in 9 objects).** `MIUnpackBitsParam`, `MIReadStreamCallbacks`, `MIi_Cpu*`
  signatures, `MI_DMA_TIMING_*`, `OS_IE_*`, `GX_FIFOINTR_COND_*`, `CTRDGBackupType`/`CTRDG_TASK_FUNC`,
  `PXIFifoEmtpyCallback` and the PXI prototypes, `HW_ROM_HEADER_BUF_END`, and NVRAM `favoriteColor` vs
  HGSS's `favouriteColour`. Grouped list in [STATE.md](STATE.md).
- **Vendored port unit tests** (the Phase 1 exit criterion): wire `3ds_port/nitro`, then `nitrosystem` and
  `pak`, to the HGSS source list with `hgss_nitro.py`, and make `make port-tests` real.
- **PXI command encodings** for RTC/TP/PM: HGSS's ARM9 sends literal words; the port's virtual ARM7 decodes
  Platinum's named constants. Needs HGSS-specific decoders in new files.
- More DIV/SQRT writers needing the 0501 hook: `fx_vec*.c`, `fx_mtx44_ortho.c`, `fx_mtx44_perspective.c`, `tp.c`.
- Header-level patches (lib/include): timer hooks (Platinum 0013), overlay ID symbols as bytes (0010). The
  memory map (0005) is done as 1301/1302. `g2d_CellTransferManager.h:24` is fixed by 1101. The generated
  `fx_const.h` include guard still contains `-` (warning only).
- Overlay patches (Platinum 0014 hooks, 0027 static initializers, 0011 libpak), 3D command forwarding (0024/0025), G3 FIFO
  sends through the port's translator (0201 uses plain stores).
- ARM7 sound (Platinum 0018-0023): rewrite against `sub/lib/src/sound_*.c`.
- NitroSystem equivalents of Platinum 0003 (GXFIFO stores in g3d_gecom.c, g3d_sbc.c), 0004 (VRAM transfer at OS_Halt), 0005 (OAM).
- `port.yml` never run; `docs/sdk-diff.md` still partial; final report and checklist.

## Decisions / facts worth keeping

- Plain stores for GX FIFO sends in 0201; routing through the port's 3D translator is a later `g3imm` patch with the
  caller-side change (Platinum 0024).
- OS_ReleaseLockID follows the HGSS ARM byte-shift semantics, not Platinum 0015's guard.
- 37 vs 39 asm files: counts differ by method; the survey (parse failures) is the measure that counts.
- 1301 makes `HW_*` expressions (`(u32)array + offset`), so the 64-bit `host` target is not supported by this design;
  `host32` and the 3DS are. `REG_ADDR_DMA0CNT`/`CLR_DATA` were absolute literals outside the remap; 1302 fixes them.
- The SDK host suites now link `tests/sdk/port_memory.c` (the port's eleven arrays). `card_spi_wait` guards the whole
  I/O array with `mprotect` rather than mapping `0x04000000`, which is no longer where the registers live.

## Exit checklist (Phase 1)

| Item | Result |
| --- | --- |
| `bootstrap.py` applies all patches cleanly from scratch | **PASS**: 39 patches, `git am`, fails hard on a bad patch (unit-tested); also re-run in a fresh clone in the CI image |
| `docs/sdk-diff.md` complete | **PARTIAL**: PXI tags, OSSystemWork/SNDSharedWork field lists, G3 opcodes, card backup, `CARD_SpiWait*`, asm inventory done; RTC/TP/PM/MIC payload formats, IRQ/thread struct layouts, NitroSystem detail not compared (the port-wiring review found the PXI encodings differ) |
| 27 + 5 patches rebased, each with a message | **NOT AS SPECIFIED, by evidence**: none of Platinum's patches applies to HGSS's different SDK tree (`git am` impossible); the triage found 4 trivial / 16 moderate / 3 rewrite / 4 N/A. Done as fresh HGSS patches by function: asm-to-C 0101, 0201, 0301, 0401, 0501, 0801, 0901 (Platinum 0003, 0006, 0008, 0012 equivalents), hooks 0601 (0009, 0017, 0026) and 0501 (0002), `u64` 1001 (0016), NNS 0301 (0001 partly), memory map 1301/1302 (0005), timers 0602 (0013), overlay + libpak 0603 (0010/0011/0014) with the header halves in 1402, 3D 0604 (0024/0025), static initializers 1401 (0027), ARM7 sound 1401/1403 (0018-0023 rewritten against `sub/`), NitroSystem 0302 (0003/0004/0005). 39 patches total. |
| HGSS-only SDK patches added | **PASS** for `CARD_SpiWait*` (0701), the GCC-10 fixes (1201) and the absolute MI DMA registers (1302). Mic PXI not examined. |
| Layout probe: zero differences, or each explained and fixed | **PASS**: 804 values, 45 save roots; 55 differences found and fixed by patch 1001; 0 remain for host `-m32`, `-m32 -malign-double`, devkitARM |
| Port unit tests green on `host` (and `host32`) | **PARTIAL — one hang**: the vendored suites build and run against HGSS. `pak` 120 checks, `test_nitro` 702, `test_fs_pak` 120, `test_threads` 68, all 0 failed; `test_arm7` passes backup/RTC/power/touch and hangs in `test_sound`; `test_nitrosystem` not reached. `make port-tests` is real and fails only on that hang. |
| `port.yml` passing | **NOT VERIFIED on GitHub** (no remote). A dry run of its exact steps (fresh clone, same image, same `docker run`) passes locally |
| No game code compiled yet | **PASS**: only lib/ SDK and NitroSystem sources were compiled with GCC; the layout probe includes game *headers* (and 7 save-struct `.c` files, syntax and DWARF only). No game object was linked. |

## DS-side matching (rule 3)

**PASSED with the SDK stack as it stood.** The tree with NitroSDK 0101-0901 and NNS 0301 (10 patches) builds the HeartGold
ROM with SHA1 `4fcded0e2713dc03929845de631d0932ea2b5a37`, 0 failed checks, 18m43s (`/work/gate-hg.log`), so those patches do
not change the DS build. **`1001`, `1101`, `1201`, `1301` and `1302` are not in that run.** Their text is inside
`#ifdef SDK_PORT_3DS` and `gcc -E -P` without the macro is identical to the unpatched headers (checked), but a full rebuild
including them has not been run. Run it before closing Phase 1. SoulSilver was not re-run against the patched tree.

## Deviations from the plan and why

- Platinum's patches are not rebased with `git am` (the SDK trees differ) but redone by function; the patch series is HGSS's
  own and numbered by area, not 1:1.
- Agents' commit trailers named the wrong model; normalized to the required `Co-Authored-By` line before committing.
- `make test` no longer includes the always-failing `port-tests` placeholder; it is its own target so CI does not pretend.
- The `cp_context` test originally expected GCC's 8-byte-aligned struct sizes; corrected to the DS layout the original asm
  implies (28/100 bytes), with an overrun guard (commit message explains; it is not a weakened assertion).

## Open questions for you

1. Is it acceptable to continue Phase 1 on the port wiring (compat headers + HGSS PXI decoders + shim + running the vendored
   tests), or should Phase 1 be closed here with the exit criterion "port unit tests pass" explicitly deferred?
2. `lib/include/nnsys/g2d/g2d_CellTransferManager.h:24` (missing return type) blocks devkitARM 16 builds of NitroSystem; fix
   as a pokeheartgold patch (GCC-only fix) or pin an older devkitARM?
3. Should the port-side fixes (`OS_IrqHandler(0)`, `card_common.h` case, `MIi_CpuClear16` signature) go to the Platinum
   maintainer as upstream PRs, per the outreach draft, or stay as HGSS-only new files?
