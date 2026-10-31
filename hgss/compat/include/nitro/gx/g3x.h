// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/gx/g3x.h: geometry-FIFO interrupt condition, status and helpers,
 * wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/gx/g3x.h provides GXShading/GXFogBlend/GXFogSlope
 * and (under SDK_PORT_3DS, via patch 1402) G3X_ResetMtxStackOverflow, but no
 * GX_FIFOINTR_COND enums, no GX_FIFOSTAT enums and no
 * G3X_GetCommandFifoStatus/Count; the flat lib/NitroSDK/include/GX_g3x.h has
 * the condition enum, the status enum and the status getter (with the same
 * values). The additions below are ntrtwl's (include/nitro/gx/g3x.h). They
 * are skipped when the flat header was already included (GUARD_GX_G3X_H), so
 * the two never define the enumerators twice in that order. Note the reverse
 * order (this wrapper first, flat GX_g3x.h later in the same translation
 * unit) would still collide; no translation unit in the build does that
 * today (HGSS's public g3x.h already defines GXShading and friends, so
 * mixing the two worlds there already fails). G3X_ResetMtxStackOverflow is
 * deliberately NOT defined here: the public header already provides it.
 */

#ifndef HGSS_COMPAT_GX_G3X_H
#define HGSS_COMPAT_GX_G3X_H

#include_next <nitro/gx/g3x.h>

#ifndef GUARD_GX_G3X_H
typedef enum {
    GX_FIFOINTR_COND_DISABLE      = 0,
    GX_FIFOINTR_COND_UNDERHALF    = 1,
    GX_FIFOINTR_COND_EMPTY        = 2
} GXFifoIntrCond;

typedef enum {
    GX_FIFOSTAT_OVERHALF          = 0,
    GX_FIFOSTAT_FULL              = 1,
    GX_FIFOSTAT_UNDERHALF         = 2,
    GX_FIFOSTAT_EMPTY             = 6
} GXFifoStat;

static inline s32 G3X_GetCommandFifoCount(void)
{
    return (s32)((reg_G3X_GXSTAT & REG_G3X_GXSTAT_FIFOCNT_MASK) >> REG_G3X_GXSTAT_FIFOCNT_SHIFT);
}

static inline GXFifoStat G3X_GetCommandFifoStatus(void)
{
    return (GXFifoStat)((reg_G3X_GXSTAT & (REG_G3X_GXSTAT_F_MASK |
                                           REG_G3X_GXSTAT_H_MASK |
                                           REG_G3X_GXSTAT_E_MASK)) >> REG_G3X_GXSTAT_F_SHIFT);
}
#else
static inline s32 HGSSCompat_G3X_GetCommandFifoCount(void)
{
    return (s32)((reg_G3X_GXSTAT & REG_G3X_GXSTAT_FIFOCNT_MASK) >> REG_G3X_GXSTAT_FIFOCNT_SHIFT);
}
#define G3X_GetCommandFifoCount HGSSCompat_G3X_GetCommandFifoCount
#endif

#endif /* HGSS_COMPAT_GX_G3X_H */
