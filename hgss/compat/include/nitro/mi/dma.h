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

#ifdef __cplusplus
extern "C" {
#endif

/* ntrtwl's include/nitro/mi/dma.h declares these; HGSS's header and SDK
 * sources only carry the 32-bit fill/copy/wait/stop/GX-command family.
 * Bodies live in hgss/compat/src/sdk_standins.c (ntrtwl's
 * libraries/mi/src/mi_dma.c and mi_dma_vblank.c, over the same MIi_
 * register helpers HGSS's mi_dma.c uses). */
void MI_DmaFill16(u32 dmaNo, void *dest, u16 data, u32 size);
void MI_DmaSend32(u32 dmaNo, const void *src, volatile void *dest, u32 size);
BOOL MI_IsDmaBusy(u32 dmaNo);
void MI_HBlankDmaCopy32(u32 dmaNo, const void *src, void *dest, u32 size);
void MI_VBlankDmaCopy32Async(u32 dmaNo, const void *src, void *dest, u32 size,
                             MIDmaCallback callback, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_MI_DMA_H */
