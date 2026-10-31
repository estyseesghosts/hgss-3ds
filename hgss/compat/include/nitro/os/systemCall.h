// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/os/systemCall.h: the BIOS-call declarations the port uses, wrapping
 * HGSS's header.
 *
 * HGSS's lib/include/nitro/os/systemCall.h only declares SVC_Halt and
 * SVC_WaitByLoop. The declarations below are ntrtwl's
 * (include/nitro/os/common/systemCall.h); the bodies live in the port's
 * 3ds_port/nitro/src/svc.c.
 */

#ifndef HGSS_COMPAT_OS_SYSTEMCALL_H
#define HGSS_COMPAT_OS_SYSTEMCALL_H

#include_next <nitro/os/systemCall.h>

#ifdef __cplusplus
extern "C" {
#endif

void SVC_CpuSet(const void *srcp, void *destp, u32 dmaCntData);
void SVC_CpuSetFast(const void *srcp, void *destp, u32 dmaCntData);
u16 SVC_GetCRC16(u32 start, const void *datap, u32 size);
s32 SVC_Div(s32 number, s32 denom);
s32 SVC_DivRem(s32 number, s32 denom);
u16 SVC_Sqrt(u32 src);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_OS_SYSTEMCALL_H */
