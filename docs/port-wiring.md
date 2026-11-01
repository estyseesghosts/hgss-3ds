# Wiring the vendored port layer to the HGSS SDK (Phase 1)

> **Status 2026-10-31 (session 5): wired, building and green.** `libnitro.a` and `libnitrosystem.a`
> compile with zero errors from HGSS's SDK sources plus `hgss/compat`, the ARM7 sound driver and the NNS
> shim. `make port-tests` exits 0: `pak` 120, the whole nitro sequence (`test_arm7` 676,
> `test_sound_sequence` 12, `test_render` 2071, `test_gx3d` 326+228, `test_vram_transfer` 24,
> `test_shadow` 375, …) and `test_nitrosystem` 17,122 — all 0 failed. The failure history is
> [progress/agent-notes/p1-runtime-bringup.md](progress/agent-notes/p1-runtime-bringup.md) and
> [progress/agent-notes/p2-wave-a-session5.md](progress/agent-notes/p2-wave-a-session5.md).
> The sections below are the original wiring analysis; the numbers in them are from before the wiring
> landed and are kept as history.

`3ds_port/` is vendored from pokeplatinum-3ds and was written against the ntrtwl NitroSDK
(`libraries/<lib>/src/*.c`, `include/nitro/**`). HGSS's SDK is a flat reconstruction (`lib/NitroSDK/src/<lib>/*.c`,
public headers in `lib/include/nitro/**` plus flat internal headers in `lib/NitroSDK/include`). The vendored
Makefiles therefore cannot drive it. Measure progress with `python3 tools/port_compile_survey.py` (inside the
container, after `bootstrap.py`).

## Measured state

Port sources syntax-checked against the patched HGSS headers, using a shim include directory that exposes only
`nitro*`, `nnsys*`, `cw*` from `lib/include` (HGSS's `stdarg.h`/`stddef.h`/`string.h` there shadow the system ones the
port code needs; without the shim only 11/46 compile). **Current: 46/46 compile, exit 0.**

The table below is the state before the compat layer landed (29/46) and is kept as the map of which
name needed which kind of fix; every row is now resolved:

| Cause | Files | Kind |
| --- | --- | --- |
| `nitro/spi/common/type.h`, `nitro/rtc/common/fifo.h`, `nitro/snd/common/command.h`, `nitro/dgt/dgt.h`, `nitro/ioreg.h`, `nitro/hw/ARM9/mmap_global.h` not found (HGSS names: `spi/common/pm_common.h`, none, flat `SND_command.h`, `dgt/common.h`, `hw/io_reg.h`, `hw/ARM9/mmap.h`) | port_pm, port_tp, port_rtc, port_snd, dgt, port_cp, port_ioreg | compat headers (done) |
| `card_common.h` (HGSS: `CARD_common.h`) | port_card | compat header, case- and `NITRO_H_`-aware (done) |
| `MIUnpackBitsParam`, `CTRDGBackupType`, `HW_ROM_HEADER_BUF_END`, `MI_DMA_TIMING_IMM`, `GX_FIFOINTR_COND_UNDERHALF`, `PXI_FIFO_TAG_CTRDG_PHI` undeclared (HGSS has no or different names) | mi_uncompress, ctrdg, port_boot, port_dma, port_gx, pxi | compat headers (done) |
| `struct MATHCRC8Table` redefined (HGSS has two copies of the header) | port_gx3d* | patch 1201 guard (done) |
| `MIi_CpuClear16` prototype differs from the port's definition | mi_memory | patch 1202 signature match (done) |
| `OS_IrqHandler()` called with no argument (HGSS takes an `OSIrqMask`) | port_os | patch 1201 (done) |
| `NitroPort_LcdcVram` undeclared | port_render2d | already fixed by 1301; the note was stale |
| `REG_ADDR_DMA0CNT` / `REG_ADDR_DMA0_CLR_DATA` are absolute addresses, outside the 1301 remap | mi_dma (via port_hooks) | patch 1302 (done) |

The fast survey is `python3 tools/port_compile_survey.py`. The real build is
`python3 tools/hgss_nitro.py make --target host32 --out /work/out/<name> lib -k -j8`: **0 errors**, both
archives. The suites run with the `test` goal and **all pass** (`make port-tests` exit 0). See
[progress/STATE.md](progress/STATE.md),
[progress/agent-notes/p1-runtime-bringup.md](progress/agent-notes/p1-runtime-bringup.md) and
[progress/agent-notes/p2-wave-a-session5.md](progress/agent-notes/p2-wave-a-session5.md) for the
runtime failures and their causes.

## Not just compile errors

- **PXI protocols differ.** The port's virtual ARM7 (`port_tp.c`, `port_pm.c`, `port_rtc.c`) decodes Platinum's encoding
  `(SPI_PXI_COMMAND_* << 8) | arg` and `RTC_PXI_COMMAND_*`. HGSS's ARM9 `tp.c`/`pm.c`/`rtc_api.c` send literal words
  (`TP_COMMAND_*`, raw `0x3000000`, PM commands `0x60`, `0x62`, `0x70..0x74`). Compat headers that only define the Platinum
  names would compile and then answer requests wrongly. The decoders need HGSS-specific versions (new files such as
  `port_pm_hgss.c`, not edits to the vendored ones). `docs/sdk-diff.md` §3.
- **Sound.** HGSS has no ARM7 sound source in `lib/NitroSDK`; the ARM7 driver is `sub/lib/src/sound_*.c`. The port's
  `port_snd.c` expects `N7_SND_CommandProc`, a renamed Platinum ARM7 symbol. `ARM7_SOUND_SRCS` must be rebuilt from `sub/`.
- **Source list.** ~30 Platinum SDK sources have no HGSS counterpart (`card_spi.c`, `fs_mem.c`, `wbt`, `wfs`, `utest`, `fft`, ...)
  and ~60 map only partly (the HGSS SDK contains only what HGSS uses). The vendored unit tests exercise Platinum-SDK APIs
  HGSS may not have; per the rules a test is never edited to pass, so each such test is either built against a provided
  stand-in or recorded as not applicable, with the reason, in the Phase 1 report.
- **Duplicates.** HGSS SDK files the port replaces (exclude from the list, see `phase-1.md`) and `MI_Copy32B/36B/48B/64B`,
  which the port's `mi_memory.c` lacks.
- **Generated headers.** `fx_const.h` (done in `bootstrap.py`), and the vendored `nitro/Makefile` generates it with a
  Platinum script; the shim must supply the generated header instead.
- **I/O mapping.** `HW_IOREG` is still `0x04000000` in HGSS's `lib/include`; Platinum 0005 / `port_ioreg.c` remap it.

## The shim and overrides (built)

`tools/hgss_nitro.py` implements the design below and keeps the vendored Makefiles unchanged:

- a shim tree under the git-ignored `build/hgss-sdk` (a `libraries/hgss` symlink to
  `build/upstream/lib/NitroSDK/src`, a filtered include directory with only `nitro*`/`nnsys*`/`cw*`
  from `lib/include`, and a stand-in `gen_fx_const.py` that copies the generated header);
- command-line overrides `SDK=`, `SDK_SRCS=`, `SDK_INCLUDES=`, `ARM7_SOUND_SRCS=` (empty) and an
  optional private `OUT=`;
- `hgss/compat/include` is on the include path for HGSS names of Platinum headers. It is empty today
  (only `.gitkeep`); writing it is the next task.

The shim is built (`tools/hgss_nitro.py`) and the libraries build; the first gate (link `libnitro` for
`TARGET=host32`) passed, and the vendored tests run one by one. What the wiring still exposes is not a
compile problem but a runtime one: DS-side code that addresses main/shared memory by literal address.
Patches 1303-1305 fixed the classes the tests hit (2D VRAM, RTC, touch/PM/MIC); the trade-off is that
any remaining literal is a crash for the next suite that reaches it. The current error map above is kept
as history; see [progress/STATE.md](progress/STATE.md) for live numbers.
