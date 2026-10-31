// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/mi/dma.h: DMA timing selectors missing from HGSS's header.
 *
 * Wrapper around HGSS's lib/include/nitro/mi/dma.h via #include_next.
 * HGSS defines MI_DMA_TIMING_MASK/CARD/H_BLANK (as (NUL << 27), matching
 * ntrtwl's REG_MI_DMA0CNT_MODE_SHIFT = 27); the port's software DMA in
 * 3ds_port/nitro/src/port_dma.c switches on every ARM9 timing, so the rest
 * are added here with ntrtwl values (include/nitro/mi/dma.h). Each is
 * #ifndef-guarded so HGSS's own macros always win. The guard is
 * compat-specific so the chained HGSS header is still processed.
 */

#ifndef HGSS_COMPAT_MI_DMA_H
#define HGSS_COMPAT_MI_DMA_H

#include_next <nitro/mi/dma.h>

#ifndef MI_DMA_TIMING_IMM
#define MI_DMA_TIMING_IMM          (0UL << 27)
#endif
#ifndef MI_DMA_TIMING_V_BLANK
#define MI_DMA_TIMING_V_BLANK      (1UL << 27)
#endif
#ifndef MI_DMA_TIMING_DISP
#define MI_DMA_TIMING_DISP         (3UL << 27)
#endif
#ifndef MI_DMA_TIMING_DISP_MMEM
#define MI_DMA_TIMING_DISP_MMEM    (4UL << 27)
#endif
#ifndef MI_DMA_TIMING_CARTRIDGE
#define MI_DMA_TIMING_CARTRIDGE    (6UL << 27)
#endif
#ifndef MI_DMA_TIMING_GXFIFO
#define MI_DMA_TIMING_GXFIFO       (7UL << 27)
#endif

#endif /* HGSS_COMPAT_MI_DMA_H */
