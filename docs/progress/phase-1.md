# Phase 1 progress (INTERIM, written at a usage-limit checkpoint)

Status: **in progress.** Phase 0 is complete (see `phase-0.md`). Nothing below is a final report.

## How to resume (fresh context)

- Container: `cd /Users/terezi/hgss/pokeheartgold-3ds && devcontainer up --workspace-folder .` then
  `devcontainer exec --workspace-folder . bash -c '...'`. Repo inside = `/workspaces/pokeheartgold-3ds`.
- Rebuild the patched tree from scratch: `python3 tools/bootstrap.py --clean` (applies `patches/**`
  in numeric order per stack, generates `fx_const.h`).
- Measure: `python3 tools/sdk_compile_survey.py -j 10 --m32` (and without `--m32`). Baseline before any
  patch: 243/284 files parse at 32-bit. Latest full run (patches 0101-0301): **265/284**.
- Tests: `bash tests/sdk/<name>/run.sh build/upstream` for fx, small, nns_clz, os_system, card_spi_wait.
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

| Patch | What | Verified |
| --- | --- | --- |
| NitroSDK 0101 | 14 fx matrix asm functions -> C | 428 host cases; asm interpreted independently (3011 checks); cpp identity |
| NitroSDK 0201 | RtcWaitBusy, MicWaitBusy, OSi_WaitVCount0, OsCountZeroBits, OS_Halt, lock ids, GX FIFO sends, NOP clear | 54 host cases; cpp identity |
| NitroSDK 0301 | os_thread linkage, `__declspec(noinline)`, OSi_AlarmHandler in os_alarm_irq_handler.c (the one in the ROM) | compile; cpp identity |
| NitroSystem 0301 | 3 `CountLeadingZeros` helpers | host test |
| NitroSDK 0401 | os_system.c CPSR functions on `OSi_PortCpsr` + raise-pending-IRQ-on-unmask | 29 host cases; cpp identity |

**DS-side gate: PASSED** for the tree with 0101+0201+0301(+NNS 0301): HeartGold ROM SHA1 `4fcded0e…`, 0 failed
checks, 18m20s. Must be re-run once more at the end of Phase 1 with all patches (0401 and later were not in it).

## Written by a subagent, NOT yet reviewed or committed

- NitroSDK 0701 (`CARD_SpiWait*` stub, no IR peer: status 0xAA, zero-length reads) + `tests/sdk/card_spi_wait/`
  (19 checks, per its note). Agent note: `docs/progress/agent-notes/s6.md`. Lead to review the patch text,
  normalize the trailer, commit.

## Started but lost (rate limit hit; clones are clean, nothing to recover)

- S4: `CP_context.c`, `os_context.c`, divider/sqrt hooks in `fx_cp.c` (brief: Platinum 0006/0002).
- S5: IRQ-flag clear (`os_interrupt.c`, `os_tick.c`), software DMA hooks (`mi_dma.c`), matrix result sync (`g3x.c`).
- S8: `MATH_QSort` C version (needs an exact comparator-call-order match; plan was a scratch ARM interpreter).
Re-brief these from scratch; their original briefs are in the session history and follow the pattern above.

## Remaining SDK parse failures (survey, 32-bit; 19 before 0401)

Port-replaced, to be EXCLUDED from the HGSS source list rather than patched (14): `crt0.c`,
`dgt_hash2_arm4_small.c`, `mi/mi_memory.c`, `mi/mi_swap.c`, `mi/mi_uncompress.c`, `os/os_cache.c`,
`os/os_exception.c`, `os/os_protectionRegion.c`, `os/os_protectionUnit.c`, `os/os_reset_boot.c`,
`os/os_reset_clear32.c`, `os/os_reset_system.c`, `os/os_tcm.c`, `os/os_alarm.c` (not in the ROM).
Need patches (4 left after 0401): `cp/CP_context.c`, `os/os_context.c`, `os/os_irqHandler.c`
(thread switch at end of IRQ: a redesign, Platinum 0012), `math/math_qsort.c`.

## Not started

- HGSS source list / build wiring for `3ds_port/nitro` (vendored Makefiles assume Platinum's tree; see
  `docs/sdk-diff.md` §1 and the build-wiring explorer findings: missing `fx_const` generation, headers
  `nitro/rtc/common/fifo.h`, `nitro/spi/common/type.h`, `nitro/cp/divider.h`..., PXI command encodings differ).
- Header-level patches (lib/include): u64 4-byte alignment (Platinum 0016), timer hooks (0013), overlay ID bytes
  (0010), memory map (0005, 0001 types).
- Overlay patches (0014 overlay hooks, 0027 static initializers, 0011 libpak), 3D command forwarding (0024/0025/26).
- ARM7 sound (Platinum 0018-0023): HGSS ARM7 driver is in `sub/lib/src/sound_*.c`; rewrite, not rebase.
- NitroSystem patches 2-5 equivalents (GXFIFO stores in g3d_gecom.c/g3d_sbc.c, VRAM transfer at OS_Halt, OAM hide).
- Layout probe run (generator `tools/layout_probe/gen_probe.py` written, untested).
- Unit tests of the vendored port against HGSS libs (blocked on build wiring), `port.yml` (never run).
- `docs/sdk-diff.md` still partial (RTC/TP/PM/MIC payloads, IRQ/thread structs).

## Decisions / facts worth keeping

- Plain stores for GX FIFO sends in 0201; routing through the port's 3D translator is a later `g3imm` patch with the
  caller-side change (Platinum 0024).
- OS_ReleaseLockID follows the HGSS ARM byte-shift semantics, not Platinum 0015's guard.
- 37 vs 39 asm files: counts differ by method; the survey (parse failures) is the measure that counts.
