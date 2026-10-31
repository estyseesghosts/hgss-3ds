// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/mi/stream.h: device-stream callbacks for the BIOS decompressors.
 *
 * HGSS has no such header. Content is ntrtwl NitroSDK's
 * (include/nitro/mi/stream.h). Only the MIReadStreamCallbacks type is used
 * today (3ds_port/nitro/src/svc.c signatures); the getter has no definition
 * anywhere in HGSS, so calling it would fail at link time.
 */

#ifndef NITRO_MI_STREAM_H_
#define NITRO_MI_STREAM_H_

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef s32 (*MIi_InitReadStreamCallback)(const u8 *devicep, void *ramp, const void *paramp);
typedef s32 (*MIi_TerminateReadStreamCallback)(const u8 *devicep);
typedef u8 (*MIi_ReadByteStreamCallback)(const u8 *devicep);
typedef u16 (*MIi_ReadShortStreamCallback)(const u8 *devicep);
typedef u32 (*MIi_ReadWordStreamCallback)(const u8 *devicep);

typedef struct {
    MIi_InitReadStreamCallback initStream;
    MIi_TerminateReadStreamCallback terminateStream;
    MIi_ReadByteStreamCallback readByteStream;
    MIi_ReadShortStreamCallback readShortStream;
    MIi_ReadWordStreamCallback readWordStream;
} MIReadStreamCallbacks;

MIReadStreamCallbacks *MI_GetReadStreamFromMemoryCallbacks(void);

#ifdef __cplusplus
}
#endif

#endif /* NITRO_MI_STREAM_H_ */
