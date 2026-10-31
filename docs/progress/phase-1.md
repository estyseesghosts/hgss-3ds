# Phase 1 progress

Status: **partially complete.** The SDK patch stack and the save-layout proof are done and verified; wiring the vendored
port layer to the HGSS SDK and getting its unit tests green is **not done** (see `docs/port-wiring.md`). Phase 2 not started.

## How to resume (fresh context)

- Container: `cd /Users/terezi/hgss/pokeheartgold-3ds && devcontainer up --workspace-folder .` then
  `devcontainer exec --workspace-folder . bash -c '...'`. Repo inside = `/workspaces/pokeheartgold-3ds`.
- Rebuild the patched tree from scratch: `python3 tools/bootstrap.py --clean` (applies `patches/**`
  in numeric order per stack, generates `fx_const.h`).
- Measure: `python3 tools/sdk_compile_survey.py -j 10 --m32` (and without `--m32`). Baseline before any
  patch: 243/284 files parse at 32-bit. Latest full run (all 11 patches): **270/284**.
- Tests: `bash tests/sdk/<name>/run.sh build/upstream` for fx, small, nns_clz, os_system, card_spi_wait, cp_context, irq_handler, port_hooks, math_qsort; layout: `PY=/work/venv/bin/python bash tests/layout_probe/run.sh` (venv with `unicorn pyelftools`).
- DS-side gate (hard rule 3): `tools/.gate.sh`-style run = clone `build/upstream` to `/work/patched`,
  `tools/install-ds-toolchain.sh /work/patched`, `make patch_mwasmarm MWCCVER=2.0/sp2p3`,
  `tools/matching-build.sh /work/patched HEARTGOLD` (~18 min), expect ROM SHA1 `4fcded0e…`.
- Patch authoring: clone `build/upstream` to `/work/wt/<name>`, edit, commit, then
  `git format-patch -1 --relative=lib/NitroSDK --start-number N -o /workspaces/pokeheartgold-3ds/patches/NitroSDK`
  (use `--relative=lib/NitroSystem` for NitroSystem; patches touching other trees go to `patches/pokeheartgold`).
  Pattern: `#ifdef SDK_PORT_3DS` C version `#else` original `#endif`; prove DS text unchanged with `gcc -E -P`.
- Commit trailer to use: `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` (agents wrote wrong
  model names; normalize with sed before committing patches).

## Done (committed in the port repo)

From-scratch `bootstrap.py --clean` applies **11 patches** (NitroSDK 0101 0201 0301 0401 0501 0601 0701 0801 0901,
NitroSystem 0301, pokeheartgold 1001). Survey: **270/284** files parse (32-bit GCC, `-DSDK_PORT_3DS`); baseline was 243.
Every patch keeps the original MWCC text (`#ifdef SDK_PORT_3DS` ... `#else` original `#endif`); each was checked with
`gcc -E -P` (without the define, output identical to unpatched) and a host test.

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

**Save layout (rule 4): verified.** `tools/layout_probe/run_probe.py` compares 804 `sizeof`/`offsetof` values covering all 45
save-chunk root types and everything reachable from them, MWCC 2.0/sp2p2 (under Wine) vs GCC. Without 1001, devkitARM GCC and
`-m32 -malign-double` differ in 55 values (POKEWALKER, PCStorage, BattleRecord: all hold a `u64`); with 1001, **0 differences**
for host `-m32`, `-m32 -malign-double` and devkitARM. `tests/layout_probe/run.sh` fails on any difference. Note the port's
`host32` flag `-malign-double` is only safe because of 1001.

**DS-side gate (rule 3).** PASSED for 0101+0201+0301(+NNS 0301): HeartGold SHA1 `4fcded0e…`, 0 failed checks. A second run
over the clone with ALL patches up to 0901 was started (`/work/gate-hg.log`); it predates 1001. Re-run once at the end.

## Remaining SDK parse failures (14, all port-replaced; exclude from the HGSS source list, do not patch)

`crt0.c`, `dgt_hash2_arm4_small.c`, `mi/mi_memory.c`, `mi/mi_swap.c`, `mi/mi_uncompress.c`, `os/os_cache.c`,
`os/os_exception.c`, `os/os_protectionRegion.c`, `os/os_protectionUnit.c`, `os/os_reset_boot.c`,
`os/os_reset_clear32.c`, `os/os_reset_system.c`, `os/os_tcm.c`, `os/os_alarm.c` (not in the ROM).
Caveat: `mi_memory.c` also defines `MI_Copy32B/36B/48B/64B` that the port's `mi_memory.c` lacks; they need a home.

## Not started / open

- **Build wiring for the vendored `3ds_port/nitro` tests** (the largest remaining item): the vendored Makefiles assume
  Platinum's `libraries/<lib>/src` layout and symbols. Needs an HGSS source list (shim dir under `build/` + command-line
  overrides, per the build-wiring explorer: `SDK=`, `SDK_SRCS=`, `SDK_INCLUDES=`, empty `ARM7_SOUND_SRCS=`), headers the port
  includes that HGSS lacks (`nitro/rtc/common/fifo.h`, `nitro/spi/common/type.h`, `nitro/cp/divider.h`, `sqrt.h`,
  `nitro/ioreg.h`, `nitro/hw/ARM9/mmap_global.h`, ...), PXI command encodings for RTC/TP/PM (HGSS ARM9 sends literal words
  the port's virtual ARM7 does not decode), `port_card.c` includes `card_common.h` (HGSS: `CARD_common.h`), and `OS_IrqHandler()`
  must be called with an argument.
- More DIV/SQRT writers needing the 0501 hook: `fx_vec*.c`, `fx_mtx44_ortho.c`, `fx_mtx44_perspective.c`, `tp.c`.
- Header-level patches (lib/include): timer hooks (Platinum 0013), overlay ID symbols as bytes (0010), memory map / I/O
  remap (0005; `HW_IOREG` is still 0x04000000), `g2d_CellTransferManager.h:24` implicit int (blocks devkitARM 16), the
  generated `fx_const.h` include guard contains `-`.
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

## Exit checklist (Phase 1)

| Item | Result |
| --- | --- |
| `bootstrap.py` applies all patches cleanly from scratch | **PASS**: 11 patches, `git am`, fails hard on a bad patch (unit-tested); also re-run in a fresh clone in the CI image |
| `docs/sdk-diff.md` complete | **PARTIAL**: PXI tags, OSSystemWork/SNDSharedWork field lists, G3 opcodes, card backup, `CARD_SpiWait*`, asm inventory done; RTC/TP/PM/MIC payload formats, IRQ/thread struct layouts, NitroSystem detail not compared (the port-wiring review found the PXI encodings differ) |
| 27 + 5 patches rebased, each with a message | **NOT AS SPECIFIED, by evidence**: none of Platinum's patches applies to HGSS's different SDK tree (`git am` impossible); the triage found 4 trivial / 16 moderate / 3 rewrite / 4 N/A. Done as fresh HGSS patches by function: asm-to-C 0101, 0201, 0301, 0401, 0501, 0801, 0901 (Platinum 0003, 0006, 0008, 0012 equivalents), hooks 0601 (0009, 0017, 0026) and 0501 (0002), `u64` 1001 (0016), NNS 0301 (0001 partly). **Not done:** Platinum 0005 memory map, 0010/0014/0027/0011 overlay + libpak, 0013 timers, 0024/0025 3D command ports, 0018-0023 ARM7 sound (rewrite against `sub/`); NitroSystem 0003/0004/0005 equivalents. |
| HGSS-only SDK patches added | **PASS** for `CARD_SpiWait*` (0701). Mic PXI not examined. |
| Layout probe: zero differences, or each explained and fixed | **PASS**: 804 values, 45 save roots; 55 differences found and fixed by patch 1001; 0 remain for host `-m32`, `-m32 -malign-double`, devkitARM |
| Port unit tests green on `host` (and `host32`) | **FAIL / NOT DONE**: the vendored `3ds_port` suites are not built against HGSS. 26/46 port sources compile; the blockers are documented. What *is* green: 9 SDK host suites in `tests/sdk` + 19 tool tests under `make test` (these are new tests of the patches, not the vendored tests) |
| `port.yml` passing | **NOT VERIFIED on GitHub** (no remote). A dry run of its exact steps (fresh clone, same image, same `docker run`) passes locally |
| No game code compiled yet | **PASS**: only lib/ SDK and NitroSystem sources were compiled with GCC; the layout probe includes game *headers* (and 7 save-struct `.c` files, syntax and DWARF only). No game object was linked. |

## DS-side matching (rule 3)

The tree with NitroSDK 0101+0201+0301 and NNS 0301 builds the HeartGold ROM `4fcded0e…` (0 failed checks). A rebuild with
patches through 0901 is running (`/work/gate-hg.log`); result recorded below when it finished. Patch 1001 is not in
that run: its text is inside `#ifdef SDK_PORT_3DS` and `gcc -E -P` of `types.h` without the macro is identical to the
unpatched header (checked), but a full rebuild including it has not been run.

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
