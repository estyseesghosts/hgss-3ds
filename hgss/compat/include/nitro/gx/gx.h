// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/gx/gx.h: the display helpers the port's tests use, wrapping HGSS's
 * header.
 *
 * HGSS's lib/include/nitro/gx/gx.h has no GX_SetMasterBrightness (only the
 * flat lib/NitroSDK/include/gx.h carries the inline, over
 * GXx_SetMasterBrightness_, which HGSS's gx.c defines), no GX_LoadBG3Bmp
 * and no GX_IsHBlank. The three below mirror the flat header and ntrtwl
 * (include/nitro/gx/gx.h): brightness over HGSS's function, the bitmap
 * loader over HGSS's GX_LoadBG3Scr, and the H-blank flag straight from
 * DISPSTAT.
 */

#ifndef HGSS_COMPAT_GX_GX_H
#define HGSS_COMPAT_GX_GX_H

#include_next <nitro/gx/gx.h>

#ifdef __cplusplus
extern "C" {
#endif

void GXx_SetMasterBrightness_(vu16 *dst, s32 brightness);
void GX_LoadBG3Scr(const void *pSrc, u32 offset, u32 szByte);

static inline void GX_SetMasterBrightness(int brightness)
{
    GXx_SetMasterBrightness_(&reg_GX_MASTER_BRIGHT, brightness);
}

static inline void GX_LoadBG3Bmp(const void *pSrc, u32 offset, u32 szByte)
{
    GX_LoadBG3Scr(pSrc, offset, szByte);
}

static inline s32 GX_IsHBlank(void)
{
    return (reg_GX_DISPSTAT & REG_GX_DISPSTAT_HBLK_MASK);
}

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_GX_GX_H */
