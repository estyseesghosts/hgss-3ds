// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/snd/ARM9/interface.h: the ARM9 sound API declarations.
 *
 * HGSS has no such header: its ARM9 entry points are declared across the
 * flat lib/NitroSDK/include/SND_{interface,command,alarm,work}.h, which no
 * public header includes (lib/include/nitro/snd.h pulls only the
 * common/global, exchannel, channel, data and work headers). The
 * declarations below cover the API the port and its tests call; the bodies
 * are HGSS's own, except SND_StartSeq (commented out in HGSS's
 * snd_interface.c; the weak body in hgss/compat/src/sdk_standins.c pushes
 * the start command the same way SND_PrepareSeq does).
 *
 * The flat headers are not included here (the README used to say they were).
 * They cannot be combined with this path in one translation unit:
 *
 *  - SND_interface.h -> SND_work.h -> SND_work_shared.h defines
 *    `struct SNDChannelInfo`/`struct SNDTrackInfo` with different members
 *    from the public nitro/snd/common/work.h typedefs, and
 *    nitro/snd.h already includes the public ones.
 *  - SND_interface.h declares SND_SetMasterVolume(s32) and
 *    SND_PrepareSeq(..., struct SNDBankData *), SND_command.h declares
 *    SND_RecvCommandReply as returning `const struct SNDCommand *`, while
 *    NitroSystem's nnsys/snd_internal.h (in the nns_all_headers.c unity
 *    build) declares them (s32), (..., void *bank) and `const void *`.
 *    Those are incompatible function types; one declaration set has to win.
 *
 * This header therefore repeats only the declarations HGSS's own definitions
 * use (snd_master_volume.c is (int), snd_read_driver_info.c is (void *));
 * where nnsys/snd_internal.h also declares a function, the declaration here
 * is the same type. The two functions whose definitions and snd_internal
 * disagree use an unprototyped declaration, which C considers compatible
 * with both (`int`/`s32` and `void *`/`SndDriverInfo *` pass the same on
 * every target the port builds for).
 */

#ifndef HGSS_COMPAT_SND_ARM9_INTERFACE_H
#define HGSS_COMPAT_SND_ARM9_INTERFACE_H

#include <nitro/types.h>
#include <nitro/snd/common/work.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SNDBankData;
typedef void (*SNDAlarmCallback)(void *);

void SND_Init(void);
void SND_StartSeq(s32 player, const void *seqBasePtr, u32 seqOffset, struct SNDBankData *bankData);
void SND_StopSeq(s32 player);
void SND_StartPreparedSeq(s32 player);
/* Deliberately unprototyped: see the comment above. A declaration with an
 * empty parameter list is compatible with every one of the prototypes these
 * four functions carry elsewhere (snd_internal.h's void *bank now, a
 * potential SDK_PORT_3DS patch's struct SNDBankData * bank, the flat header's
 * s32/struct SNDDriverInfo *, and the definitions' int/void *). */
void SND_PrepareSeq();
void SND_SetMasterVolume();
void SND_ReadDriverInfo();
void SND_StartTimer(u32 chnBitMask, u32 capBitMask, u32 alarmBitMask, u32 flags);
void SND_StopTimer(u32 chnBitMask, u32 capBitMask, u32 alarmBitMask, u32 flags);
void SND_SetupAlarm(s32 alarm, u32 tick, u32 period, SNDAlarmCallback cb, void *userData);
u32 SND_GetPlayerStatus(void);
BOOL SND_FlushCommand(u32 flags);
void SND_WaitForCommandProc(u32 tag);
u32 SND_GetCurrentCommandTag(void);
BOOL SND_IsFinishedCommandTag(u32 tag);
s32 SND_CountFreeCommand(void);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_SND_ARM9_INTERFACE_H */
