// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/os/common/interrupt_shared.h: FIFO-receive and geometry-FIFO
 * interrupt bits missing from HGSS's shared header.
 *
 * Wrapper around HGSS's lib/include/nitro/os/common/interrupt_shared.h via
 * #include_next. HGSS only names the ARM7 side's FIFO bit
 * (lib/include/nitro/os/ARM7/interrupt.h); the ARM9 chain the port builds
 * against (<nitro.h> -> <nitro/os.h> -> <nitro/os/interrupt.h> -> here) has
 * neither OS_IE_FIFO_RECV nor OS_IE_GXFIFO, both of which the port needs
 * (pxi.c, port_gx.c). Values are ntrtwl's
 * (include/nitro/os/common/interrupt.h): the FIFO alias of OS_IE_SPFIFO_RECV
 * and bit REG_OS_IE_GF_SHIFT. The guard is compat-specific so the chained
 * HGSS header is still processed.
 */

#ifndef HGSS_COMPAT_OS_COMMON_INTERRUPT_SHARED_H
#define HGSS_COMPAT_OS_COMMON_INTERRUPT_SHARED_H

#include_next <nitro/os/common/interrupt_shared.h>

#ifndef OS_IE_FIFO_SEND
#define OS_IE_FIFO_SEND             OS_IE_SPFIFO_SEND
#endif
#ifndef OS_IE_FIFO_RECV
#define OS_IE_FIFO_RECV             OS_IE_SPFIFO_RECV
#endif
#ifndef OS_IE_GXFIFO
#define OS_IE_GXFIFO            (1UL << REG_OS_IE_GF_SHIFT)
#endif

#endif /* HGSS_COMPAT_OS_COMMON_INTERRUPT_SHARED_H */
