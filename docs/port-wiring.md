# Wiring the vendored port layer to the HGSS SDK (Phase 1, not finished)

`3ds_port/` is vendored byte-identical from pokeplatinum-3ds and was written against the ntrtwl NitroSDK
(`libraries/<lib>/src/*.c`, `include/nitro/**`). HGSS's SDK is a flat reconstruction (`lib/NitroSDK/src/<lib>/*.c`,
public headers in `lib/include/nitro/**` plus flat internal headers in `lib/NitroSDK/include`). The vendored
Makefiles therefore cannot drive it. Measure progress with `python3 tools/port_compile_survey.py` (inside the
container, after `bootstrap.py`).

## Measured state

Port sources syntax-checked against the patched HGSS headers, using a shim include directory that exposes only
`nitro*`, `nnsys*`, `cw*` from `lib/include` (HGSS's `stdarg.h`/`stddef.h`/`string.h` there shadow the system ones the
port code needs; without the shim only 11/46 compile): **26/46 compile.** The other 20 fail for these 17 reasons:

| Cause | Files | Kind |
| --- | --- | --- |
| `nitro/spi/common/type.h`, `nitro/rtc/common/fifo.h`, `nitro/snd/common/command.h`, `nitro/dgt/dgt.h`, `nitro/ioreg.h`, `nitro/hw/ARM9/mmap_global.h` not found (HGSS names: `spi/common/pm_common.h`, none, flat `SND_command.h`, `dgt/common.h`, `hw/io_reg.h`, `hw/ARM9/mmap.h`) | port_pm, port_tp, port_rtc, port_snd, dgt, port_cp, port_ioreg | compat headers (new files) |
| `card_common.h` (HGSS: `CARD_common.h`) | port_card | case; port-layer fix, or compat header |
| `MIUnpackBitsParam`, `CTRDGBackupType`, `HW_ROM_HEADER_BUF_END`, `MI_DMA_TIMING_IMM`, `GX_FIFOINTR_COND_UNDERHALF`, `PXI_FIFO_TAG_CTRDG_PHI` undeclared (HGSS has no or different names) | mi_uncompress, ctrdg, port_boot, port_dma, port_gx, pxi | define in a compat header |
| `struct MATHCRC8Table` redefined (HGSS has two copies of the header: `lib/include/nitro/math/crc.h` and `lib/NitroSDK/include/MATH_crc.h`) | port_gx3d* | include order / header guard |
| `MIi_CpuClear16` prototype differs from the port's definition | mi_memory | port or SDK signature |
| `OS_IrqHandler()` called with no argument (HGSS takes an `OSIrqMask`) | port_os | port-layer fix: pass 0 |
| `NitroPort_LcdcVram` undeclared | port_render2d | port-internal header, probably a missing generated header |

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

## Suggested design (from the build-wiring review, not yet built)

An out-of-tree shim under the git-ignored `build/` (symlinks mapping Platinum names to HGSS files, the generated
header, and the compat headers kept in a new tracked directory) plus command-line overrides (`SDK=`, `SDK_SRCS=`,
`SDK_INCLUDES=`, `ARM7_SOUND_SRCS=`, `PORT_DEFINES=`) so the vendored Makefiles stay unchanged. The first gate is a
link of `libnitro` for `TARGET=host32`, then the vendored tests one by one.
