// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/os/system.h: OS_WaitInterrupt, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/os/system.h declares OS_WaitVBlankIntr but not
 * OS_WaitInterrupt (ntrtwl's include/nitro/os/common/system.h declares
 * both; HGSS split waiting into OS_WaitIrq/OS_WaitVBlankIntr in
 * os_wait_irq.c/os_wait_vblank.c instead). The body lives in
 * hgss/compat/src/sdk_standins.c and is ntrtwl's
 * (libraries/os/src/os_system.c): disable interrupts, enable the IRQ
 * master, clear the waited flags, and OS_Halt (the port: advance one frame)
 * until they arrive.
 */

#ifndef HGSS_COMPAT_OS_SYSTEM_H
#define HGSS_COMPAT_OS_SYSTEM_H

#include_next <nitro/os/system.h>
#include <nitro/os/common/interrupt_shared.h>

#ifdef __cplusplus
extern "C" {
#endif

void OS_WaitInterrupt(BOOL clear, OSIrqMask irqFlags);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_OS_SYSTEM_H */
