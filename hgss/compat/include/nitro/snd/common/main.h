// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/snd/common/main.h: the sound driver's tick interval.
 *
 * HGSS has no such header and no SND_PROC_INTERVAL; the value below is
 * ntrtwl NitroSDK's (include/nitro/snd/common/main.h: 0xAA8 = 2728). Only
 * 3ds_port/nitro/src/port_snd.c includes this path, and it needs just the
 * interval: the SNDSharedWork typedef is a forward declaration on purpose.
 * HGSS's own `struct SNDSharedWork`
 * (lib/NitroSDK/include-pokediamond/nitro/SND_work_shared.h) cannot be
 * pulled in here because that header also defines `struct SNDChannelInfo`
 * with different members than HGSS's public lib/include/nitro/snd/common/
 * work.h typedef, so including both worlds in one translation unit fails.
 */

#ifndef NITRO_SND_COMMON_MAIN_H_
#define NITRO_SND_COMMON_MAIN_H_

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SNDSharedWork SNDSharedWork;

#ifndef SND_PROC_INTERVAL
#define SND_PROC_INTERVAL           0xAA8
#endif
#ifndef SND_MESSAGE_PERIODIC
#define SND_MESSAGE_PERIODIC        1
#endif
#ifndef SND_MESSAGE_WAKEUP_THREAD
#define SND_MESSAGE_WAKEUP_THREAD   2
#endif

#ifdef __cplusplus
}
#endif

#endif /* NITRO_SND_COMMON_MAIN_H_ */
