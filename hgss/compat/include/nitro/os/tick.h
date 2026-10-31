// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/os/tick.h: the tick conversion helpers, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/os/tick.h only declares the tick counter API;
 * OS_SYSTEM_CLOCK (via HW_SYSTEM_CLOCK) and OS_MilliSecondsToTicks already
 * live in tick_shared.h. The conversions below are ntrtwl's
 * (include/nitro/os/common/tick.h); the port's tests use
 * OS_TicksToMilliSeconds.
 */

#ifndef HGSS_COMPAT_OS_TICK_H
#define HGSS_COMPAT_OS_TICK_H

#include_next <nitro/os/tick.h>

#ifndef OS_TicksToMilliSeconds
#define OS_TicksToMilliSeconds(tick)      (((u64)(tick) * 64) / (OS_SYSTEM_CLOCK / 1000))
#endif
#ifndef OS_TicksToMilliSeconds32
#define OS_TicksToMilliSeconds32(tick)    (((u32)(tick) * 64) / (OS_SYSTEM_CLOCK / 1000))
#endif

#endif /* HGSS_COMPAT_OS_TICK_H */
