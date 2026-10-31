// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/snd/ARM9/interface.h: the ARM9 sound API declarations.
 *
 * HGSS has no such header: its ARM9 entry points are declared across the
 * flat lib/NitroSDK/include/SND_{interface,command,alarm,work}.h, which no
 * public header includes (lib/include/nitro/snd.h pulls only the
 * common/global, exchannel, channel, data and work headers). The
 * declarations below match the flat headers; the bodies are HGSS's own,
 * except SND_StartSeq (commented out in HGSS's snd_interface.c; the weak
 * body in hgss/compat/src/sdk_standins.c pushes the start command the same
 * way SND_PrepareSeq does).
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
void SND_PrepareSeq(s32 player, const void *seqBasePtr, u32 seqOffset, struct SNDBankData *bankData);
void SND_StartPreparedSeq(s32 player);
/* HGSS's snd_master_volume.c declares/defines these two with (int) and
 * (void *) while the flat headers say (s32) and (struct SNDDriverInfo *).
 * The int/void forms are used here because this header reaches the two
 * global.h-based translation units above, whose local prototypes would
 * otherwise conflict; the symbols and ABI are the same. */
void SND_SetMasterVolume(int volume);
void SND_ReadDriverInfo(void *info);
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
