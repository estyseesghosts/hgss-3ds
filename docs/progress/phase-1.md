# Phase 1 progress (INTERIM, written at a usage-limit checkpoint)

Status: **in progress.** Phase 0 is complete (see `phase-0.md`). Nothing below is a final report.

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
