// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/snd/common/work.h: SNDDriverInfo, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/snd/common/work.h provides SNDChannelInfo and
 * SNDTrackInfo but no SNDDriverInfo; HGSS's own driver-info struct lives in
 * lib/NitroSDK/include-pokediamond/nitro/SND_work_shared.h with different
 * member names (work/channelControls/workPtr/lockedChannels) but the same
 * layout: SNDWork (0x1180 bytes), sixteen u32 channel controls, a work
 * pointer, lockedChannels, six u32 of padding (offsets 0x1180/0x11C0/0x11C4
 * there, as here). The typedef below uses ntrtwl's member names
 * (include/nitro/snd/common/work.h), which the port's tests read; HGSS's
 * SND_ReadDriverInfo fills the same bytes.
 *
 * The struct is tagged `SNDDriverInfo` so its type is the same one the flat
 * SND_work_shared.h names as `struct SNDDriverInfo` (the ntrtwl member names
 * are a compat-only spelling; the layout, 0x11E0 bytes, is HGSS's).
 */

#ifndef HGSS_COMPAT_SND_COMMON_WORK_H
#define HGSS_COMPAT_SND_COMMON_WORK_H

#include_next <nitro/snd/common/work.h>

typedef struct SNDDriverInfo {
    u8 work[0x1180];
    u32 chCtrl[16];
    void *workAddress;
    u32 lockedChannels;
    u32 padding[6];
} SNDDriverInfo;

#endif /* HGSS_COMPAT_SND_COMMON_WORK_H */
