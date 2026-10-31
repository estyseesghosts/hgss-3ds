// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/mi/memory.h: the FIFO send helpers and fixed-block helpers the
 * port's tests use, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/mi/memory.h declares the MIi_Cpu primitives
 * (signatures matched to the port by patch 1202) and the fill/copy/clear
 * inlines, but no MI_CpuSend16/32 and no MI_Copy128B/MI_Zero48B. The inlines
 * below are ntrtwl's (include/nitro/mi/memory.h) over HGSS's MIi_
 * primitives; the fixed-block declarations match the port's definitions in
 * 3ds_port/nitro/src/mi_memory.c.
 */

#ifndef HGSS_COMPAT_MI_MEMORY_H
#define HGSS_COMPAT_MI_MEMORY_H

#include_next <nitro/mi/memory.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void MI_CpuSend32(const void *src, volatile void *dest, u32 size)
{
    MIi_CpuSend32(src, dest, size);
}

static inline void MI_CpuSend16(const void *src, volatile void *dest, u32 size)
{
    MIi_CpuSend16(src, dest, size);
}

void MI_Copy128B(const void *pSrc, void *pDest);
void MI_Zero48B(void *pDest);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_MI_MEMORY_H */
