// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/os/common/interrupt.h: ntrtwl interrupt-header path compatibility.
 *
 * HGSS has no such file (its shared header is interrupt_shared.h, wrapped
 * separately). Nothing in the port includes this path today; it exists so
 * code written against ntrtwl's include/nitro/os/common/interrupt.h keeps
 * resolving, providing the same two interrupt bits as the
 * interrupt_shared.h wrapper. Self-sufficient: pulls in HGSS's shared
 * header for OS_IE_SPFIFO_RECV and REG_OS_IE_GF_SHIFT first.
 */

#ifndef HGSS_COMPAT_OS_COMMON_INTERRUPT_H
#define HGSS_COMPAT_OS_COMMON_INTERRUPT_H

#include <nitro/os/common/interrupt_shared.h>

#ifndef OS_IE_FIFO_SEND
#define OS_IE_FIFO_SEND             OS_IE_SPFIFO_SEND
#endif
#ifndef OS_IE_FIFO_RECV
#define OS_IE_FIFO_RECV             OS_IE_SPFIFO_RECV
#endif
#ifndef OS_IE_GXFIFO
#define OS_IE_GXFIFO            (1UL << REG_OS_IE_GF_SHIFT)
#endif

#endif /* HGSS_COMPAT_OS_COMMON_INTERRUPT_H */
